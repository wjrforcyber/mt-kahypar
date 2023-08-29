/*******************************************************************************
 * MIT License
 *
 * This file is part of Mt-KaHyPar.
 *
 * Copyright (C) 2023 Nikolai Maas <nikolai.maas@kit.edu>
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

#pragma once

#include "mt-kahypar/partition/refinement/fm/fm_commons.h"


// TODO: HIGH_DEGREE_THRESHOLD in PartitionedHypergraph/PartitionedGraph might be problematic
// for unconstrained refinement


namespace mt_kahypar {

  /*
   * FMStrategy interface
   * static constexpr bool uses_gain_cache
   * static constexpr bool maintain_gain_cache_between_rounds
   * static constexpr bool is_unconstrained
   *
   * Constructor(context, sharedData, runStats)
   * applyWithDispatchedStrategy(applicator_fn)
   * insertIntoPQ(phg, gain_cache, node)
   * updateGain(phg, gain_cache, node, move)
   * findNextMove(phg, gain_cache, move)
   * skipMove(phg, gain_cache, move)
   * clearPQs()
   * deltaGainUpdates(phg, gain_cache, sync_update)
   * changeNumberOfBlocks(new_k)
   * memoryConsumption(utils::MemoryTreeNode* parent) const
   *
   */

class UnconstrainedStrategy {
public:

  using BlockPriorityQueue = ds::ExclusiveHandleHeap< ds::MaxHeap<Gain, PartitionID> >;
  using VertexPriorityQueue = ds::MaxHeap<Gain, HypernodeID>;    // these need external handles

  static constexpr bool uses_gain_cache = true;
  static constexpr bool maintain_gain_cache_between_rounds = true;
  static constexpr bool is_unconstrained = true;

  UnconstrainedStrategy(const Context& context,
                    FMSharedData& sharedData,
                    FMStats& runStats) :
      context(context),
      runStats(runStats),
      sharedData(sharedData),
      blockPQ(static_cast<size_t>(context.partition.k)),
      vertexPQs(static_cast<size_t>(context.partition.k),
        VertexPriorityQueue(sharedData.vertexPQHandles.data(), sharedData.numberOfNodes)) { }

  template<typename DispatchedStrategyApplicatorFn>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void applyWithDispatchedStrategy(size_t /*taskID*/, size_t /*round*/, DispatchedStrategyApplicatorFn applicator_fn) {
    applicator_fn(static_cast<UnconstrainedStrategy&>(*this));
  }

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void insertIntoPQ(const PartitionedHypergraph& phg,
                    const GainCache& gain_cache,
                    const HypernodeID v) {
    const PartitionID pv = phg.partID(v);
    ASSERT(pv < context.partition.k);
    auto [target, gain] = computeBestTargetBlock(phg, gain_cache, v, pv);
    ASSERT(target < context.partition.k);
    sharedData.targetPart[v] = target;
    vertexPQs[pv].insert(v, gain);  // blockPQ updates are done later, collectively.
    runStats.pushes++;
  }

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void updateGain(const PartitionedHypergraph& phg,
                  const GainCache& gain_cache,
                  const HypernodeID v,
                  const Move& move) {
    const PartitionID pv = phg.partID(v);
    ASSERT(vertexPQs[pv].contains(v));
    const PartitionID designatedTargetV = sharedData.targetPart[v];
    Gain gain = 0;
    PartitionID newTarget = kInvalidPartition;

    if (context.partition.k < 4 || designatedTargetV == move.from || designatedTargetV == move.to) {
      // penalty term of designatedTargetV is affected.
      // and may now be greater than that of other blocks --> recompute full
      std::tie(newTarget, gain) = computeBestTargetBlock(phg, gain_cache, v, pv);
    } else {
      // penalty term of designatedTargetV is not affected.
      // only move.from and move.to may be better
      std::tie(newTarget, gain) = bestOfThree(phg, gain_cache,
        v, pv, { designatedTargetV, move.from, move.to });
    }

    sharedData.targetPart[v] = newTarget;
    vertexPQs[pv].adjustKey(v, gain);
  }

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  bool findNextMove(const PartitionedHypergraph& phg,
                    const GainCache& gain_cache,
                    Move& m) {
    updatePQs();

    if (blockPQ.empty()) {
      return false;
    }

    while (true) {
      const PartitionID from = blockPQ.top();
      const HypernodeID u = vertexPQs[from].top();
      const Gain estimated_gain = vertexPQs[from].topKey();
      ASSERT(estimated_gain == blockPQ.topKey());
      auto [to, gain] = computeBestTargetBlock(phg, gain_cache, u, phg.partID(u));

      if (gain >= estimated_gain) { // accept any gain that is at least as good
        m.node = u; m.to = to; m.from = from;
        m.gain = gain;
        runStats.extractions++;
        vertexPQs[from].deleteTop();  // blockPQ updates are done later, collectively.
        return true;
      } else {
        runStats.retries++;
        vertexPQs[from].adjustKey(u, gain);
        sharedData.targetPart[u] = to;
        if (vertexPQs[from].topKey() != blockPQ.keyOf(from)) {
          blockPQ.adjustKey(from, vertexPQs[from].topKey());
        }
      }
    }
  }

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void skipMove(const PartitionedHypergraph&, const GainCache&, Move) {
    // TODO
  }

