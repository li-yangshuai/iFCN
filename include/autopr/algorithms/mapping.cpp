#include"mapping.h"

#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fcngraph{
namespace {

struct ShiftedPosition {
    position pos{0, 0};
    bool valid = false;
};

ShiftedPosition shiftedCell(const position& base, int dx, int dy)
{
    const auto x = static_cast<long long>(base.first) + dx;
    const auto y = static_cast<long long>(base.second) + dy;
    const auto maxCoord = static_cast<long long>(std::numeric_limits<unsigned int>::max());
    if (x < 0 || y < 0 || x > maxCoord || y > maxCoord) {
        return {};
    }
    return {{static_cast<unsigned int>(x), static_cast<unsigned int>(y)}, true};
}

bool routeBoundaryCell(const position& cell,
                       const position& gatePos,
                       const position& neighborGate,
                       bool neighborIsNode)
{
    const unsigned int baseX = gatePos.first * 5;
    const unsigned int baseY = gatePos.second * 5;

    if (neighborGate.first < gatePos.first && neighborGate.second == gatePos.second) {
        return cell.first == baseX && (!neighborIsNode || cell.second == baseY + 2);
    }
    if (neighborGate.first > gatePos.first && neighborGate.second == gatePos.second) {
        return cell.first == baseX + 4 && (!neighborIsNode || cell.second == baseY + 2);
    }
    if (neighborGate.second < gatePos.second && neighborGate.first == gatePos.first) {
        return cell.second == baseY && (!neighborIsNode || cell.first == baseX + 2);
    }
    if (neighborGate.second > gatePos.second && neighborGate.first == gatePos.first) {
        return cell.second == baseY + 4 && (!neighborIsNode || cell.first == baseX + 2);
    }
    return false;
}

ShiftedPosition nodeBoundaryCell(const position& gatePos, const position& neighborGate)
{
    const unsigned int baseX = gatePos.first * 5;
    const unsigned int baseY = gatePos.second * 5;

    if (neighborGate.first < gatePos.first && neighborGate.second == gatePos.second) {
        return {{baseX, baseY + 2}, true};
    }
    if (neighborGate.first > gatePos.first && neighborGate.second == gatePos.second) {
        return {{baseX + 4, baseY + 2}, true};
    }
    if (neighborGate.second < gatePos.second && neighborGate.first == gatePos.first) {
        return {{baseX + 2, baseY}, true};
    }
    if (neighborGate.second > gatePos.second && neighborGate.first == gatePos.first) {
        return {{baseX + 2, baseY + 4}, true};
    }
    return {};
}

unsigned int manhattanDistance(const position& left, const position& right)
{
    const unsigned int dx = left.first > right.first
                                ? left.first - right.first
                                : right.first - left.first;
    const unsigned int dy = left.second > right.second
                                ? left.second - right.second
                                : right.second - left.second;
    return dx + dy;
}

std::vector<position> bridgeBetween(position start, const position& target)
{
    std::vector<position> bridge;
    bridge.push_back(start);
    while (start.first != target.first) {
        if (start.first < target.first) {
            ++start.first;
        } else {
            --start.first;
        }
        if (bridge.back() != start) {
            bridge.push_back(start);
        }
    }
    while (start.second != target.second) {
        if (start.second < target.second) {
            ++start.second;
        } else {
            --start.second;
        }
        if (bridge.back() != start) {
            bridge.push_back(start);
        }
    }
    return bridge;
}

void connectUnitMappingToNodeBoundary(std::vector<position>& unitMapping,
                                      const position& gatePos,
                                      const position& neighborGate,
                                      bool prepend)
{
    const auto boundary = nodeBoundaryCell(gatePos, neighborGate);
    if (!boundary.valid || unitMapping.empty()) {
        return;
    }

    if (std::find(unitMapping.begin(), unitMapping.end(), boundary.pos) != unitMapping.end()) {
        return;
    }

    auto nearestIt = unitMapping.begin();
    unsigned int bestDistance = manhattanDistance(*nearestIt, boundary.pos);
    for (auto it = std::next(unitMapping.begin()); it != unitMapping.end(); ++it) {
        const unsigned int distance = manhattanDistance(*it, boundary.pos);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearestIt = it;
        }
    }

    std::vector<position> bridge = prepend
                                       ? bridgeBetween(boundary.pos, *nearestIt)
                                       : bridgeBetween(*nearestIt, boundary.pos);
    if (bridge.empty()) {
        return;
    }

    if (prepend) {
        if (bridge.back() == *nearestIt) {
            bridge.pop_back();
        }
        unitMapping.insert(unitMapping.begin(), bridge.begin(), bridge.end());
    } else {
        if (bridge.front() == *nearestIt) {
            bridge.erase(bridge.begin());
        }
        unitMapping.insert(unitMapping.end(), bridge.begin(), bridge.end());
    }
}

void connectSegmentToBoundary(std::vector<position>& segment,
                              const position& boundary,
                              bool prepend)
{
    if (segment.empty() ||
        std::find(segment.begin(), segment.end(), boundary) != segment.end()) {
        return;
    }

    auto nearestIt = segment.begin();
    unsigned int bestDistance = manhattanDistance(*nearestIt, boundary);
    for (auto it = std::next(segment.begin()); it != segment.end(); ++it) {
        const unsigned int distance = manhattanDistance(*it, boundary);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearestIt = it;
        }
    }

    std::vector<position> bridge = prepend
                                       ? bridgeBetween(boundary, *nearestIt)
                                       : bridgeBetween(*nearestIt, boundary);
    if (bridge.empty()) {
        return;
    }

    if (prepend) {
        if (bridge.back() == *nearestIt) {
            bridge.pop_back();
        }
        segment.insert(segment.begin(), bridge.begin(), bridge.end());
    } else {
        if (bridge.front() == *nearestIt) {
            bridge.erase(bridge.begin());
        }
        segment.insert(segment.end(), bridge.begin(), bridge.end());
    }
}

void connectRouteMappingsToOriginalEndpoints(
    std::map<std::pair<position, position>, std::vector<std::vector<position>>>& routeMappings,
    const std::vector<std::vector<position>>& routes)
{
    for (const auto& route : routes) {
        if (route.size() < 2) {
            continue;
        }

        auto mappingIt = routeMappings.find({route.front(), route.back()});
        if (mappingIt == routeMappings.end()) {
            continue;
        }

        if (route.size() == 2 && mappingIt->second.empty()) {
            const auto startBoundary = nodeBoundaryCell(route.front(), route.back());
            const auto endBoundary = nodeBoundaryCell(route.back(), route.front());
            if (startBoundary.valid && endBoundary.valid) {
                auto bridge = bridgeBetween(startBoundary.pos, endBoundary.pos);
                if (!bridge.empty()) {
                    mappingIt->second.push_back(std::move(bridge));
                }
            }
            continue;
        }

        if (mappingIt->second.empty()) {
            continue;
        }

        const auto startBoundary = nodeBoundaryCell(route[1], route.front());
        if (startBoundary.valid) {
            connectSegmentToBoundary(mappingIt->second.front(), startBoundary.pos, true);
        }

        const auto endBoundary = nodeBoundaryCell(route[route.size() - 2], route.back());
        if (endBoundary.valid) {
            connectSegmentToBoundary(mappingIt->second.back(), endBoundary.pos, false);
        }
    }
}

void appendUniqueCell(std::vector<position>& cells, const position& cell)
{
    if (std::find(cells.begin(), cells.end(), cell) == cells.end()) {
        cells.push_back(cell);
    }
}

bool fallbackLogicGateMapping(
    std::map<std::string, std::vector<position>>& nodeCells,
    const position& gatePos,
    const std::vector<position>& inputs,
    const std::vector<position>& outputs,
    const std::string& fixedBucket)
{
    const position center{gatePos.first * 5 + 2, gatePos.second * 5 + 2};
    std::vector<position> usedBoundaries;

    const auto addNormalArm = [&](const position& boundary) {
        appendUniqueCell(usedBoundaries, boundary);
        for (const position& cell : bridgeBetween(boundary, center)) {
            appendUniqueCell(nodeCells["normal"], cell);
        }
    };

    bool hasTerminal = false;
    for (const position& input : inputs) {
        const auto boundary = nodeBoundaryCell(gatePos, input);
        if (boundary.valid) {
            addNormalArm(boundary.pos);
            hasTerminal = true;
        }
    }
    for (const position& output : outputs) {
        const auto boundary = nodeBoundaryCell(gatePos, output);
        if (boundary.valid) {
            addNormalArm(boundary.pos);
            hasTerminal = true;
        }
    }

    if (!hasTerminal) {
        return false;
    }

    const unsigned int baseX = gatePos.first * 5;
    const unsigned int baseY = gatePos.second * 5;
    const std::vector<position> fixedCandidates{
        {baseX + 2, baseY},
        {baseX + 4, baseY + 2},
        {baseX + 2, baseY + 4},
        {baseX, baseY + 2},
    };

    for (const position& fixedBoundary : fixedCandidates) {
        if (std::find(usedBoundaries.begin(), usedBoundaries.end(), fixedBoundary) != usedBoundaries.end()) {
            continue;
        }
        appendUniqueCell(nodeCells[fixedBucket], fixedBoundary);
        const auto bridge = bridgeBetween(fixedBoundary, center);
        for (std::size_t index = 1; index < bridge.size(); ++index) {
            appendUniqueCell(nodeCells["normal"], bridge[index]);
        }
        return true;
    }

    return true;
}

int portDirection(const position& gatePos, const position& neighborGate)
{
    if (neighborGate.first < gatePos.first && neighborGate.second == gatePos.second) return 0; // left
    if (neighborGate.first > gatePos.first && neighborGate.second == gatePos.second) return 1; // right
    if (neighborGate.second < gatePos.second && neighborGate.first == gatePos.first) return 2; // up
    if (neighborGate.second > gatePos.second && neighborGate.first == gatePos.first) return 3; // down
    return -1;
}

position transformLocalCell(const position& localCell, int transformIndex)
{
    const unsigned int x = localCell.first;
    const unsigned int y = localCell.second;
    switch (transformIndex) {
    case 0: return {x, y};
    case 1: return {4 - y, x};
    case 2: return {4 - x, 4 - y};
    case 3: return {y, 4 - x};
    case 4: return {4 - x, y};
    case 5: return {y, x};
    case 6: return {x, 4 - y};
    case 7: return {4 - y, 4 - x};
    default: return {x, y};
    }
}

int transformDirection(int direction, int transformIndex)
{
    position localPort{2, 2};
    if (direction == 0) localPort = {0, 2};
    else if (direction == 1) localPort = {4, 2};
    else if (direction == 2) localPort = {2, 0};
    else if (direction == 3) localPort = {2, 4};
    else return -1;

    const position transformed = transformLocalCell(localPort, transformIndex);
    if (transformed.first < 2 && transformed.second == 2) return 0;
    if (transformed.first > 2 && transformed.second == 2) return 1;
    if (transformed.second < 2 && transformed.first == 2) return 2;
    if (transformed.second > 2 && transformed.first == 2) return 3;
    return -1;
}

bool placeMultiOutputNotTemplate(std::map<std::string, std::vector<position>>& nodeCells,
                                 const position& gatePos,
                                 const std::vector<position>& inputs,
                                 const std::vector<position>& outputs)
{
    if (inputs.size() != 1 || outputs.size() != 2) {
        return false;
    }

    const int inputDirection = portDirection(gatePos, inputs.front());
    const int outputDirectionA = portDirection(gatePos, outputs.front());
    const int outputDirectionB = portDirection(gatePos, outputs.back());
    if (inputDirection < 0 || outputDirectionA < 0 || outputDirectionB < 0) {
        return false;
    }

    const std::set<int> targetOutputDirections{outputDirectionA, outputDirectionB};
    const std::vector<position> canonicalCells{
        {0, 1}, {1, 1},
        {1, 2}, {2, 2}, {3, 2}, {4, 2},
        {0, 3}, {1, 3}, {2, 3},
        {2, 4},
    };

    for (int transformIndex = 0; transformIndex < 8; ++transformIndex) {
        if (transformDirection(0, transformIndex) != inputDirection) {
            continue;
        }
        const std::set<int> transformedOutputDirections{
            transformDirection(1, transformIndex),
            transformDirection(3, transformIndex),
        };
        if (transformedOutputDirections != targetOutputDirections) {
            continue;
        }

        const unsigned int baseX = gatePos.first * 5;
        const unsigned int baseY = gatePos.second * 5;
        for (const position& localCell : canonicalCells) {
            const position transformed = transformLocalCell(localCell, transformIndex);
            appendUniqueCell(nodeCells["normal"], {baseX + transformed.first, baseY + transformed.second});
        }
        return true;
    }

    return false;
}

