#include "autopr/sequential/globalPhaseSolver.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace fcngraph::sequential
{
namespace
{

// This dependency-free backend is deliberately a bounded reference solver.
// The limits keep recursive search depth finite and establish a conservative
// numeric range in which all int64_t potential arithmetic is overflow-free.
constexpr std::size_t kMaxEvents = 256;
// Larger feedback-retaining layouts can contain more than the former 512
// coarse-tile occurrences. DFS work remains independently bounded by
// maxDfsNodes; these structural limits primarily protect parsing/index
// arithmetic.
constexpr std::size_t kMaxClockResources = 4096;
constexpr std::size_t kMaxOccurrences = 4096;
constexpr std::size_t kMaxRoutes = 128;
constexpr std::size_t kMaxTimingArcs = 512;
constexpr std::size_t kMaxRouteEdges = 8192;
constexpr std::size_t kMaxIiCandidates = 16;
constexpr int kMaxPhaseCount = 8;
constexpr int kMaxEpochTerm = 1000000;
constexpr int kMaxAnchorMagnitude = 1000000000;

int floorMod(std::int64_t value, int modulus)
{
    const std::int64_t remainder = value % modulus;
    return static_cast<int>(remainder < 0 ? remainder + modulus : remainder);
}

bool checkedAdd(std::int64_t left,
                std::int64_t right,
                std::int64_t &result)
{
    if ((right > 0 &&
         left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 &&
         left < std::numeric_limits<std::int64_t>::min() - right))
    {
        return false;
    }
    result = left + right;
    return true;
}

void setError(std::string *error, const std::string &message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

class PotentialDsu
{
public:
    PotentialDsu(std::size_t size, int modulus)
        : parent_(size), size_(size, 1), differenceToParent_(size, 0),
          modulus_(modulus)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            parent_[index] = index;
        }
    }

    bool unite(std::size_t from, std::size_t to, std::int64_t difference)
    {
        const auto [fromRoot, fromPotential] = find(from);
        const auto [toRoot, toPotential] = find(to);
        difference = normalize(difference);
        if (fromRoot == toRoot)
        {
            return normalize(toPotential - fromPotential) == difference;
        }

        if (size_[fromRoot] >= size_[toRoot])
        {
            parent_[toRoot] = fromRoot;
            // value(toRoot) - value(fromRoot)
            differenceToParent_[toRoot] =
                normalize(difference + fromPotential - toPotential);
            size_[fromRoot] += size_[toRoot];
        }
        else
        {
            parent_[fromRoot] = toRoot;
            // value(fromRoot) - value(toRoot)
            differenceToParent_[fromRoot] =
                normalize(toPotential - fromPotential - difference);
            size_[toRoot] += size_[fromRoot];
        }
        return true;
    }

    std::optional<std::int64_t> difference(std::size_t from,
                                           std::size_t to) const
    {
        const auto [fromRoot, fromPotential] = find(from);
        const auto [toRoot, toPotential] = find(to);
        if (fromRoot != toRoot)
        {
            return std::nullopt;
        }
        return normalize(toPotential - fromPotential);
    }

private:
    std::pair<std::size_t, std::int64_t> find(std::size_t node) const
    {
        std::int64_t potential = 0;
        while (parent_[node] != node)
        {
            potential = normalize(potential + differenceToParent_[node]);
            node = parent_[node];
        }
        return {node, potential};
    }

    std::int64_t normalize(std::int64_t value) const
    {
        return modulus_ > 0 ? floorMod(value, modulus_) : value;
    }

    std::vector<std::size_t> parent_;
    std::vector<std::size_t> size_;
    std::vector<std::int64_t> differenceToParent_;
    int modulus_ = 0;
};

struct CompiledEdge
{
    std::size_t from = 0;
    std::size_t to = 0;
    std::size_t route = 0;
};

struct CompiledRoute
{
    const FixedRouteSpec *spec = nullptr;
    std::vector<std::size_t> occurrenceNodes;
    std::vector<std::size_t> edgeIndexes;
};

struct CompiledProblem
{
    std::size_t zeroNode = 0;
    std::size_t nodeCount = 0;
    std::map<std::string, std::size_t> eventNodes;
    std::map<std::string, std::size_t> epochNodes;
    std::map<std::string, const RouteOccurrenceSpec *> occurrenceSpecs;
    std::vector<CompiledRoute> routes;
    std::vector<CompiledEdge> edges;
};

struct SearchState
{
    SearchState(std::size_t nodeCount, int phaseCount, std::size_t edgeCount)
        : integerEpoch(nodeCount, 0), moduloPhase(nodeCount, phaseCount),
          edgeValues(edgeCount, -1)
    {
    }

    PotentialDsu integerEpoch;
    PotentialDsu moduloPhase;
    std::vector<int> edgeValues;
};

struct EventEpochSeed
{
    bool feasible = false;
    std::map<std::string, std::int64_t> epoch;
};

enum class SearchStatus
{
    Sat,
    Unsat,
    Limit
};

int minimumRouteAdvances(std::size_t edgeCount, int maxSamePhaseCells)
{
    if (maxSamePhaseCells <= 0)
    {
        return 0;
    }
    return static_cast<int>(
        edgeCount / static_cast<std::size_t>(maxSamePhaseCells));
}

// Solve the event-level relaxation before enumerating individual route
// holds/advances.  A route of E binary steps and a K-cell same-phase limit
// has a contiguous feasible advance-count interval [floor(E/K), E].  These
// intervals become integer difference constraints between source/sink
// events.  Bellman-Ford either proves that relaxation infeasible or returns
// one integral event-epoch seed.  The seed is only a search heuristic: when
// its detailed edge assignment is infeasible (for example because of shared
// physical resources), the complete unseeded DFS still runs.
EventEpochSeed makeEventEpochSeed(const GlobalClockProblem &problem, int ii)
{
    EventEpochSeed result;
    std::map<std::string, std::size_t> node;
    node["<zero>"] = 0;
    for (const auto &event : problem.events)
    {
        node[event] = node.size();
    }

    struct BoundEdge
    {
        std::size_t from = 0;
        std::size_t to = 0;
        std::int64_t upper = 0;
    };
    std::vector<BoundEdge> edges;
    const auto addUpper = [&edges](std::size_t from, std::size_t to,
                                   std::int64_t upper) {
        edges.push_back({from, to, upper});
    };
    const auto addEquality = [&addUpper](std::size_t from, std::size_t to,
                                         std::int64_t difference) {
        addUpper(from, to, difference);
        addUpper(to, from, -difference);
    };

    for (const auto &anchor : problem.anchors)
    {
        addEquality(node.at("<zero>"), node.at(anchor.event), anchor.epoch);
    }
    for (const auto &arc : problem.timingArcs)
    {
        const std::int64_t difference =
            static_cast<std::int64_t>(arc.latencyEpochs) -
            static_cast<std::int64_t>(arc.iterationDistance) * ii;
        addEquality(node.at(arc.sourceEvent), node.at(arc.sinkEvent),
                    difference);
    }
    for (const auto &route : problem.routes)
    {
        const std::size_t routeEdges = route.occurrences.size() - 1;
        const std::int64_t minimum = minimumRouteAdvances(
            routeEdges, problem.maxConsecutiveSamePhaseCells);
        const std::int64_t maximum =
            static_cast<std::int64_t>(routeEdges);
        const std::int64_t iterationOffset =
            static_cast<std::int64_t>(route.iterationDistance) * ii;
        // minimum <= E(sink)-E(source)+distance*II <= maximum
        addUpper(node.at(route.sourceEvent), node.at(route.sinkEvent),
                 maximum - iterationOffset);
        addUpper(node.at(route.sinkEvent), node.at(route.sourceEvent),
                 -minimum + iterationOffset);
    }

    std::vector<std::int64_t> distance(node.size(), 0);
    bool changed = false;
    for (std::size_t pass = 0; pass < node.size(); ++pass)
    {
        changed = false;
        for (const auto &edge : edges)
        {
            const std::int64_t candidate =
                distance[edge.from] + edge.upper;
            if (candidate < distance[edge.to])
            {
                distance[edge.to] = candidate;
                changed = true;
            }
        }
        if (!changed)
        {
            break;
        }
    }
    if (changed)
    {
        // A relaxation on the |V|-th pass witnesses a negative cycle.
        return result;
    }

    const std::int64_t zero = distance[node.at("<zero>")];
    result.feasible = true;
    for (const auto &event : problem.events)
    {
        result.epoch[event] = distance[node.at(event)] - zero;
    }
    return result;
}

bool uniqueNonEmpty(const std::vector<std::string> &values,
                    const std::string &kind,
                    std::string &error)
{
    std::set<std::string> observed;
    for (const auto &value : values)
    {
        if (value.empty())
        {
            error = kind + " identifier is empty";
            return false;
        }
        if (!observed.insert(value).second)
        {
            error = "duplicate " + kind + " identifier: " + value;
            return false;
        }
    }
    return true;
}

bool zeroDistanceGraphIsAcyclic(const GlobalClockProblem &problem,
                                std::string &error)
{
    std::map<std::string, std::vector<std::string>> adjacency;
    for (const auto &event : problem.events)
    {
        adjacency[event];
    }
    for (const auto &route : problem.routes)
    {
        if (route.iterationDistance == 0)
        {
            adjacency[route.sourceEvent].push_back(route.sinkEvent);
        }
    }
    for (const auto &arc : problem.timingArcs)
    {
        if (arc.iterationDistance == 0)
        {
            adjacency[arc.sourceEvent].push_back(arc.sinkEvent);
        }
    }

    std::map<std::string, int> color;
    std::function<bool(const std::string &)> visit = [&](const std::string &node) {
        color[node] = 1;
        for (const auto &sink : adjacency[node])
        {
            if (color[sink] == 1)
            {
                error = "zero-iteration-distance combinational cycle reaches event " +
                        sink;
                return false;
            }
            if (color[sink] == 0 && !visit(sink))
            {
                return false;
            }
        }
        color[node] = 2;
        return true;
    };

    for (const auto &[event, unused] : adjacency)
    {
        (void)unused;
        if (color[event] == 0 && !visit(event))
        {
            return false;
        }
    }
    return true;
}

bool validateProblem(const GlobalClockProblem &problem, std::string &error)
{
    if (problem.phaseCount < 2 || problem.phaseCount > kMaxPhaseCount)
    {
        error = "phaseCount is outside the bounded reference range [2,8]";
        return false;
    }
    if (problem.maxDfsNodes == 0)
    {
        error = "maxDfsNodes must be positive";
        return false;
    }
    if (problem.iiCandidates.empty() ||
        problem.iiCandidates.size() > kMaxIiCandidates)
    {
        error = "II candidate count is outside the bounded reference range";
        return false;
    }
    for (const int ii : problem.iiCandidates)
    {
        if (ii <= 0 || ii > kMaxEpochTerm ||
            ii % problem.phaseCount != 0)
        {
            error = "every II candidate must be a bounded positive phaseCount multiple";
            return false;
        }
    }
    if (problem.events.size() > kMaxEvents ||
        problem.clockResources.size() > kMaxClockResources ||
        problem.occurrences.size() > kMaxOccurrences ||
        problem.routes.size() > kMaxRoutes ||
        problem.timingArcs.size() > kMaxTimingArcs)
    {
        error = "problem exceeds the bounded reference solver size limits";
        return false;
    }
    if (!uniqueNonEmpty(problem.events, "event", error))
    {
        return false;
    }

    const std::set<std::string> eventIds(problem.events.begin(),
                                         problem.events.end());
    std::vector<std::string> resourceIds;
    for (const auto &resource : problem.clockResources)
    {
        resourceIds.push_back(resource.id);
    }
    if (!uniqueNonEmpty(resourceIds, "clock resource", error))
    {
        return false;
    }
    const std::set<std::string> resourceIdSet(resourceIds.begin(),
                                               resourceIds.end());
    std::map<std::string, ClockResourceSharing> resourceSharing;
    for (const auto &resource : problem.clockResources)
    {
        resourceSharing[resource.id] = resource.sharing;
    }

    std::vector<std::string> occurrenceIds;
    std::map<std::string, const RouteOccurrenceSpec *> occurrenceById;
    std::map<std::string, std::set<std::string>> resourceEpochVariables;
    for (const auto &occurrence : problem.occurrences)
    {
        occurrenceIds.push_back(occurrence.id);
        if (occurrence.clockResource.empty() ||
            resourceIdSet.count(occurrence.clockResource) == 0)
        {
            error = "occurrence " + occurrence.id +
                    " refers to an unknown clock resource";
            return false;
        }
        if (occurrence.epochVariable.empty())
        {
            error = "occurrence " + occurrence.id +
                    " has an empty epoch variable";
            return false;
        }
        occurrenceById[occurrence.id] = &occurrence;
        resourceEpochVariables[occurrence.clockResource].insert(
            occurrence.epochVariable);
    }
    if (!uniqueNonEmpty(occurrenceIds, "occurrence", error))
    {
        return false;
    }
    for (const auto &[resource, epochVariables] : resourceEpochVariables)
    {
        if (resourceSharing.at(resource) ==
                ClockResourceSharing::ExclusiveOrAliased &&
            epochVariables.size() > 1)
        {
            error = "exclusive resource " + resource +
                    " is used by independent epoch variables";
            return false;
        }
    }

    std::set<std::string> routeIds;
    std::set<std::string> usedOccurrences;
    std::size_t totalRouteEdges = 0;
    for (const auto &route : problem.routes)
    {
        if (route.id.empty() || !routeIds.insert(route.id).second)
        {
            error = "empty or duplicate route identifier: " + route.id;
            return false;
        }
        if (eventIds.count(route.sourceEvent) == 0 ||
            eventIds.count(route.sinkEvent) == 0)
        {
            error = "route " + route.id + " refers to an unknown event";
            return false;
        }
        if (route.iterationDistance < 0 || route.iterationDistance > 1)
        {
            error = "route " + route.id +
                    " exceeds the v0 iteration-distance range [0,1]";
            return false;
        }
        if (route.occurrences.empty())
        {
            error = "route " + route.id + " has no occurrences";
            return false;
        }
        totalRouteEdges += route.occurrences.size() - 1;
        if (totalRouteEdges > kMaxRouteEdges)
        {
            error = "problem exceeds the 1024-edge reference search limit";
            return false;
        }
        std::set<std::string> routeOccurrences;
        for (const auto &occurrence : route.occurrences)
        {
            if (occurrenceById.count(occurrence) == 0)
            {
                error = "route " + route.id +
                        " refers to unknown occurrence " + occurrence;
                return false;
            }
            if (!routeOccurrences.insert(occurrence).second)
            {
                error = "route " + route.id +
                        " repeats occurrence " + occurrence;
                return false;
            }
            usedOccurrences.insert(occurrence);
        }
    }
    if (usedOccurrences.size() != problem.occurrences.size())
    {
        error = "every occurrence must belong to at least one route";
        return false;
    }

    std::set<std::string> timingArcIds;
    for (const auto &arc : problem.timingArcs)
    {
        if (arc.id.empty() || !timingArcIds.insert(arc.id).second)
        {
            error = "empty or duplicate timing arc identifier: " + arc.id;
            return false;
        }
        if (eventIds.count(arc.sourceEvent) == 0 ||
            eventIds.count(arc.sinkEvent) == 0)
        {
            error = "timing arc " + arc.id + " refers to an unknown event";
            return false;
        }
        if (arc.iterationDistance < 0 || arc.iterationDistance > 1 ||
            arc.latencyEpochs < 0 || arc.latencyEpochs > kMaxEpochTerm)
        {
            error = "timing arc " + arc.id+
                    " exceeds the v0 distance/latency range";
            return false;
        }
    }

    if (problem.anchors.empty())
    {
        error = "at least one absolute epoch anchor is required";
        return false;
    }
    for (const auto &anchor : problem.anchors)
    {
        if (eventIds.count(anchor.event) == 0)
        {
            error = "anchor refers to unknown event " + anchor.event;
            return false;
        }
        if (anchor.epoch < -kMaxAnchorMagnitude ||
            anchor.epoch > kMaxAnchorMagnitude)
        {
            error = "anchor epoch exceeds the bounded numeric range";
            return false;
        }
    }

    if (!zeroDistanceGraphIsAcyclic(problem, error))
    {
        return false;
    }

    // Every absolute-timing component must have an anchor.  Route edges connect
    // epoch variables regardless of their later 0/1 assignment.
    std::map<std::string, std::set<std::string>> adjacency;
    const auto eventNode = [](const std::string &id) { return "event:" + id; };
    const auto epochNode = [](const std::string &id) { return "epoch:" + id; };
    const auto connect = [&adjacency](const std::string &left,
                                      const std::string &right) {
        adjacency[left].insert(right);
        adjacency[right].insert(left);
    };
    for (const auto &event : problem.events)
    {
        adjacency[eventNode(event)];
    }
    for (const auto &occurrence : problem.occurrences)
    {
        adjacency[epochNode(occurrence.epochVariable)];
    }
    for (const auto &route : problem.routes)
    {
        const auto &first = *occurrenceById.at(route.occurrences.front());
        const auto &last = *occurrenceById.at(route.occurrences.back());
        connect(eventNode(route.sourceEvent), epochNode(first.epochVariable));
        connect(eventNode(route.sinkEvent), epochNode(last.epochVariable));
        for (std::size_t index = 1; index < route.occurrences.size(); ++index)
        {
            const auto &previous =
                *occurrenceById.at(route.occurrences[index - 1]);
            const auto &current = *occurrenceById.at(route.occurrences[index]);
            connect(epochNode(previous.epochVariable),
                    epochNode(current.epochVariable));
        }
    }
    for (const auto &arc : problem.timingArcs)
    {
        connect(eventNode(arc.sourceEvent), eventNode(arc.sinkEvent));
    }

    std::set<std::string> anchorNodes;
    for (const auto &anchor : problem.anchors)
    {
        anchorNodes.insert(eventNode(anchor.event));
    }
    std::set<std::string> visited;
    for (const auto &[start, unused] : adjacency)
    {
        (void)unused;
        if (visited.count(start) != 0)
        {
            continue;
        }
        bool anchored = false;
        std::queue<std::string> pending;
        pending.push(start);
        visited.insert(start);
        while (!pending.empty())
        {
            const auto node = pending.front();
            pending.pop();
            anchored = anchored || anchorNodes.count(node) != 0;
            for (const auto &next : adjacency[node])
            {
                if (visited.insert(next).second)
                {
                    pending.push(next);
                }
            }
        }
        if (!anchored)
        {
            error = "an absolute-timing component has no epoch anchor";
            return false;
        }
    }
    return true;
}

CompiledProblem compileProblem(const GlobalClockProblem &problem)
{
    CompiledProblem compiled;
    std::map<std::string, std::size_t> nodes;
    const auto addNode = [&nodes](const std::string &key) {
        const auto found = nodes.find(key);
        if (found != nodes.end())
        {
            return found->second;
        }
        const std::size_t index = nodes.size();
        nodes[key] = index;
        return index;
    };

    compiled.zeroNode = addNode("constant:zero");
    for (const auto &event : problem.events)
    {
        compiled.eventNodes[event] = addNode("event:" + event);
    }
    for (const auto &occurrence : problem.occurrences)
    {
        compiled.occurrenceSpecs[occurrence.id] = &occurrence;
        compiled.epochNodes[occurrence.epochVariable] =
            addNode("epoch:" + occurrence.epochVariable);
    }

    for (std::size_t routeIndex = 0;
         routeIndex < problem.routes.size(); ++routeIndex)
    {
        const auto &route = problem.routes[routeIndex];
        CompiledRoute compiledRoute;
        compiledRoute.spec = &route;
        for (const auto &occurrenceId : route.occurrences)
        {
            const auto *occurrence = compiled.occurrenceSpecs.at(occurrenceId);
            compiledRoute.occurrenceNodes.push_back(
                compiled.epochNodes.at(occurrence->epochVariable));
        }
        for (std::size_t index = 1;
             index < compiledRoute.occurrenceNodes.size(); ++index)
        {
            const std::size_t edgeIndex = compiled.edges.size();
            compiled.edges.push_back({
                compiledRoute.occurrenceNodes[index - 1],
                compiledRoute.occurrenceNodes[index],
                routeIndex
            });
            compiledRoute.edgeIndexes.push_back(edgeIndex);
        }
        compiled.routes.push_back(std::move(compiledRoute));
    }
    compiled.nodeCount = nodes.size();
    return compiled;
}

bool addEquation(SearchState &state,
                 std::size_t from,
                 std::size_t to,
                 std::int64_t difference)
{
    return state.integerEpoch.unite(from, to, difference) &&
           state.moduloPhase.unite(from, to, difference);
}

bool initializeForIi(const GlobalClockProblem &problem,
                     const CompiledProblem &compiled,
                     int ii,
                     SearchState &state)
{
    for (const auto &anchor : problem.anchors)
    {
        if (!addEquation(state,
                         compiled.zeroNode,
                         compiled.eventNodes.at(anchor.event),
                         anchor.epoch))
        {
            return false;
        }
    }
    for (const auto &arc : problem.timingArcs)
    {
        const std::int64_t difference =
            static_cast<std::int64_t>(arc.latencyEpochs) -
            static_cast<std::int64_t>(arc.iterationDistance) * ii;
        if (!addEquation(state,
                         compiled.eventNodes.at(arc.sourceEvent),
                         compiled.eventNodes.at(arc.sinkEvent),
                         difference))
        {
            return false;
        }
    }
    for (const auto &route : compiled.routes)
    {
        const auto &spec = *route.spec;
        if (!addEquation(state,
                         compiled.eventNodes.at(spec.sourceEvent),
                         route.occurrenceNodes.front(),
                         0))
        {
            return false;
        }
        const std::int64_t sinkOffset =
            static_cast<std::int64_t>(spec.iterationDistance) * ii;
        if (!addEquation(state,
                         compiled.eventNodes.at(spec.sinkEvent),
                         route.occurrenceNodes.back(),
                         sinkOffset))
        {
            return false;
        }
    }

    std::map<std::string, std::size_t> resourceRepresentative;
    for (const auto &occurrence : problem.occurrences)
    {
        const std::size_t node = compiled.epochNodes.at(
            occurrence.epochVariable);
        const auto [position, inserted] =
            resourceRepresentative.emplace(occurrence.clockResource, node);
        if (!inserted &&
            !state.moduloPhase.unite(position->second, node, 0))
        {
            return false;
        }
    }
    return true;
}

int edgeMask(const SearchState &state,
             const CompiledProblem &compiled,
             std::size_t edgeIndex)
{
    const int assigned = state.edgeValues[edgeIndex];
    if (assigned >= 0)
    {
        return 1 << assigned;
    }
    const auto &edge = compiled.edges[edgeIndex];
    int mask = 0b11;
    const auto exact = state.integerEpoch.difference(edge.from, edge.to);
    if (exact.has_value())
    {
        if (exact.value() == 0)
        {
            mask &= 0b01;
        }
        else if (exact.value() == 1)
        {
            mask &= 0b10;
        }
        else
        {
            return 0;
        }
    }
    const auto residue = state.moduloPhase.difference(edge.from, edge.to);
    if (residue.has_value())
    {
        if (residue.value() == 0)
        {
            mask &= 0b01;
        }
        else if (residue.value() == 1)
        {
            mask &= 0b10;
        }
        else
        {
            return 0;
        }
    }
    return mask;
}

bool assignEdge(SearchState &state,
                const CompiledProblem &compiled,
                std::size_t edgeIndex,
                int value)
{
    if (state.edgeValues[edgeIndex] >= 0)
    {
        return state.edgeValues[edgeIndex] == value;
    }
    const auto &edge = compiled.edges[edgeIndex];
    state.edgeValues[edgeIndex] = value;
    return addEquation(state, edge.from, edge.to, value);
}

bool propagate(const GlobalClockProblem &problem,
               const CompiledProblem &compiled,
               SearchState &state,
               GlobalClockSolveStats &stats)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (std::size_t edgeIndex = 0;
             edgeIndex < compiled.edges.size(); ++edgeIndex)
        {
            const int mask = edgeMask(state, compiled, edgeIndex);
            if (mask == 0)
            {
                return false;
            }
            if (state.edgeValues[edgeIndex] < 0 &&
                (mask == 0b01 || mask == 0b10))
            {
                const int value = mask == 0b01 ? 0 : 1;
                if (!assignEdge(state, compiled, edgeIndex, value))
                {
                    return false;
                }
                ++stats.forcedEdges;
                changed = true;
            }
        }

        // Every route edge is a binary hold/advance decision, so an exact
        // endpoint epoch difference is also an exact cardinality constraint
        // on those decisions.  Propagating its lower/upper bounds avoids
        // enumerating paths whose total number of advances can no longer
        // reach the required value.  This is an exact inference; it does not
        // turn a search-budget exhaustion into an UNSAT result.
        for (const auto &route : compiled.routes)
        {
            const auto endpointDifference = state.integerEpoch.difference(
                route.occurrenceNodes.front(),
                route.occurrenceNodes.back());
            if (!endpointDifference.has_value())
            {
                continue;
            }

            const std::int64_t requiredAdvances =
                endpointDifference.value();
            std::int64_t assignedAdvances = 0;
            std::vector<std::size_t> unknownEdges;
            for (const std::size_t edgeIndex : route.edgeIndexes)
            {
                if (state.edgeValues[edgeIndex] == 1)
                {
                    ++assignedAdvances;
                }
                else if (state.edgeValues[edgeIndex] < 0)
                {
                    unknownEdges.push_back(edgeIndex);
                }
            }
            const std::int64_t maximumAdvances = assignedAdvances +
                static_cast<std::int64_t>(unknownEdges.size());
            if (requiredAdvances < assignedAdvances ||
                requiredAdvances > maximumAdvances)
            {
                return false;
            }

            int forcedValue = -1;
            if (requiredAdvances == assignedAdvances)
            {
                forcedValue = 0;
            }
            else if (requiredAdvances == maximumAdvances)
            {
                forcedValue = 1;
            }
            if (forcedValue >= 0)
            {
                for (const std::size_t edgeIndex : unknownEdges)
                {
                    if (!assignEdge(state, compiled, edgeIndex, forcedValue))
                    {
                        return false;
                    }
                    ++stats.forcedEdges;
                    changed = true;
                }
            }
        }

        const int maxOccurrences = problem.maxConsecutiveSamePhaseCells;
        if (maxOccurrences <= 0)
        {
            continue;
        }
        for (const auto &route : compiled.routes)
        {
            const auto &edges = route.edgeIndexes;
            if (edges.size() < static_cast<std::size_t>(maxOccurrences))
            {
                continue;
            }
            for (std::size_t start = 0;
                 start + static_cast<std::size_t>(maxOccurrences) <= edges.size();
                 ++start)
            {
                int zeros = 0;
                int ones = 0;
                std::size_t unknown = std::numeric_limits<std::size_t>::max();
                int unknownCount = 0;
                for (int offset = 0; offset < maxOccurrences; ++offset)
                {
                    const std::size_t edgeIndex =
                        edges[start + static_cast<std::size_t>(offset)];
                    const int value = state.edgeValues[edgeIndex];
                    if (value == 0)
                    {
                        ++zeros;
                    }
                    else if (value == 1)
                    {
                        ++ones;
                    }
                    else
                    {
                        unknown = edgeIndex;
                        ++unknownCount;
                    }
                }
                if (ones > 0)
                {
                    continue;
                }
                if (zeros == maxOccurrences)
                {
                    return false;
                }
                if (zeros == maxOccurrences - 1 && unknownCount == 1)
                {
                    if (!assignEdge(state, compiled, unknown, 1))
                    {
                        return false;
                    }
                    ++stats.forcedEdges;
                    changed = true;
                }
            }
        }
    }
    return true;
}

