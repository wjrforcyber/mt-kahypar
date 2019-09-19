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

#include <iostream>

#include "mt-kahypar/definitions.h"
#include "mt-kahypar/partition/context.h"
#include "mt-kahypar/application/command_line_options.h"
#include "mt-kahypar/io/hypergraph_io.h"

#include "mt-kahypar/utils/timer.h"

int main(int argc, char* argv[]) {

  mt_kahypar::Context context;
  mt_kahypar::processCommandLineInput(context, argc, argv);
  mt_kahypar::io::printBanner(context);
  mt_kahypar::io::printInputInformation(context/*, hypergraph*/);

  // Initialize TBB task arenas on numa nodes
  mt_kahypar::TBBNumaArena::instance(context.shared_memory.num_threads);

  mt_kahypar::HighResClockTimepoint start = std::chrono::high_resolution_clock::now();
  mt_kahypar::Hypergraph hypergraph = mt_kahypar::io::readHypergraphFile(
    context.partition.graph_filename);
  mt_kahypar::HighResClockTimepoint end = std::chrono::high_resolution_clock::now();
  mt_kahypar::utils::Timer::instance().add_timing("hypergraph_import", "Reading Hypergraph File",
    "", mt_kahypar::utils::Timer::Type::IMPORT, 0, std::chrono::duration<double>(end - start).count());

  mt_kahypar::TBBNumaArena::instance().terminate();


  LOG << mt_kahypar::utils::Timer::instance();

  return 0;
}
