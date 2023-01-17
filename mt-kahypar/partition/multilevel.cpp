/*******************************************************************************
 * MIT License
 *
 * This file is part of Mt-KaHyPar.
 *
 * Copyright (C) 2019 Lars Gottesbüren <lars.gottesbueren@kit.edu>
 * Copyright (C) 2019 Tobias Heuer <tobias.heuer@kit.edu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

#include "mt-kahypar/partition/multilevel.h"

#include <memory>

#include "tbb/task.h"

#include "mt-kahypar/partition/factories.h"
#include "mt-kahypar/partition/preprocessing/sparsification/degree_zero_hn_remover.h"
#include "mt-kahypar/partition/preprocessing/sparsification/large_he_remover.h"
#include "mt-kahypar/partition/initial_partitioning/flat/pool_initial_partitioner.h"
#include "mt-kahypar/parallel/memory_pool.h"
#include "mt-kahypar/io/partitioning_output.h"
#include "mt-kahypar/partition/coarsening/multilevel_uncoarsener.h"
#include "mt-kahypar/partition/coarsening/nlevel_uncoarsener.h"
#include "mt-kahypar/utils/utilities.h"

namespace mt_kahypar::multilevel {

  class RefinementTask : public tbb::task {

  public:
    RefinementTask(Hypergraph& hypergraph,
                   PartitionedHypergraph& partitioned_hypergraph,
                   const Context& context,
                   std::shared_ptr<UncoarseningData> uncoarseningData,
                   const bool vcycle) :
            _ip_context(context),
            _degree_zero_hn_remover(context),
            _uncoarsener(nullptr),
            _hg(hypergraph),
            _partitioned_hg(partitioned_hypergraph),
            _context(context),
            _uncoarseningData(uncoarseningData),
            _vcycle(vcycle) {
      // Switch refinement context from IP to main
      _ip_context.refinement = _context.initial_partitioning.refinement;
    }

    tbb::task* execute() override {
      enableTimerAndStats();

      _degree_zero_hn_remover.restoreDegreeZeroHypernodes(
        _uncoarseningData->coarsestPartitionedHypergraph());

      utils::Timer& timer = utils::Utilities::instance().getTimer(_context.utility_id);
      timer.stop_timer("initial_partitioning");

      io::printPartitioningResults(_uncoarseningData->coarsestPartitionedHypergraph(),
                                   _context, "Initial Partitioning Results:");
      if ( _context.partition.verbose_output && !_vcycle ) {
        utils::Utilities::instance().getInitialPartitioningStats(
          _context.utility_id).printInitialPartitioningStats();
      }

      // ################## LOCAL SEARCH ##################
      io::printLocalSearchBanner(_context);

      timer.start_timer("refinement", "Refinement");
      std::unique_ptr<IRefiner> label_propagation =
              LabelPropagationFactory::getInstance().createObject(
                      _context.refinement.label_propagation.algorithm,
                      _hg, _context);
      std::unique_ptr<IRefiner> fm =
              FMFactory::getInstance().createObject(
                      _context.refinement.fm.algorithm,
                      _hg, _context);

      if (_uncoarseningData->nlevel) {
        _uncoarsener = std::make_unique<NLevelUncoarsener>(_hg, _context, *_uncoarseningData);
      } else {
        _uncoarsener = std::make_unique<MultilevelUncoarsener>(_hg, _context, *_uncoarseningData);
      }
      _partitioned_hg = _uncoarsener->uncoarsen(label_propagation, fm);
      timer.stop_timer("refinement");

      io::printPartitioningResults(_partitioned_hg, _context, "Local Search Results:");

      return nullptr;
    }

  public:
    Context _ip_context;
    DegreeZeroHypernodeRemover _degree_zero_hn_remover;

  private:
    void enableTimerAndStats() {
      if ( _context.type == ContextType::main && _context.partition.mode == Mode::direct ) {
        utils::Utilities& utils = utils::Utilities::instance();
        parallel::MemoryPool::instance().activate_unused_memory_allocations();
        utils.getTimer(_context.utility_id).enable();
        utils.getStats(_context.utility_id).enable();
      }
    }

    std::unique_ptr<IUncoarsener> _uncoarsener;
    Hypergraph& _hg;
    PartitionedHypergraph& _partitioned_hg;
    const Context& _context;
    std::shared_ptr<UncoarseningData> _uncoarseningData;
    const bool _vcycle;
  };

  class CoarseningTask : public tbb::task {

  public:
    CoarseningTask(Hypergraph& hypergraph,
                   const Context& context,
                   const Context& ip_context,
                   DegreeZeroHypernodeRemover& degree_zero_hn_remover,
                   UncoarseningData& uncoarseningData,
                   const bool vcycle) :
            _hg(hypergraph),
            _context(context),
            _ip_context(ip_context),
            _degree_zero_hn_remover(degree_zero_hn_remover),
            _uncoarseningData(uncoarseningData),
            _vcycle(vcycle) { }

    tbb::task* execute() override {
      // ################## COARSENING ##################
      mt_kahypar::io::printCoarseningBanner(_context);

      utils::Timer& timer = utils::Utilities::instance().getTimer(_context.utility_id);
      timer.start_timer("coarsening", "Coarsening");
      _coarsener = CoarsenerFactory::getInstance().createObject(
              _context.coarsening.algorithm, _hg, _context, _uncoarseningData);
      _coarsener->coarsen();
      timer.stop_timer("coarsening");

      Hypergraph& coarsestHypergraph = _coarsener->coarsestHypergraph();
      _coarsener.reset();
      if (_context.partition.verbose_output) {
        mt_kahypar::io::printHypergraphInfo(
                coarsestHypergraph, "Coarsened Hypergraph",
                _context.partition.show_memory_consumption);
      }

      // ################## INITIAL PARTITIONING ##################
      timer.start_timer("initial_partitioning", "Initial Partitioning");
      initialPartition(_uncoarseningData.coarsestPartitionedHypergraph());

      return nullptr;
    }

  private:
    void initialPartition(PartitionedHypergraph& phg) {
      io::printInitialPartitioningBanner(_context);

      if ( !_vcycle ) {
        if ( _context.initial_partitioning.remove_degree_zero_hns_before_ip ) {
          _degree_zero_hn_remover.removeDegreeZeroHypernodes(phg.hypergraph());
        }

        if ( _context.initial_partitioning.mode == Mode::direct ) {
          disableTimerAndStats();
          PoolInitialPartitionerContinuation& ip_continuation = *new(allocate_continuation())
                  PoolInitialPartitionerContinuation(phg, _ip_context);
          spawn_initial_partitioner(ip_continuation);
        } else {
          std::unique_ptr<IInitialPartitioner> initial_partitioner =
                  InitialPartitionerFactory::getInstance().createObject(
                          _ip_context.initial_partitioning.mode, phg, _ip_context);
          initial_partitioner->initialPartition();
        }
      } else {
        // V-Cycle: Partition IDs are given by its community IDs
        const Hypergraph& hypergraph = phg.hypergraph();
        phg.doParallelForAllNodes([&](const HypernodeID hn) {
          const PartitionID part_id = hypergraph.communityID(hn);
          ASSERT(part_id != kInvalidPartition && part_id < _context.partition.k);
          ASSERT(phg.partID(hn) == kInvalidPartition);
          phg.setOnlyNodePart(hn, part_id);
        });
        phg.initializePartition();
      }
    }

    void disableTimerAndStats() {
      if ( _context.type == ContextType::main && _context.partition.mode == Mode::direct ) {
        utils::Utilities& utils = utils::Utilities::instance();
        parallel::MemoryPool::instance().deactivate_unused_memory_allocations();
        utils.getTimer(_context.utility_id).disable();
        utils.getStats(_context.utility_id).disable();
      }
    }

    Hypergraph& _hg;
    const Context& _context;
    const Context& _ip_context;
    DegreeZeroHypernodeRemover& _degree_zero_hn_remover;
    std::unique_ptr<ICoarsener> _coarsener;
    UncoarseningData& _uncoarseningData;
    const bool _vcycle;
  };

// ! Helper function that spawns the multilevel partitioner in
// ! TBB continuation style with a given parent task.
  static void spawn_multilevel_partitioner(Hypergraph& hypergraph,
                                           PartitionedHypergraph& partitioned_hypergraph,
                                           const Context& context,
                                           const bool vcycle,
                                           tbb::task& parent) {
    // The coarsening task is first executed and once it finishes the
    // refinement task continues (without blocking)
    bool nlevel = context.coarsening.algorithm == CoarseningAlgorithm::nlevel_coarsener;
    std::shared_ptr<UncoarseningData> uncoarseningData =
      std::make_shared<UncoarseningData>(nlevel, hypergraph, context);

    RefinementTask& refinement_task = *new(parent.allocate_continuation())
            RefinementTask(hypergraph, partitioned_hypergraph, context, uncoarseningData, vcycle);
    refinement_task.set_ref_count(1);
    CoarseningTask& coarsening_task = *new(refinement_task.allocate_child()) CoarseningTask(
            hypergraph, context, refinement_task._ip_context,
            refinement_task._degree_zero_hn_remover, *uncoarseningData, vcycle);
    tbb::task::spawn(coarsening_task);
  }

  class MultilevelPartitioningTask : public tbb::task {

  public:
    MultilevelPartitioningTask(Hypergraph& hypergraph,
                               PartitionedHypergraph& partitioned_hypergraph,
                               const Context& context,
                               const bool vcycle) :
            _hg(hypergraph),
            _partitioned_hg(partitioned_hypergraph),
            _context(context),
            _vcycle(vcycle) { }

    tbb::task* execute() override {
      spawn_multilevel_partitioner(
              _hg, _partitioned_hg, _context, _vcycle, *this);
      return nullptr;
    }

  private:
    Hypergraph& _hg;
    PartitionedHypergraph& _partitioned_hg;
    const Context& _context;
    const bool _vcycle;
  };

  class VCycleTask : public tbb::task {
  public:
    VCycleTask(Hypergraph& hypergraph,
               PartitionedHypergraph& partitioned_hypergraph,
               const Context& context) :
            _hg(hypergraph),
            _partitioned_hg(partitioned_hypergraph),
            _context(context) { }

    tbb::task* execute() override {
      ASSERT(_context.partition.num_vcycles > 0);

      for ( size_t i = 0; i < _context.partition.num_vcycles; ++i ) {
        // Reset memory pool
        _hg.reset();
        parallel::MemoryPool::instance().reset();
        parallel::MemoryPool::instance().release_mem_group("Preprocessing");

        if ( _context.partition.paradigm == Paradigm::nlevel ) {
          // Workaround: reset() function of hypergraph reinserts all removed
          // hyperedges to incident net lists of each vertex again.
          LargeHyperedgeRemover large_he_remover(_context);
          large_he_remover.removeLargeHyperedgesInNLevelVCycle(_hg);
        }

        // Store partition and assign it as community ids in order to
        // restrict contractions in v-cycle to partition ids
        _hg.doParallelForAllNodes([&](const HypernodeID& hn) {
          _hg.setCommunityID(hn, _partitioned_hg.partID(hn));
        });

        // V-Cycle Multilevel Partitioning
        io::printVCycleBanner(_context, i + 1);
        MultilevelPartitioningTask& multilevel_task = *new(tbb::task::allocate_root())
                MultilevelPartitioningTask(_hg, _partitioned_hg, _context, true /* vcycle */);
        tbb::task::spawn_root_and_wait(multilevel_task);
      }

      return nullptr;
    }

  private:
    Hypergraph& _hg;
    PartitionedHypergraph& _partitioned_hg;
    const Context& _context;
  };