bool materialize(const GlobalClockProblem &problem,
                 const CompiledProblem &compiled,
                 int ii,
                 const SearchState &state,
                 GlobalClockSolution &solution,
                 std::string &error)
{
    solution.phaseCount = problem.phaseCount;
    solution.initiationInterval = ii;
    for (const auto &[event, node] : compiled.eventNodes)
    {
        const auto epoch = state.integerEpoch.difference(compiled.zeroNode,
                                                         node);
        if (!epoch.has_value())
        {
            error = "event " + event + " is not connected to an anchor";
            return false;
        }
        solution.eventEpoch[event] = epoch.value();
    }

    for (const auto &occurrence : problem.occurrences)
    {
        const std::size_t node = compiled.epochNodes.at(
            occurrence.epochVariable);
        const auto epoch = state.integerEpoch.difference(compiled.zeroNode,
                                                         node);
        if (!epoch.has_value())
        {
            error = "occurrence " + occurrence.id +
                    " is not connected to an anchor";
            return false;
        }
        solution.occurrenceEpoch[occurrence.id] = epoch.value();
        const int phase = floorMod(epoch.value(), problem.phaseCount);
        const auto [position, inserted] =
            solution.clockResourcePhase.emplace(occurrence.clockResource,
                                                phase);
        if (!inserted && position->second != phase)
        {
            error = "clock resource " + occurrence.clockResource +
                    " receives incompatible modulo phases";
            return false;
        }
    }
    return true;
}

