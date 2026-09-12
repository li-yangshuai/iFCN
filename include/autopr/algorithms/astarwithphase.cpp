#include "astarwithphase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace fcngraph {
namespace {

constexpr double kHeuristicWeight = 2.20;

int nextPhase(int phase, int phaseCount)
{
    return phase % phaseCount + 1;
}

struct SearchState
{
    position pos{0, 0};
    int phase = 1;
    int sameRun = 1;
    int direction = -1;

    bool operator==(const SearchState &other) const noexcept
    {
        return pos == other.pos && phase == other.phase &&
               sameRun == other.sameRun && direction == other.direction;
    }
};

struct SearchStateHash
{
    std::size_t operator()(const SearchState &state) const noexcept
    {
        std::size_t value = PositionHash{}(state.pos);
        value ^= static_cast<std::size_t>(state.phase + 17 * state.sameRun + 131 * (state.direction + 1))
                 * 0x9e3779b97f4a7c15ULL;
        return value;
    }
};

struct QueueEntry
{
    double f = 0.0;
    int h = 0;
    std::uint64_t serial = 0;
    SearchState state;
};

struct QueueGreater
{
    bool operator()(const QueueEntry &left, const QueueEntry &right) const noexcept
    {
        if (left.f != right.f)
        {
            return left.f > right.f;
        }
        if (left.h != right.h)
        {
            return left.h > right.h;
        }
        return left.serial > right.serial;
    }
};

int manhattan(const position &left, const position &right)
{
    return std::abs(static_cast<int>(left.first) - static_cast<int>(right.first)) +
           std::abs(static_cast<int>(left.second) - static_cast<int>(right.second));
}

} // namespace

PhaseAwareAstar::PhaseAwareAstar(GridChessboard &board,
                                 int phases,
                                 int samePhaseLimit,
                                 double searchCost,
                                 int patternOffset)
    : chessboard(board),
      phaseCount(std::max(2, phases)),
      maxSamePhase(std::max(1, samePhaseLimit)),
      maxSearchCost(std::max(16.0, searchCost)),
      phasePatternOffset(patternOffset)
{
}

void PhaseAwareAstar::reset()
{
    inDirections.clear();
    outDirections.clear();
    sourceRouteCells.clear();
    wireOwnership.clear();
    lastErrorMessage.clear();
}

bool PhaseAwareAstar::isNodeCell(const position &pos) const
{
    const auto cell = chessboard.gridMap.find(pos);
    if (cell == chessboard.gridMap.end())
    {
        return false;
    }
    const auto weight = cell->second.get_current_weight();
    return weight >= NODE_WEIGHT && ((weight - NODE_WEIGHT) % WIRE_WEIGHT == 0);
}

bool PhaseAwareAstar::isPassable(const position &pos,
                                 const position &start,
                                 const position &goal) const
{
    if (pos == start || pos == goal)
    {
        return true;
    }

    const auto cell = chessboard.gridMap.find(pos);
    if (isNodeCell(pos))
    {
        return false;
    }

    // Keep the one-cell port halo of unrelated gates free.  Without this,
    // early long nets can legally consume every neighbor of a later gate and
    // make the final fanin/fanout connection impossible even on a sparse grid.
    static constexpr std::array<int, 4> haloDx{{1, 0, -1, 0}};
    static constexpr std::array<int, 4> haloDy{{0, 1, 0, -1}};
    for (int direction = 0; direction < 4; ++direction)
    {
        const int nx = static_cast<int>(pos.first) + haloDx[direction];
        const int ny = static_cast<int>(pos.second) + haloDy[direction];
        if (nx < 0 || ny < 0)
        {
            continue;
        }
        const position adjacent{static_cast<unsigned int>(nx), static_cast<unsigned int>(ny)};
        if (adjacent != start && adjacent != goal && isNodeCell(adjacent))
        {
            return false;
        }
    }
    if (cell == chessboard.gridMap.end() || cell->second.get_current_weight() == 0)
    {
        return true;
    }
    const auto sourceTree = sourceRouteCells.find(start);
    if (sourceTree != sourceRouteCells.end() && sourceTree->second.count(pos) != 0)
    {
        return true;
    }
    return cell->second.get_current_weight() <= WIRE_WEIGHT;
}

bool PhaseAwareAstar::sourceDirectionAllowed(const position &start,
                                             const position &neighbor) const
{
    const auto incoming = inDirections.equal_range(start);
    for (auto iter = incoming.first; iter != incoming.second; ++iter)
    {
        if (iter->second == neighbor)
        {
            return false;
        }
    }
    return true;
}

bool PhaseAwareAstar::targetDirectionAllowed(const position &goal,
                                             const position &neighbor) const
{
    const auto incoming = inDirections.equal_range(goal);
    for (auto iter = incoming.first; iter != incoming.second; ++iter)
    {
        if (iter->second == neighbor)
        {
            return false;
        }
    }
    const auto outgoing = outDirections.find(goal);
    return outgoing == outDirections.end() || outgoing->second != neighbor;
}