std::vector<position> shortestUnitPath(const std::vector<position>& unitMapping,
                                       const position& gatePos,
                                       const position& prevGate,
                                       const position& nextGate,
                                       bool prevIsNode,
                                       bool nextIsNode)
{
    if (unitMapping.size() <= 2 || (!prevIsNode && !nextIsNode)) {
        return unitMapping;
    }

    std::unordered_set<position, MappingPositionHash> unitCells(unitMapping.begin(), unitMapping.end());
    std::vector<position> starts;
    std::unordered_set<position, MappingPositionHash> ends;
    starts.reserve(unitMapping.size());

    if (prevIsNode) {
        for (const position& cell : unitMapping) {
            if (routeBoundaryCell(cell, gatePos, prevGate, true)) {
                starts.push_back(cell);
            }
        }
    } else {
        starts.push_back(unitMapping.front());
    }

    if (nextIsNode) {
        for (const position& cell : unitMapping) {
            if (routeBoundaryCell(cell, gatePos, nextGate, true)) {
                ends.insert(cell);
            }
        }
    } else {
        ends.insert(unitMapping.back());
    }

    if (starts.empty() || ends.empty()) {
        return unitMapping;
    }

    std::queue<position> pending;
    std::unordered_set<position, MappingPositionHash> visited;
    std::unordered_map<position, position, MappingPositionHash> parent;

    for (const position& start : starts) {
        if (visited.insert(start).second) {
            pending.push(start);
        }
    }

    position reached{};
    bool found = false;
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!pending.empty() && !found) {
        const position current = pending.front();
        pending.pop();

        if (ends.find(current) != ends.end()) {
            reached = current;
            found = true;
            break;
        }

        for (const auto& dir : dirs) {
            const auto next = shiftedCell(current, dir[0], dir[1]);
            if (!next.valid || unitCells.find(next.pos) == unitCells.end()) {
                continue;
            }
            if (visited.insert(next.pos).second) {
                parent[next.pos] = current;
                pending.push(next.pos);
            }
        }
    }

    if (!found) {
        return unitMapping;
    }

    std::vector<position> path;
    position current = reached;
    path.push_back(current);
    while (parent.find(current) != parent.end()) {
        current = parent[current];
        path.push_back(current);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

bool unitSegmentsTouch(const std::vector<position>& first,
                       const std::vector<position>& second)
{
    for (const position& left : first) {
        for (const position& right : second) {
            const unsigned int dx = left.first > right.first
                                        ? left.first - right.first
                                        : right.first - left.first;
            const unsigned int dy = left.second > right.second
                                        ? left.second - right.second
                                        : right.second - left.second;
            if (dx + dy <= 1) {
                return true;
            }
        }
    }
    return false;
}

void appendStepToward(std::vector<position>& segment,
                      position& current,
                      const position& target,
                      bool stepX)
{
    if (stepX) {
        if (current.first < target.first) {
            ++current.first;
        } else if (current.first > target.first) {
            --current.first;
        }
    } else {
        if (current.second < target.second) {
            ++current.second;
        } else if (current.second > target.second) {
            --current.second;
        }
    }

    if (segment.empty() || segment.back() != current) {
        segment.push_back(current);
    }
}

void bridgeUnitSegments(std::vector<position>& first,
                        const std::vector<position>& second)
{
    if (first.empty() || second.empty() || unitSegmentsTouch(first, second)) {
        return;
    }

    position start = first.front();
    position target = second.front();
    unsigned int bestDistance = std::numeric_limits<unsigned int>::max();
    for (const position& left : first) {
        for (const position& right : second) {
            const unsigned int dx = left.first > right.first
                                        ? left.first - right.first
                                        : right.first - left.first;
            const unsigned int dy = left.second > right.second
                                        ? left.second - right.second
                                        : right.second - left.second;
            const unsigned int distance = dx + dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                start = left;
                target = right;
            }
        }
    }

    if (bestDistance <= 1) {
        return;
    }

    position current = start;
    const unsigned int dx = current.first > target.first
                                ? current.first - target.first
                                : target.first - current.first;
    const unsigned int dy = current.second > target.second
                                ? current.second - target.second
                                : target.second - current.second;
    const bool stepYFirst = dx <= 1 && dy > 1;

    if (stepYFirst) {
        while (current.second != target.second) {
            appendStepToward(first, current, target, false);
        }
        while (current.first != target.first) {
            appendStepToward(first, current, target, true);
        }
    } else {
        while (current.first != target.first) {
            appendStepToward(first, current, target, true);
        }
        while (current.second != target.second) {
            appendStepToward(first, current, target, false);
        }
    }
}

void stitchRouteMapping(std::vector<std::vector<position>>& routeMapping)
{
    for (std::size_t index = 1; index < routeMapping.size(); ++index) {
        bridgeUnitSegments(routeMapping[index - 1], routeMapping[index]);
    }
}

using RouteMappingKey = std::pair<position, position>;
using RouteMappingList = std::map<RouteMappingKey, std::vector<std::vector<position>>>;

void removeCellsFromRouteMappings(RouteMappingList& routeMappings,
                                  const std::unordered_set<position, MappingPositionHash>& cellsToRemove)
{
    if (cellsToRemove.empty()) {
        return;
    }

    for (auto& routeEntry : routeMappings) {
        auto& segments = routeEntry.second;
        for (auto& segment : segments) {
            segment.erase(
                std::remove_if(segment.begin(),
                               segment.end(),
                               [&](const position& cell) {
                                   return cellsToRemove.find(cell) != cellsToRemove.end();
                               }),
                segment.end());
        }
        segments.erase(
            std::remove_if(segments.begin(),
                           segments.end(),
                           [](const std::vector<position>& segment) {
                               return segment.empty();
                           }),
            segments.end());
    }
}

enum class RouteDirection {
    None,
    Left,
    Right,
    Up,
    Down,
};

bool sameAxis(const position& left, const position& right)
{
    return left.first == right.first || left.second == right.second;
}

RouteDirection routeDirection(const position& from, const position& to)
{
    if (from.second == to.second) {
        if (to.first < from.first) return RouteDirection::Left;
        if (to.first > from.first) return RouteDirection::Right;
    }
    if (from.first == to.first) {
        if (to.second < from.second) return RouteDirection::Up;
        if (to.second > from.second) return RouteDirection::Down;
    }
    return RouteDirection::None;
}

std::vector<RouteDirection> incidentDirections(const position& gatePos,
                                               const std::vector<position>& neighbors)
{
    std::vector<RouteDirection> directions;
    directions.reserve(neighbors.size());
    for (const position& neighbor : neighbors) {
        const RouteDirection direction = routeDirection(gatePos, neighbor);
        if (direction != RouteDirection::None &&
            std::find(directions.begin(), directions.end(), direction) == directions.end()) {
            directions.push_back(direction);
        }
    }
    return directions;
}

bool hasDirection(const std::vector<RouteDirection>& directions,
                  RouteDirection direction)
{
    return std::find(directions.begin(), directions.end(), direction) != directions.end();
}

void appendUniqueNormalCell(std::map<std::string, std::vector<position>>& nodeCells,
                            const position& cell)
{
    appendUniqueCell(nodeCells["normal"], cell);
}

void connectIncidentPortsToCenter(std::map<std::string, std::vector<position>>& nodeCells,
                                  const position& gatePos,
                                  const std::vector<position>& inputs,
                                  const std::vector<position>& outputs,
                                  bool includeCenterCell)
{
    const position center{gatePos.first * 5 + 2, gatePos.second * 5 + 2};
    const auto addArm = [&](const position& neighbor) {
        const auto boundary = nodeBoundaryCell(gatePos, neighbor);
        if (!boundary.valid) {
            return;
        }

        const auto bridge = bridgeBetween(boundary.pos, center);
        for (const position& cell : bridge) {
            if (!includeCenterCell && cell == center) {
                continue;
            }
            appendUniqueNormalCell(nodeCells, cell);
        }
    };

    for (const position& input : inputs) {
        addArm(input);
    }
    for (const position& output : outputs) {
        addArm(output);
    }
}

bool mapOppositeInputFanout(std::map<std::string, std::vector<position>>& nodeCells,
                            const position& gatePos,
                            const std::vector<position>& outputs)
{
    if (outputs.size() != 2) {
        return false;
    }

    const auto directions = incidentDirections(gatePos, outputs);
    if (directions.size() != 2) {
        return false;
    }

    const unsigned int baseX = gatePos.first * 5;
    const unsigned int baseY = gatePos.second * 5;

    if (hasDirection(directions, RouteDirection::Up) &&
        hasDirection(directions, RouteDirection::Down)) {
        const std::vector<position> cells{
            {baseX + 2, baseY},
            {baseX + 2, baseY + 1},
            {baseX + 2, baseY + 3},
            {baseX + 2, baseY + 4},
        };
        for (const position& cell : cells) {
            appendUniqueNormalCell(nodeCells, cell);
        }
        return true;
    }

    if (hasDirection(directions, RouteDirection::Left) &&
        hasDirection(directions, RouteDirection::Right)) {
        const std::vector<position> cells{
            {baseX, baseY + 2},
            {baseX + 1, baseY + 2},
            {baseX + 3, baseY + 2},
            {baseX + 4, baseY + 2},
        };
        for (const position& cell : cells) {
            appendUniqueNormalCell(nodeCells, cell);
        }
        return true;
    }

    return false;
}

bool horizontalDirection(RouteDirection direction)
{
    return direction == RouteDirection::Left || direction == RouteDirection::Right;
}

bool verticalDirection(RouteDirection direction)
{
    return direction == RouteDirection::Up || direction == RouteDirection::Down;
}

bool sameDirectionAxis(RouteDirection left, RouteDirection right)
{
    return (horizontalDirection(left) && horizontalDirection(right)) ||
           (verticalDirection(left) && verticalDirection(right));
}

bool oppositeDirections(RouteDirection left, RouteDirection right)
{
    return (left == RouteDirection::Left && right == RouteDirection::Right) ||
           (left == RouteDirection::Right && right == RouteDirection::Left) ||
           (left == RouteDirection::Up && right == RouteDirection::Down) ||
           (left == RouteDirection::Down && right == RouteDirection::Up);
}

void appendBridgePath(std::vector<position>& cells, const std::vector<position>& bridge)
{
    for (const position& cell : bridge) {
        if (cells.empty() || cells.back() != cell) {
            cells.push_back(cell);
        }
    }
}

std::vector<position> centerlineUnitMapping(const position& gatePos,
                                            const position& prevGate,
                                            const position& nextGate)
{
    if (!sameAxis(gatePos, prevGate) || !sameAxis(gatePos, nextGate)) {
        return {};
    }

    const auto inputBoundary = nodeBoundaryCell(gatePos, prevGate);
    const auto outputBoundary = nodeBoundaryCell(gatePos, nextGate);
    if (!inputBoundary.valid || !outputBoundary.valid) {
        return {};
    }

    const position center{gatePos.first * 5 + 2, gatePos.second * 5 + 2};
    std::vector<position> cells;
    appendBridgePath(cells, bridgeBetween(inputBoundary.pos, center));
    appendBridgePath(cells, bridgeBetween(center, outputBoundary.pos));
    return cells;
}

std::vector<std::vector<position>> centerlineRouteMapping(const std::vector<position>& route)
{
    if (route.size() < 2) {
        return {};
    }

    if (route.size() == 2) {
        if (!sameAxis(route.front(), route.back())) {
            return {};
        }
        const auto startBoundary = nodeBoundaryCell(route.front(), route.back());
        const auto endBoundary = nodeBoundaryCell(route.back(), route.front());
        if (!startBoundary.valid || !endBoundary.valid) {
            return {};
        }
        return {bridgeBetween(startBoundary.pos, endBoundary.pos)};
    }

    std::vector<std::vector<position>> routeMapping;
    routeMapping.reserve(route.size() - 2);
    for (std::size_t index = 1; index + 1 < route.size(); ++index) {
        auto unitMapping = centerlineUnitMapping(route[index], route[index - 1], route[index + 1]);
        if (unitMapping.empty()) {
            return {};
        }
        routeMapping.push_back(std::move(unitMapping));
    }
    stitchRouteMapping(routeMapping);
    return routeMapping;
}

std::size_t uniqueCellCount(const std::vector<std::vector<position>>& routeMapping)
{
    std::unordered_set<position, MappingPositionHash> cells;
    for (const auto& segment : routeMapping) {
        cells.insert(segment.begin(), segment.end());
    }
    return cells.size();
}

std::size_t sharedPrefixLength(const std::vector<position>& first,
                               const std::vector<position>& second)
{
    const std::size_t limit = std::min(first.size(), second.size());
    std::size_t length = 0;
    while (length < limit && first[length] == second[length]) {
        ++length;
    }
    return length;
}

bool routeHasUnexpectedOverlap(const std::vector<std::vector<position>>& routes,
                               std::size_t routeIndex)
{
    const auto& route = routes[routeIndex];
    if (route.size() <= 2) {
        return false;
    }

    std::unordered_set<position, MappingPositionHash> selfPositions;
    for (std::size_t index = 1; index + 1 < route.size(); ++index) {
        if (!selfPositions.insert(route[index]).second) {
            return true;
        }
    }

    for (std::size_t otherIndex = 0; otherIndex < routes.size(); ++otherIndex) {
        if (otherIndex == routeIndex) {
            continue;
        }

        const auto& other = routes[otherIndex];
        if (other.size() <= 2) {
            continue;
        }

        const bool sameStart = route.front() == other.front();
        const std::size_t commonPrefix = sameStart ? sharedPrefixLength(route, other) : 0;
        for (std::size_t index = 1; index + 1 < route.size(); ++index) {
            for (std::size_t otherPos = 1; otherPos + 1 < other.size(); ++otherPos) {
                if (route[index] != other[otherPos]) {
                    continue;
                }

                const bool allowedSharedTrunk =
                    sameStart && index < commonPrefix && otherPos < commonPrefix;
                if (!allowedSharedTrunk) {
                    return true;
                }
            }
        }
    }

    return false;
}

std::unordered_set<position, MappingPositionHash> crossCellsForRoute(
    const RouteMappingList& crossMappings,
    const RouteMappingKey& routeKey)
{
    std::unordered_set<position, MappingPositionHash> cells;
    const auto crossIt = crossMappings.find(routeKey);
    if (crossIt == crossMappings.end()) {
        return cells;
    }

    for (const auto& segment : crossIt->second) {
        cells.insert(segment.begin(), segment.end());
    }
    return cells;
}

bool segmentTouchesAnyCell(const std::vector<position>& segment,
                           const std::unordered_set<position, MappingPositionHash>& cells)
{
    if (cells.empty()) {
        return false;
    }

    for (const position& cell : segment) {
        if (cells.find(cell) != cells.end()) {
            return true;
        }
    }
    return false;
}

std::size_t uniqueCellCount(const std::vector<position>& segment)
{
    std::unordered_set<position, MappingPositionHash> cells(segment.begin(), segment.end());
    return cells.size();
}

void insertRouteCells(std::unordered_set<position, MappingPositionHash>& cells,
                      const std::vector<std::vector<position>>& routeMapping)
{
    for (const auto& segment : routeMapping) {
        cells.insert(segment.begin(), segment.end());
    }
}

bool replaceRoutePointWithCenterline(RouteMappingList& routeMappings,
                                     const RouteMappingList& crossMappings,
                                     const std::vector<std::vector<position>>& routes,
                                     std::size_t routeIndex,
                                     std::size_t routePoint)
{
    if (routeIndex >= routes.size()) {
        return false;
    }

    const auto& route = routes[routeIndex];
    if (routePoint == 0 || routePoint + 1 >= route.size()) {
        return false;
    }

    const RouteMappingKey routeKey{route.front(), route.back()};
    auto routeIt = routeMappings.find(routeKey);
    if (routeIt == routeMappings.end() || routeIt->second.size() != route.size() - 2) {
        return false;
    }

    const std::size_t segmentIndex = routePoint - 1;
    if (segmentIndex >= routeIt->second.size()) {
        return false;
    }

    auto centerlineMapping = centerlineUnitMapping(route[routePoint],
                                                   route[routePoint - 1],
                                                   route[routePoint + 1]);
    if (centerlineMapping.empty()) {
        return false;
    }

    const auto crossCells = crossCellsForRoute(crossMappings, routeKey);
    if (segmentTouchesAnyCell(routeIt->second[segmentIndex], crossCells) ||
        segmentTouchesAnyCell(centerlineMapping, crossCells) ||
        uniqueCellCount(centerlineMapping) > uniqueCellCount(routeIt->second[segmentIndex])) {
        return false;
    }

    routeIt->second[segmentIndex] = std::move(centerlineMapping);
    return true;
}