bool routeCompletionFeasible(const GlobalClockProblem &problem,
                             const CompiledProblem &compiled,
                             const SearchState &state,
                             std::size_t routeIndex,
                             std::size_t trialEdge,
                             int trialValue)
{
    const auto &route = compiled.routes.at(routeIndex);
    const auto endpointDifference = state.integerEpoch.difference(
        route.occurrenceNodes.front(), route.occurrenceNodes.back());
    if (!endpointDifference.has_value())
    {
        // Without a fixed route total, the normal propagation/search remains
        // complete.  This helper is only an exact pruning rule when the two
        // endpoint epochs are already related.
        return true;
    }

    const std::int64_t required64 = endpointDifference.value();
    if (required64 < 0 ||
        required64 > static_cast<std::int64_t>(route.edgeIndexes.size()))
    {
        return false;
    }
    const std::size_t required = static_cast<std::size_t>(required64);
    const int maxOccurrences = problem.maxConsecutiveSamePhaseCells;
    const std::size_t effectiveMaxOccurrences =
        maxOccurrences > 0 &&
        static_cast<std::size_t>(maxOccurrences) < route.edgeIndexes.size() + 1U
        ? static_cast<std::size_t>(maxOccurrences)
        : 0U;

    // DP state: after the processed prefix, whether `advances` one-steps and
    // `zeroRun` consecutive hold edges are attainable. A run of
    // maxOccurrences zero edges would contain maxOccurrences+1 occurrences
    // in one phase and is illegal.
    const std::size_t runStates = effectiveMaxOccurrences > 0
        ? effectiveMaxOccurrences
        : 1U;
    std::vector<unsigned char> current((required + 1U) * runStates, 0U);
    std::vector<unsigned char> next(current.size(), 0U);
    current[0] = 1U;

    for (const std::size_t edgeIndex : route.edgeIndexes)
    {
        std::fill(next.begin(), next.end(), 0U);
        const int assigned = edgeIndex == trialEdge
            ? trialValue
            : state.edgeValues[edgeIndex];
        for (std::size_t advances = 0; advances <= required; ++advances)
        {
            for (std::size_t zeroRun = 0; zeroRun < runStates; ++zeroRun)
            {
                if (current[advances * runStates + zeroRun] == 0U)
                {
                    continue;
                }
                if (assigned <= 0 &&
                    (effectiveMaxOccurrences == 0U || zeroRun + 1U < runStates))
                {
                    const std::size_t nextRun = effectiveMaxOccurrences > 0
                        ? zeroRun + 1U
                        : 0U;
                    next[advances * runStates + nextRun] = 1U;
                }
                if (assigned != 0 && advances < required)
                {
                    next[(advances + 1U) * runStates] = 1U;
                }
            }
        }
        current.swap(next);
    }

    for (std::size_t zeroRun = 0; zeroRun < runStates; ++zeroRun)
    {
        if (current[required * runStates + zeroRun] != 0U)
        {
            return true;
        }
    }
    return false;
}

