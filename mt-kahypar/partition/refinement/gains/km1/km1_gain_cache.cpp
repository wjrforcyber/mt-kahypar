/*******************************************************************************
 * MIT License
 *
 * This file is part of Mt-KaHyPar.
 *
 * Copyright (C) 2023 Tobias Heuer <tobias.heuer@kit.edu>
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

#include "mt-kahypar/partition/refinement/gains/km1/km1_gain_cache.h"

#include <tbb/parallel_for.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/concurrent_vector.h>

#include "mt-kahypar/datastructures/synchronized_edge_update.h"
#include "mt-kahypar/definitions.h"

namespace mt_kahypar {

template<typename PartitionedHypergraph>
void Km1GainCache::initializeGainCache(const PartitionedHypergraph& partitioned_hg) {
  ASSERT(!_is_initialized, "Gain cache is already initialized");
  ASSERT(_k <= 0 || _k >= partitioned_hg.k(),
    "Gain cache was already initialized for a different k" << V(_k) << V(partitioned_hg.k()));
  allocateGainTable(partitioned_hg.topLevelNumNodes(), partitioned_hg.k());


  // Gain calculation consist of two stages
  //  1. Compute gain of all low degree vertices
  //  2. Compute gain of all high degree vertices
  tbb::enumerable_thread_specific< vec<HyperedgeWeight> > ets_mtb(_k, 0);
  tbb::concurrent_vector<HypernodeID> high_degree_vertices;
  // Compute gain of all low degree vertices
  tbb::parallel_for(tbb::blocked_range<HypernodeID>(HypernodeID(0), partitioned_hg.initialNumNodes()),
    [&](tbb::blocked_range<HypernodeID>& r) {
      vec<HyperedgeWeight>& benefit_aggregator = ets_mtb.local();
      for (HypernodeID u = r.begin(); u < r.end(); ++u) {
        if ( partitioned_hg.nodeIsEnabled(u)) {
          if ( partitioned_hg.nodeDegree(u) <= HIGH_DEGREE_THRESHOLD) {
            initializeGainCacheEntryForNode(partitioned_hg, u, benefit_aggregator);
          } else {
            // Collect high degree vertices
            high_degree_vertices.push_back(u);
          }
        }
      }
    });

  auto aggregate_contribution_of_he_for_node =
    [&](const PartitionID block_of_u,
        const HyperedgeID he,
        HyperedgeWeight& penalty_aggregator,
        vec<HyperedgeWeight>& benefit_aggregator) {
    HyperedgeWeight edge_weight = partitioned_hg.edgeWeight(he);
    if (partitioned_hg.pinCountInPart(he, block_of_u) > 1) {
      penalty_aggregator += edge_weight;
    }

    for (const PartitionID block : partitioned_hg.connectivitySet(he)) {
      benefit_aggregator[block] += edge_weight;
    }
  };

  // Compute gain of all high degree vertices
  for ( const HypernodeID& u : high_degree_vertices ) {
    tbb::enumerable_thread_specific<HyperedgeWeight> ets_mfp(0);
    const PartitionID from = partitioned_hg.partID(u);
    const HypernodeID degree_of_u = partitioned_hg.nodeDegree(u);
    tbb::parallel_for(tbb::blocked_range<HypernodeID>(ID(0), degree_of_u),
      [&](tbb::blocked_range<HypernodeID>& r) {
      vec<HyperedgeWeight>& benefit_aggregator = ets_mtb.local();
      HyperedgeWeight& penalty_aggregator = ets_mfp.local();
      size_t current_pos = r.begin();
      for ( const HyperedgeID& he : partitioned_hg.incidentEdges(u, r.begin()) ) {
        aggregate_contribution_of_he_for_node(from, he,
          penalty_aggregator, benefit_aggregator);
        ++current_pos;
        if ( current_pos == r.end() ) {
          break;
        }
      }
    });

    // Aggregate thread locals to compute overall gain of the high degree vertex
    const HyperedgeWeight penalty_term = ets_mfp.combine(std::plus<HyperedgeWeight>());
    _gain_cache[penalty_index(u)].store(penalty_term, std::memory_order_relaxed);
    for (PartitionID p = 0; p < _k; ++p) {
      HyperedgeWeight move_to_benefit = 0;
      for ( auto& l_move_to_benefit : ets_mtb ) {
        move_to_benefit += l_move_to_benefit[p];
        l_move_to_benefit[p] = 0;
      }
      _gain_cache[benefit_index(u, p)].store(move_to_benefit, std::memory_order_relaxed);
    }
  }

  _is_initialized = true;
}

bool Km1GainCache::triggersDeltaGainUpdate(const SynchronizedEdgeUpdate& sync_update) {
  return sync_update.pin_count_in_from_part_after == 0 ||
         sync_update.pin_count_in_from_part_after == 1 ||
         sync_update.pin_count_in_to_part_after == 1 ||
         sync_update.pin_count_in_to_part_after == 2;
}

template<typename PartitionedHypergraph>
void Km1GainCache::deltaGainUpdate(const PartitionedHypergraph& partitioned_hg,
                                   const SynchronizedEdgeUpdate& sync_update) {
  ASSERT(_is_initialized, "Gain cache is not initialized");
  const HyperedgeID he = sync_update.he;
  const PartitionID from = sync_update.from;
  const PartitionID to = sync_update.to;
  const HyperedgeWeight edge_weight = sync_update.edge_weight;
  const HypernodeID pin_count_in_from_part_after = sync_update.pin_count_in_from_part_after;
  const HypernodeID pin_count_in_to_part_after = sync_update.pin_count_in_to_part_after;
  if ( pin_count_in_from_part_after == 1 ) {
    for (const HypernodeID& u : partitioned_hg.pins(he)) {
      ASSERT(nodeGainAssertions(u, from));
      if (partitioned_hg.partID(u) == from) {
        _gain_cache[penalty_index(u)].fetch_sub(edge_weight, std::memory_order_relaxed);
      }
    }
  } else if (pin_count_in_from_part_after == 0) {
    for (const HypernodeID& u : partitioned_hg.pins(he)) {
      ASSERT(nodeGainAssertions(u, from));
      _gain_cache[benefit_index(u, from)].fetch_sub(edge_weight, std::memory_order_relaxed);
    }
  }

  if (pin_count_in_to_part_after == 1) {
    for (const HypernodeID& u : partitioned_hg.pins(he)) {
      ASSERT(nodeGainAssertions(u, to));
      _gain_cache[benefit_index(u, to)].fetch_add(edge_weight, std::memory_order_relaxed);
    }
  } else if (pin_count_in_to_part_after == 2) {
    for (const HypernodeID& u : partitioned_hg.pins(he)) {
      ASSERT(nodeGainAssertions(u, to));
      if (partitioned_hg.partID(u) == to) {
        _gain_cache[penalty_index(u)].fetch_add(edge_weight, std::memory_order_relaxed);
      }
    }
  }
}

template<typename PartitionedHypergraph>
void Km1GainCache::uncontractUpdateAfterRestore(const PartitionedHypergraph& partitioned_hg,
                                                const HypernodeID u,
                                                const HypernodeID v,
                                                const HyperedgeID he,
                                                const HypernodeID pin_count_in_part_after) {
  if ( _is_initialized ) {
    // If u was the only pin of hyperedge he in its block before then moving out vertex u
    // of hyperedge he does not decrease the connectivity any more after the
    // uncontraction => p(u) += w(he)
    const PartitionID block = partitioned_hg.partID(u);
    const HyperedgeWeight edge_weight = partitioned_hg.edgeWeight(he);
    if ( pin_count_in_part_after == 2 ) {
      // u might be replaced by an other vertex in the batch
      // => search for other pin of the corresponding block and
      // add edge weight.
      for ( const HypernodeID& pin : partitioned_hg.pins(he) ) {
        if ( pin != v && partitioned_hg.partID(pin) == block ) {
          _gain_cache[penalty_index(pin)].add_fetch(edge_weight, std::memory_order_relaxed);
          break;
        }
      }
    }

    _gain_cache[penalty_index(v)].add_fetch(edge_weight, std::memory_order_relaxed);
    // For all blocks contained in the connectivity set of hyperedge he
    // we increase the b(u, block) for vertex v by w(e)
    for ( const PartitionID block : partitioned_hg.connectivitySet(he) ) {
      _gain_cache[benefit_index(v, block)].add_fetch(
        edge_weight, std::memory_order_relaxed);
    }
  }
}

template<typename PartitionedHypergraph>
void Km1GainCache::uncontractUpdateAfterReplacement(const PartitionedHypergraph& partitioned_hg,
                                                    const HypernodeID u,
                                                    const HypernodeID v,
                                                    const HyperedgeID he) {
  // In this case, u is replaced by v in hyperedge he
  // => Pin counts of hyperedge he does not change
  if ( _is_initialized ) {
    const PartitionID block = partitioned_hg.partID(u);
    const HyperedgeWeight edge_weight = partitioned_hg.edgeWeight(he);
    // Since u is no longer incident to hyperedge he its contribution for decreasing
    // the connectivity of he is shifted to vertex v
    if ( partitioned_hg.pinCountInPart(he, block) == 1 ) {
      _gain_cache[penalty_index(u)].add_fetch(edge_weight, std::memory_order_relaxed);
      _gain_cache[penalty_index(v)].sub_fetch(edge_weight, std::memory_order_relaxed);
    }

    _gain_cache[penalty_index(u)].sub_fetch(
      edge_weight, std::memory_order_relaxed);
    _gain_cache[penalty_index(v)].add_fetch(
      edge_weight, std::memory_order_relaxed);
    // For all blocks contained in the connectivity set of hyperedge he
    // we increase the move_to_benefit for vertex v by w(e) and decrease
    // it for vertex u by w(e)
    for ( const PartitionID block : partitioned_hg.connectivitySet(he) ) {
      _gain_cache[benefit_index(u, block)].sub_fetch(
        edge_weight, std::memory_order_relaxed);
      _gain_cache[benefit_index(v, block)].add_fetch(
        edge_weight, std::memory_order_relaxed);
    }
  }
}

void Km1GainCache::restoreSinglePinHyperedge(const HypernodeID u,
                                             const PartitionID block_of_u,
                                             const HyperedgeWeight weight_of_he) {
  if ( _is_initialized ) {
    _gain_cache[benefit_index(u, block_of_u)].add_fetch(
      weight_of_he, std::memory_order_relaxed);
  }
}

template<typename PartitionedHypergraph>
void Km1GainCache::initializeGainCacheEntryForNode(const PartitionedHypergraph& partitioned_hg,
                                                  const HypernodeID u,
                                                  vec<Gain>& benefit_aggregator) {
  PartitionID from = partitioned_hg.partID(u);
  Gain penalty = 0;
  for (const HyperedgeID& e : partitioned_hg.incidentEdges(u)) {
    HyperedgeWeight ew = partitioned_hg.edgeWeight(e);
    if ( partitioned_hg.pinCountInPart(e, from) > 1 ) {
      penalty += ew;
    }
    for (const PartitionID& i : partitioned_hg.connectivitySet(e)) {
      benefit_aggregator[i] += ew;
    }
  }

  _gain_cache[penalty_index(u)].store(penalty, std::memory_order_relaxed);
  for (PartitionID i = 0; i < _k; ++i) {
    _gain_cache[benefit_index(u, i)].store(benefit_aggregator[i], std::memory_order_relaxed);
    benefit_aggregator[i] = 0;
  }
}

namespace {
#define KM1_INITIALIZE_GAIN_CACHE(X) void Km1GainCache::initializeGainCache(const X&)
#define KM1_DELTA_GAIN_UPDATE(X) void Km1GainCache::deltaGainUpdate(const X&,                     \
                                                                    const SynchronizedEdgeUpdate&)
#define KM1_RESTORE_UPDATE(X) void Km1GainCache::uncontractUpdateAfterRestore(const X&,          \
                                                                              const HypernodeID, \
                                                                              const HypernodeID, \
                                                                              const HyperedgeID, \
                                                                              const HypernodeID)
#define KM1_REPLACEMENT_UPDATE(X) void Km1GainCache::uncontractUpdateAfterReplacement(const X&,            \
                                                                                      const HypernodeID,   \
                                                                                      const HypernodeID,   \
                                                                                      const HyperedgeID)
#define KM1_INIT_GAIN_CACHE_ENTRY(X) void Km1GainCache::initializeGainCacheEntryForNode(const X&,           \
                                                                                        const HypernodeID,  \
                                                                                        vec<Gain>&)
}

INSTANTIATE_FUNC_WITH_PARTITIONED_HG(KM1_INITIALIZE_GAIN_CACHE)
INSTANTIATE_FUNC_WITH_PARTITIONED_HG(KM1_DELTA_GAIN_UPDATE)
INSTANTIATE_FUNC_WITH_PARTITIONED_HG(KM1_RESTORE_UPDATE)
INSTANTIATE_FUNC_WITH_PARTITIONED_HG(KM1_REPLACEMENT_UPDATE)
INSTANTIATE_FUNC_WITH_PARTITIONED_HG(KM1_INIT_GAIN_CACHE_ENTRY)

}  // namespace mt_kahypar
