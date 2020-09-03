/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2020 Tobias Heuer <tobias.heuer@kit.edu>
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

#include "gmock/gmock.h"

#include "mt-kahypar/datastructures/incident_net_array.h"

using ::testing::Test;

namespace mt_kahypar {
namespace ds {

void verifyIncidentNets(const HypernodeID u,
                        const HyperedgeID num_hyperedges,
                        const IncidentNetArray& incident_nets,
                        const std::set<HyperedgeID>& _expected_incident_nets) {
  size_t num_incident_edges = 0;
  std::vector<bool> actual_incident_edges(num_hyperedges, false);
  for ( const HyperedgeID& he : incident_nets.incidentEdges(u) ) {
    ASSERT_TRUE(_expected_incident_nets.find(he) != _expected_incident_nets.end())
      << "Hyperedge " << he << " should be not part of incident nets of vertex " << u;
    ASSERT_FALSE(actual_incident_edges[he])
      << "Hyperedge " << he << " occurs more than once in incident nets of vertex " << u;
    actual_incident_edges[he] = true;
    ++num_incident_edges;
  }
  ASSERT_EQ(num_incident_edges, _expected_incident_nets.size());
}

kahypar::ds::FastResetFlagArray<> createFlagArray(const HyperedgeID num_hyperedges,
                                                  const std::vector<HyperedgeID>& contained_hes) {
  kahypar::ds::FastResetFlagArray<> flag_array(num_hyperedges);
  for ( const HyperedgeID& he : contained_hes ) {
    flag_array.set(he, true);
  }
  return flag_array;
}

TEST(AIncidentNetArray, VerifyInitialIncidentNetsOfEachVertex) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(1, 4, incident_nets, { 1 });
  verifyIncidentNets(2, 4, incident_nets, { 0, 3 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
}

TEST(AIncidentNetArray, ContractTwoVertices1) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(3, 4, createFlagArray(4, { 1, 2 }));
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
}

TEST(AIncidentNetArray, ContractTwoVertices2) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 2, createFlagArray(4, { 0 }));
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 3 });
}

TEST(AIncidentNetArray, ContractTwoVertices3) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 6, createFlagArray(4, { }));
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2, 3 });
}

TEST(AIncidentNetArray, ContractSeveralVertices1) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(3, 4, createFlagArray(4, { 1, 2 }));
  incident_nets.contract(3, 0, createFlagArray(4, { 1 }));
  verifyIncidentNets(3, 4, incident_nets, { 0, 1, 2 });
}

TEST(AIncidentNetArray, ContractSeveralVertices2) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(1, 5, createFlagArray(4, { }));
  incident_nets.contract(4, 1, createFlagArray(4, { 1 }));
  verifyIncidentNets(4, 4, incident_nets, { 1, 2, 3 });
}

TEST(AIncidentNetArray, ContractSeveralVertices3) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 3, createFlagArray(4, { 1 }));
  incident_nets.contract(0, 5, createFlagArray(4, { }));
  incident_nets.contract(0, 6, createFlagArray(4, { 2, 3 }));
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2, 3 });
}

TEST(AIncidentNetArray, ContractSeveralVertices4) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 2, createFlagArray(4, { 0 }));
  incident_nets.contract(3, 4, createFlagArray(4, { 1, 2 }));
  incident_nets.contract(5, 6, createFlagArray(4, { 3 }));
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 3 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 2, 3 });
  incident_nets.contract(0, 3, createFlagArray(4, { 1 }));
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2, 3 });
  incident_nets.contract(0, 5, createFlagArray(4, { 2, 3 }));
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2, 3 });
}

TEST(AIncidentNetArray, UncontractTwoVertices1) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(3, 4, createFlagArray(4, { 1, 2 }));
  incident_nets.uncontract(4);
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
}

TEST(AIncidentNetArray, UnontractTwoVertices2) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 2, createFlagArray(4, { 0 }));
  incident_nets.uncontract(2);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(2, 4, incident_nets, { 0, 3 });
}

TEST(AIncidentNetArray, UncontractTwoVertices3) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 6, createFlagArray(4, { }));
  incident_nets.uncontract(6);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
}

TEST(AIncidentNetArray, UncontractSeveralVertices1) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(3, 4, createFlagArray(4, { 1, 2 }));
  incident_nets.contract(3, 0, createFlagArray(4, { 1 }));
  incident_nets.uncontract(0);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  incident_nets.uncontract(4);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
}

TEST(AIncidentNetArray, UncontractSeveralVertices2) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(1, 5, createFlagArray(4, { }));
  incident_nets.contract(4, 1, createFlagArray(4, { 1 }));
  incident_nets.uncontract(1);
  verifyIncidentNets(1, 4, incident_nets, { 1, 3 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
  incident_nets.uncontract(5);
  verifyIncidentNets(1, 4, incident_nets, { 1 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
}

TEST(AIncidentNetArray, UncontractSeveralVertices3) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 3, createFlagArray(4, { 1 }));
  incident_nets.contract(0, 5, createFlagArray(4, { }));
  incident_nets.contract(0, 6, createFlagArray(4, { 2, 3 }));
  incident_nets.uncontract(6);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2, 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
  incident_nets.uncontract(5);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
  incident_nets.uncontract(3);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
}

TEST(AIncidentNetArray, UncontractSeveralVertices4) {
  IncidentNetArray incident_nets(
    7, {{0, 2}, {0, 1, 3, 4}, {3, 4, 6}, {2, 5, 6}});
  incident_nets.contract(0, 2, createFlagArray(4, { 0 }));
  incident_nets.contract(3, 4, createFlagArray(4, { 1, 2 }));
  incident_nets.contract(5, 6, createFlagArray(4, { 3 }));
  incident_nets.contract(0, 3, createFlagArray(4, { 1 }));
  incident_nets.contract(0, 5, createFlagArray(4, { 2, 3 }));
  incident_nets.uncontract(5);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 2, 3 });
  verifyIncidentNets(5, 4, incident_nets, { 2, 3 });
  incident_nets.uncontract(3);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 3 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 2, 3 });
  incident_nets.uncontract(6);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 3 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
  incident_nets.uncontract(4);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1, 3 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
  incident_nets.uncontract(2);
  verifyIncidentNets(0, 4, incident_nets, { 0, 1 });
  verifyIncidentNets(2, 4, incident_nets, { 0, 3 });
  verifyIncidentNets(3, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(4, 4, incident_nets, { 1, 2 });
  verifyIncidentNets(5, 4, incident_nets, { 3 });
  verifyIncidentNets(6, 4, incident_nets, { 2, 3 });
}

}  // namespace ds
}  // namespace mt_kahypar