SearchStatus search(const GlobalClockProblem &problem,
                    const CompiledProblem &compiled,
                    int ii,
                    SearchState state,
                    GlobalClockSolveStats &stats,
                    GlobalClockSolution &solution)
{
    if (stats.dfsNodes >= problem.maxDfsNodes)
    {
        return SearchStatus::Limit;
    }
    ++stats.dfsNodes;
    if (!propagate(problem, compiled, state, stats))
    {
        ++stats.conflicts;
        return SearchStatus::Unsat;
    }

    std::size_t selected = std::numeric_limits<std::size_t>::max();
    for (std::size_t edgeIndex = 0;
         edgeIndex < state.edgeValues.size(); ++edgeIndex)
    {
        if (state.edgeValues[edgeIndex] < 0)
        {
            selected = edgeIndex;
            break;
        }
    }
    if (selected == std::numeric_limits<std::size_t>::max())
    {
        std::string error;
        if (!materialize(problem, compiled, ii, state, solution, error) ||
            !GlobalPhaseSolver::validateSolution(problem, solution, &error))
        {
            ++stats.conflicts;
            return SearchStatus::Unsat;
        }
        return SearchStatus::Sat;
    }

    const int mask = edgeMask(state, compiled, selected);
    // Prefer an advance near the source. For a fixed route total this groups
    // the remaining hold occurrences around the sink clock zone. The
    // exact cardinality propagation above still prunes impossible route sums,
    // so this is a deterministic witness-quality choice rather than a change
    // to the satisfiability semantics.
    for (const int value : {1, 0})
    {
        if ((mask & (1 << value)) == 0)
        {
            continue;
        }
        const std::size_t routeIndex = compiled.edges[selected].route;
        if (!routeCompletionFeasible(problem, compiled, state, routeIndex,
                                     selected, value))
        {
            ++stats.conflicts;
            continue;
        }
        ++stats.decisions;
        SearchState child = state;
        if (!assignEdge(child, compiled, selected, value))
        {
            ++stats.conflicts;
            continue;
        }
        const SearchStatus result = search(problem, compiled, ii,
                                           std::move(child), stats, solution);
        if (result == SearchStatus::Sat || result == SearchStatus::Limit)
        {
            return result;
        }
    }
    return SearchStatus::Unsat;
}

} // namespace

