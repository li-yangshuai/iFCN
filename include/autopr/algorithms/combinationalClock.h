#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fcngraph
{
// One variable per occupied coarse clock tile. Shared fanout trunks and
// crossings therefore share an absolute epoch, not only an epoch modulo P.
// This conservative fixed-geometry contract can reject a geometry that would
// require independent token epochs at a crossing; callers must reroute it.
struct CombinationalClockProblem
{
    std::size_t tileCount = 0;
    std::vector<std::vector<std::size_t>> routes;
    std::vector<std::size_t> primaryInputs;
    int maxSamePhaseTiles = 4;
};

// Integer difference constraints are complete for this absolute-epoch model.
// Unlike the sequential solver, no modular-resource or iteration choices
// remain, so Bellman-Ford needs neither DFS nor an external SMT dependency.
// fixedPhases, when supplied, validates an existing assignment by fixing every
// adjacent hold/advance rather than choosing a replacement assignment.
inline bool solveCombinationalClockEpochs(
    const CombinationalClockProblem &problem,
    std::vector<std::int64_t> &epochs,
    std::string *error = nullptr,
    const std::vector<int> *fixedPhases = nullptr,
    int phaseCount = 4)
{
    const auto fail = [error](const char *message) {
        if (error) *error = message;
        return false;
    };
    epochs.clear();
    if (phaseCount < 2 || phaseCount > 8 || problem.tileCount == 0 ||
        problem.routes.empty() || problem.primaryInputs.empty() ||
        problem.maxSamePhaseTiles < 1 ||
        (fixedPhases && fixedPhases->size() != problem.tileCount))
        return fail("invalid combinational clock problem");

    struct Bound { std::size_t from, to; std::int64_t upper; };
    std::vector<Bound> bounds;
    const std::size_t zero = problem.tileCount;
    std::vector<bool> isPrimaryInput(problem.tileCount, false);
    for (std::size_t tile = 0; tile < problem.tileCount; ++tile) {
        bounds.push_back({tile, zero, 0}); // epoch(tile) >= 0
        if (fixedPhases && ((*fixedPhases)[tile] < 1 ||
                            (*fixedPhases)[tile] > phaseCount))
            return fail("phase outside the clock cycle");
    }
    for (const auto input : problem.primaryInputs) {
        if (input >= problem.tileCount ||
            (fixedPhases && (*fixedPhases)[input] != 1))
            return fail("primary inputs must share epoch zero and phase one");
        isPrimaryInput[input] = true;
        bounds.push_back({zero, input, 0});
    }
    for (const auto &route : problem.routes) {
        if (route.size() < 2)
            return fail("logical route requires distinct source and sink");
        for (const auto tile : route)
            if (tile >= problem.tileCount)
                return fail("route refers to an unknown clock tile");
        for (std::size_t i = 1; i < route.size(); ++i) {
            const auto from = route[i - 1], to = route[i];
            if (fixedPhases) {
                const int delta = ((*fixedPhases)[to] - (*fixedPhases)[from] +
                                   phaseCount) % phaseCount;
                if (delta > 1) return fail("route clock step is not hold or advance");
                if (i == 1 && !isPrimaryInput[route.front()] && delta != 1)
                    return fail("logic gate output port must advance into the next phase");
                if (i + 1 == route.size() && delta != 1)
                    return fail("logic gate input port must advance into the gate phase");
                bounds.push_back({from, to, delta});
                bounds.push_back({to, from, -delta});
            } else {
                bounds.push_back({from, to, 1});
                bounds.push_back({to, from, 0});
            }
            // The last wire tile must hold its value while the destination
            // gate switches; sharing its phase lets the fixed gate arm drive
            // the fanin wire backwards in the physical model.
            // Likewise a driven gate output must hold while its first wire
            // tile switches. Externally clamped primary inputs do not need
            // this additional output-isolation constraint.
            if (i + 1 == route.size() ||
                (i == 1 && !isPrimaryInput[route.front()]))
                bounds.push_back({to, from, -1});
            const auto limit = static_cast<std::size_t>(problem.maxSamePhaseTiles);
            if (i >= limit)
                bounds.push_back({route[i], route[i - limit], -1});
        }
        // Distinct logic events may never be connected entirely in one phase.
        bounds.push_back({route.back(), route.front(), -1});
    }

    std::vector<std::int64_t> distance(problem.tileCount + 1, 0);
    bool changed = false;
    for (std::size_t pass = 0; pass < distance.size(); ++pass) {
        changed = false;
        for (const auto &bound : bounds) {
            const auto candidate = distance[bound.from] + bound.upper;
            if (distance[bound.to] > candidate) {
                distance[bound.to] = candidate;
                changed = true;
            }
        }
        if (!changed) break;
    }
    if (changed) return fail("no globally synchronized clock epochs on this geometry");
    epochs.resize(problem.tileCount);
    for (std::size_t tile = 0; tile < problem.tileCount; ++tile) {
        epochs[tile] = distance[tile] - distance[zero];
        if (fixedPhases && epochs[tile] % phaseCount + 1 != (*fixedPhases)[tile])
            return fail("clock phase disagrees with the input-anchored absolute epoch");
    }
    if (error) error->clear();
    return true;
}
} // namespace fcngraph