void preferOppositeFanoutBranchSegments(RouteMappingList& routeMappings,
                                        const RouteMappingList& crossMappings,
                                        const std::vector<std::vector<position>>& routes)
{
    std::unordered_map<std::size_t, std::set<std::size_t>> routePointsByRoute;
    for (std::size_t left = 0; left < routes.size(); ++left) {
        if (routes[left].size() <= 2) {
            continue;
        }

        for (std::size_t right = left + 1; right < routes.size(); ++right) {
            if (routes[right].size() <= 2 || routes[left].front() != routes[right].front()) {
                continue;
            }

            const std::size_t commonPrefix = sharedPrefixLength(routes[left], routes[right]);
            const std::size_t minLength = std::min(routes[left].size(), routes[right].size());
            if (commonPrefix < 2 || commonPrefix >= minLength) {
                continue;
            }

            const std::size_t branchPoint = commonPrefix - 1;
            if (branchPoint == 0 ||
                branchPoint + 1 >= routes[left].size() ||
                branchPoint + 1 >= routes[right].size()) {
                continue;
            }

            const position& prev = routes[left][branchPoint - 1];
            const position& branch = routes[left][branchPoint];
            const position& leftNext = routes[left][branchPoint + 1];
            const position& rightNext = routes[right][branchPoint + 1];
            if (branch != routes[right][branchPoint] ||
                prev != routes[right][branchPoint - 1]) {
                continue;
            }

            const RouteDirection trunkDirection = routeDirection(prev, branch);
            const RouteDirection leftBranchDirection = routeDirection(branch, leftNext);
            const RouteDirection rightBranchDirection = routeDirection(branch, rightNext);
            if (trunkDirection == RouteDirection::None ||
                !oppositeDirections(leftBranchDirection, rightBranchDirection) ||
                sameDirectionAxis(trunkDirection, leftBranchDirection)) {
                continue;
            }

            routePointsByRoute[left].insert(branchPoint);
            routePointsByRoute[right].insert(branchPoint);

            const auto addStraightContinuation = [&](std::size_t routeIndex) {
                const auto& route = routes[routeIndex];
                if (branchPoint + 2 >= route.size()) {
                    return;
                }

                const RouteDirection firstDirection =
                    routeDirection(route[branchPoint], route[branchPoint + 1]);
                const RouteDirection continuationDirection =
                    routeDirection(route[branchPoint + 1], route[branchPoint + 2]);
                if (firstDirection != RouteDirection::None &&
                    firstDirection == continuationDirection) {
                    routePointsByRoute[routeIndex].insert(branchPoint + 1);
                }
            };

            addStraightContinuation(left);
            addStraightContinuation(right);
        }
    }

    for (const auto& entry : routePointsByRoute) {
        bool changed = false;
        for (const std::size_t routePoint : entry.second) {
            changed = replaceRoutePointWithCenterline(routeMappings,
                                                      crossMappings,
                                                      routes,
                                                      entry.first,
                                                      routePoint) || changed;
        }

        if (!changed) {
            continue;
        }

        const auto& route = routes[entry.first];
        const RouteMappingKey routeKey{route.front(), route.back()};
        auto routeIt = routeMappings.find(routeKey);
        if (routeIt != routeMappings.end()) {
            stitchRouteMapping(routeIt->second);
        }
    }
}

void preferSharedFanoutCenterlineGroups(RouteMappingList& routeMappings,
                                        const std::vector<std::vector<position>>& routes)
{
    std::unordered_map<position, std::vector<std::size_t>, MappingPositionHash> routesByStart;
    for (std::size_t routeIndex = 0; routeIndex < routes.size(); ++routeIndex) {
        if (routes[routeIndex].size() > 2) {
            routesByStart[routes[routeIndex].front()].push_back(routeIndex);
        }
    }

    for (const auto& startEntry : routesByStart) {
        const auto& routeIndices = startEntry.second;
        if (routeIndices.size() < 2) {
            continue;
        }

        std::set<std::size_t> fanoutRouteIndices;
        for (std::size_t leftPos = 0; leftPos < routeIndices.size(); ++leftPos) {
            const std::size_t left = routeIndices[leftPos];
            for (std::size_t rightPos = leftPos + 1; rightPos < routeIndices.size(); ++rightPos) {
                const std::size_t right = routeIndices[rightPos];
                const std::size_t commonPrefix = sharedPrefixLength(routes[left], routes[right]);
                const std::size_t minLength = std::min(routes[left].size(), routes[right].size());
                if (commonPrefix >= 2 && commonPrefix < minLength) {
                    fanoutRouteIndices.insert(left);
                    fanoutRouteIndices.insert(right);
                }
            }
        }

        if (fanoutRouteIndices.size() < 2) {
            continue;
        }

        std::map<std::size_t, std::vector<std::vector<position>>> centerlineMappings;
        std::unordered_set<position, MappingPositionHash> originalCells;
        std::unordered_set<position, MappingPositionHash> centerlineCells;
        bool canUseCenterlineGroup = true;

        for (const std::size_t routeIndex : fanoutRouteIndices) {
            const auto& route = routes[routeIndex];
            const RouteMappingKey routeKey{route.front(), route.back()};
            const auto routeIt = routeMappings.find(routeKey);
            if (routeIt == routeMappings.end()) {
                canUseCenterlineGroup = false;
                break;
            }

            auto centerlineMapping = centerlineRouteMapping(route);
            if (centerlineMapping.empty()) {
                canUseCenterlineGroup = false;
                break;
            }

            insertRouteCells(originalCells, routeIt->second);
            insertRouteCells(centerlineCells, centerlineMapping);
            centerlineMappings.emplace(routeIndex, std::move(centerlineMapping));
        }

        if (!canUseCenterlineGroup || centerlineCells.size() > originalCells.size()) {
            continue;
        }

        for (auto& centerlineEntry : centerlineMappings) {
            const auto& route = routes[centerlineEntry.first];
            const RouteMappingKey routeKey{route.front(), route.back()};
            routeMappings[routeKey] = std::move(centerlineEntry.second);
        }
    }
}

void preferSharedFanoutTrunkSegments(RouteMappingList& routeMappings,
                                     const RouteMappingList& crossMappings,
                                     const std::vector<std::vector<position>>& routes)
{
    std::unordered_map<std::size_t, std::set<std::size_t>> routeTrunkIndices;
    for (std::size_t left = 0; left < routes.size(); ++left) {
        if (routes[left].size() <= 2) {
            continue;
        }
        for (std::size_t right = left + 1; right < routes.size(); ++right) {
            if (routes[right].size() <= 2 || routes[left].front() != routes[right].front()) {
                continue;
            }

            const std::size_t commonPrefix = sharedPrefixLength(routes[left], routes[right]);
            const std::size_t minLength = std::min(routes[left].size(), routes[right].size());
            if (commonPrefix < 2 || commonPrefix >= minLength) {
                continue;
            }

            for (std::size_t routePoint = 1; routePoint < commonPrefix; ++routePoint) {
                routeTrunkIndices[left].insert(routePoint);
                routeTrunkIndices[right].insert(routePoint);
            }
        }
    }

    for (const auto& entry : routeTrunkIndices) {
        const std::size_t routeIndex = entry.first;
        const auto& route = routes[routeIndex];
        if (route.size() <= 2) {
            continue;
        }

        const RouteMappingKey routeKey{route.front(), route.back()};
        auto routeIt = routeMappings.find(routeKey);
        if (routeIt == routeMappings.end() || routeIt->second.size() != route.size() - 2) {
            continue;
        }

        const auto crossCells = crossCellsForRoute(crossMappings, routeKey);
        bool changed = false;
        for (const std::size_t routePoint : entry.second) {
            if (routePoint == 0 || routePoint + 1 >= route.size()) {
                continue;
            }

            const std::size_t segmentIndex = routePoint - 1;
            if (segmentIndex >= routeIt->second.size()) {
                continue;
            }

            auto centerlineMapping = centerlineUnitMapping(route[routePoint],
                                                           route[routePoint - 1],
                                                           route[routePoint + 1]);
            if (centerlineMapping.empty() ||
                segmentTouchesAnyCell(routeIt->second[segmentIndex], crossCells) ||
                segmentTouchesAnyCell(centerlineMapping, crossCells) ||
                uniqueCellCount({centerlineMapping}) > uniqueCellCount({routeIt->second[segmentIndex]})) {
                continue;
            }

            routeIt->second[segmentIndex] = std::move(centerlineMapping);
            changed = true;
        }

        if (changed) {
            stitchRouteMapping(routeIt->second);
        }
    }
}

void preferSharedFanoutCenterlines(RouteMappingList& routeMappings,
                                   const RouteMappingList& crossMappings,
                                   const std::vector<std::vector<position>>& routes)
{
    std::unordered_set<std::size_t> fanoutRoutes;
    for (std::size_t left = 0; left < routes.size(); ++left) {
        if (routes[left].size() <= 2) {
            continue;
        }
        for (std::size_t right = left + 1; right < routes.size(); ++right) {
            if (routes[right].size() <= 2 || routes[left].front() != routes[right].front()) {
                continue;
            }

            const std::size_t commonPrefix = sharedPrefixLength(routes[left], routes[right]);
            const std::size_t minLength = std::min(routes[left].size(), routes[right].size());
            if (commonPrefix >= 2 && commonPrefix < minLength) {
                fanoutRoutes.insert(left);
                fanoutRoutes.insert(right);
            }
        }
    }

    for (const std::size_t routeIndex : fanoutRoutes) {
        if (routeHasUnexpectedOverlap(routes, routeIndex)) {
            continue;
        }

        const auto& route = routes[routeIndex];
        const RouteMappingKey routeKey{route.front(), route.back()};
        if (crossMappings.find(routeKey) != crossMappings.end()) {
            continue;
        }

        auto routeIt = routeMappings.find(routeKey);
        if (routeIt == routeMappings.end()) {
            continue;
        }

        auto centerlineMapping = centerlineRouteMapping(route);
        if (centerlineMapping.empty()) {
            continue;
        }

        if (uniqueCellCount(centerlineMapping) <= uniqueCellCount(routeIt->second)) {
            routeIt->second = std::move(centerlineMapping);
        }
    }
}

std::vector<std::vector<std::size_t>> buildRouteOrderCandidates(const std::vector<std::vector<position>>& routes)
{
    std::vector<std::vector<std::size_t>> candidates;
    if (routes.empty()) {
        return candidates;
    }

    std::vector<std::size_t> original(routes.size());
    std::iota(original.begin(), original.end(), 0);

    std::set<std::vector<std::size_t>> seenOrders;
    const auto addCandidate = [&](std::vector<std::size_t> order) {
        if (seenOrders.insert(order).second) {
            candidates.push_back(std::move(order));
        }
    };

    std::vector<std::unordered_set<position, MappingPositionHash>> routePositions;
    routePositions.reserve(routes.size());
    std::unordered_map<position, std::size_t, MappingPositionHash> positionFrequency;
    for (const auto& route : routes) {
        auto& cells = routePositions.emplace_back();
        if (route.size() <= 2) {
            continue;
        }
        for (std::size_t index = 1; index + 1 < route.size(); ++index) {
            cells.insert(route[index]);
        }
        for (const position& cell : cells) {
            ++positionFrequency[cell];
        }
    }

    std::vector<std::size_t> conflictWeight(routes.size(), 0);
    for (std::size_t routeIndex = 0; routeIndex < routePositions.size(); ++routeIndex) {
        for (const position& cell : routePositions[routeIndex]) {
            const auto freqIt = positionFrequency.find(cell);
            if (freqIt != positionFrequency.end() && freqIt->second > 1) {
                conflictWeight[routeIndex] += freqIt->second - 1;
            }
        }
    }

    const auto routeStart = [&](std::size_t index) -> position {
        return routes[index].empty() ? position{0, 0} : routes[index].front();
    };
    const auto routeSecond = [&](std::size_t index) -> position {
        return routes[index].size() > 1 ? routes[index][1] : routeStart(index);
    };
    const auto routeEnd = [&](std::size_t index) -> position {
        return routes[index].empty() ? position{0, 0} : routes[index].back();
    };
    const auto routeAbsDx = [&](std::size_t index) -> unsigned int {
        const position start = routeStart(index);
        const position end = routeEnd(index);
        return start.first > end.first ? start.first - end.first : end.first - start.first;
    };
    const auto routeAbsDy = [&](std::size_t index) -> unsigned int {
        const position start = routeStart(index);
        const position end = routeEnd(index);
        return start.second > end.second ? start.second - end.second : end.second - start.second;
    };
    const auto minFirst = [&](std::size_t index) -> unsigned int {
        return std::min(routeStart(index).first, routeEnd(index).first);
    };
    const auto maxFirst = [&](std::size_t index) -> unsigned int {
        return std::max(routeStart(index).first, routeEnd(index).first);
    };
    const auto minSecond = [&](std::size_t index) -> unsigned int {
        return std::min(routeStart(index).second, routeEnd(index).second);
    };
    const auto maxSecond = [&](std::size_t index) -> unsigned int {
        return std::max(routeStart(index).second, routeEnd(index).second);
    };
    const auto addOuterLaneCandidate = [&](bool topToDown) {
        auto laneOrder = original;
        std::stable_sort(laneOrder.begin(), laneOrder.end(), [&](std::size_t left, std::size_t right) {
            const bool leftMatches = topToDown ? (routeAbsDy(left) >= routeAbsDx(left))
                                               : (routeAbsDx(left) > routeAbsDy(left));
            const bool rightMatches = topToDown ? (routeAbsDy(right) >= routeAbsDx(right))
                                                : (routeAbsDx(right) > routeAbsDy(right));
            if (leftMatches != rightMatches) return leftMatches > rightMatches;

            if (topToDown) {
                if (minSecond(left) != minSecond(right)) return minSecond(left) < minSecond(right);
                if (maxSecond(left) != maxSecond(right)) return maxSecond(left) < maxSecond(right);
                if (minFirst(left) != minFirst(right)) return minFirst(left) < minFirst(right);
                if (maxFirst(left) != maxFirst(right)) return maxFirst(left) < maxFirst(right);
            } else {
                if (minFirst(left) != minFirst(right)) return minFirst(left) < minFirst(right);
                if (maxFirst(left) != maxFirst(right)) return maxFirst(left) < maxFirst(right);
                if (minSecond(left) != minSecond(right)) return minSecond(left) < minSecond(right);
                if (maxSecond(left) != maxSecond(right)) return maxSecond(left) < maxSecond(right);
            }

            if (conflictWeight[left] != conflictWeight[right]) return conflictWeight[left] > conflictWeight[right];
            if (routes[left].size() != routes[right].size()) return routes[left].size() > routes[right].size();
            return left < right;
        });
        addCandidate(laneOrder);
    };

    addCandidate(original);
    addOuterLaneCandidate(true);
    addOuterLaneCandidate(false);

    auto order = original;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (routeStart(left) != routeStart(right)) return routeStart(left) < routeStart(right);
        if (routeSecond(left) != routeSecond(right)) return routeSecond(left) < routeSecond(right);
        if (routes[left].size() != routes[right].size()) return routes[left].size() > routes[right].size();
        if (conflictWeight[left] != conflictWeight[right]) return conflictWeight[left] > conflictWeight[right];
        return left < right;
    });
    addCandidate(order);

    order = original;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (routeStart(left) != routeStart(right)) return routeStart(left) < routeStart(right);
        if (routeSecond(left) != routeSecond(right)) return routeSecond(left) < routeSecond(right);
        if (routes[left].size() != routes[right].size()) return routes[left].size() < routes[right].size();
        if (routeEnd(left) != routeEnd(right)) return routeEnd(left) < routeEnd(right);
        return left < right;
    });
    addCandidate(order);

    order = original;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (conflictWeight[left] != conflictWeight[right]) return conflictWeight[left] > conflictWeight[right];
        if (routes[left].size() != routes[right].size()) return routes[left].size() > routes[right].size();
        if (routeStart(left) != routeStart(right)) return routeStart(left) < routeStart(right);
        if (routeEnd(left) != routeEnd(right)) return routeEnd(left) < routeEnd(right);
        return left < right;
    });
    addCandidate(order);

    order = original;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (conflictWeight[left] != conflictWeight[right]) return conflictWeight[left] < conflictWeight[right];
        if (routes[left].size() != routes[right].size()) return routes[left].size() > routes[right].size();
        if (routeStart(left) != routeStart(right)) return routeStart(left) < routeStart(right);
        if (routeEnd(left) != routeEnd(right)) return routeEnd(left) < routeEnd(right);
        return left < right;
    });
    addCandidate(order);

    order = original;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        if (routes[left].size() != routes[right].size()) return routes[left].size() > routes[right].size();
        if (conflictWeight[left] != conflictWeight[right]) return conflictWeight[left] > conflictWeight[right];
        if (routeStart(left) != routeStart(right)) return routeStart(left) < routeStart(right);
        if (routeEnd(left) != routeEnd(right)) return routeEnd(left) < routeEnd(right);
        return left < right;
    });
    addCandidate(order);

    order = original;
    std::reverse(order.begin(), order.end());
    addCandidate(order);

    return candidates;
}

