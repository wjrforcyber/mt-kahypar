/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2019 Tobias Heuer <tobias.heuer@kit.edu>
 *
 * KaHyPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaHyPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaHyPar.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#pragma once

#include "kahypar/meta/mandatory.h"

#include "mt-kahypar/datastructures/hypergraph_common.h"

namespace mt_kahypar {
namespace ds {

template <typename Hypergraph = Mandatory,
          typename TBBNumaArena = Mandatory>
class PartitionedHypergraph {

 public:
  static constexpr bool is_static_hypergraph = Hypergraph::is_static_hypergraph;
  static constexpr bool is_partitioned = true;

  explicit PartitionedHypergraph(Hypergraph& hypergraph) :
    _hg(hypergraph) { }

  PartitionedHypergraph(const PartitionedHypergraph&) = delete;
  PartitionedHypergraph & operator= (const PartitionedHypergraph &) = delete;

 private:
  Hypergraph& _hg;
};

} // namespace ds
} // namespace mt_kahypar