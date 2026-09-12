#pragma once

#include "autopr/grid/grid.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fcngraph {

/** A routed path whose clock phases were chosen during path search. */
struct PhaseAwareRoute
{
    std::vector<position> positions;
    std::vector<int> phases;
};

/**
 * Four-direction maze router for an irregular (stochastic) clock field.
 *
 * Phase is part of the search state.  A move may retain the current phase or
 * advance by one modulo phaseCount, and already occupied cells constrain the
 * phase of a crossing.  This is deliberately independent of the fixed-clock
 * Astar class used by the 2DDWave flow.
 */
class PhaseAwareAstar
{
public:
    PhaseAwareAstar(GridChessboard &chessboard,
                    int phaseCount = 4,
                    int maxSamePhase = 4,
                    double maxSearchCost = 160.0,
                    int phasePatternOffset = 0);

    std::optional<PhaseAwareRoute> findPath(const position &start,
                                            const position &goal,
                                            int preferredStartPhase = -1,
                                            bool reuseFanoutTrunk = false);
    void reset();
    const std::string &lastError() const { return lastErrorMessage; }

private:
    bool isNodeCell(const position &pos) const;
    bool isPassable(const position &pos,
                    const position &start,
                    const position &goal) const;
    bool sourceDirectionAllowed(const position &start, const position &neighbor) const;
    bool targetDirectionAllowed(const position &goal, const position &neighbor) const;
    bool commit(const PhaseAwareRoute &route,
                const position &start,
                const position &goal);

    GridChessboard &chessboard;
    int phaseCount;
    int maxSamePhase;
    double maxSearchCost;
    int phasePatternOffset;
    std::multimap<position, position> inDirections;
    std::map<position, position> outDirections;
    std::map<position, std::set<position>> sourceRouteCells;
    enum class WireOrientation
    {
        Horizontal,
        Vertical,
        Bend
    };
    std::map<position, std::map<position, std::set<WireOrientation>>> wireOwnership;
    std::string lastErrorMessage;
};

} // namespace fcngraph