std::unordered_set<position, MappingPositionHash> buildRequiredCrossPositions(
    const std::vector<std::vector<position>>& routes)
{
    std::unordered_map<position, std::set<position>, MappingPositionHash> routeStartsByPosition;
    for (const auto& route : routes) {
        if (route.size() <= 2) {
            continue;
        }
        const position routeStart = route.front();
        for (std::size_t index = 1; index + 1 < route.size(); ++index) {
            routeStartsByPosition[route[index]].insert(routeStart);
        }
    }

    std::unordered_set<position, MappingPositionHash> requiredPositions;
    for (const auto& [gatePos, starts] : routeStartsByPosition) {
        if (starts.size() > 1) {
            requiredPositions.insert(gatePos);
        }
    }
    return requiredPositions;
}

} // namespace

struct MappingOrderScore
{
    std::size_t missingRequiredCrossPositions = 0;
    std::size_t crossSegments = 0;
    std::size_t uniqueCrossCells = 0;
    std::size_t totalCrossCells = 0;
    std::size_t totalRouteCells = 0;
};

bool shouldUseFanoutOptimizedMapping(const MappingOrderScore& optimized,
                                     const MappingOrderScore& baseline)
{
    if (optimized.missingRequiredCrossPositions > baseline.missingRequiredCrossPositions) {
        return false;
    }

    if (optimized.crossSegments <= baseline.crossSegments &&
        optimized.uniqueCrossCells <= baseline.uniqueCrossCells &&
        optimized.totalRouteCells <= baseline.totalRouteCells) {
        return optimized.crossSegments < baseline.crossSegments ||
               optimized.uniqueCrossCells < baseline.uniqueCrossCells ||
               optimized.totalRouteCells < baseline.totalRouteCells;
    }

    if (optimized.totalRouteCells >= baseline.totalRouteCells) {
        return false;
    }

    const std::size_t routeSavings = baseline.totalRouteCells - optimized.totalRouteCells;
    const std::size_t crossIncrease = optimized.crossSegments > baseline.crossSegments
                                          ? optimized.crossSegments - baseline.crossSegments
                                          : 0;
    const std::size_t crossCellIncrease = optimized.uniqueCrossCells > baseline.uniqueCrossCells
                                              ? optimized.uniqueCrossCells - baseline.uniqueCrossCells
                                              : 0;
    return crossIncrease <= routeSavings && crossCellIncrease <= routeSavings * 5;
}

std::uint16_t Mapping::deviateTypeMask(const std::string& type)
{
    if (type == "XMIDDLE") return 1u << 0;
    if (type == "XSIDE")   return 1u << 1;
    if (type == "YMIDDLE") return 1u << 2;
    if (type == "YSIDE")   return 1u << 3;
    if (type == "XMYM")    return 1u << 4;
    if (type == "XMYS")    return 1u << 5;
    if (type == "XSYM")    return 1u << 6;
    if (type == "XSYS")    return 1u << 7;
    return 0;
}

void Mapping::updateDeviateLookup(const std::pair<position, position>& route_key,
                                  const std::vector<std::pair<position, std::string>>& route_entries)
{
    for (std::size_t i = 0; i < route_entries.size(); ++i)
    {
        const auto& [pos, type] = route_entries[i];
        auto& entry = deviate_lookup[pos];
        entry.type_mask |= deviateTypeMask(type);
        if (!entry.initialized ||
            route_key < entry.first_route_key ||
            (route_key == entry.first_route_key && i < entry.first_index))
        {
            entry.first_route_key = route_key;
            entry.first_index = i;
            entry.first_type = type;
            entry.initialized = true;
        }
    }
}

    //门级->元胞级坐标映射
std::map<std::pair<position, position>, std::vector<std::vector<position>>> Mapping::mapping_line(std::vector<std::vector<position>>& _example){
    const auto resetMappingState = [&]() {
        deviate_list.clear();
        deviatemapping_list.clear();
        crossline_list.clear();
        deviate_lookup.clear();
    };

    const auto requiredCrossPositions = buildRequiredCrossPositions(_example);
    std::unordered_set<position, MappingPositionHash> mustKeepCrossPositions;

    const auto currentCoveredCrossBlocks = [&]() {
        std::unordered_set<position, MappingPositionHash> coveredCrossBlocks;
        for (const auto& crossline : crossline_list) {
            for (const auto& segment : crossline.second) {
                for (const position& cell : segment) {
                    coveredCrossBlocks.insert({cell.first / 5, cell.second / 5});
                }
            }
        }
        return coveredCrossBlocks;
    };

    const auto currentScore = [&]() {
        MappingOrderScore score;
        std::unordered_set<position, MappingPositionHash> uniqueCrossCells;
        std::unordered_set<position, MappingPositionHash> coveredCrossBlocks;
        for (const auto& crossline : crossline_list) {
            score.crossSegments += crossline.second.size();
            for (const auto& segment : crossline.second) {
                score.totalCrossCells += segment.size();
                for (const position& cell : segment) {
                    uniqueCrossCells.insert(cell);
                    coveredCrossBlocks.insert({cell.first / 5, cell.second / 5});
                }
            }
        }
        score.uniqueCrossCells = uniqueCrossCells.size();
        for (const position& requiredPos : mustKeepCrossPositions) {
            if (coveredCrossBlocks.find(requiredPos) == coveredCrossBlocks.end()) {
                ++score.missingRequiredCrossPositions;
            }
        }
        for (const auto& route : deviatemapping_list) {
            for (const auto& segment : route.second) {
                score.totalRouteCells += segment.size();
            }
        }
        return score;
    };

    const auto betterScore = [](const MappingOrderScore& left, const MappingOrderScore& right) {
        if (left.missingRequiredCrossPositions != right.missingRequiredCrossPositions) {
            return left.missingRequiredCrossPositions < right.missingRequiredCrossPositions;
        }
        if (left.crossSegments != right.crossSegments) return left.crossSegments < right.crossSegments;
        if (left.uniqueCrossCells != right.uniqueCrossCells) return left.uniqueCrossCells < right.uniqueCrossCells;
        if (left.totalCrossCells != right.totalCrossCells) return left.totalCrossCells < right.totalCrossCells;
        return left.totalRouteCells < right.totalRouteCells;
    };

    const auto orderedRoutes = [&](const std::vector<std::size_t>& order) {
        std::vector<std::vector<position>> routes;
        routes.reserve(order.size());
        for (const std::size_t index : order) {
            routes.push_back(_example[index]);
        }
        return routes;
    };

    const auto removeBlockedTemplatePorts = [&]() {
        removeCellsFromRouteMappings(deviatemapping_list, multi_output_not_input_boundaries);
        removeCellsFromRouteMappings(crossline_list, multi_output_not_input_boundaries);
    };

    const auto runMappingWithOrder = [&](const std::vector<std::size_t>& order) {
        resetMappingState();
        auto routes = orderedRoutes(order);
        for (auto &oneroute : routes)//此处是头文件中的存放的坐标形式的多条线路，含有起始点std::vector<std::vector<position>> routepos_list;
        {
            routepos_Deviate(oneroute);
        }
        deviate_mapping(deviate_list);
        connectRouteMappingsToOriginalEndpoints(deviatemapping_list, routes);
        removeBlockedTemplatePorts();
        const auto rawBaselineRouteMappings = deviatemapping_list;
        crossline_mapping(routes);
        preferOppositeFanoutBranchSegments(deviatemapping_list, crossline_list, routes);
        removeBlockedTemplatePorts();
        const auto baselineRouteMappings = deviatemapping_list;
        const auto baselineCrossMappings = crossline_list;
        const MappingOrderScore baselineScore = currentScore();

        deviatemapping_list = rawBaselineRouteMappings;
        crossline_list.clear();
        preferSharedFanoutCenterlineGroups(deviatemapping_list, routes);
        const RouteMappingList noCrossMappings;
        preferSharedFanoutTrunkSegments(deviatemapping_list, noCrossMappings, routes);
        crossline_mapping(routes);
        preferOppositeFanoutBranchSegments(deviatemapping_list, crossline_list, routes);
        preferSharedFanoutCenterlines(deviatemapping_list, crossline_list, routes);
        removeBlockedTemplatePorts();
        const MappingOrderScore optimizedScore = currentScore();

        if (!shouldUseFanoutOptimizedMapping(optimizedScore, baselineScore)) {
            deviatemapping_list = baselineRouteMappings;
            crossline_list = baselineCrossMappings;
            return baselineScore;
        }

        return optimizedScore;
    };

    auto candidates = buildRouteOrderCandidates(_example);
    if (_example.size() > 256 && candidates.size() > 3) {
        candidates.resize(3);
    }

    if (candidates.empty()) {
        resetMappingState();
        return deviatemapping_list;
    }

    runMappingWithOrder(candidates.front());
    const auto originalCoveredCrossBlocks = currentCoveredCrossBlocks();
    for (const position& requiredPos : requiredCrossPositions) {
        if (originalCoveredCrossBlocks.find(requiredPos) != originalCoveredCrossBlocks.end()) {
            mustKeepCrossPositions.insert(requiredPos);
        }
    }

    std::vector<std::size_t> bestOrder = candidates.front();
    MappingOrderScore bestScore;
    bool hasBestScore = false;
    for (const auto& candidate : candidates) {
        const MappingOrderScore score = runMappingWithOrder(candidate);
        if (!hasBestScore || betterScore(score, bestScore)) {
            bestScore = score;
            bestOrder = candidate;
            hasBestScore = true;
        }
    }

    runMappingWithOrder(bestOrder);

    return deviatemapping_list;
}