std::optional<PhaseAwareRoute> PhaseAwareAstar::findPath(const position &start,
                                                         const position &goal,
                                                         int preferredStartPhase,
                                                         bool reuseFanoutTrunk)
{
    lastErrorMessage.clear();
    if (start == goal)
    {
        lastErrorMessage = "source and target occupy the same grid";
        return std::nullopt;
    }

    const int margin = std::max(8, std::min(96, static_cast<int>(std::ceil(maxSearchCost / 3.0))));
    unsigned int occupiedMinX = std::min(start.first, goal.first);
    unsigned int occupiedMaxX = std::max(start.first, goal.first);
    unsigned int occupiedMinY = std::min(start.second, goal.second);
    unsigned int occupiedMaxY = std::max(start.second, goal.second);
    for (const auto &entry : chessboard.gridMap)
    {
        if (entry.second.get_current_weight() == 0)
        {
            continue;
        }
        occupiedMinX = std::min(occupiedMinX, entry.first.first);
        occupiedMaxX = std::max(occupiedMaxX, entry.first.first);
        occupiedMinY = std::min(occupiedMinY, entry.first.second);
        occupiedMaxY = std::max(occupiedMaxY, entry.first.second);
    }
    const unsigned int minX = occupiedMinX > static_cast<unsigned int>(margin)
        ? occupiedMinX - static_cast<unsigned int>(margin) : 0u;
    const unsigned int minY = occupiedMinY > static_cast<unsigned int>(margin)
        ? occupiedMinY - static_cast<unsigned int>(margin) : 0u;
    const unsigned int maxX = occupiedMaxX + static_cast<unsigned int>(margin);
    const unsigned int maxY = occupiedMaxY + static_cast<unsigned int>(margin);

    static constexpr std::array<int, 4> dx{{1, 0, -1, 0}};
    static constexpr std::array<int, 4> dy{{0, 1, 0, -1}};
    const auto insideSearchBounds = [&](int x, int y) {
        return x >= static_cast<int>(minX) && y >= static_cast<int>(minY) &&
               x <= static_cast<int>(maxX) && y <= static_cast<int>(maxY);
    };
    const auto hasLegalPort = [&](const position &node, bool sourcePort) {
        for (int direction = 0; direction < 4; ++direction)
        {
            const int nx = static_cast<int>(node.first) + dx[direction];
            const int ny = static_cast<int>(node.second) + dy[direction];
            if (!insideSearchBounds(nx, ny))
            {
                continue;
            }
            const position neighbor{static_cast<unsigned int>(nx),
                                    static_cast<unsigned int>(ny)};
            if (!isPassable(neighbor, start, goal))
            {
                continue;
            }
            if (sourcePort ? sourceDirectionAllowed(start, neighbor)
                           : targetDirectionAllowed(goal, neighbor))
            {
                return true;
            }
        }
        return false;
    };
    if (!hasLegalPort(start, true))
    {
        lastErrorMessage = "source has no legal routing port";
        return std::nullopt;
    }
    if (!hasLegalPort(goal, false))
    {
        lastErrorMessage = "target has no legal routing port";
        return std::nullopt;
    }

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueGreater> open;
    std::unordered_map<SearchState, double, SearchStateHash> bestCost;
    std::unordered_map<SearchState, SearchState, SearchStateHash> parent;
    std::uint64_t serial = 0;

    const auto pathAlreadyUsesSource = [&](SearchState current,
                                           const position &source) {
        while (true)
        {
            const auto ownership = wireOwnership.find(current.pos);
            if (ownership != wireOwnership.end() &&
                ownership->second.find(source) != ownership->second.end())
            {
                return true;
            }
            const auto previous = parent.find(current);
            if (previous == parent.end())
            {
                break;
            }
            current = previous->second;
        }
        return false;
    };
    const auto canContinueFromCurrent = [&](const SearchState &current,
                                             int direction) {
        const auto ownership = wireOwnership.find(current.pos);
        if (ownership == wireOwnership.end())
        {
            return true;
        }
        for (const auto &sourceUse : ownership->second)
        {
            if (sourceUse.first != start)
            {
                return current.direction >= 0 && current.direction == direction;
            }
        }
        return true;
    };
    const auto canEnterWireCell = [&](const SearchState &current,
                                      const position &neighbor,
                                      int direction) {
        const auto ownership = wireOwnership.find(neighbor);
        if (ownership == wireOwnership.end())
        {
            return true;
        }
        if (ownership->second.size() != 1)
        {
            return false;
        }
        const auto &existingUse = *ownership->second.begin();
        if (existingUse.first == start)
        {
            const auto sourceTree = sourceRouteCells.find(start);
            const bool currentOnTree = sourceTree != sourceRouteCells.end() &&
                                       sourceTree->second.count(current.pos) != 0;
            return reuseFanoutTrunk && currentOnTree;
        }
        if (existingUse.second.size() != 1 ||
            existingUse.second.count(WireOrientation::Bend) != 0)
        {
            return false;
        }
        const WireOrientation crossingDirection = direction == 0 || direction == 2
            ? WireOrientation::Horizontal : WireOrientation::Vertical;
        if (existingUse.second.count(crossingDirection) != 0)
        {
            return false;
        }
        if (pathAlreadyUsesSource(current, existingUse.first))
        {
            return false;
        }
        return true;
    };

    const auto startCell = chessboard.gridMap.find(start);
    const int fixedStartPhase = startCell == chessboard.gridMap.end()
        ? -1 : startCell->second.getPhase();
    // Deterministic routing attempts already rotate preferredStartPhase.  Seed
    // just that phase here instead of multiplying every geometric state by all
    // phases.  The flexible compaction pass (negative pattern offset) retains
    // the exhaustive phase seeds.
    const bool usePreferredSeed = fixedStartPhase < 1 &&
        phasePatternOffset >= 0 && preferredStartPhase >= 1 &&
        preferredStartPhase <= phaseCount;
    for (int phase = 1; phase <= phaseCount; ++phase)
    {
        if (fixedStartPhase >= 1 && phase != fixedStartPhase)
        {
            continue;
        }
        if (usePreferredSeed && phase != preferredStartPhase)
        {
            continue;
        }
        SearchState seed{start, phase, 1, -1};
        const double preferencePenalty = preferredStartPhase >= 1 && phase != preferredStartPhase
            ? 0.20 : 0.0;
        bestCost[seed] = preferencePenalty;
        const int h = manhattan(start, goal);
        open.push({preferencePenalty + kHeuristicWeight * h, h, serial++, seed});
    }

    const int directDistance = manhattan(start, goal);
    const std::size_t maxExpandedStates = phasePatternOffset < 0
        ? 30000u
        : static_cast<std::size_t>(std::clamp(
              2000 + static_cast<int>(maxSearchCost * 18.0) + directDistance * 48,
              4000,
              16000));
    std::size_t expandedStates = 0;
    std::optional<SearchState> reached;

    while (!open.empty() && expandedStates++ < maxExpandedStates)
    {
        const QueueEntry entry = open.top();
        open.pop();
        const SearchState current = entry.state;
        const auto currentBest = bestCost.find(current);
        if (currentBest == bestCost.end())
        {
            continue;
        }
        if (currentBest->second +
                kHeuristicWeight * manhattan(current.pos, goal) + 1e-9 < entry.f)
        {
            continue;
        }
        if (currentBest->second + manhattan(current.pos, goal) > maxSearchCost)
        {
            continue;
        }
        if (current.pos == goal)
        {
            reached = current;
            break;
        }

        for (int direction = 0; direction < 4; ++direction)
        {
            if (!canContinueFromCurrent(current, direction))
            {
                continue;
            }
            const int nx = static_cast<int>(current.pos.first) + dx[direction];
            const int ny = static_cast<int>(current.pos.second) + dy[direction];
            if (nx < static_cast<int>(minX) || ny < static_cast<int>(minY) ||
                nx > static_cast<int>(maxX) || ny > static_cast<int>(maxY))
            {
                continue;
            }
            const position neighbor{static_cast<unsigned int>(nx), static_cast<unsigned int>(ny)};
            if (!isPassable(neighbor, start, goal))
            {
                continue;
            }
            if (neighbor != goal && !canEnterWireCell(current, neighbor, direction))
            {
                continue;
            }
            if (current.pos == start && !sourceDirectionAllowed(start, neighbor))
            {
                continue;
            }
            if (neighbor == goal && !targetDirectionAllowed(goal, current.pos))
            {
                continue;
            }

            const auto neighborCell = chessboard.gridMap.find(neighbor);
            const int fixedPhase = neighborCell == chessboard.gridMap.end()
                ? -1 : neighborCell->second.getPhase();
            const int advancedPhase = nextPhase(current.phase, phaseCount);
            int phaseCandidates[2] = {advancedPhase, advancedPhase};
            int candidateCount = 1;
            if (fixedPhase >= 1)
            {
                candidateCount = 0;
                if (fixedPhase == advancedPhase)
                {
                    phaseCandidates[candidateCount++] = advancedPhase;
                }
                if (fixedPhase == current.phase)
                {
                    phaseCandidates[candidateCount++] = current.phase;
                }
            }
            else
            {
                if (phasePatternOffset < 0)
                {
                    phaseCandidates[0] = advancedPhase;
                    phaseCandidates[1] = current.phase;
                    candidateCount = 2;
                }
                else
                {
                    phaseCandidates[0] = advancedPhase;
                    // One optional launch wait changes the parity class without
                    // multiplying phase branches over the complete maze.
                    if (current.pos == start)
                    {
                        phaseCandidates[1] = current.phase;
                        candidateCount = 2;
                        if ((phasePatternOffset & 1) != 0)
                        {
                            std::swap(phaseCandidates[0], phaseCandidates[1]);
                        }
                    }
                }
            }
            for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
            {
                const int phase = phaseCandidates[candidateIndex];
                const int sameRun = phase == current.phase ? current.sameRun + 1 : 1;
                if (sameRun > maxSamePhase)
                {
                    continue;
                }

                double stepCost = 1.0;
                if (phase == current.phase)
                {
                    stepCost += 0.18;
                }
                if (current.direction >= 0 && current.direction != direction)
                {
                    stepCost += 0.12;
                }
                if (neighborCell != chessboard.gridMap.end() &&
                    neighborCell->second.get_current_weight() > 0 && neighbor != goal)
                {
                    const auto sourceTree = sourceRouteCells.find(start);
                    const bool sameTree = sourceTree != sourceRouteCells.end() &&
                                          sourceTree->second.count(neighbor) != 0;
                    stepCost += sameTree ? -0.35 : 3.0;
                }
                const auto establishedOutput = outDirections.find(start);
                if (reuseFanoutTrunk && current.pos == start &&
                    establishedOutput != outDirections.end() && establishedOutput->second != neighbor)
                {
                    stepCost += 2.0;
                }

                SearchState next{neighbor, phase, sameRun, direction};
                const double candidateCost = currentBest->second + stepCost;
                const auto known = bestCost.find(next);
                if (known != bestCost.end() && known->second <= candidateCost)
                {
                    continue;
                }
                bestCost[next] = candidateCost;
                parent[next] = current;
                const int h = manhattan(neighbor, goal);
                const double f = candidateCost + kHeuristicWeight * h;
                if (candidateCost + h <= maxSearchCost)
                {
                    open.push({f, h, serial++, next});
                }
            }
        }
    }

    if (!reached.has_value())
    {
        std::ostringstream detail;
        detail << "no phase-aware path; distance=" << manhattan(start, goal)
               << ", expanded=" << expandedStates
               << ", remaining-open=" << open.size()
               << ", cost-limit=" << maxSearchCost;
        lastErrorMessage = detail.str();
        return std::nullopt;
    }

    PhaseAwareRoute route;
    SearchState cursor = reached.value();
    while (true)
    {
        route.positions.push_back(cursor.pos);
        route.phases.push_back(cursor.phase);
        const auto previous = parent.find(cursor);
        if (previous == parent.end())
        {
            break;
        }
        cursor = previous->second;
    }
    std::reverse(route.positions.begin(), route.positions.end());
    std::reverse(route.phases.begin(), route.phases.end());
    if (!commit(route, start, goal))
    {
        lastErrorMessage = "phase/capacity conflict while committing path";
        return std::nullopt;
    }
    return route;
}

