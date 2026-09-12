#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fcngraph {
class Parse;
class CircuitGraph;

struct CombinationalLayoutMetrics {
    bool valid = false;
    std::string error;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint64_t area = 0;
    std::size_t occupiedTiles = 0;
    std::size_t physicalCells = 0;
    std::size_t routeLength = 0;
};

// Shared acceptance and area objective for irregular-clock placement seeds.
// Crossings are checked on realized physical sites, so a routing strategy's
// optional single-crossing restriction is not mistaken for universal DRC.
// This is a structural/timing check; source equivalence and physical truth are
// independent checks performed by the benchmark and workflow layers.
CombinationalLayoutMetrics validateCombinationalLayout(
    Parse& parse, CircuitGraph& graph, int phaseCount = 4, int maxSamePhase = 4);
}