//给予门级线路偏移量
void Mapping::routepos_Deviate(std::vector<position>& _oneroutepos_list){
    std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::string>> RouteDeviate_list;
    std::vector<std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::string>>> temp_list_vector;
    std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::string>> temp_list;

    std::string XMIDDLE("XMIDDLE"), XSIDE("XSIDE"), YMIDDLE("YMIDDLE"), YSIDE("YSIDE"), XMYM("XMYM"), XMYS("XMYS"), XSYM("XSYM"), XSYS("XSYS"), EMPTY("EMPTY");  
    bool isFirst = true; 
    bool isFanout = false;
    int tempdirection = 0; 
    std::pair<unsigned int, unsigned int> startpos = _oneroutepos_list.front();
    std::pair<unsigned int, unsigned int> endpos = _oneroutepos_list.back();
    
    
    if(_oneroutepos_list.size() == 2)
    {
        deviate_list.insert({{startpos, endpos}, {}});
    }
    else
    {
        for (auto it = _oneroutepos_list.begin(); it != _oneroutepos_list.end(); ++it)
        {
            if (isFirst)
            {
                //判断是否多扇出
                temp_list_vector.clear();
                if (!deviate_list.empty())
                {
                    for ( auto& pair : deviate_list) 
                    {  
                        if (pair.first.first == startpos) 
                        {  
                            position tempsecondpos = _oneroutepos_list[1];
                            if(!pair.second.empty())
                            {
                                if (tempsecondpos == (pair.second.front()).first)
                                {
                                    auto temp = pair.second;
                                    temp_list_vector.emplace_back(temp);
                                    isFanout = true;
                                }
                            }
                        }  
                    } 
                    //多条线路复用，后面线路与前面多条线路比较，追踪复用坐标最多线路
                    int maxrepeat = 0;
                    if (!temp_list_vector.empty())
                    {
                        for (auto &v : temp_list_vector)
                        {
                            int temprepeat = 0;
                            for (size_t i = 0; i < v.size(); ++i)
                            {
                                if (v[i].first == _oneroutepos_list[i+1])
                                {
                                    temprepeat++;
                                }
                                else
                                {
                                    break;
                                }
                            }
                            if (temprepeat >= maxrepeat)
                            {
                                maxrepeat = temprepeat;
                                temp_list = v;
                            }
                        }
                    }
                }
                if (isFanout)
                {
                    for (size_t i = 0; i < temp_list.size(); ++i) 
                    {  
                        if (_oneroutepos_list[i+1] == temp_list[i].first) 
                        {  
                            if (i != (temp_list.size()-1))
                            {
                                RouteDeviate_list.push_back(temp_list[i]);
                            }
                            else
                            {
                                RouteDeviate_list.push_back(temp_list[i]);
                                std::pair<unsigned int, unsigned int> fanoutpos = temp_list[i].first;
                                if (!RouteDeviate_list.empty()){
                                    if ((RouteDeviate_list.back().second == XMIDDLE) || (RouteDeviate_list.back().second == XSIDE))
                                    {
                                        tempdirection = 1;
                                    }
                                    else if ((RouteDeviate_list.back().second == YMIDDLE) || (RouteDeviate_list.back().second == YSIDE))
                                    {
                                        tempdirection = 0;
                                    }
                                    else 
                                    {
                                        if (_oneroutepos_list[i+1].second == _oneroutepos_list[i].second)//沿X轴
                                        {
                                            RouteDeviate_list.pop_back();
                                            if ((temp_list[i].second == XMYM) || (temp_list[i].second == XMYS))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], XMIDDLE);
                                                tempdirection = 1;
                                            }
                                            else if ((temp_list[i].second == XSYM) || (temp_list[i].second == XSYS))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], XSIDE);
                                                tempdirection = 1;
                                            }
                                        }
                                        else
                                        {
                                            RouteDeviate_list.pop_back();
                                            if ((temp_list[i].second == XMYM) || (temp_list[i].second == XSYM))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], YMIDDLE);
                                                tempdirection = 0;
                                            }
                                            else if ((temp_list[i].second == XMYS) && (temp_list[i].second == XSYS))
                                            {
                                                RouteDeviate_list.emplace_back(_oneroutepos_list[i+1], YSIDE);
                                                tempdirection = 0;
                                            }
                                        }
                                        
                                    }
                                }
                                it = std::find(_oneroutepos_list.begin(), _oneroutepos_list.end(), fanoutpos); 
                                break;
                            }
                        
                            //RouteDeviate_list.push_back(temp_list[i]);
                        } 
                        else
                        {
                            std::pair<unsigned int, unsigned int> fanoutpos = temp_list[i-1].first;
                            if (!RouteDeviate_list.empty()){
                                if ((RouteDeviate_list.back().second == XMIDDLE) || (RouteDeviate_list.back().second == XSIDE))
                                {
                                    tempdirection = 1;
                                }
                                else if ((RouteDeviate_list.back().second == YMIDDLE) || (RouteDeviate_list.back().second == YSIDE))
                                {
                                    tempdirection = 0;
                                }
                                else 
                                {
                                    if ((!_oneroutepos_list.empty()) && (i > 0) && (_oneroutepos_list[i].second == _oneroutepos_list[i-1].second))//沿X轴
                                    {
                                        RouteDeviate_list.pop_back();
                                        if ((temp_list[i-1].second == XMYM) || (temp_list[i-1].second == XMYS))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], XMIDDLE);
                                            tempdirection = 1;
                                        }
                                        else if ((temp_list[i-1].second == XSYM) || (temp_list[i-1].second == XSYS))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], XSIDE);
                                            tempdirection = 1;
                                        }
                                    }
                                    else
                                    {
                                        RouteDeviate_list.pop_back();
                                        if ((temp_list[i-1].second == XMYM) || (temp_list[i-1].second == XSYM))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], YMIDDLE);
                                            tempdirection = 0;
                                        }
                                        else if ((temp_list[i-1].second == XMYS) && (temp_list[i-1].second == XSYS))
                                        {
                                            RouteDeviate_list.emplace_back(_oneroutepos_list[i], YSIDE);
                                            tempdirection = 0;
                                        }
                                    }
                                    
                                }
                            }
                            it = std::find(_oneroutepos_list.begin(), _oneroutepos_list.end(), fanoutpos); 
                            break;
                        } 
                    }

                }
                else
                {
                    auto next_it = std::next(it);  
                    const auto& next_cell = *next_it; 
                    if (next_cell.second == (*it).second)
                    {
                        tempdirection = 1; //初始沿X轴出边
                    }
                    else
                    {
                        tempdirection = 0; //初始沿Y轴出边
                    }
                }
                
                isFirst = false;

            }
            else if (it != _oneroutepos_list.begin())
            {
                auto prev_it = std::prev(it);  
                auto& prev_cell = *prev_it; 
                int judging_direction;

                if (((*it).first != prev_cell.first) && ((*it).second == prev_cell.second))
                {
                    judging_direction = 1;
                }
                else
                {
                    judging_direction = 0;
                }
                
                switch (judging_direction)
                {
                //沿X轴方向
                case 1:
                    if (tempdirection == 1)
                    {
                        if (prev_it == _oneroutepos_list.begin())//起点第一条边的放置
                        {
                            if ((findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XMYS))
                            {
                                RouteDeviate_list.emplace_back(*it, XSIDE);
                            }
                            else
                            {
                                RouteDeviate_list.emplace_back(*it, XMIDDLE);
                            }
                        }
                        else//后一条边尽量同步前一条的位置
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, *it) == XSIDE)||(findInVectorPairFirst(deviate_list, *it) == XSYM)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                                    {
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                }
                                else
                                {
                                    if ((findInVectorPairFirst(deviate_list, *it) == XMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XMYS))
                                    {
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                    
                                }
                            } 
                        }

                        tempdirection = 1;
                    }
                    //Y转X的拐点
                    else if (tempdirection == 0)
                    {
                        if ((findInVectorPairFirst(deviate_list, *it) == XMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XMYS))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == YMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XSIDE);
                                }
                                else if (RouteDeviate_list.back().second == YSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XSIDE);
                                }
                            }
                        }
                        else if ((findInVectorPairFirst(deviate_list, *it) == XSIDE)||(findInVectorPairFirst(deviate_list, *it) == XSYM)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == YMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                }
                                else if (RouteDeviate_list.back().second == YSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                }
                            }
                        }
                        else 
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == YMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                
                                }
                                else if (RouteDeviate_list.back().second == YSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == XMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                        RouteDeviate_list.emplace_back(*it, XSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                        RouteDeviate_list.emplace_back(*it, XMIDDLE);
                                    }
                                
                                }
                            }
                        }

                        tempdirection = 1;
                    }
                    
                    break;
                
                //沿Y轴方向
                case 0:
                    if (tempdirection == 0)
                    {
                        if (prev_it == _oneroutepos_list.begin())//起点第一条边的放置
                        {
                            if ((findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XSYM))
                            {
                                RouteDeviate_list.emplace_back(*it, YSIDE);
                            }
                            else
                            {
                                RouteDeviate_list.emplace_back(*it, YMIDDLE);
                            }
                        }
                        else//后一条边尽量同步前一条的位置
                        {
                            if (RouteDeviate_list.back().second == YSIDE)
                            {
                                if ((findInVectorPairFirst(deviate_list, *it) == YSIDE)||(findInVectorPairFirst(deviate_list, *it) == XMYS)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                                {
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                                else
                                {
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                            }
                            else
                            {
                                if ((findInVectorPairFirst(deviate_list, *it) == YMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XSYM))
                                {
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                                else
                                {
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                                
                            }
                            
                        }

                        tempdirection = 0;
                    }
                    //X转Y的拐点
                    else if (tempdirection == 1)
                    {
                        if ((findInVectorPairFirst(deviate_list, *it) == YMIDDLE)||(findInVectorPairFirst(deviate_list, *it) == XMYM)||(findInVectorPairFirst(deviate_list, *it) == XSYM))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                                else if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YSIDE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYS)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYS))
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YSIDE);
                                }
                            }
                        }
                        else if ((findInVectorPairFirst(deviate_list, *it) == YSIDE)||(findInVectorPairFirst(deviate_list, *it) == XMYS)||(findInVectorPairFirst(deviate_list, *it) == XSYS))
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                                else if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                    }
                                    //拐点后一个点
                                    RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                }
                            }
                        }
                        else 
                        {
                            if (!RouteDeviate_list.empty()){
                                if (RouteDeviate_list.back().second == XMIDDLE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XMYS;
                                        RouteDeviate_list.emplace_back(*it, YSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XMYM;
                                        RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                    }
                                
                                }
                                else if (RouteDeviate_list.back().second == XSIDE)
                                {
                                    if ((findInVectorPairFirst(deviate_list, prev_cell) == YMIDDLE)||(findInVectorPairFirst(deviate_list, prev_cell) == XMYM)||(findInVectorPairFirst(deviate_list, prev_cell) == XSYM))
                                    {
                                        RouteDeviate_list.back().second = XSYS;
                                        RouteDeviate_list.emplace_back(*it, YSIDE);
                                    }
                                    else
                                    {
                                        RouteDeviate_list.back().second = XSYM;
                                        RouteDeviate_list.emplace_back(*it, YMIDDLE);
                                    }
                                
                                }
                            }
                        }

                        tempdirection = 0;
                    }
                    break;

                default:
                    break;
                }
            }
            
            
        }
        if (!RouteDeviate_list.empty() && RouteDeviate_list.back().first == endpos) {
            RouteDeviate_list.pop_back();
        }
        const auto route_key = std::make_pair(startpos, endpos);
        deviate_list.insert({route_key, RouteDeviate_list});
        updateDeviateLookup(route_key, RouteDeviate_list);
    }
}

//通过门级线路偏移量进行具体元胞映射
void Mapping::deviate_mapping(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list){
    std::string XMIDDLE("XMIDDLE"), XSIDE("XSIDE"), YMIDDLE("YMIDDLE"), YSIDE("YSIDE"), XMYM("XMYM"), XMYS("XMYS"), XSYM("XSYM"), XSYS("XSYS"), EMPTY("EMPYT");
    //std::vector<std::vector<position>> route_mapping;

    if(!_deviate_list.empty())
    {
        for (const auto& route : _deviate_list)
        {
            auto startpos = route.first.first;
            auto endpos = route.first.second;
            std::vector<std::pair<position, std::string>> single_route;
            single_route = route.second;
            std::vector<std::vector<position>> route_mapping;

            for (auto it = single_route.begin(); it != single_route.end(); ++it)
            {
                std::vector<position> unit_mapping;
                auto itpos = (*it).first;
                std::pair<unsigned int, unsigned int> nwpos = itpos;
                nwpos.first *= 5;
                nwpos.second *= 5;
                const bool has_next = (std::next(it) != single_route.end());
                const bool has_prev = (it != single_route.begin());
                auto nextpos = has_next ? std::next(it)->first : itpos;
                auto prevpos = has_prev ? std::prev(it)->first : itpos;

                if (it == single_route.begin())
                {
                    if (single_route.size() == 1)
                    {
                        switch (((*it).first.first != startpos.first) && ((*it).first.second == startpos.second))
                        {
                        case true://左右
                            if (itpos.first > startpos.first)//右
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                            }
                            else//左
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else
                                {
                                    if ((endpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                            }
                            
                            break;
                        
                        case false://上下
                            if (itpos.second > startpos.second)//下
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                            }
                            else//上
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else
                                {
                                    if ((endpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                            }
                            
                            break;
                        
                        default:
                            break;
                        }
                    }
                    else
                    {
                        switch (((*it).first.first != startpos.first) && ((*it).first.second == startpos.second))
                        {
                        case true://左右
                            if (itpos.first > startpos.first)//右
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    if (((*(std::next(it))).second == XSIDE)||((*(std::next(it))).second == XSYM)||((*(std::next(it))).second ==XSYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    if (((*(std::next(it))).second == XMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XMYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                        {
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XMYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                
                            }
                            else//左
                            {
                                if ((*it).second == XMIDDLE)
                                {
                                    if (((*(std::next(it))).second == XSIDE)||((*(std::next(it))).second == XSYM)||((*(std::next(it))).second ==XSYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        }
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                    
                                }
                                else if ((*it).second == XSIDE)
                                {
                                    if (((*(std::next(it))).second == XMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XMYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))//实际电路应该不存在此情况
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XMYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }    
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.second > itpos.second))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                            }
                            
                            break;
                        
                        case false://上下
                            if (itpos.second > startpos.second)//下
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    if (((*(std::next(it))).second == YSIDE)||((*(std::next(it))).second == XMYS)||((*(std::next(it))).second ==XSYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    if (((*(std::next(it))).second == YMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XSYM))
                                    {
                                        if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))//实际电路应该不存在此情况
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYS)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }    
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                            }
                            else//上
                            {
                                if ((*it).second == YMIDDLE)
                                {
                                    if (((*(std::next(it))).second == YSIDE)||((*(std::next(it))).second == XMYS)||((*(std::next(it))).second ==XSYS))
                                    {
                                        if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        }
                                        
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                    
                                }
                                else if ((*it).second == YSIDE)
                                {
                                    if (((*(std::next(it))).second == YMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second ==XSYM))
                                    {
                                        if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))//实际电路应该不存在此情况
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        }
                                        else
                                        {
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                            unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        }
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                                else if ((*it).second == XMYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                                else if ((*it).second == XSYM)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                                else if ((*it).second == XMYS)//情况特殊几乎不存在
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }    
                                }
                                else if ((*it).second == XSYS)
                                {
                                    if ((nextpos.first > itpos.first))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                            }
                            
                            break;
                        
                        default:
                            break;
                        }
                    }
                }
                else if (std::next(it) == single_route.end())
                {
                    switch (((*it).first.first != endpos.first) && ((*it).first.second == endpos.second))
                    {
                    case true://左右
                        if (itpos.first > endpos.first)//右
                        {
                            if ((*it).second == XMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                            }
                            else if((*it).second == XSIDE)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                            }
                            else if((*it).second == XMYM)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }   
                            }
                            else if((*it).second == XMYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYM)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                }
                            }
                        }
                        else//左
                        {
                            if ((*it).second == XMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                            }
                            
                            else if((*it).second == XSIDE)
                            {
                                if (isfindpostype(deviate_list, (*it).first, YSIDE)||isfindpostype(deviate_list, (*it).first, XMYS)||isfindpostype(deviate_list, (*it).first, XSYS))
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            
                            }
                            else if((*it).second == XMYM)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                
                            }
                            else if((*it).second == XMYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYM)//此情况不太可能
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            }
                            else if((*it).second == XSYS)
                            {
                                if (prevpos.second > itpos.second)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                }
                            }
                        }
                        break;
                    
                    case false://上下
                        if (itpos.second > endpos.second)//下
                        {
                            if ((*it).second == YMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            else if ((*it).second == YSIDE)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            else if ((*it).second == XMYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                            else if ((*it).second == XSYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                            else if ((*it).second == XMYS)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                            else if ((*it).second == XSYS)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                }
                            }
                        }
                        else//上
                        {
                            if ((*it).second == YMIDDLE)
                            {
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            else if ((*it).second == YSIDE)
                            {
                                if (isfindpostype(deviate_list, (*it).first, XSIDE)||isfindpostype(deviate_list, (*it).first, XSYM)||isfindpostype(deviate_list, (*it).first, XSYS))
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if ((*it).second == XMYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if((*it).second == XSYM)
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if ((*it).second == XMYS)//特殊情况
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                            else if ((*it).second == XSYS)//特殊情况
                            {
                                if (prevpos.first > itpos.first)
                                {
                                    unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                                else
                                {
                                    unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                    unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                }
                            }
                        }
                        break;
                    
                    default:
                        break;
                    }
                }
                else
                {
                    if (((*it).second == XMYM)||((*it).second == XMYS)||((*it).second == XSYM)||((*it).second == XSYS))
                    {
                        if (((prevpos.first<itpos.first)&&(nextpos.second<itpos.second))||((prevpos.second<itpos.second)&&(nextpos.first<itpos.first)))
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                        }
                        else if (((prevpos.first>itpos.first)&&(nextpos.second<itpos.second))||((prevpos.second<itpos.second)&&(nextpos.first>itpos.first)))
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                            }
                        }
                        else if (((prevpos.first>itpos.first)&&(nextpos.second>itpos.second))||((prevpos.second>itpos.second)&&(nextpos.first>itpos.first)))
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                        else
                        {
                            if ((*it).second == XMYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XMYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYM)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                            
                            else if ((*it).second == XSYS)
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                        
                    }
                    else if(((*it).second == XMIDDLE)||((*it).second == XSIDE)||((*it).second == YMIDDLE)||((*it).second == YSIDE))
                    {
                        if ((*it).second == XMIDDLE)
                        {
                            if (((*(std::next(it))).second == XSIDE)||((*(std::next(it))).second == XSYM)||((*(std::next(it))).second == XSYS))
                            {
                                if (nextpos.first > itpos.first)
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                            }
                            
                        }
                        
                        else if ((*it).second == XSIDE)
                        {
                            if (((*(std::next(it))).second == XMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second == XMYS))
                            {
                                if (nextpos.first > itpos.first)
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, YMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XSYM))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+1, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first, nwpos.second+2);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+1, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                        
                        else if ((*it).second == YMIDDLE)
                        {
                            if (((*(std::next(it))).second == YSIDE)||((*(std::next(it))).second == XMYS)||((*(std::next(it))).second == XSYS))
                            {
                                if (nextpos.second > itpos.second)
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                            }
                        }
                        
                        else if ((*it).second == YSIDE)
                        {
                            if (((*(std::next(it))).second == YMIDDLE)||((*(std::next(it))).second == XMYM)||((*(std::next(it))).second == XSYM))
                            {
                                if (nextpos.second > itpos.second)
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                    }
                                }
                                else
                                {
                                    if (isfindpostype(deviate_list, itpos, XMIDDLE)||isfindpostype(deviate_list, itpos, XMYM)||isfindpostype(deviate_list, itpos, XMYS))
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                    else
                                    {
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                        unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+3, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+2);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second+1);
                                        unit_mapping.emplace_back(nwpos.first+2, nwpos.second);
                                    }
                                }
                            }
                            else
                            {
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+1);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+2);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+3);
                                unit_mapping.emplace_back(nwpos.first+4, nwpos.second+4);
                            }
                        }
                    }
                }
                if (!unit_mapping.empty()) {
                    if (!has_prev) {
                        connectUnitMappingToNodeBoundary(unit_mapping, itpos, startpos, true);
                    }
                    if (!has_next) {
                        connectUnitMappingToNodeBoundary(unit_mapping, itpos, endpos, false);
                    }
                    unit_mapping = shortestUnitPath(
                        unit_mapping,
                        itpos,
                        has_prev ? prevpos : startpos,
                        has_next ? nextpos : endpos,
                        !has_prev,
                        !has_next
                    );
                }
                route_mapping.push_back(unit_mapping);
            }
            stitchRouteMapping(route_mapping);
            if(!route_mapping.empty()){
                deviatemapping_list.insert({{startpos, endpos}, route_mapping});
            }
            else
            {
                deviatemapping_list.insert({{startpos, endpos}, {}});
            }
        }
    }
    
}