bool PhaseAwareAstar::commit(const PhaseAwareRoute &route,
                             const position &start,
                             const position &goal)
{
    if (route.positions.size() < 2 || route.positions.size() != route.phases.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < route.positions.size(); ++index)
    {
        const position pos = route.positions[index];
        auto existing = chessboard.gridMap.find(pos);
        if (existing != chessboard.gridMap.end())
        {
            const int phase = existing->second.getPhase();
            if (phase >= 1 && phase != route.phases[index])
            {
                return false;
            }
        }
    }

    auto &sourceTree = sourceRouteCells[start];
    for (std::size_t index = 0; index < route.positions.size(); ++index)
    {
        const position pos = route.positions[index];
        const bool reusedSourceCell = sourceTree.count(pos) != 0;
        auto &cell = chessboard.gridMap[pos];
        if (!reusedSourceCell)
        {
            cell.put_wire();
        }
        cell.setPhase(route.phases[index]);
        sourceTree.insert(pos);
    }

    for (std::size_t index = 1; index + 1 < route.positions.size(); ++index)
    {
        const position &previous = route.positions[index - 1];
        const position &current = route.positions[index];
        const position &next = route.positions[index + 1];
        WireOrientation orientation = WireOrientation::Bend;
        if (previous.second == current.second && current.second == next.second)
        {
            orientation = WireOrientation::Horizontal;
        }
        else if (previous.first == current.first && current.first == next.first)
        {
            orientation = WireOrientation::Vertical;
        }
        wireOwnership[current][start].insert(orientation);
    }

    inDirections.insert({goal, route.positions[route.positions.size() - 2]});
    outDirections.emplace(start, route.positions[1]);
    return true;
}

} // namespace fcngraph
