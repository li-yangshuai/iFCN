#include "autopr/sequential/physicalStateMacro.h"

#include "autopr/algorithms/mapping.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace fcngraph::sequential
{
namespace
{

std::string resourceId(const position &coordinate)
{
    return "state.cell." + std::to_string(coordinate.first) + "." +
           std::to_string(coordinate.second);
}

const PhysicalStateNode *findNode(const PhysicalStateMacro &macro, int index)
{
    const auto found = std::find_if(
        macro.nodes.begin(), macro.nodes.end(),
        [index](const PhysicalStateNode &node) { return node.index == index; });
    return found == macro.nodes.end() ? nullptr : &*found;
}

const PhysicalStateNet *findNet(const PhysicalStateMacro &macro,
                                int source,
                                int sink)
{
    const auto found = std::find_if(
        macro.nets.begin(), macro.nets.end(),
        [source, sink](const PhysicalStateNet &net) {
            return net.sourceNode == source && net.sinkNode == sink;
        });
    return found == macro.nets.end() ? nullptr : &*found;
}

bool isFourConnected(const std::vector<position> &path)
{
    if (path.size() < 2)
    {
        return false;
    }
    for (std::size_t index = 1; index < path.size(); ++index)
    {
        const int distance =
            std::abs(static_cast<int>(path[index].first) -
                     static_cast<int>(path[index - 1].first)) +
            std::abs(static_cast<int>(path[index].second) -
                     static_cast<int>(path[index - 1].second));
        if (distance != 1)
        {
            return false;
        }
    }
    return true;
}

std::string occurrenceId(const PhysicalStateNet &net, std::size_t index)
{
    return net.id + ".occ." + std::to_string(index);
}

void setError(std::string *error, const std::string &message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

std::string escapeLatexText(const std::string &text)
{
    std::ostringstream escaped;
    for (const char character : text)
    {
        switch (character)
        {
        case '\\':
            escaped << "\\textbackslash{}";
            break;
        case '_':
            escaped << "\\_";
            break;
        case '%':
            escaped << "\\%";
            break;
        case '&':
            escaped << "\\&";
            break;
        case '#':
            escaped << "\\#";
            break;
        case '$':
            escaped << "\\$";
            break;
        case '{':
            escaped << "\\{";
            break;
        case '}':
            escaped << "\\}";
            break;
        case '^':
            escaped << "\\textasciicircum{}";
            break;
        case '~':
            escaped << "\\textasciitilde{}";
            break;
        default:
            escaped << character;
            break;
        }
    }
    return escaped.str();
}

GlobalClockProblem makeClockProblem(const PhysicalStateMacro &macro)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    // The v0 gate-level contract treats every state stage as one clock zone.
    // A hold between consecutive stages would collapse that four-stage timing
    // witness; cell-level characterization remains a separate sign-off step.
    problem.maxConsecutiveSamePhaseCells = 1;
    problem.iiCandidates = {macro.initiationInterval};
    problem.maxDfsNodes = 100000;

    std::set<position> coordinates;
    for (const auto &node : macro.nodes)
    {
        problem.events.push_back(node.id);
        coordinates.insert(node.coordinate);
    }
    for (const auto &net : macro.nets)
    {
        coordinates.insert(net.path.begin(), net.path.end());
    }
    for (const position &coordinate : coordinates)
    {
        problem.clockResources.push_back(ClockResourceSpec{
            resourceId(coordinate),
            ClockResourceSharing::PhaseSharedIndependentEpochs});
    }

    for (const auto &net : macro.nets)
    {
        const auto *source = findNode(macro, net.sourceNode);
        const auto *sink = findNode(macro, net.sinkNode);
        std::vector<std::string> occurrences;
        occurrences.reserve(net.path.size());
        for (std::size_t index = 0; index < net.path.size(); ++index)
        {
            const std::string id = occurrenceId(net, index);
            problem.occurrences.push_back(RouteOccurrenceSpec{
                id,
                resourceId(net.path[index]),
                id + ".epoch"});
            occurrences.push_back(id);
        }
        problem.routes.push_back(FixedRouteSpec{
            net.id,
            source->id,
            sink->id,
            net.iterationDistance,
            std::move(occurrences)});
    }

    for (const int stateNode : macro.stateNodes)
    {
        problem.anchors.push_back(EpochAnchorSpec{
            findNode(macro, stateNode)->id, 0});
    }
    return problem;
}

std::map<position, int> extractCoordinatePhases(
    const PhysicalStateMacro &macro,
    const GlobalClockSolution &solution)
{
    std::map<position, int> phases;
    std::set<position> coordinates;
    for (const auto &node : macro.nodes)
    {
        coordinates.insert(node.coordinate);
    }
    for (const auto &net : macro.nets)
    {
        coordinates.insert(net.path.begin(), net.path.end());
    }
    for (const position &coordinate : coordinates)
    {
        phases[coordinate] =
            solution.clockResourcePhase.at(resourceId(coordinate));
    }
    return phases;
}

} // namespace

PhysicalStateMacro makePhysicalT1ToggleMacro(position northWest)
{
    PhysicalStateMacro macro;
    macro.id = "physical_t1_toggle";
    macro.phaseCount = 4;
    macro.initiationInterval = 4;

    const position state{northWest.first + 1, northWest.second + 1};
    const position inverter{northWest.first + 2, northWest.second + 1};
    const position hold2{northWest.first + 2, northWest.second + 2};
    const position hold3{northWest.first + 1, northWest.second + 2};
    const position observation{northWest.first, northWest.second + 1};

    macro.nodes = {
        PhysicalStateNode{0, "state.q", "fanout", state},
        PhysicalStateNode{1, "inv0.y", "not", inverter},
        PhysicalStateNode{2, "state.hold2", "wire", hold2},
        PhysicalStateNode{3, "state.hold3", "wire", hold3},
        PhysicalStateNode{4, "q", "output", observation}
    };
    macro.stateRing = {0, 1, 2, 3};
    macro.stateNodes = {0};
    macro.stateTransitions = {
        PhysicalStateTransition{"toggle", {0, 1, 2, 3, 0}}
    };
    macro.nets = {
        PhysicalStateNet{"state_to_not", 0, 1, 0, {state, inverter}},
        PhysicalStateNet{"not_to_hold2", 1, 2, 0, {inverter, hold2}},
        PhysicalStateNet{"hold2_to_hold3", 2, 3, 0, {hold2, hold3}},
        // This edge is physical and is the only scheduling cut.  Its sink is
        // the same state/fanout tile in the next iteration.
        PhysicalStateNet{"state_feedback", 3, 0, 1, {hold3, state}},
        PhysicalStateNet{"state_observe", 0, 4, 0, {state, observation}}
    };

    macro.clockProblem = makeClockProblem(macro);
    const GlobalClockSolveResult solve =
        GlobalPhaseSolver(macro.clockProblem).solve();
    if (solve.status == GlobalClockSolveStatus::Sat &&
        solve.solution.has_value())
    {
        macro.clockSolution = solve.solution;
        macro.initiationInterval = solve.solution->initiationInterval;
        macro.phaseByCoordinate = extractCoordinatePhases(
            macro, solve.solution.value());
    }
    return macro;
}

PhysicalStateMacro makePhysicalResetToggleMacro(position northWest)
{
    PhysicalStateMacro macro;
    macro.id = "physical_reset_toggle";
    macro.phaseCount = 4;
    macro.initiationInterval = 4;

    const position state{northWest.first + 1, northWest.second + 1};
    const position reset{northWest.first + 2, northWest.second};
    const position orGate{northWest.first + 2, northWest.second + 1};
    const position inverter{northWest.first + 2, northWest.second + 2};
    const position hold{northWest.first + 1, northWest.second + 2};
    const position observation{northWest.first, northWest.second + 1};

    macro.nodes = {
        PhysicalStateNode{0, "state.q", "fanout", state},
        PhysicalStateNode{1, "rst", "input", reset},
        PhysicalStateNode{2, "next.or", "or", orGate},
        PhysicalStateNode{3, "next.not", "not", inverter},
        PhysicalStateNode{4, "state.hold", "wire", hold},
        PhysicalStateNode{5, "q", "output", observation}
    };
    macro.stateRing = {0, 2, 3, 4};
    macro.stateNodes = {0};
    macro.stateTransitions = {
        PhysicalStateTransition{"reset_toggle", {0, 2, 3, 4, 0}}
    };
    macro.nets = {
        PhysicalStateNet{"state_to_or", 0, 2, 0, {state, orGate}},
        PhysicalStateNet{"reset_to_or", 1, 2, 0, {reset, orGate}},
        PhysicalStateNet{"or_to_not", 2, 3, 0, {orGate, inverter}},
        PhysicalStateNet{"not_to_hold", 3, 4, 0, {inverter, hold}},
        PhysicalStateNet{"state_feedback", 4, 0, 1, {hold, state}},
        PhysicalStateNet{"state_observe", 0, 5, 0, {state, observation}}
    };

    macro.clockProblem = makeClockProblem(macro);
    const GlobalClockSolveResult solve =
        GlobalPhaseSolver(macro.clockProblem).solve();
    if (solve.status == GlobalClockSolveStatus::Sat &&
        solve.solution.has_value())
    {
        macro.clockSolution = solve.solution;
        macro.initiationInterval = solve.solution->initiationInterval;
        macro.phaseByCoordinate = extractCoordinatePhases(
            macro, solve.solution.value());
    }
    return macro;
}

PhysicalStateMacro makePhysicalResetJohnson2Macro(position northWest)
{
    PhysicalStateMacro macro;
    macro.id = "physical_reset_johnson2";
    macro.phaseCount = 4;
    macro.initiationInterval = 8;

    const unsigned int x = northWest.first;
    const unsigned int y = northWest.second;
    const position q0{x + 3, y + 2};
    const position q1{x + 1, y + 4};
    const position reset{x + 2, y + 3};

    macro.nodes = {
        PhysicalStateNode{0, "state.q0", "fanout", q0},
        PhysicalStateNode{1, "state.q1", "fanout", q1},
        PhysicalStateNode{2, "rst", "input", reset},
        PhysicalStateNode{3, "q0.delay", "wire", {x + 4, y + 2}},
        PhysicalStateNode{4, "rst.not", "not", {x + 3, y + 3}},
        PhysicalStateNode{5, "q1.rst.or", "or", {x + 1, y + 3}},
        PhysicalStateNode{6, "next.q1", "and", {x + 4, y + 3}},
        PhysicalStateNode{7, "next.q0", "not", {x + 1, y + 2}},
        PhysicalStateNode{8, "q1.h3", "wire", {x + 4, y + 4}},
        PhysicalStateNode{9, "q1.h4", "wire", {x + 3, y + 4}},
        PhysicalStateNode{10, "q1.h5", "wire", {x + 3, y + 5}},
        PhysicalStateNode{11, "q1.h6", "wire", {x + 2, y + 5}},
        PhysicalStateNode{12, "q1.h7", "wire", {x + 2, y + 4}},
        PhysicalStateNode{13, "q0.h3", "wire", {x + 1, y + 1}},
        PhysicalStateNode{14, "q0.h4", "wire", {x + 2, y + 1}},
        PhysicalStateNode{15, "q0.h5", "wire", {x + 2, y}},
        PhysicalStateNode{16, "q0.h6", "wire", {x + 3, y}},
        PhysicalStateNode{17, "q0.h7", "wire", {x + 3, y + 1}},
        PhysicalStateNode{18, "q0", "output", {x + 2, y + 2}},
        PhysicalStateNode{19, "q1", "output", {x, y + 4}}
    };

    macro.stateNodes = {0, 1};
    macro.stateTransitions = {
        PhysicalStateTransition{
            "q0_to_q1", {0, 3, 6, 8, 9, 10, 11, 12, 1}},
        PhysicalStateTransition{
            "q1_to_q0", {1, 5, 7, 13, 14, 15, 16, 17, 0}}
    };

    const auto direct = [](position source, position sink) {
        return std::vector<position>{source, sink};
    };
    macro.nets = {
        PhysicalStateNet{"q0_delay", 0, 3, 0, direct(q0, {x + 4, y + 2})},
        PhysicalStateNet{"observe_q0", 0, 18, 0, direct(q0, {x + 2, y + 2})},
        PhysicalStateNet{"q1_to_or", 1, 5, 0, direct(q1, {x + 1, y + 3})},
        PhysicalStateNet{"observe_q1", 1, 19, 0, direct(q1, {x, y + 4})},
        PhysicalStateNet{"reset_to_or", 2, 5, 0, direct(reset, {x + 1, y + 3})},
        PhysicalStateNet{"reset_to_not", 2, 4, 0, direct(reset, {x + 3, y + 3})},
        PhysicalStateNet{"q0_to_and", 3, 6, 0, direct({x + 4, y + 2}, {x + 4, y + 3})},
        PhysicalStateNet{"not_reset_to_and", 4, 6, 0, direct({x + 3, y + 3}, {x + 4, y + 3})},
        PhysicalStateNet{"or_to_not", 5, 7, 0, direct({x + 1, y + 3}, {x + 1, y + 2})},

        PhysicalStateNet{"q1_h3", 6, 8, 0, direct({x + 4, y + 3}, {x + 4, y + 4})},
        PhysicalStateNet{"q1_h4", 8, 9, 0, direct({x + 4, y + 4}, {x + 3, y + 4})},
        PhysicalStateNet{"q1_h5", 9, 10, 0, direct({x + 3, y + 4}, {x + 3, y + 5})},
        PhysicalStateNet{"q1_h6", 10, 11, 0, direct({x + 3, y + 5}, {x + 2, y + 5})},
        PhysicalStateNet{"q1_h7", 11, 12, 0, direct({x + 2, y + 5}, {x + 2, y + 4})},
        PhysicalStateNet{"capture_q1", 12, 1, 1, direct({x + 2, y + 4}, q1)},

        PhysicalStateNet{"q0_h3", 7, 13, 0, direct({x + 1, y + 2}, {x + 1, y + 1})},
        PhysicalStateNet{"q0_h4", 13, 14, 0, direct({x + 1, y + 1}, {x + 2, y + 1})},
        PhysicalStateNet{"q0_h5", 14, 15, 0, direct({x + 2, y + 1}, {x + 2, y})},
        PhysicalStateNet{"q0_h6", 15, 16, 0, direct({x + 2, y}, {x + 3, y})},
        PhysicalStateNet{"q0_h7", 16, 17, 0, direct({x + 3, y}, {x + 3, y + 1})},
        PhysicalStateNet{"capture_q0", 17, 0, 1, direct({x + 3, y + 1}, q0)}
    };

    macro.clockProblem = makeClockProblem(macro);
    const GlobalClockSolveResult solve =
        GlobalPhaseSolver(macro.clockProblem).solve();
    if (solve.status == GlobalClockSolveStatus::Sat &&
        solve.solution.has_value())
    {
        macro.clockSolution = solve.solution;
        macro.initiationInterval = solve.solution->initiationInterval;
        macro.phaseByCoordinate = extractCoordinatePhases(
            macro, solve.solution.value());
    }
    return macro;
}

PhysicalStateMacro makePhysicalJohnson4Macro(position northWest)
{
    PhysicalStateMacro macro;
    macro.id = "physical_johnson4";
    macro.phaseCount = 4;
    macro.initiationInterval = 4;

    const unsigned int x = northWest.first;
    const unsigned int y = northWest.second;
    const position q0{x + 1, y + 1};
    const position q1{x + 5, y + 1};
    const position q2{x + 5, y + 5};
    const position q3{x + 1, y + 5};

    macro.nodes = {
        PhysicalStateNode{0, "state.q0", "fanout", q0},
        PhysicalStateNode{1, "state.q1", "fanout", q1},
        PhysicalStateNode{2, "state.q2", "fanout", q2},
        PhysicalStateNode{3, "state.q3", "fanout", q3},

        PhysicalStateNode{4, "q0.shift1", "wire", {x + 2, y + 1}},
        PhysicalStateNode{5, "q0.shift2", "wire", {x + 3, y + 1}},
        PhysicalStateNode{6, "q0.shift3", "wire", {x + 4, y + 1}},

        PhysicalStateNode{7, "q1.shift1", "wire", {x + 5, y + 2}},
        PhysicalStateNode{8, "q1.shift2", "wire", {x + 5, y + 3}},
        PhysicalStateNode{9, "q1.shift3", "wire", {x + 5, y + 4}},

        PhysicalStateNode{10, "q2.shift1", "wire", {x + 4, y + 5}},
        PhysicalStateNode{11, "q2.shift2", "wire", {x + 3, y + 5}},
        PhysicalStateNode{12, "q2.shift3", "wire", {x + 2, y + 5}},

        PhysicalStateNode{13, "feedback.not", "not", {x + 1, y + 4}},
        PhysicalStateNode{14, "feedback.hold2", "wire", {x + 1, y + 3}},
        PhysicalStateNode{15, "feedback.hold3", "wire", {x + 1, y + 2}},

        PhysicalStateNode{16, "q0", "output", {x, y + 1}},
        PhysicalStateNode{17, "q1", "output", {x + 5, y}},
        PhysicalStateNode{18, "q2", "output", {x + 6, y + 5}},
        PhysicalStateNode{19, "q3", "output", {x + 1, y + 6}}
    };

    macro.stateNodes = {0, 1, 2, 3};
    macro.stateTransitions = {
        PhysicalStateTransition{"q0_to_q1", {0, 4, 5, 6, 1}},
        PhysicalStateTransition{"q1_to_q2", {1, 7, 8, 9, 2}},
        PhysicalStateTransition{"q2_to_q3", {2, 10, 11, 12, 3}},
        PhysicalStateTransition{"q3_to_q0_inverted", {3, 13, 14, 15, 0}}
    };

    const auto direct = [](position source, position sink) {
        return std::vector<position>{source, sink};
    };
    macro.nets = {
        PhysicalStateNet{"q0_s1", 0, 4, 0, direct(q0, {x + 2, y + 1})},
        PhysicalStateNet{"q0_s2", 4, 5, 0, direct({x + 2, y + 1}, {x + 3, y + 1})},
        PhysicalStateNet{"q0_s3", 5, 6, 0, direct({x + 3, y + 1}, {x + 4, y + 1})},
        PhysicalStateNet{"q0_capture_q1", 6, 1, 1, direct({x + 4, y + 1}, q1)},

        PhysicalStateNet{"q1_s1", 1, 7, 0, direct(q1, {x + 5, y + 2})},
        PhysicalStateNet{"q1_s2", 7, 8, 0, direct({x + 5, y + 2}, {x + 5, y + 3})},
        PhysicalStateNet{"q1_s3", 8, 9, 0, direct({x + 5, y + 3}, {x + 5, y + 4})},
        PhysicalStateNet{"q1_capture_q2", 9, 2, 1, direct({x + 5, y + 4}, q2)},

        PhysicalStateNet{"q2_s1", 2, 10, 0, direct(q2, {x + 4, y + 5})},
        PhysicalStateNet{"q2_s2", 10, 11, 0, direct({x + 4, y + 5}, {x + 3, y + 5})},
        PhysicalStateNet{"q2_s3", 11, 12, 0, direct({x + 3, y + 5}, {x + 2, y + 5})},
        PhysicalStateNet{"q2_capture_q3", 12, 3, 1, direct({x + 2, y + 5}, q3)},

        PhysicalStateNet{"q3_not", 3, 13, 0, direct(q3, {x + 1, y + 4})},
        PhysicalStateNet{"q3_hold2", 13, 14, 0, direct({x + 1, y + 4}, {x + 1, y + 3})},
        PhysicalStateNet{"q3_hold3", 14, 15, 0, direct({x + 1, y + 3}, {x + 1, y + 2})},
        PhysicalStateNet{"q3_capture_q0", 15, 0, 1, direct({x + 1, y + 2}, q0)},

        PhysicalStateNet{"observe_q0", 0, 16, 0, direct(q0, {x, y + 1})},
        PhysicalStateNet{"observe_q1", 1, 17, 0, direct(q1, {x + 5, y})},
        PhysicalStateNet{"observe_q2", 2, 18, 0, direct(q2, {x + 6, y + 5})},
        PhysicalStateNet{"observe_q3", 3, 19, 0, direct(q3, {x + 1, y + 6})}
    };

    macro.clockProblem = makeClockProblem(macro);
    const GlobalClockSolveResult solve =
        GlobalPhaseSolver(macro.clockProblem).solve();
    if (solve.status == GlobalClockSolveStatus::Sat &&
        solve.solution.has_value())
    {
        macro.clockSolution = solve.solution;
        macro.initiationInterval = solve.solution->initiationInterval;
        macro.phaseByCoordinate = extractCoordinatePhases(
            macro, solve.solution.value());
    }
    return macro;
}

PhysicalStateValidationResult validatePhysicalStateMacro(
    const PhysicalStateMacro &macro)
{
    const auto fail = [](const std::string &message) {
        return PhysicalStateValidationResult{false, message};
    };

    if (macro.phaseCount != 4 || macro.initiationInterval <= 0 ||
        macro.initiationInterval % macro.phaseCount != 0)
    {
        return fail("the physical state layout requires four phases and II=4k");
    }
    if (macro.nodes.size() < 4 || macro.stateNodes.empty() ||
        macro.stateTransitions.empty())
    {
        return fail("the physical state layout has no complete state transition");
    }

    std::set<int> indexes;
    std::set<std::string> ids;
    std::set<position> coordinates;
    for (const auto &node : macro.nodes)
    {
        if (node.index < 0 || node.id.empty() || node.mappingType.empty() ||
            !indexes.insert(node.index).second || !ids.insert(node.id).second ||
            !coordinates.insert(node.coordinate).second)
        {
            return fail("state macro nodes are not uniquely materialized");
        }
    }

    const std::set<int> stateNodeSet(
        macro.stateNodes.begin(), macro.stateNodes.end());
    if (stateNodeSet.size() != macro.stateNodes.size())
    {
        return fail("the state-node list contains duplicates");
    }
    for (const int stateNode : macro.stateNodes)
    {
        const auto *node = findNode(macro, stateNode);
        if (node == nullptr || node->mappingType == "input" ||
            node->mappingType == "output")
        {
            return fail("state must reside in internal clocked nodes, not I/O");
        }
    }

    int feedbackCount = 0;
    std::set<std::string> netIds;
    std::set<std::pair<int, int>> directedNets;
    for (const auto &net : macro.nets)
    {
        const auto *source = findNode(macro, net.sourceNode);
        const auto *sink = findNode(macro, net.sinkNode);
        if (net.id.empty() || !netIds.insert(net.id).second ||
            !directedNets.emplace(net.sourceNode, net.sinkNode).second)
        {
            return fail("state macro nets are not uniquely materialized");
        }
        if (source == nullptr || sink == nullptr || net.path.size() < 2 ||
            net.path.front() != source->coordinate ||
            net.path.back() != sink->coordinate ||
            !isFourConnected(net.path) ||
            std::set<position>(net.path.begin(), net.path.end()).size() !=
                net.path.size())
        {
            return fail("state macro contains a non-physical routed net");
        }
        if (net.iterationDistance == 1)
        {
            ++feedbackCount;
        }
        else if (net.iterationDistance != 0)
        {
            return fail("state net iteration distance is outside {0,1}");
        }
    }

    std::set<std::string> transitionIds;
    std::set<std::pair<int, int>> captureEdges;
    std::map<int, int> launchCount;
    std::map<int, int> captureCount;
    for (const auto &transition : macro.stateTransitions)
    {
        if (transition.id.empty() ||
            !transitionIds.insert(transition.id).second ||
            transition.stages.size() !=
                static_cast<std::size_t>(macro.initiationInterval + 1))
        {
            return fail("state transitions do not span exactly one II");
        }
        const int launch = transition.stages.front();
        const int capture = transition.stages.back();
        if (stateNodeSet.count(launch) == 0 ||
            stateNodeSet.count(capture) == 0)
        {
            return fail("a state transition does not launch and capture at state nodes");
        }
        ++launchCount[launch];
        ++captureCount[capture];

        for (std::size_t index = 0; index < transition.stages.size(); ++index)
        {
            const auto *node = findNode(macro, transition.stages[index]);
            if (node == nullptr ||
                (index > 0 && index + 1 < transition.stages.size() &&
                 (node->mappingType == "input" ||
                  node->mappingType == "output")))
            {
                return fail("a state transition contains an invalid clocked stage");
            }
        }

        for (std::size_t index = 0; index + 1 < transition.stages.size();
             ++index)
        {
            const int source = transition.stages[index];
            const int sink = transition.stages[index + 1];
            const PhysicalStateNet *net = findNet(macro, source, sink);
            const int expectedDistance =
                index + 2 == transition.stages.size() ? 1 : 0;
            if (net == nullptr ||
                net->iterationDistance != expectedDistance)
            {
                return fail("a declared state transition is not physically complete");
            }
            if (expectedDistance == 1)
            {
                captureEdges.emplace(source, sink);
            }
        }
    }

    if (feedbackCount != static_cast<int>(macro.stateTransitions.size()) ||
        captureEdges.size() != macro.stateTransitions.size())
    {
        return fail("every state transition must own exactly one capture edge");
    }
    for (const auto &net : macro.nets)
    {
        if (net.iterationDistance == 1 &&
            captureEdges.count({net.sourceNode, net.sinkNode}) == 0)
        {
            return fail("an iteration-distance edge is not a declared state capture");
        }
    }
    for (const int stateNode : macro.stateNodes)
    {
        if (launchCount[stateNode] != 1 || captureCount[stateNode] != 1)
        {
            return fail("each state node must launch and capture exactly once per II");
        }
    }

    if (!macro.stateRing.empty())
    {
        if (macro.stateTransitions.size() != 1 ||
            macro.initiationInterval != macro.phaseCount ||
            macro.stateRing.size() != static_cast<std::size_t>(macro.phaseCount) ||
            std::vector<int>(macro.stateTransitions.front().stages.begin(),
                             macro.stateTransitions.front().stages.end() - 1) !=
                macro.stateRing)
        {
            return fail("the compact single-state ring disagrees with its transition");
        }
    }

    if (!macro.clockSolution.has_value())
    {
        return fail("the physical state ring has no phase/epoch solution");
    }
    std::string solverError;
    if (!GlobalPhaseSolver::validateSolution(
            macro.clockProblem, macro.clockSolution.value(), &solverError))
    {
        return fail("invalid phase/epoch witness: " + solverError);
    }

    const auto &solution = macro.clockSolution.value();
    for (const int stateNode : macro.stateNodes)
    {
        const auto *node = findNode(macro, stateNode);
        const auto epoch = solution.eventEpoch.find(node->id);
        if (epoch == solution.eventEpoch.end() || epoch->second != 0)
        {
            return fail("state launches are not aligned at absolute epoch zero");
        }
    }
    for (const auto &transition : macro.stateTransitions)
    {
        for (int epochIndex = 0; epochIndex < macro.initiationInterval;
             ++epochIndex)
        {
            const auto *node = findNode(
                macro, transition.stages[static_cast<std::size_t>(epochIndex)]);
            const auto epoch = solution.eventEpoch.find(node->id);
            const auto phase = macro.phaseByCoordinate.find(node->coordinate);
            if (epoch == solution.eventEpoch.end() ||
                epoch->second != epochIndex ||
                phase == macro.phaseByCoordinate.end() ||
                phase->second != epochIndex % macro.phaseCount)
            {
                return fail("a state path does not advance one phase per epoch");
            }
        }
        const auto *captureNode = findNode(macro, transition.stages.back());
        const auto capturePhase =
            macro.phaseByCoordinate.find(captureNode->coordinate);
        if (capturePhase == macro.phaseByCoordinate.end() ||
            capturePhase->second != 0)
        {
            return fail("a state capture does not return to phase zero");
        }

        const auto *captureNet = findNet(
            macro,
            transition.stages[transition.stages.size() - 2],
            transition.stages.back());
        const auto feedbackLast = solution.occurrenceEpoch.find(
            occurrenceId(*captureNet, captureNet->path.size() - 1));
        if (feedbackLast == solution.occurrenceEpoch.end() ||
            feedbackLast->second != macro.initiationInterval)
        {
            return fail("a state capture does not arrive exactly one II later");
        }
    }
    return {true, {}};
}

PhysicalStateMappingResult validatePhysicalStateMapping(
    const PhysicalStateMacro &macro)
{
    const PhysicalStateValidationResult validation =
        validatePhysicalStateMacro(macro);
    if (!validation.valid)
    {
        return {false, 0, validation.message};
    }

    NodeLinkMap links;
    for (const auto &node : macro.nodes)
    {
        links.try_emplace(
            std::make_pair(node.coordinate, node.mappingType),
            std::make_pair(std::vector<position>{},
                           std::vector<position>{}));
    }

    std::vector<std::vector<position>> routePaths;
    std::vector<unsigned int> routeIterationDistances;
    for (const auto &net : macro.nets)
    {
        const auto *source = findNode(macro, net.sourceNode);
        const auto *sink = findNode(macro, net.sinkNode);
        links[{source->coordinate, source->mappingType}].second.push_back(
            net.path[1]);
        links[{sink->coordinate, sink->mappingType}].first.push_back(
            net.path[net.path.size() - 2]);
        routePaths.push_back(net.path);
        if (net.iterationDistance < 0)
        {
            return {false, 0, "negative iteration distance in physical state macro"};
        }
        routeIterationDistances.push_back(
            static_cast<unsigned int>(net.iterationDistance));
    }

    for (auto &link : links)
    {
        for (auto *ports : {&link.second.first, &link.second.second})
        {
            std::sort(ports->begin(), ports->end());
            ports->erase(std::unique(ports->begin(), ports->end()),
                         ports->end());
        }
    }

    Mapping mapping;
    mapping.node_mapping(links, MappingMode::Sequential);
    const RouteCellMap routes = mapping.mapping_line(
        routePaths, MappingMode::Sequential, routeIterationDistances);
    std::string mappingError;
    if (!mapping.validate_crossovers(&mappingError))
    {
        return {false, 0, "QCA mapping DRC failed: " + mappingError};
    }

    std::set<position> cells;
    for (const auto &bucket : mapping.nodecell_list)
    {
        cells.insert(bucket.second.begin(), bucket.second.end());
    }
    for (const auto &route : routes)
    {
        for (const auto &segment : route.second)
        {
            cells.insert(segment.begin(), segment.end());
        }
    }
    if (cells.empty())
    {
        return {false, 0, "QCA mapping emitted no physical cells"};
    }
    return {true, cells.size(), {}};
}

bool writePhysicalStateMacroLatex(const std::string &outputPath,
                                  const PhysicalStateMacro &macro,
                                  std::string *error)
{
    const PhysicalStateValidationResult validation =
        validatePhysicalStateMacro(macro);
    if (!validation.valid)
    {
        setError(error, "invalid physical state macro: " + validation.message);
        return false;
    }
    if (outputPath.empty())
    {
        setError(error, "physical state LaTeX output path is empty");
        return false;
    }

    std::set<position> layoutCoordinates;
    for (const auto &node : macro.nodes)
    {
        layoutCoordinates.insert(node.coordinate);
    }
    for (const auto &net : macro.nets)
    {
        layoutCoordinates.insert(net.path.begin(), net.path.end());
    }
    for (const position &coordinate : layoutCoordinates)
    {
        const auto phase = macro.phaseByCoordinate.find(coordinate);
        if (phase == macro.phaseByCoordinate.end() || phase->second < 0 ||
            phase->second >= macro.phaseCount)
        {
            setError(error, "missing physical-state phase at coordinate (" +
                            std::to_string(coordinate.first) + "," +
                            std::to_string(coordinate.second) + ")");
            return false;
        }
    }

    std::ofstream os(outputPath);
    if (!os.is_open())
    {
        setError(error, "cannot open physical state LaTeX output: " +
                        outputPath);
        return false;
    }

    // Keep this preamble and default style byte-for-byte aligned with the
    // non-2DD branch of CircuitGraph::printLaTex.  The sequential semantics
    // are carried exclusively by the closed physical route and its phases.
    os << R"(\documentclass[tikz]{standalone}
            %graphics
            \usepackage{pgfmath}
            \usetikzlibrary{calc,arrows.meta}
            \definecolor{ifcnink}{HTML}{23344A}
            \definecolor{ifcnaccent}{HTML}{2457A6}
            \definecolor{ifcnguide}{HTML}{EAF0F5}
            \definecolor{ifcnedge}{HTML}{A7B3C3}
            \definecolor{ifcninput}{HTML}{DCEAF7}
            \definecolor{ifcnoutput}{HTML}{F8E8C9}
            \definecolor{ifcnmajority}{HTML}{F2D58A}
            \definecolor{ifcnand}{HTML}{F2C9CB}
            \definecolor{ifcnor}{HTML}{E8DDF3}
            \definecolor{ifcnnot}{HTML}{DDF1E3}
            \definecolor{ifcnfanout}{HTML}{CDEBDD}
            \definecolor{ifcnwire}{HTML}{E7EBEF}

            \begin{document}
            \begin{tikzpicture}[
            scale=0.5,transform shape,
            c1/.style={rectangle, fill, lightgray!50, minimum size=1cm},
            c2/.style={rectangle, fill, lightgray, minimum size=1cm},
            c3/.style={rectangle, fill, gray, minimum size=1cm},
            c4/.style={rectangle, fill, darkgray!90, minimum size=1cm},
            route/.style={->, >={Stealth[]},line width=0.8pt, blue!50},
            v/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.7cm}
            ]
            )"
       << std::endl;

    unsigned int maxLayoutY = 0;
    for (const auto &node : macro.nodes)
    {
        maxLayoutY = std::max(maxLayoutY, node.coordinate.second);
    }
    for (const auto &net : macro.nets)
    {
        for (const position &coordinate : net.path)
        {
            maxLayoutY = std::max(maxLayoutY, coordinate.second);
        }
    }
    const auto drawY = [maxLayoutY](unsigned int y) {
        return maxLayoutY - y;
    };

    // GlobalPhaseSolver is zero based; the legacy combinational renderer's
    // clock styles are one based (c1..c4).
    for (const position &coordinate : layoutCoordinates)
    {
        const auto phase = macro.phaseByCoordinate.find(coordinate);
        os << "\\node[c" << phase->second + 1 << "] at ("
           << coordinate.first << ',' << drawY(coordinate.second)
           << ") {};" << std::endl;
    }

    os << R"(%nodes and edges)" << std::endl;
    for (const auto &node : macro.nodes)
    {
        std::string label;
        if (node.mappingType == "input" || node.mappingType == "output")
        {
            label = escapeLatexText(node.id);
        }
        else if (node.mappingType == "maj")
        {
            label = "M";
        }
        else if (node.mappingType == "and")
        {
            label = "\\&";
        }
        else if (node.mappingType == "or")
        {
            label = "\\textbar";
        }
        else if (node.mappingType == "not")
        {
            label = "$\\neg$";
        }
        else if (node.mappingType == "wire")
        {
            label = "w";
        }
        else if (node.mappingType == "fanout")
        {
            label = "F";
        }
        os << "\\node(" << node.index << ") [v] at ("
           << node.coordinate.first << ',' << drawY(node.coordinate.second)
           << ") {" << label << "};" << std::endl;
    }

    os << std::endl;
    for (const auto &net : macro.nets)
    {
        os << "\\draw[route] (" << net.sourceNode << ") -- ";
        for (std::size_t index = 1; index + 1 < net.path.size(); ++index)
        {
            os << '(' << net.path[index].first << ','
               << drawY(net.path[index].second) << ") -- ";
        }
        os << '(' << net.sinkNode << ");" << std::endl;
    }

    os << R"(
\end{tikzpicture}
    \end{document})"
       << std::endl;
    if (!os)
    {
        setError(error, "failed while writing physical state LaTeX output");
        return false;
    }
    return true;
}

} // namespace fcngraph::sequential