//在布线时查找deviate_list里是否在该位置已经布线
std::string Mapping::findInVectorPairFirst(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair){  
    (void)_deviate_list;
    const auto it = deviate_lookup.find(target_pair);
    if (it != deviate_lookup.end() && it->second.initialized)
    {
        return it->second.first_type;
    }
    std::string EMPTY("EMPYT");
    return EMPTY; // 没有找到  
}  

//查找deviate_list里指定坐标位置是否具有指定偏移态
bool Mapping::isfindpostype(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair, std::string& _type){
    (void)_deviate_list;
    const auto it = deviate_lookup.find(target_pair);
    if (it != deviate_lookup.end())
    {
        return (it->second.type_mask & deviateTypeMask(_type)) != 0;
    }
    return false;
}

//找出交叉线映射元胞坐标
void Mapping::crossline_mapping(std::vector<std::vector<position>> &_routepos_list){
    std::vector<std::pair<std::pair<std::pair<position, position>, std::pair<position, position>>, position>> temppos_list;
    std::vector<std::pair<std::pair<position, position>, position>> oneroutepos_list;
    using RouteKey = std::pair<position, position>;
    using RoutePair = std::pair<RouteKey, RouteKey>;
    std::set<std::pair<RoutePair, position>> tempposKeys;
    std::unordered_set<position, MappingPositionHash> onerouteposPositions;
    std::vector<std::unordered_set<position, MappingPositionHash>> routePositionSets;
    routePositionSets.reserve(_routepos_list.size());
    for (const auto& route : _routepos_list)
    {
        routePositionSets.emplace_back(route.begin(), route.end());
    }
    const auto addTempCross = [&](const RoutePair& routePair, const position& crossPos) {
        if (tempposKeys.insert(std::make_pair(routePair, crossPos)).second)
        {
            temppos_list.emplace_back(std::make_pair(routePair, crossPos));
        }
    };

    // for (auto it = _routepos_list.begin(); it != _routepos_list.end(); it++)
    // {
    //     //检查单条线自身是否有交叉情况
    //     std::vector<position> oneroute = (*it);
    //     for (size_t i = 0; i < oneroute.size(); ++i) 
    //     {  
    //         for (size_t j = i + 1; j < oneroute.size(); ++j) 
    //         {  
    //             if (oneroute[i] == oneroute[j]) 
    //             {  
    //                 std::vector<position> existpos_list;
    //                 if(!temppos_list.empty())
    //                 {
    //                     for (auto &v: temppos_list)
    //                     {
    //                         existpos_list.push_back(v.second);
    //                     }
    //                 }
    //                 auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), oneroute[i]); 
    //                 if (exitpos == existpos_list.end())
    //                 {
    //                     oneroutepos_list.emplace_back(std::make_pair(std::make_pair((*it).front(), (*it).back()), oneroute[i]));
    //                 }
    //             }  
    //         }  
    //     }   

    //     //不同线之间交叉情况
    //     if (std::next(it) != _routepos_list.end())
    //     {
    //         for (auto temp = std::next(it); temp != _routepos_list.end(); temp++)
    //         {
    //             //同起点扇出前部分不交叉
    //             if ((*it).front() == (*temp).front())
    //             {
    //                 int i = 0;
    //                 while (i < (*it).size() && i < (*temp).size() && (*it)[i] == (*temp)[i])
    //                 {
    //                     ++i;
    //                 }
    //                 if (i != (*it).size() && i != (*temp).size())
    //                 {
    //                     std::vector<position> itpart((*it).begin()+i, (*it).end());
    //                     std::vector<position> temppart((*temp).begin()+i, (*temp).end());
    //                     if(!temppart.empty())
    //                     {
    //                         for (auto &pos : temppart)
    //                         {
    //                             auto temppos = std::find(itpart.begin(), itpart.end(), pos);
                                
    //                             std::vector<position> existpos_list;
    //                             if(!temppos_list.empty())
    //                             {
    //                                 for (auto &v: temppos_list)
    //                                 {
    //                                     existpos_list.push_back(v.second);
    //                                 }
    //                             }
    //                             auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos); 

    //                             if (temppos != (*it).end() && (*temppos) != (*it).back() && (*temppos) != (*temp).back() && exitpos == existpos_list.end())
    //                             {
    //                                 temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair((*it).front(), (*it).back()), std::make_pair((*temp).front(), (*temp).back()))), pos));
    //                             }
    //                         }
                            
    //                     }
    //                     // for (i; i < (*it).size() && i < (*temp).size(); i++)
    //                     // {
    //                     //     if ((*it)[i] == (*temp)[i])
    //                     //     {
    //                     //         temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair((*it).front(), (*it).back()), std::make_pair((*temp).front(), (*temp).back()))), (*temp)[i]));
    //                     //     }
                            
    //                     // }
                        
    //                 }
                    
    //             }
    //             else
    //             {
    //                 for (auto &pos : (*temp))
    //                 {
    //                     auto temppos = std::find((*it).begin(), (*it).end(), pos);

    //                     //避免重复检查其他线与扇出前的重合线导致重复输出交叉点
    //                     std::vector<position> existpos_list;
    //                     if(!temppos_list.empty())
    //                     {
    //                         for (auto &v: temppos_list)
    //                         {
    //                             existpos_list.push_back(v.second);
    //                         }
    //                     }
    //                     auto exitpos = std::find(existpos_list.begin(), existpos_list.end(), pos); 

    //                     if (temppos != (*it).end() && (*temppos) != (*it).back() && (*temppos) != (*temp).back() && exitpos == existpos_list.end())
    //                     {
    //                         temppos_list.emplace_back(std::make_pair((std::make_pair(std::make_pair((*it).front(), (*it).back()), std::make_pair((*temp).front(), (*temp).back()))), pos));
    //                     }
                        
    //                 }
                    
    //             }
    //         }
    //     }
    //     else
    //     {
    //         break;
    //     }
    // }

    for (size_t i = 0; i < _routepos_list.size(); i++)
    {
        std::vector<position> oneroute = _routepos_list[i];
        for (size_t i1 = 0; i1 < oneroute.size(); i1++) 
        {  
            for (size_t j1 = i1 + 1; j1 < oneroute.size(); j1++) 
            {  
                if (oneroute[i1] == oneroute[j1]) 
                {  
                    if (onerouteposPositions.insert(oneroute[i1]).second)
                    {
                        oneroutepos_list.emplace_back(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), oneroute[i1]));
                    }
                }  
            }  
        }
        for (size_t j = i+1; j < _routepos_list.size(); j++)
        {
            if (_routepos_list[i].front() == _routepos_list[j].front())
            {
                int i1 = 0;
                while (i1 < _routepos_list[i].size() && i1 < _routepos_list[j].size() && _routepos_list[i][i1] == _routepos_list[j][i1])
                {
                    ++i1;
                }
                if ((i1 <= _routepos_list[i].size()) && (i1 <= _routepos_list[j].size()))
                {
                    std::vector<position> itpart(_routepos_list[i].begin()+i1, _routepos_list[i].end());
                    const std::unordered_set<position, MappingPositionHash> itpartSet(itpart.begin(), itpart.end());
                    std::vector<position> temppart(_routepos_list[j].begin()+i1, _routepos_list[j].end());
                    if(!temppart.empty())
                    {
                        for (auto &pos1 : temppart)
                        {
                            const bool existsInSuffix = itpartSet.find(pos1) != itpartSet.end();
                            if (existsInSuffix && pos1 != itpart.back() && pos1 != temppart.back())
                            {
                                addTempCross(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()),
                                                            std::make_pair(_routepos_list[j].front(), _routepos_list[j].back())),
                                             pos1);
                            }
                        }
                        
                    }
                    
                }
                
            }
            else
            {
                for (auto &pos2 : _routepos_list[j])
                {
                    if (routePositionSets[i].find(pos2) != routePositionSets[i].end() &&
                        pos2 != _routepos_list[i].back() &&
                        pos2 != _routepos_list[j].back())
                    {
                        addTempCross(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()),
                                                    std::make_pair(_routepos_list[j].front(), _routepos_list[j].back())),
                                     pos2);
                    }
                    
                }
                
            }
        }
        
    }
    //temppos_list_examp = temppos_list;
    
    //输出元胞级映射的交叉线具体坐标（5×5结构）
    std::vector<position> allcrosspos;
    if (!oneroutepos_list.empty())
    {
        for (auto &tempcross : oneroutepos_list)
        {
            allcrosspos.push_back(tempcross.second);
        }
    }
    if (!temppos_list.empty())
    {
        for (auto &tempcross : temppos_list)
        {
            allcrosspos.push_back(tempcross.second);
        }
    }
    

    for (auto it = oneroutepos_list.begin(); it != oneroutepos_list.end(); it++)
    {
        auto pospair = (*it).first;
        auto crosspos = (*it).second;
        auto vec = deviate_list[pospair];
        auto mapping = deviatemapping_list[pospair];
        std::vector<std::vector<position>> unitpos;
        std::vector<std::string> deviate;
        

        for (auto pos = vec.begin(); pos != vec.end(); pos++)
        {
            if ((*pos).first == crosspos)
            {
                size_t index = std::distance(vec.begin(), pos); 
                unitpos.push_back(mapping[index]);
                deviate.push_back((*pos).second);
            }
        }
        if((unitpos.size() == 2) && (deviate.size() == 2))
        {
            auto unitpos1 = unitpos[0];
            auto unitpos2 = unitpos[1];
            for (auto &temp_unitpos : unitpos1)
            {
                auto temp_it = std::find(unitpos2.begin(), unitpos2.end(), temp_unitpos);
                if (temp_it != unitpos2.end())
                {
                    position diru = {crosspos.first, crosspos.second - 1}; 
                    position dird = {crosspos.first, crosspos.second + 1}; 
                    position dirl = {crosspos.first - 1, crosspos.second}; 
                    position dirr = {crosspos.first + 1, crosspos.second}; 
                    if ((deviate[0] == "XSIDE") || (deviate[0] == "YSIDE") || (deviate[0] == "XSYS"))
                    {
                        if (((deviate[0] == "XSIDE")&&((deviate[1] == "YSIDE")||(deviate[1] == "XMYS")))
                        &&((std::find(allcrosspos.begin(), allcrosspos.end(), diru)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dird)!=allcrosspos.end())))
                        {
                            if (crossline_list.find(pospair) == crossline_list.end()) 
                            {  
                                crossline_list[pospair] = {}; 
                                crossline_list[pospair].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair].push_back(unitpos2);  
                            }
                            break;
                        }
                        else if (((deviate[0] == "YSIDE")&&((deviate[1] == "XSIDE")||(deviate[1] == "XSYM")))
                        &&((std::find(allcrosspos.begin(), allcrosspos.end(), dirl)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dirr)!=allcrosspos.end())))
                        {
                            if (crossline_list.find(pospair) == crossline_list.end()) 
                            {  
                                crossline_list[pospair] = {}; 
                                crossline_list[pospair].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair].push_back(unitpos2);  
                            }
                            break;
                        }
                        else
                        {
                            if (crossline_list.find(pospair) == crossline_list.end()) 
                            {  
                                crossline_list[pospair] = {}; 
                                crossline_list[pospair].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair].push_back(unitpos1);  
                            }
                            break;
                        }    
                    }
                    else
                    {
                        if (crossline_list.find(pospair) == crossline_list.end()) 
                        {  
                            crossline_list[pospair] = {}; 
                            crossline_list[pospair].push_back(unitpos2);  
                        } else 
                        {  
                            crossline_list[pospair].push_back(unitpos2);  
                        }
                        break;
                    }
                }
            }
        }
    }
    
    for (auto it = temppos_list.begin(); it != temppos_list.end(); it++)
    {
        auto pospair1 = (*it).first.first;
        auto pospair2 = (*it).first.second;
        auto crosspos = (*it).second;

        auto vec1 = deviate_list[pospair1];
        auto vec2 = deviate_list[pospair2];
        auto mapping1 = deviatemapping_list[pospair1];
        auto mapping2 = deviatemapping_list[pospair2];
        std::vector<position> unitpos1;
        std::vector<position> unitpos2;
        std::string deviate1;
        std::string deviate2;

        std::vector<position> tempcrosscell;
        if (!crossline_list.empty())
        {
            for (auto &v : crossline_list)
            {
                auto tempv = v.second;
                for (auto &unit : tempv)
                {
                    tempcrosscell.insert(tempcrosscell.end(), unit.begin(), unit.end());
                }
            }
        }

        for (auto it1 = vec1.begin(); it1 != vec1.end(); it1++)
        {
            if ((*it1).first == crosspos)
            {
                size_t index1 = std::distance(vec1.begin(), it1); 
                unitpos1 = mapping1[index1];
                deviate1 = (*it1).second;
            }
            
        }

        for (auto it2 = vec2.begin(); it2 != vec2.end(); it2++)
        {
            if ((*it2).first == crosspos)
            {
                size_t index2 = std::distance(vec2.begin(), it2); 
                unitpos2 = mapping2[index2];
                deviate2 = (*it2).second;
            }
            
        }
        
        for (auto &unitpos : unitpos1)
        {
            auto it3 = std::find(unitpos2.begin(), unitpos2.end(), unitpos);
            if (it3 != unitpos2.end())
            {
                /*
                if ((deviate1 == "XSIDE") || (deviate1 == "YSIDE") || (deviate1 == "XSYS"))
                {
                    if (crossline_list.find(pospair1) == crossline_list.end()) 
                    {  
                        crossline_list[pospair1] = {}; 
                        crossline_list[pospair1].push_back(unitpos1);  
                    } else 
                    {  
                        crossline_list[pospair1].push_back(unitpos1);  
                    }

                    break;
                }
                else
                {
                    if (crossline_list.find(pospair2) == crossline_list.end()) 
                    {  
                        crossline_list[pospair2] = {}; 
                        crossline_list[pospair2].push_back(unitpos2);  
                    } else 
                    {  
                        crossline_list[pospair2].push_back(unitpos2);  
                    }

                    break;
                }
                */
                
                if ((deviate1 == "XSIDE") || (deviate1 == "YSIDE") || (deviate1 == "XSYS"))
                {
                    if ((deviate1 == "XSIDE") || (deviate1 == "YSIDE")/*unitpos1.size() == 5*/)
                    {
                        position midpos;
                        if (deviate1 == "XSIDE")
                        {
                            midpos.first = (crosspos.first)*5+2;
                            midpos.second = (crosspos.second)*5+4;
                        }
                        else if(deviate1 == "YSIDE")
                        {
                            midpos.first = (crosspos.first)*5+4;
                            midpos.second = (crosspos.second)*5+2;
                        }
                        
                        //auto midpos = unitpos1[2]; //还有种情况：XSYS还需考虑unitpos1[6]
                        position dir1 = {midpos.first, midpos.second + 1}; 
                        position dir2 = {midpos.first, midpos.second - 1}; 
                        position dir3 = {midpos.first - 1, midpos.second}; 
                        position dir4 = {midpos.first + 1, midpos.second}; 
                        
                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir3) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir4) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair2) == crossline_list.end()) 
                            {  
                                crossline_list[pospair2] = {}; 
                                crossline_list[pospair2].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair2].push_back(unitpos2);  
                            }
                        
                            break;
                        }
                        else
                        {
                            position diru = {crosspos.first, crosspos.second - 1}; 
                            position dird = {crosspos.first, crosspos.second + 1}; 
                            position dirl = {crosspos.first - 1, crosspos.second}; 
                            position dirr = {crosspos.first + 1, crosspos.second}; 
                            if (((deviate1 == "XSIDE")&&((deviate2 == "YSIDE")||(deviate2 == "XMYS")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), diru)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dird)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else if (((deviate1 == "YSIDE")&&((deviate2 == "XSIDE")||(deviate2 == "XSYM")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), dirl)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dirr)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                        }
                            
                    }
                    else
                    {
                        position midpos1;
                        midpos1.first = (crosspos.first)*5+2;
                        midpos1.second = (crosspos.second)*5+4;
                        position midpos2;
                        midpos2.first = (crosspos.first)*5+4;
                        midpos2.second = (crosspos.second)*5+2;
                        position dir1 = {midpos1.first, midpos1.second + 1}; 
                        position dir2 = {midpos2.first + 1, midpos2.second}; 

                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair2) == crossline_list.end()) 
                            {  
                                crossline_list[pospair2] = {}; 
                                crossline_list[pospair2].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair2].push_back(unitpos2);  
                            }
                        
                            break;
                        }
                        else
                        {
                            if (crossline_list.find(pospair1) == crossline_list.end()) 
                            {  
                                crossline_list[pospair1] = {}; 
                                crossline_list[pospair1].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair1].push_back(unitpos1);  
                            }
                            break;
                        }
                    }
                }
                else if ((deviate2 == "XSIDE") || (deviate2 == "YSIDE") || (deviate2 == "XSYS"))
                {
                    
                    if ((deviate2 == "XSIDE") || (deviate2 == "YSIDE"))
                    {
                        position midpos;
                        if (deviate2 == "XSIDE")
                        {
                            midpos.first = (crosspos.first)*5+2;
                            midpos.second = (crosspos.second)*5+4;
                        }
                        else if(deviate2 == "YSIDE")
                        {
                            midpos.first = (crosspos.first)*5+4;
                            midpos.second = (crosspos.second)*5+2;
                        }
                        //auto midpos = unitpos2[2];
                        position dir1 = {midpos.first, midpos.second + 1}; 
                        position dir2 = {midpos.first, midpos.second - 1}; 
                        position dir3 = {midpos.first - 1, midpos.second}; 
                        position dir4 = {midpos.first + 1, midpos.second}; 
                        
                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir3) != tempcrosscell.end())||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir4) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair1) == crossline_list.end()) 
                            {  
                                crossline_list[pospair1] = {}; 
                                crossline_list[pospair1].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair1].push_back(unitpos1);  
                            }
                        
                            break;
                        }
                        else
                        {
                            position diru = {crosspos.first, crosspos.second - 1}; 
                            position dird = {crosspos.first, crosspos.second + 1}; 
                            position dirl = {crosspos.first - 1, crosspos.second}; 
                            position dirr = {crosspos.first + 1, crosspos.second}; 
                            if (((deviate2 == "XSIDE")&&((deviate1 == "YSIDE")||(deviate1 == "XMYS")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), diru)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dird)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else if (((deviate2 == "YSIDE")&&((deviate1 == "XSIDE")||(deviate1 == "XSYM")))
                            &&((std::find(allcrosspos.begin(), allcrosspos.end(), dirl)!=allcrosspos.end())||(std::find(allcrosspos.begin(), allcrosspos.end(), dirr)!=allcrosspos.end())))
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                        }
                    }
                    else 
                    {
                        position midpos1;
                        midpos1.first = (crosspos.first)*5+2;
                        midpos1.second = (crosspos.second)*5+4;
                        position midpos2;
                        midpos2.first = (crosspos.first)*5+4;
                        midpos2.second = (crosspos.second)*5+2;
                        position dir1 = {midpos1.first, midpos1.second + 1}; 
                        position dir2 = {midpos2.first + 1, midpos2.second}; 

                        if ((std::find(tempcrosscell.begin(), tempcrosscell.end(), dir1) != tempcrosscell.end())
                        ||(std::find(tempcrosscell.begin(), tempcrosscell.end(), dir2) != tempcrosscell.end()))
                        {
                            if (crossline_list.find(pospair1) == crossline_list.end()) 
                            {  
                                crossline_list[pospair1] = {}; 
                                crossline_list[pospair1].push_back(unitpos1);  
                            } else 
                            {  
                                crossline_list[pospair1].push_back(unitpos1);  
                            }
                        
                            break;
                        }
                        else
                        {
                            if (crossline_list.find(pospair2) == crossline_list.end()) 
                            {  
                                crossline_list[pospair2] = {}; 
                                crossline_list[pospair2].push_back(unitpos2);  
                            } else 
                            {  
                                crossline_list[pospair2].push_back(unitpos2);  
                            }
                            break; 
                        }
                
                    }
                }
                else
                {
                    position Ymidpos;
                    Ymidpos.first = crosspos.first*5+4;
                    Ymidpos.second = crosspos.second*5+2;
                    position Xmidpos;
                    Xmidpos.first = crosspos.first*5+2;
                    Xmidpos.second = crosspos.second*5+4;
                    
                    //在MIDDLE向SIDE转变的过程中，即使显示是MIDDLE但是在映射中还是会变成SIDE位置
                    if (((deviate1 == "YMIDDLE")&&(std::find(unitpos1.begin(), unitpos1.end(), Ymidpos) != unitpos1.end()))
                    ||((deviate1 == "XMIDDLE")&&(std::find(unitpos1.begin(), unitpos1.end(), Xmidpos) != unitpos1.end())))
                    {
                        if (deviate1 == "YMIDDLE")
                        {
                            position dir = {Ymidpos.first + 1, Ymidpos.second};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }                             
                        }
                        else
                        {
                            position dir = {Xmidpos.first, Xmidpos.second + 1};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }  
                        }
                    }
                    else if(((deviate2 == "YMIDDLE")&&(std::find(unitpos2.begin(), unitpos2.end(), Ymidpos) != unitpos2.end()))
                    ||((deviate2 == "XMIDDLE")&&(std::find(unitpos2.begin(), unitpos2.end(), Xmidpos) != unitpos2.end())))
                    {
                        if (deviate2 == "YMIDDLE")
                        {
                            position dir = {Ymidpos.first + 1, Ymidpos.second};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }                             
                        }
                        else
                        {
                            position dir = {Xmidpos.first, Xmidpos.second + 1};
                            if (std::find(tempcrosscell.begin(), tempcrosscell.end(), dir) != tempcrosscell.end())
                            {
                                if (crossline_list.find(pospair1) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair1] = {}; 
                                    crossline_list[pospair1].push_back(unitpos1);  
                                } else 
                                {  
                                    crossline_list[pospair1].push_back(unitpos1);  
                                }
                                break;
                            }
                            else
                            {
                                if (crossline_list.find(pospair2) == crossline_list.end()) 
                                {  
                                    crossline_list[pospair2] = {}; 
                                    crossline_list[pospair2].push_back(unitpos2);  
                                } 
                                else 
                                {  
                                    crossline_list[pospair2].push_back(unitpos2);  
                                }
                                break;
                            }  
                        }
                    }

                    auto headpos1 = unitpos1.front();
                    auto tailpos1 = unitpos1.back();
                    auto headpos2 = unitpos2.front();
                    auto tailpos2 = unitpos2.back();
                    position hdir11 = {headpos1.first, headpos1.second + 1}; 
                    position hdir12 = {headpos1.first, headpos1.second + 2}; 
                    position hdir21 = {headpos1.first, headpos1.second - 1}; 
                    position hdir22 = {headpos1.first, headpos1.second - 2}; 
                    position hdir31 = {headpos1.first - 1, headpos1.second}; 
                    position hdir32 = {headpos1.first - 2, headpos1.second}; 
                    position hdir41 = {headpos1.first + 1, headpos1.second}; 
                    position hdir42 = {headpos1.first + 2, headpos1.second}; 

                    position tdir11 = {tailpos1.first, tailpos1.second + 1}; 
                    position tdir12 = {tailpos1.first, tailpos1.second + 2}; 
                    position tdir21 = {tailpos1.first, tailpos1.second - 1}; 
                    position tdir22 = {tailpos1.first, tailpos1.second - 2}; 
                    position tdir31 = {tailpos1.first - 1, tailpos1.second}; 
                    position tdir32 = {tailpos1.first - 2, tailpos1.second}; 
                    position tdir41 = {tailpos1.first + 1, tailpos1.second}; 
                    position tdir42 = {tailpos1.first + 2, tailpos1.second}; 

                    bool isconflict = false;
                    
                    
                    if (((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir11) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir12) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir21) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir22) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir31) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir32) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir41) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), hdir42) == tempcrosscell.end())))
                    {
                        if (crossline_list.find(pospair2) == crossline_list.end()) 
                        {  
                            crossline_list[pospair2] = {}; 
                            crossline_list[pospair2].push_back(unitpos2);  
                        } else 
                        {  
                            crossline_list[pospair2].push_back(unitpos2);  
                        }
                        
                        break;
                    }
                    else if(((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir11) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir12) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir21) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir22) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir31) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir32) == tempcrosscell.end()))
                    ||((std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir41) != tempcrosscell.end())&&(std::find(tempcrosscell.begin(), tempcrosscell.end(), tdir42) == tempcrosscell.end())))
                    {
                        if (crossline_list.find(pospair2) == crossline_list.end()) 
                        {  
                            crossline_list[pospair2] = {}; 
                            crossline_list[pospair2].push_back(unitpos2);  
                        } else 
                        {  
                            crossline_list[pospair2].push_back(unitpos2);  
                        }
                        
                        break;
                    }
                      
                    if (!isconflict)
                    {
                        if (crossline_list.find(pospair1) == crossline_list.end()) 
                        {  
                            crossline_list[pospair1] = {}; 
                            crossline_list[pospair1].push_back(unitpos1);  
                        } else 
                        {  
                            crossline_list[pospair1].push_back(unitpos1);  
                        }
                        break;
                    }
                    
                }

                break;
                
            }
            
        }
    }
}