PartitionedHypergraph partition(Hypergraph& hypergraph, const Context& context) {
  PartitionedHypergraph partitioned_hypergraph;
    MultilevelPartitioningTask& multilevel_task = *new(tbb::task::allocate_root())
          MultilevelPartitioningTask(hypergraph, partitioned_hypergraph, context, false);
  tbb::task::spawn_root_and_wait(multilevel_task);

  if ( context.partition.num_vcycles > 0 && context.type == ContextType::main ) {
    partitionVCycle(hypergraph, partitioned_hypergraph, context);
  }
  return partitioned_hypergraph;
}

void partition_async(Hypergraph& hypergraph, PartitionedHypergraph& partitioned_hypergraph,
                     const Context& context, tbb::task* parent) {
  ASSERT(parent);

  if ( context.partition.num_vcycles > 0 && context.type == ContextType::main ) {
    VCycleTask& vcycle_task = *new(parent->allocate_continuation())
            VCycleTask(hypergraph, partitioned_hypergraph, context);
    MultilevelPartitioningTask& multilevel_task = *new(vcycle_task.allocate_child())
            MultilevelPartitioningTask(hypergraph, partitioned_hypergraph, context, false);
    tbb::task::spawn(multilevel_task);
  } else {
    spawn_multilevel_partitioner(hypergraph, partitioned_hypergraph, context, false, *parent);
  }
}


void partitionVCycle(Hypergraph& hypergraph, PartitionedHypergraph& partitioned_hypergraph,
                     const Context& context) {
  VCycleTask& vcycle_task = *new(tbb::task::allocate_root())
          VCycleTask(hypergraph, partitioned_hypergraph, context);
  tbb::task::spawn_root_and_wait(vcycle_task);
}

}