GlobalPhaseSolver::GlobalPhaseSolver(GlobalClockProblem problem)
    : problem_(std::move(problem))
{
}

GlobalClockSolveResult GlobalPhaseSolver::solve() const
{
    GlobalClockSolveResult result;
    std::string error;
    if (!validateProblem(problem_, error))
    {
        result.status = GlobalClockSolveStatus::InvalidInput;
        result.message = error;
        return result;
    }

    const CompiledProblem compiled = compileProblem(problem_);
    std::vector<int> iiCandidates = problem_.iiCandidates;
    std::sort(iiCandidates.begin(), iiCandidates.end());
    iiCandidates.erase(std::unique(iiCandidates.begin(), iiCandidates.end()),
                       iiCandidates.end());

    for (const int ii : iiCandidates)
    {
        ++result.stats.iiCandidatesTried;
        const EventEpochSeed eventSeed = makeEventEpochSeed(problem_, ii);
        if (!eventSeed.feasible)
        {
            // The event-level route interval relaxation is a necessary
            // condition, so its negative-cycle proof is a sound UNSAT proof
            // for this II candidate.
            ++result.stats.conflicts;
            continue;
        }
        SearchState initial(compiled.nodeCount, problem_.phaseCount,
                            compiled.edges.size());
        if (!initializeForIi(problem_, compiled, ii, initial))
        {
            ++result.stats.conflicts;
            continue;
        }

        GlobalClockSolution solution;
        SearchState seeded = initial;
        bool seedCompatible = true;
        for (const auto &[event, epoch] : eventSeed.epoch)
        {
            if (!addEquation(seeded, compiled.zeroNode,
                             compiled.eventNodes.at(event), epoch))
            {
                seedCompatible = false;
                break;
            }
        }
        if (seedCompatible)
        {
            const SearchStatus seededResult = search(
                problem_, compiled, ii, std::move(seeded), result.stats,
                solution);
            if (seededResult == SearchStatus::Sat)
            {
                result.status = GlobalClockSolveStatus::Sat;
                result.solution = std::move(solution);
                result.message =
                    "global phase/epoch assignment found via event-level seed";
                return result;
            }
            if (seededResult == SearchStatus::Limit)
            {
                result.status = GlobalClockSolveStatus::Limit;
                result.message = "DFS node budget exhausted before proof";
                return result;
            }
        }

        // The relaxation seed chooses only one feasible set of route totals.
        // Shared clock resources can invalidate that choice even when another
        // set works, so retain the complete unseeded search as a fallback.
        solution = GlobalClockSolution{};
        const SearchStatus searchResult = search(problem_, compiled, ii,
                                                 std::move(initial),
                                                 result.stats, solution);
        if (searchResult == SearchStatus::Sat)
        {
            result.status = GlobalClockSolveStatus::Sat;
            result.solution = std::move(solution);
            result.message = "global phase/epoch assignment found";
            return result;
        }
        if (searchResult == SearchStatus::Limit)
        {
            result.status = GlobalClockSolveStatus::Limit;
            result.message = "DFS node budget exhausted before proof";
            return result;
        }
    }

    result.status = GlobalClockSolveStatus::Unsat;
    result.message = "all II candidates were exhaustively proven infeasible";
    return result;
}