//通过节点的扇入扇出关系对节点的组成元胞分类映射(input,output,normal,fix0,fix1)
void Mapping::node_mapping(std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>>& _Nodelink)
{
    nodecell_list["input"];
    nodecell_list["output"];
    nodecell_list["normal"];
    nodecell_list["fix0"];
    nodecell_list["fix1"];
    multi_output_not_input_boundaries.clear();
    



    if(!_Nodelink.empty())
    {
    for (auto &node : _Nodelink)
    {

        //TODO

        position temppos = node.first.first;
        unsigned int temp_first = (temppos.first)*5;
        unsigned int temp_second = (temppos.second)*5;
        position temppos1 = std::pair<unsigned int, unsigned int>(temp_first, temp_second);
        if (node.first.second == "input")
        {
            nodecell_list["input"].emplace_back(temppos1.first+2, temppos1.second+2);
            int size = node.second.second.size();
            if (size == 1)
            {
                if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))//左
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                }
                else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))//右
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))//上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                }
                else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))//下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
            }
            else if(size == 2)
            {
                if (mapOppositeInputFanout(nodecell_list, temppos, node.second.second))
                {
                    continue;
                }
                else if (((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                 || ((node.second.second.back().first < temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second)))//左上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                }
                else if (((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                 || ((node.second.second.back().first > temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second)))//右上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                }
                else if (((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                 || ((node.second.second.back().first < temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second)))//左下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
                else if (((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second)&&(node.second.second.back().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                 || ((node.second.second.back().first > temppos.first)&&(node.second.second.back().second == temppos.second)&&(node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second)))//右下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
            }
            else
            {
                continue;
            }
        }
        else if (node.first.second == "output")
        {
            nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+2);
            if ((node.second.first.size() == 1)||(node.second.first.size() == 2))
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))//左
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))//右
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))//上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))//下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                }
            }
            else
            {
                continue;
            }
        }
        else if (node.first.second == "maj")
        {
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
        }
        else if (node.first.second == "and")
        {
            const auto normalBefore = nodecell_list["normal"].size();
            const auto fix0Before = nodecell_list["fix0"].size();
            const auto fix1Before = nodecell_list["fix1"].size();
            const auto outputBefore = nodecell_list["output"].size();
            if (node.second.first.size() == 2)
            {
                if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                    || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//左上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                        
                    }
                }
                else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                    || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//右上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["fix0"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                    || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//左下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                    || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//右下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["fix0"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                    || ((node.second.first.back().first == temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//上下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix0"].emplace_back(temppos1.first+4, temppos1.second+2);
                            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                            nodecell_list["fix0"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second))
                    || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second == temppos.second)))//左右
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else
                    {
                        if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                        {
                            nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["fix0"].emplace_back(temppos1.first+2, temppos1.second);
                        }
                    }
                }
            }
            if (nodecell_list["normal"].size() == normalBefore &&
                nodecell_list["fix0"].size() == fix0Before &&
                nodecell_list["fix1"].size() == fix1Before &&
                nodecell_list["output"].size() == outputBefore)
            {
                fallbackLogicGateMapping(nodecell_list, temppos, node.second.first, node.second.second, "fix0");
            }
        }
        else if (node.first.second == "or")
        {
            const auto normalBefore = nodecell_list["normal"].size();
            const auto fix0Before = nodecell_list["fix0"].size();
            const auto fix1Before = nodecell_list["fix1"].size();
            const auto outputBefore = nodecell_list["output"].size();
            if (node.second.first.size() == 2)
            {
                if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                    || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//左上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                        
                    }
                }
                else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second < temppos.second))
                    || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)))//右上
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["fix1"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                    || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//左下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["output"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                    || ((node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//右下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                            nodecell_list["fix1"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second)&&(node.second.first.back().first == temppos.first)&&(node.second.first.back().second > temppos.second))
                    || ((node.second.first.back().first == temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second)))//上下
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["output"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                        {
                            nodecell_list["fix1"].emplace_back(temppos1.first+4, temppos1.second+2);
                            nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                            nodecell_list["fix1"].emplace_back(temppos1.first, temppos1.second+2);
                        }
                    }
                }
                else if (((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second)&&(node.second.first.back().first > temppos.first)&&(node.second.first.back().second == temppos.second))
                    || ((node.second.first.back().first < temppos.first)&&(node.second.first.back().second == temppos.second)&&(node.second.first.front().first == temppos.first)&&(node.second.first.front().second == temppos.second)))//左右
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    if(node.second.second.empty())
                    {
                        nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else
                    {
                        if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                        {
                            nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        }
                        else
                        {
                            nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                            nodecell_list["fix1"].emplace_back(temppos1.first+2, temppos1.second);
                        }
                    }
                }
            }
            if (nodecell_list["normal"].size() == normalBefore &&
                nodecell_list["fix0"].size() == fix0Before &&
                nodecell_list["fix1"].size() == fix1Before &&
                nodecell_list["output"].size() == outputBefore)
            {
                fallbackLogicGateMapping(nodecell_list, temppos, node.second.first, node.second.second, "fix1");
            }
        }
        else if (node.first.second == "not")
        {
            int size_output = node.second.second.size();
            int size_input = node.second.first.size();
            if ((size_input == 1) && (size_output == 2) &&
                placeMultiOutputNotTemplate(nodecell_list, temppos, node.second.first, node.second.second))
            {
                const auto inputBoundary = nodeBoundaryCell(temppos, node.second.first.front());
                if (inputBoundary.valid) {
                    multi_output_not_input_boundaries.insert(inputBoundary.pos);
                }
                continue;
            }
            if ((size_input == 1) && (size_output == 1))
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))//左
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))//上
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))//右
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))//下
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.front().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.front().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                }
            }
            else if ((size_input == 1) && (size_output == 2))
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if (((node.second.second.front().second < temppos.second)&&(node.second.second.back().first > temppos.first))
                    ||((node.second.second.back().second < temppos.second)&&(node.second.second.front().first > temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if (((node.second.second.front().second > temppos.second)&&(node.second.second.back().first > temppos.first))
                    ||((node.second.second.back().second > temppos.second)&&(node.second.second.front().first > temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))
                {
                    if (((node.second.second.front().first < temppos.first)&&(node.second.second.back().second > temppos.second))
                    ||((node.second.second.back().first < temppos.first)&&(node.second.second.front().second > temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if (((node.second.second.front().first > temppos.first)&&(node.second.second.back().second > temppos.second))
                    ||((node.second.second.back().first > temppos.first)&&(node.second.second.front().second > temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    if (((node.second.second.front().second < temppos.second)&&(node.second.second.back().first < temppos.first))
                    ||((node.second.second.back().second < temppos.second)&&(node.second.second.front().first < temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if (((node.second.second.front().second > temppos.second)&&(node.second.second.back().first < temppos.first))
                    ||((node.second.second.back().second > temppos.second)&&(node.second.second.front().first < temppos.first)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                {
                    if (((node.second.second.front().first < temppos.first)&&(node.second.second.back().second < temppos.second))
                    ||((node.second.second.back().first < temppos.first)&&(node.second.second.front().second < temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if (((node.second.second.front().first > temppos.first)&&(node.second.second.back().second < temppos.second))
                    ||((node.second.second.back().first > temppos.first)&&(node.second.second.front().second < temppos.second)))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
            }
            else if ((size_input == 1) && (size_output < 1))//作为输出
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+3, temppos1.second+2);
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+3);
                   
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+1, temppos1.second+2);
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                {
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+3);
                    nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                    nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                    nodecell_list["output"].emplace_back(temppos1.first+2, temppos1.second+1);
                }
                
            }
            else
            {
                continue;
            }
        }
        else if (node.first.second == "wire")
        {
            if (!node.second.first.empty() || !node.second.second.empty())
            {
                connectIncidentPortsToCenter(nodecell_list,
                                             temppos,
                                             node.second.first,
                                             node.second.second,
                                             true);
            }
            continue;

            if (node.second.first.empty() || node.second.second.empty())
            {
                continue;
            }
            if ((node.second.first.front().first == temppos.first)&&(node.second.second.front().first == temppos.first))//上下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
            }
            else if ((node.second.first.front().second == temppos.second)&&(node.second.second.front().second == temppos.second))//左右
            {
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.second.front().second < temppos.second))
            ||((node.second.first.front().second < temppos.second)&&(node.second.second.front().first < temppos.first)))//左上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.second.front().second < temppos.second))
            ||((node.second.first.front().second < temppos.second)&&(node.second.second.front().first > temppos.first)))//右上
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
            else if (((node.second.first.front().first < temppos.first)&&(node.second.second.front().second > temppos.second))
            ||((node.second.first.front().second > temppos.second)&&(node.second.second.front().first < temppos.first)))//左下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
            }
            else if (((node.second.first.front().first > temppos.first)&&(node.second.second.front().second > temppos.second))
            ||((node.second.first.front().second > temppos.second)&&(node.second.second.front().first > temppos.first)))//右下
            {
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
            }
        }
        else if (node.first.second == "fanout")
        {
            if (!node.second.first.empty() || !node.second.second.empty())
            {
                connectIncidentPortsToCenter(nodecell_list,
                                             temppos,
                                             node.second.first,
                                             node.second.second,
                                             true);
            }
            continue;

            auto size_output = node.second.second.size();
            auto size_input = node.second.first.size();
            if (size_input == 1 && size_output == 1)//1个输出
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))//输入在左边
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))//输入在上边
                {
                    if ((node.second.second.front().first > temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first < temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))//输入在右边
                {
                    if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second > temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    else if ((node.second.second.front().first < temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))//输入在下边
                {
                    if ((node.second.second.front().first < temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first == temppos.first)&&(node.second.second.back().second < temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().first > temppos.first)&&(node.second.second.back().second == temppos.second))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                
            }
            else if (size_input == 1 && size_output == 2)//2个输出
            {
                if ((node.second.first.front().first < temppos.first)&&(node.second.first.front().second == temppos.second))//输入在左边
                {
                    if ((node.second.second.front().second < temppos.second)||(node.second.second.back().second < temppos.second))//输出在右上
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().second > temppos.second)||(node.second.second.back().second > temppos.second))//输出在右下
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                    
                }
                else if ((node.second.first.front().first == temppos.first)&&(node.second.first.front().second < temppos.second))//输入在上边
                {
                    if ((node.second.second.front().first < temppos.first)||(node.second.second.back().first < temppos.first))//输出在左下
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)||(node.second.second.back().first > temppos.first))//输出在右下
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                else if ((node.second.first.front().first > temppos.first)&&(node.second.first.front().second == temppos.second))//输入在右边
                {
                    if ((node.second.second.front().second < temppos.second)||(node.second.second.back().second < temppos.second))//输出在左上
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                    }
                    else if ((node.second.second.front().second > temppos.second)||(node.second.second.back().second > temppos.second))//输出在左下
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                    }
                }
                // else((node.second.first.front().first == temppos.first)&&(node.second.first.front().second > temppos.second))
                else//输出在下边
                {
                    if ((node.second.second.front().first < temppos.first)||(node.second.second.back().first < temppos.first))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+1, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first, temppos1.second+2);
                    }
                    else if ((node.second.second.front().first > temppos.first)||(node.second.second.back().first > temppos.first))
                    {
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+1);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+3);
                        nodecell_list["normal"].emplace_back(temppos1.first+2, temppos1.second+4);
                        nodecell_list["normal"].emplace_back(temppos1.first+3, temppos1.second+2);
                        nodecell_list["normal"].emplace_back(temppos1.first+4, temppos1.second+2);
                    }
                }
                
            }
            else
            {
                continue;
            }
        }
        else
        {
            continue;
        }
        
    }
    }
}

