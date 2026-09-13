#include <autopr/algorithms/genetic.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>

using namespace fcngraph;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<position> axisPath(position from, position to) {
    std::vector<position> result{from};
    while (from.first < to.first) { ++from.first; result.push_back(from); }
    while (from.second < to.second) { ++from.second; result.push_back(from); }
    require(from == to, "Test path must be right/down monotone");
    return result;
}
}

int main() {
    char temporary[] = "/tmp/ifcn-heuristic-contract-XXXXXX";
    if (!mkdtemp(temporary)) return 1;
    const auto directory = std::filesystem::path(temporary);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    const auto source = directory / "and.v";
    std::ofstream(source) << "module top(input a, input b, output y); assign y=a&b; endmodule\n";
    try {
        Parse parse;
        parse.parseVerilog(source.string());
        parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
        parse.optimizeBufferNode();
        parse.caculateSameLayerNodeRoutePair();
        const unsigned int a = parse.getVertexIndex("a"), b = parse.getVertexIndex("b"), y = parse.getVertexIndex("y");
        require(parse.getEffectiveNodes().size() == 3 && parse.getNodeType(y) == "and", "Unexpected AND fixture topology");
        for (const auto scheme : {CLOCK_SCHEME::USE, CLOCK_SCHEME::RES, CLOCK_SCHEME::TDD}) {
            GridChessboard fixed(scheme, {0, 0}, {8, 8});
            Astar fixedRouter(fixed);
            for (int trial = 0; trial < 12; ++trial) {
                Individual placed(parse, fixed, fixedRouter);
                require(placed.nodeindex_pos.size() == 3, "Initialization lost nodes despite sufficient launch sites");
                for (const auto input : {a, b}) {
                    const auto pos = placed.nodeindex_pos.at(input);
                    require(fixed.getCoorPos_Phase(pos.first, pos.second) == 1,
                            "Primary input initialized outside its fixed phase-1 launch sites");
                }
                if (scheme == CLOCK_SCHEME::TDD) {
                    const auto first = placed.nodeindex_pos.at(a), second = placed.nodeindex_pos.at(b);
                    require(first.first + first.second == second.first + second.second,
                            "TDD inputs were initialized on different absolute launch diagonals");
                }
                placed.mutateNodes(20);
                std::set<position> occupied;
                for (const auto& node : placed.nodeindex_pos) {
                    require(occupied.insert(node.second).second, "Mutation introduced duplicate node positions");
                    require(node.second.first < 8 && node.second.second < 8, "Mutation escaped the fixed grid");
                }
                for (const auto input : {a, b}) {
                    const auto pos = placed.nodeindex_pos.at(input);
                    require(fixed.getCoorPos_Phase(pos.first, pos.second) == 1,
                            "Mutation moved a primary input outside phase 1");
                }
                if (scheme == CLOCK_SCHEME::TDD) {
                    const auto first = placed.nodeindex_pos.at(a), second = placed.nodeindex_pos.at(b);
                    require(first.first + first.second == second.first + second.second,
                            "TDD input mutation changed its launch diagonal");
                }
            }
            Individual seed(parse, fixed, fixedRouter, {});
            require(seed.placeClockReachableNodes(0) && seed.nodeindex_pos.size() == 3,
                    "Bounded fixed-pattern seed failed the deterministic AND topology");
            const auto shortest = [&](position start, position target) {
                std::queue<position> pending;
                std::map<position, unsigned> distance{{start, 0}};
                pending.push(start);
                while (!pending.empty()) {
                    const auto from = pending.front(); pending.pop();
                    if (from == target) return distance.at(from);
                    for (const auto to : fixed.getPosssibleDirection(from)) {
                        if (to.first >= 8 || to.second >= 8 || distance.count(to)) continue;
                        if ((fixed.getCoorPos_Phase(to.first, to.second) + 4 -
                             fixed.getCoorPos_Phase(from.first, from.second)) % 4 != 1) continue;
                        distance[to] = distance.at(from) + 1;
                        pending.push(to);
                    }
                }
                return std::numeric_limits<unsigned>::max();
            };
            const auto distanceA = shortest(seed.nodeindex_pos.at(a), seed.nodeindex_pos.at(y));
            const auto distanceB = shortest(seed.nodeindex_pos.at(b), seed.nodeindex_pos.at(y));
            require(distanceA > 0 && distanceA != std::numeric_limits<unsigned>::max() && distanceA == distanceB,
                    "Fixed-pattern seed did not align both shortest fanin arrival epochs");
        }
        // Four free sites suffice for three nodes, but the single phase-1 site
        // cannot host both PIs. Initialization must fail cleanly and terminate.
        GridChessboard insufficientLaunch(CLOCK_SCHEME::TDD, {0, 0}, {4, 1});
        Astar insufficientRouter(insufficientLaunch);
        Individual impossible(parse, insufficientLaunch, insufficientRouter);
        require(impossible.nodeindex_pos.empty() && !impossible.is_placed &&
                impossible.validation_error.find("phase-1") != std::string::npos,
                "Insufficient primary-input clock capacity retained a partial placement");
        GeneticAlgorithm impossibleGa(parse, insufficientLaunch, insufficientRouter, 1, 2, .9, .5);
        require(!impossibleGa.gaRun() && impossibleGa.best_individuals.empty(),
                "GA accepted a board with insufficient primary-input launch capacity");

        GridChessboard occupiedLaunch(CLOCK_SCHEME::TDD, {0, 0}, {5, 1});
        Astar occupiedRouter(occupiedLaunch);
        Individual constrained(parse, occupiedLaunch, occupiedRouter, {{a, {0, 0}}, {b, {4, 0}}, {y, {2, 0}}});
        constrained.mutateNodes(20);
        require(constrained.nodeindex_pos.at(a) == position{0, 0} &&
                constrained.nodeindex_pos.at(b) == position{4, 0},
                "Mutation escaped the launch phase when no other phase-1 site was free");
        auto wrongLaunch = constrained;
        wrongLaunch.nodeindex_pos = {{a, {1, 0}}, {b, {4, 0}}, {y, {2, 0}}};
        wrongLaunch.add_placement_value_to();
        require(!wrongLaunch.is_placed, "Placement scoring accepted a wrong-phase primary input");

        GridChessboard board(CLOCK_SCHEME::TDD, {0, 0}, {10, 10});
        Astar router(board);
        Individual valid(parse, board, router, {{a, {1, 3}}, {b, {3, 1}}, {y, {3, 3}}});
        valid.routes = {{{a, y}, axisPath({1, 3}, {3, 3})}, {{b, y}, axisPath({3, 1}, {3, 3})}};
        valid.is_placed = valid.is_routed = true;
        valid.fitness = 5;
        const auto originalNodes = valid.nodeindex_pos;
        const auto originalRoutes = valid.routes;
        const auto originalGrid = board.gridMap;
        require(valid.validateLayout(), "Legal fixed-clock AND was rejected");
        require(valid.is_synced && valid.validation_error.empty(), "Legal validation state missing");
        require(valid.nodeindex_pos == originalNodes && valid.routes == originalRoutes,
                "Validation changed fixed placement or routes");
        require(board.gridMap.size() == originalGrid.size(), "Validation changed the shared board");
        for (const auto& cell : originalGrid) {
            require(board.gridMap.at(cell.first).getPhase() == cell.second.getPhase() &&
                    board.gridMap.at(cell.first).get_current_weight() == cell.second.get_current_weight(),
                    "Validation rewrote shared phases/occupancy");
        }

        // Both PIs have fixed phase 1, and every adjacent route step advances
        // by one. The reconvergent arrivals nevertheless differ by one cycle.
        Individual delayed(parse, board, router, {{a, {1, 3}}, {b, {1, 7}}, {y, {7, 7}}});
        delayed.routes = {{{a, y}, axisPath({1, 3}, {7, 7})}, {{b, y}, axisPath({1, 7}, {7, 7})}};
        delayed.is_placed = delayed.is_routed = true;
        delayed.fitness = 1000;
        for (const auto& route : delayed.routes) {
            require(board.getCoorPos_Phase(route.second.front().first, route.second.front().second) == 1,
                    "Counterexample PI must start in the first clock phase");
            for (std::size_t i = 1; i < route.second.size(); ++i) {
                const auto before = route.second[i - 1], after = route.second[i];
                require((board.getCoorPos_Phase(after.first, after.second) -
                         board.getCoorPos_Phase(before.first, before.second) + 4) % 4 == 1,
                        "Counterexample must pass local forward-phase checks");
            }
        }
        require(!delayed.validateLayout() && !delayed.is_synced &&
                delayed.validation_error.find("clock") != std::string::npos,
                "Locally forward routes with a whole-cycle fanin conflict were accepted");
        auto sharedPort = valid;
        sharedPort.routes[{b, y}] = {{3, 1}, {2, 1}, {2, 2}, {2, 3}, {3, 3}};
        require(!sharedPort.validateLayout() && sharedPort.validation_error.find("port") != std::string::npos,
                "Two sources sharing the same gate input port were accepted");
        auto missing = valid;
        missing.routes.erase({a, y});
        require(!missing.validateLayout(), "A forged routed flag hid a missing edge");

        GeneticAlgorithm ga(parse, board, router, 2, 2, .9, .5);
        ga.populations = {delayed, valid};
        ga.reserve_the_best();
        require(ga.best_individuals.size() == 1 && ga.getRoutes() == originalRoutes,
                "Invalid high-fitness individual hid the valid lower-fitness candidate");
        ga.populations = {delayed};
        ga.reserve_the_best();
        require(ga.best_individuals.size() == 1, "An invalid generation replaced the legal incumbent");
        auto nonfinite = valid;
        nonfinite.fitness = std::numeric_limits<long double>::infinity();
        ga.populations = {nonfinite, delayed};
        ga.reserve_the_best();
        ga.select_next_generation();
        require(ga.getNodePos() == originalNodes && ga.getRoutes() == originalRoutes,
                "Selection modified the saved incumbent");
        require(ga.populations.size() == 2 && ga.populations.back().routes == originalRoutes,
                "Selection did not retain an independent elite copy");
        ga.populations.back().nodeindex_pos[a] = {8, 8};
        ga.populations.back().routes.clear();
        require(ga.getNodePos() == originalNodes && ga.getRoutes() == originalRoutes,
                "Mutating the elite copy changed the incumbent snapshot");

        GeneticAlgorithm protectedElite(parse, board, router, 2, 4, 1, 1);
        protectedElite.populations = {delayed, valid, delayed, delayed};
        protectedElite.reserve_the_best();
        protectedElite.evolve_next_generation();
        require(protectedElite.populations.back().nodeindex_pos == originalNodes &&
                protectedElite.populations.back().routes == originalRoutes &&
                protectedElite.populations.back().is_synced,
                "Crossover/mutation destroyed the selected legal elite at 100% operator rates");

        // Neither starting placement is fully routed. Keep the real one-edge
        // progress while evolving; preservation must not depend on having an
        // already valid layout in best_individuals.
        Individual partial(parse, board, router, {{a, {0, 4}}, {b, {4, 0}}, {y, {2, 4}}});
        Individual weak(parse, board, router, {{a, {0, 4}}, {b, {4, 0}}, {y, {1, 1}}});
        partial.infoReset(); partial.computeFitness();
        weak.infoReset(); weak.computeFitness();
        require(!partial.is_routed && !weak.is_routed && partial.fitness > weak.fitness,
                "Partial-routing regression fixture did not distinguish useful progress");
        GeneticAlgorithm progress(parse, board, router, 4, 4, 1, 1);
        progress.populations = {partial, weak, weak, weak};
        progress.evolve_next_generation();
        require(progress.best_individuals.empty() &&
                progress.populations.back().nodeindex_pos == partial.nodeindex_pos &&
                progress.populations.back().routes == partial.routes,
                "Evolution lost the best partial routing before finding a legal layout");
        long double previousMaximum = partial.fitness;
        for (int generation = 0; generation < 4; ++generation) {
            long double maximum = 0;
            for (auto& individual : progress.populations) {
                individual.infoReset(); individual.computeFitness();
                maximum = std::max(maximum, individual.fitness);
            }
            require(maximum >= previousMaximum,
                    "Evolution regressed below previously evaluated routing progress");
            previousMaximum = maximum;
            progress.reserve_the_best();
            progress.evolve_next_generation();
        }

        // With multiple repeated mutations, all fanins must still reach their
        // gate in the directed 2DDWave grid and both PIs must share one launch.
        auto directedMutation = valid;
        for (int iteration = 0; iteration < 20; ++iteration) {
            directedMutation.mutateNodes(5);
            for (const auto edge : parse.getEffectiveEdges()) {
                const auto from = directedMutation.nodeindex_pos.at(edge.first);
                const auto to = directedMutation.nodeindex_pos.at(edge.second);
                require(from != to && from.first <= to.first && from.second <= to.second,
                        "2DDWave mutation made a previously reachable fanin/fanout edge point backward");
            }
            const auto first = directedMutation.nodeindex_pos.at(a), second = directedMutation.nodeindex_pos.at(b);
            require(first.first + first.second == second.first + second.second &&
                    board.getCoorPos_Phase(first.first, first.second) == 1 &&
                    board.getCoorPos_Phase(second.first, second.second) == 1,
                    "Directed mutation changed primary-input launch epochs or phases");
        }

        Individual area(parse, board, router, {{a, {0, 0}}, {b, {0, 1}}, {y, {0, 2}}});
        area.is_placed = true;
        area.add_area_value_to();
        require(std::isfinite(area.fitness) && std::abs(area.fitness - 700.0L / 3) < 1e-9L,
                "Origin/one-column placement produced invalid inclusive bounding area");
        area.nodeindex_pos = {{a, {2, 2}}, {b, {2, 3}}, {y, {2, 4}}};
        area.fitness = 0;
        area.add_area_value_to();
        require(std::abs(area.fitness - 700.0L / 3) < 1e-9L, "Area fitness changed under translation");
        area.routes[{a, y}] = {{2, 2}, {5, 2}, {5, 4}, {2, 4}};
        area.fitness = 0;
        area.add_area_value_to();
        require(std::abs(area.fitness - 700.0L / 12) < 1e-9L, "Area fitness omitted the routed footprint");

        Individual incomplete(parse, board, router, {{a, {0, 0}}, {b, {0, 4}}, {y, {1, 0}}});
        incomplete.infoReset();
        incomplete.computeFitness();
        require(!incomplete.is_routed && !incomplete.is_synced && incomplete.fitness <= 15,
                "Area reward allowed a compact incomplete routing to dominate fitness");

        auto mutation = valid;
        ga.mutate(mutation); // A three-node genotype must still mutate one node.
        require(mutation.nodeindex_pos != originalNodes && mutation.routes.empty() && !mutation.is_routed,
                "Small-genotype mutation was skipped or retained stale routes");
        for (const auto& node : mutation.nodeindex_pos)
            require(node.second.first < 10 && node.second.second < 10, "Mutation escaped half-open grid bounds");
        GridChessboard fullBoard(CLOCK_SCHEME::TDD, {0, 0}, {3, 1});
        Astar fullRouter(fullBoard);
        Individual full(parse, fullBoard, fullRouter, {{a, {0, 0}}, {b, {1, 0}}, {y, {2, 0}}});
        const auto fullBefore = full.nodeindex_pos;
        full.mutateNodes(1);
        require(full.nodeindex_pos == fullBefore, "Full-board mutation must terminate without inventing a free site");
        GeneticAlgorithm one(parse, board, router, 1, 1, 1, 1);
        one.populations = {valid};
        one.reserve_the_best();
        one.select_next_generation();
        require(one.populations.size() == 1 && one.getRoutes() == originalRoutes,
                "One-member population selection lost its incumbent");
        std::cout << "Heuristic fixed-clock legality, incumbent, finite-area and bounded-mutation regressions passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Heuristic layout regression failed: " << error.what() << '\n';
        return 1;
    }
}