bool GlobalPhaseSolver::validateSolution(const GlobalClockProblem &problem,
                                         const GlobalClockSolution &solution,
                                         std::string *error)
{
    std::string problemError;
    if (!validateProblem(problem, problemError))
    {
        setError(error, "invalid clock problem: " + problemError);
        return false;
    }
    if (solution.phaseCount != problem.phaseCount ||
        solution.initiationInterval <= 0 ||
        solution.initiationInterval % problem.phaseCount != 0)
    {
        setError(error, "solution phase count or II is invalid");
        return false;
    }
    if (std::find(problem.iiCandidates.begin(), problem.iiCandidates.end(),
                  solution.initiationInterval) == problem.iiCandidates.end())
    {
        setError(error, "solution II was not an allowed candidate");
        return false;
    }

    // Treat externally supplied solutions as a closed-schema object.  Merely
    // checking that all required keys exist would allow an untrusted backend
    // to smuggle additional clock resources into downstream phase mapping.
    const std::set<std::string> expectedEvents(problem.events.begin(),
                                                problem.events.end());
    if (solution.eventEpoch.size() != expectedEvents.size())
    {
        setError(error, "solution event set does not match the problem");
        return false;
    }
    for (const auto &[event, unused] : solution.eventEpoch)
    {
        (void)unused;
        if (expectedEvents.count(event) == 0)
        {
            setError(error, "solution contains unknown event " + event);
            return false;
        }
    }

    std::set<std::string> expectedOccurrences;
    std::set<std::string> expectedResources;
    for (const auto &occurrence : problem.occurrences)
    {
        expectedOccurrences.insert(occurrence.id);
        expectedResources.insert(occurrence.clockResource);
    }
    if (solution.occurrenceEpoch.size() != expectedOccurrences.size())
    {
        setError(error, "solution occurrence set does not match the problem");
        return false;
    }
    for (const auto &[occurrence, unused] : solution.occurrenceEpoch)
    {
        (void)unused;
        if (expectedOccurrences.count(occurrence) == 0)
        {
            setError(error, "solution contains unknown occurrence " +
                            occurrence);
            return false;
        }
    }
    if (solution.clockResourcePhase.size() != expectedResources.size())
    {
        setError(error, "solution clock-resource set does not match the problem");
        return false;
    }
    for (const auto &[resource, unused] : solution.clockResourcePhase)
    {
        (void)unused;
        if (expectedResources.count(resource) == 0)
        {
            setError(error, "solution contains unknown clock resource " +
                            resource);
            return false;
        }
    }

    for (const auto &event : problem.events)
    {
        if (solution.eventEpoch.count(event) == 0)
        {
            setError(error, "solution is missing event " + event);
            return false;
        }
    }
    for (const auto &anchor : problem.anchors)
    {
        if (solution.eventEpoch.at(anchor.event) != anchor.epoch)
        {
            setError(error, "anchor mismatch at event " + anchor.event);
            return false;
        }
    }
    for (const auto &arc : problem.timingArcs)
    {
        const std::int64_t epochOffset =
            static_cast<std::int64_t>(arc.latencyEpochs) -
            static_cast<std::int64_t>(arc.iterationDistance) *
                solution.initiationInterval;
        std::int64_t expectedSink = 0;
        if (!checkedAdd(solution.eventEpoch.at(arc.sourceEvent),
                        epochOffset, expectedSink) ||
            solution.eventEpoch.at(arc.sinkEvent) != expectedSink)
        {
            setError(error, "timing arc mismatch at " + arc.id);
            return false;
        }
    }

    std::map<std::string, const RouteOccurrenceSpec *> occurrenceById;
    std::map<std::string, std::int64_t> epochVariableValue;
    for (const auto &occurrence : problem.occurrences)
    {
        occurrenceById[occurrence.id] = &occurrence;
        const auto found = solution.occurrenceEpoch.find(occurrence.id);
        if (found == solution.occurrenceEpoch.end())
        {
            setError(error, "solution is missing occurrence " + occurrence.id);
            return false;
        }
        const auto [position, inserted] = epochVariableValue.emplace(
            occurrence.epochVariable, found->second);
        if (!inserted && position->second != found->second)
        {
            setError(error, "aliased epoch variable mismatch at " +
                            occurrence.id);
            return false;
        }
        const auto phase = solution.clockResourcePhase.find(
            occurrence.clockResource);
        if (phase == solution.clockResourcePhase.end() ||
            phase->second < 0 || phase->second >= problem.phaseCount ||
            phase->second != floorMod(found->second, problem.phaseCount))
        {
            setError(error, "resource phase mismatch at occurrence " +
                            occurrence.id);
            return false;
        }
    }

    for (const auto &route : problem.routes)
    {
        const auto first = solution.occurrenceEpoch.at(
            route.occurrences.front());
        const auto last = solution.occurrenceEpoch.at(
            route.occurrences.back());
        if (first != solution.eventEpoch.at(route.sourceEvent))
        {
            setError(error, "route source epoch mismatch at " + route.id);
            return false;
        }
        const std::int64_t sinkOffset =
            static_cast<std::int64_t>(route.iterationDistance) *
            solution.initiationInterval;
        std::int64_t expectedLast = 0;
        if (!checkedAdd(solution.eventEpoch.at(route.sinkEvent),
                        sinkOffset, expectedLast) ||
            last != expectedLast)
        {
            setError(error, "route sink epoch mismatch at " + route.id);
            return false;
        }

        int samePhaseRun = 1;
        for (std::size_t index = 1; index < route.occurrences.size(); ++index)
        {
            const std::int64_t previous = solution.occurrenceEpoch.at(
                route.occurrences[index - 1]);
            const std::int64_t current = solution.occurrenceEpoch.at(
                route.occurrences[index]);
            std::int64_t nextEpoch = 0;
            const bool holds = current == previous;
            const bool advances = checkedAdd(previous, 1, nextEpoch) &&
                                  current == nextEpoch;
            if (!holds && !advances)
            {
                setError(error, "route step is not hold/+1 at " + route.id);
                return false;
            }
            samePhaseRun = holds ? samePhaseRun + 1 : 1;
            if (problem.maxConsecutiveSamePhaseCells > 0 &&
                samePhaseRun > problem.maxConsecutiveSamePhaseCells)
            {
                setError(error, "same-phase run exceeded at " + route.id);
                return false;
            }
        }
    }
    return true;
}

} // namespace fcngraph::sequential