void Mapping::not_check(std::vector<std::vector<position>> &_routepos_list){
    std::vector<std::pair<std::pair<std::pair<position, position>, std::pair<position, position>>, position>> temppos_list;
    std::vector<std::pair<std::pair<position, position>, position>> oneroutepos_list;
    using RouteKey = std::pair<position, position>;
    using RoutePair = std::pair<RouteKey, RouteKey>;
    std::set<std::pair<RoutePair, position>> tempposKeys;
    std::unordered_set<position, MappingPositionHash> onerouteposPositions;
    const auto addTempCross = [&](const RoutePair& routePair, const position& crossPos) {
        if (tempposKeys.insert(std::make_pair(routePair, crossPos)).second)
        {
            temppos_list.emplace_back(std::make_pair(routePair, crossPos));
        }
    };

    for (size_t i = 0; i < _routepos_list.size(); i++)
    {
        std::vector<position> oneroute = _routepos_list[i];
        for (size_t i1 = 0; i1 < oneroute.size(); i1++) 
        {  
            for (size_t j1 = i1 + 1; j1 < oneroute.size(); j1++) 
            {  
                if (oneroute[i1] == oneroute[j1]) 
                {  
                    if (onerouteposPositions.insert(oneroute[i1]).second)
                    {
                        oneroutepos_list.emplace_back(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()), oneroute[i1]));
                    }
                }  
            }  
        }
        for (size_t j = i+1; j < _routepos_list.size(); j++)
        {
            if (_routepos_list[i].front() == _routepos_list[j].front())
            {
                int i1 = 0;
                while (i1 < _routepos_list[i].size() && i1 < _routepos_list[j].size() && _routepos_list[i][i1] == _routepos_list[j][i1])
                {
                    ++i1;
                }
                if ((i1 <= _routepos_list[i].size()) && (i1 <= _routepos_list[j].size()))
                {
                    std::vector<position> itpart(_routepos_list[i].begin()+i1, _routepos_list[i].end());
                    std::vector<position> temppart(_routepos_list[j].begin()+i1, _routepos_list[j].end());
                    if(!temppart.empty())
                    {
                        for (auto &pos1 : temppart)
                        {
                            auto temppos = std::find(itpart.begin(), itpart.end(), pos1);

                            if (temppos != itpart.end() && (*temppos) != itpart.back() && (*temppos) != temppart.back())
                            {
                                addTempCross(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()),
                                                            std::make_pair(_routepos_list[j].front(), _routepos_list[j].back())),
                                             pos1);
                            }
                        }
                        
                    }
                    
                }
                
            }
            else
            {
                for (auto &pos2 : _routepos_list[j])
                {
                    auto temppos = std::find(_routepos_list[i].begin(), _routepos_list[i].end(), pos2);

                    if (temppos != _routepos_list[i].end() && (*temppos) != _routepos_list[i].back() && (*temppos) != _routepos_list[j].back())
                    {
                        addTempCross(std::make_pair(std::make_pair(_routepos_list[i].front(), _routepos_list[i].back()),
                                                    std::make_pair(_routepos_list[j].front(), _routepos_list[j].back())),
                                     pos2);
                    }
                    
                }
                
            }
        }
        
    }
    oneroutepos_list_examp = oneroutepos_list;
    temppos_list_examp = temppos_list;
}
};