  void clearPQs(const size_t /* bestImprovementIndex */ ) {
    // release all nodes that were not moved
    const bool release = sharedData.release_nodes
                         && runStats.moves > 0;

    if (release) {
      // Release all nodes contained in PQ
      for (PartitionID i = 0; i < context.partition.k; ++i) {
        for (PosT j = 0; j < vertexPQs[i].size(); ++j) {
          const HypernodeID v = vertexPQs[i].at(j);
          sharedData.nodeTracker.releaseNode(v);
        }
      }
    }

    for (PartitionID i = 0; i < context.partition.k; ++i) {
      vertexPQs[i].clear();
    }
    blockPQ.clear();
  }


  // We're letting the FM details implementation decide what happens here, since some may not want to do gain cache updates,
  // but rather update gains in their PQs or something

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void deltaGainUpdates(PartitionedHypergraph& phg,
                        GainCache& gain_cache,
                        const SyncronizedEdgeUpdate& sync_update) {
    gain_cache.deltaGainUpdate(phg, sync_update);
  }

  void changeNumberOfBlocks(const PartitionID new_k) {
    blockPQ.resize(new_k);
    for ( VertexPriorityQueue& pq : vertexPQs ) {
      pq.setHandle(sharedData.vertexPQHandles.data(), sharedData.numberOfNodes);
    }
    while ( static_cast<size_t>(new_k) > vertexPQs.size() ) {
      vertexPQs.emplace_back(sharedData.vertexPQHandles.data(), sharedData.numberOfNodes);
    }
  }

  void memoryConsumption(utils::MemoryTreeNode *parent) const {
    size_t vertex_pq_sizes = std::accumulate(
            vertexPQs.begin(), vertexPQs.end(), 0,
            [](size_t init, const VertexPriorityQueue& pq) { return init + pq.size_in_bytes(); }
    );
    parent->addChild("PQs", blockPQ.size_in_bytes() + vertex_pq_sizes);
  }

private:


  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  void updatePQs() {
    for (PartitionID i = 0; i < context.partition.k; ++i) {
      if (!vertexPQs[i].empty()) {
        blockPQ.insertOrAdjustKey(i, vertexPQs[i].topKey());
      } else if (blockPQ.contains(i)) {
        blockPQ.remove(i);
      }
    }
  }

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  std::pair<PartitionID, HyperedgeWeight> computeBestTargetBlock(const PartitionedHypergraph& phg,
                                                                 const GainCache& gain_cache,
                                                                 const HypernodeID u,
                                                                 const PartitionID from) {
    const HypernodeWeight wu = phg.nodeWeight(u);
    const HypernodeWeight from_weight = phg.partWeight(from);
    PartitionID to = kInvalidPartition;
    HyperedgeWeight to_benefit = std::numeric_limits<HyperedgeWeight>::min();
    HypernodeWeight best_to_weight = from_weight - wu;
    for (PartitionID i = 0; i < context.partition.k; ++i) {
      if (i != from) {
        const HypernodeWeight to_weight = phg.partWeight(i);
        const HyperedgeWeight penalty = gain_cache.benefitTerm(u, i);
        if ( ( penalty > to_benefit || ( penalty == to_benefit && to_weight < best_to_weight ) ) &&
             to_weight + wu <= context.partition.max_part_weights[i] ) {
          to_benefit = penalty;
          to = i;
          best_to_weight = to_weight;
        }
      }
    }
    const Gain gain = to != kInvalidPartition ? to_benefit - gain_cache.penaltyTerm(u, phg.partID(u))
                                              : std::numeric_limits<HyperedgeWeight>::min();
    return std::make_pair(to, gain);
  }

  template<typename PartitionedHypergraph, typename GainCache>
  MT_KAHYPAR_ATTRIBUTE_ALWAYS_INLINE
  std::pair<PartitionID, HyperedgeWeight> bestOfThree(const PartitionedHypergraph& phg,
                                                      const GainCache& gain_cache,
                                                      HypernodeID u,
                                                      PartitionID from,
                                                      std::array<PartitionID, 3> parts) {

    const HypernodeWeight wu = phg.nodeWeight(u);
    const HypernodeWeight from_weight = phg.partWeight(from);
    PartitionID to = kInvalidPartition;
    HyperedgeWeight to_benefit = std::numeric_limits<HyperedgeWeight>::min();
    HypernodeWeight best_to_weight = from_weight - wu;
    for (PartitionID i : parts) {
      if (i != from && i != kInvalidPartition) {
        const HypernodeWeight to_weight = phg.partWeight(i);
        const HyperedgeWeight penalty = gain_cache.benefitTerm(u, i);
        if ( ( penalty > to_benefit || ( penalty == to_benefit && to_weight < best_to_weight ) ) &&
             to_weight + wu <= context.partition.max_part_weights[i] ) {
          to_benefit = penalty;
          to = i;
          best_to_weight = to_weight;
        }
      }
    }
    const Gain gain = to != kInvalidPartition ? to_benefit - gain_cache.penaltyTerm(u, phg.partID(u))
                                              : std::numeric_limits<HyperedgeWeight>::min();
    return std::make_pair(to, gain);
  }

  const Context& context;

  FMStats& runStats;

protected:
  FMSharedData& sharedData;

  // ! Priority Queue that contains for each block of the partition
  // ! the vertex with the best gain value
  BlockPriorityQueue blockPQ;

  // ! From PQs -> For each block it contains the vertices (contained
  // ! in that block) touched by the current local search associated
  // ! with their gain values
  vec<VertexPriorityQueue> vertexPQs;
};

}