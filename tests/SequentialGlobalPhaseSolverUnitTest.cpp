#include "autopr/sequential/globalPhaseSolver.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

void require(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        std::cerr << "Requirement failed at line " << line << ": "
                  << expression << '\n';
        std::exit(1);
    }
}

#define REQUIRE(condition) require((condition), #condition, __LINE__)

using fcngraph::sequential::ClockResourceSpec;
using fcngraph::sequential::ClockResourceSharing;
using fcngraph::sequential::EpochAnchorSpec;
using fcngraph::sequential::ExactTimingArcSpec;
using fcngraph::sequential::FixedRouteSpec;
using fcngraph::sequential::GlobalClockProblem;
using fcngraph::sequential::GlobalClockSolution;
using fcngraph::sequential::GlobalClockSolveStatus;
using fcngraph::sequential::GlobalPhaseSolver;
using fcngraph::sequential::RouteOccurrenceSpec;

GlobalClockProblem makeToggleProblem(bool repairedDogleg)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = 2;
    problem.iiCandidates = {4};
    problem.maxDfsNodes = 10000;
    problem.events = {"reg.q", "inv.a", "inv.y", "reg.d"};

    std::vector<std::string> resourceIds{
        "cell.q", "cell.inv_a", "cell.inv_y", "cell.d"
    };
    if (repairedDogleg)
    {
        resourceIds.push_back("cell.delay0");
        resourceIds.push_back("cell.delay1");
    }
    for (const auto &id : resourceIds)
    {
        problem.clockResources.push_back(ClockResourceSpec{id});
    }

    problem.occurrences = {
        RouteOccurrenceSpec{"q.launch", "cell.q", "epoch.q"},
        RouteOccurrenceSpec{"inv.input", "cell.inv_a", "epoch.inv_a"},
        RouteOccurrenceSpec{"inv.output", "cell.inv_y", "epoch.inv_y"},
        RouteOccurrenceSpec{"d.capture", "cell.d", "epoch.d"}
    };
    std::vector<std::string> feedbackOccurrences{
        "inv.output", "d.capture"
    };
    if (repairedDogleg)
    {
        problem.occurrences.push_back(
            RouteOccurrenceSpec{"delay.0", "cell.delay0", "epoch.delay0"});
        problem.occurrences.push_back(
            RouteOccurrenceSpec{"delay.1", "cell.delay1", "epoch.delay1"});
        feedbackOccurrences = {
            "inv.output", "delay.0", "delay.1", "d.capture"
        };
    }

    problem.routes = {
        FixedRouteSpec{
            "q_to_inv", "reg.q", "inv.a", 0,
            {"q.launch", "inv.input"}
        },
        FixedRouteSpec{
            "inv_to_d", "inv.y", "reg.d", 0,
            feedbackOccurrences
        }
    };
    problem.timingArcs = {
        ExactTimingArcSpec{"inv.latency", "inv.a", "inv.y", 0, 1},
        // E(Q) + II - E(D) = 0, hence D is captured one II after Q.
        ExactTimingArcSpec{"reg.state", "reg.d", "reg.q", 1, 0}
    };
    problem.anchors = {EpochAnchorSpec{"reg.q", 0}};
    return problem;
}

GlobalClockProblem makeSharedResourceProblem(
    int secondAnchor,
    ClockResourceSharing sharing,
    bool aliasAbsoluteEpoch)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = 2;
    problem.iiCandidates = {4};
    problem.events = {"a.source", "a.sink", "b.source", "b.sink"};
    problem.clockResources = {ClockResourceSpec{"shared.cell", sharing}};
    problem.occurrences = {
        RouteOccurrenceSpec{"a.use", "shared.cell", "epoch.a"},
        RouteOccurrenceSpec{
            "b.use", "shared.cell",
            aliasAbsoluteEpoch ? "epoch.a" : "epoch.b"
        }
    };
    problem.routes = {
        FixedRouteSpec{"a.route", "a.source", "a.sink", 0, {"a.use"}},
        FixedRouteSpec{"b.route", "b.source", "b.sink", 0, {"b.use"}}
    };
    problem.anchors = {
        EpochAnchorSpec{"a.source", 0},
        EpochAnchorSpec{"b.source", secondAnchor}
    };
    return problem;
}

GlobalClockProblem makeSingleRouteRecurrence(int routeEdges,
                                             int ii,
                                             int stateLatency,
                                             int maxSamePhaseCells)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = maxSamePhaseCells;
    problem.iiCandidates = {ii};
    problem.maxDfsNodes = 1000000;
    problem.events = {"q", "d"};

    std::vector<std::string> occurrenceIds;
    for (int index = 0; index <= routeEdges; ++index)
    {
        const std::string suffix = std::to_string(index);
        problem.clockResources.push_back(ClockResourceSpec{"cell." + suffix});
        problem.occurrences.push_back(RouteOccurrenceSpec{
            "occ." + suffix, "cell." + suffix, "epoch." + suffix
        });
        occurrenceIds.push_back("occ." + suffix);
    }
    problem.routes = {
        FixedRouteSpec{"q_to_d", "q", "d", 0, occurrenceIds}
    };
    problem.timingArcs = {
        ExactTimingArcSpec{"state", "d", "q", 1, stateLatency}
    };
    problem.anchors = {EpochAnchorSpec{"q", 0}};
    return problem;
}

bool bruteForceSingleRouteFeasible(int routeEdges,
                                   int ii,
                                   int stateLatency,
                                   int maxSamePhaseCells)
{
    const std::uint64_t patterns = std::uint64_t{1} << routeEdges;
    for (std::uint64_t pattern = 0; pattern < patterns; ++pattern)
    {
        int advances = 0;
        int run = 1;
        bool runLegal = true;
        for (int edge = 0; edge < routeEdges; ++edge)
        {
            const bool advance = ((pattern >> edge) & 1U) != 0;
            advances += advance ? 1 : 0;
            run = advance ? 1 : run + 1;
            runLegal = runLegal &&
                (maxSamePhaseCells <= 0 || run <= maxSamePhaseCells);
        }
        if (runLegal && advances + stateLatency == ii)
        {
            return true;
        }
    }
    return false;
}

GlobalClockProblem makeIterationDistanceRoute(int routeEdges)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = 1;
    problem.iiCandidates = {4};
    problem.events = {"launch", "capture"};
    problem.anchors = {
        EpochAnchorSpec{"launch", 0},
        EpochAnchorSpec{"capture", 0}
    };

    std::vector<std::string> occurrenceIds;
    for (int index = 0; index <= routeEdges; ++index)
    {
        const std::string suffix = std::to_string(index);
        problem.clockResources.push_back(ClockResourceSpec{"route.cell." + suffix});
        problem.occurrences.push_back(RouteOccurrenceSpec{
            "route.occ." + suffix,
            "route.cell." + suffix,
            "route.epoch." + suffix
        });
        occurrenceIds.push_back("route.occ." + suffix);
    }
    problem.routes = {
        FixedRouteSpec{
            "next_iteration_route", "launch", "capture", 1,
            occurrenceIds
        }
    };
    return problem;
}

void directFeedbackIsProvenUnsat()
{
    const auto result = GlobalPhaseSolver(makeToggleProblem(false)).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::Unsat);
    REQUIRE(!result.solution.has_value());
    REQUIRE(result.stats.iiCandidatesTried == 1);
    REQUIRE(result.stats.conflicts > 0);
}

void doglegFeedbackGetsGlobalEpochAndPhaseSolution()
{
    const auto problem = makeToggleProblem(true);
    const auto result = GlobalPhaseSolver(problem).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::Sat);
    REQUIRE(result.solution.has_value());

    const GlobalClockSolution &solution = result.solution.value();
    REQUIRE(solution.initiationInterval == 4);
    REQUIRE(solution.eventEpoch.at("reg.q") == 0);
    REQUIRE(solution.eventEpoch.at("reg.d") == 4);
    REQUIRE(solution.eventEpoch.at("inv.y") -
               solution.eventEpoch.at("inv.a") == 1);
    REQUIRE(solution.clockResourcePhase.at("cell.q") == 0);
    REQUIRE(solution.clockResourcePhase.at("cell.d") == 0);

    bool observedThreeToZeroAdvance = false;
    const std::vector<std::string> feedback{
        "inv.output", "delay.0", "delay.1", "d.capture"
    };
    for (std::size_t index = 1; index < feedback.size(); ++index)
    {
        const std::int64_t previous =
            solution.occurrenceEpoch.at(feedback[index - 1]);
        const std::int64_t current =
            solution.occurrenceEpoch.at(feedback[index]);
        const auto previousPhase = static_cast<int>((previous % 4 + 4) % 4);
        const auto currentPhase = static_cast<int>((current % 4 + 4) % 4);
        if (previousPhase == 3 && currentPhase == 0 && current - previous == 1)
        {
            observedThreeToZeroAdvance = true;
        }
    }
    REQUIRE(observedThreeToZeroAdvance);

    std::string error;
    REQUIRE(GlobalPhaseSolver::validateSolution(problem, solution, &error));
}

void zeroDistanceCombinationalCycleIsInvalidInput()
{
    auto problem = makeToggleProblem(true);
    problem.timingArcs.back().iterationDistance = 0;
    const auto result = GlobalPhaseSolver(std::move(problem)).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::InvalidInput);
    REQUIRE(result.message.find("cycle") != std::string::npos);
}

void samePhaseRunLimitIsHard()
{
    auto problem = makeToggleProblem(true);
    problem.maxConsecutiveSamePhaseCells = 1;
    const auto result = GlobalPhaseSolver(std::move(problem)).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::Unsat);
}

void searchBudgetDoesNotMasqueradeAsUnsat()
{
    auto problem = makeToggleProblem(true);
    problem.maxDfsNodes = 1;
    const auto result = GlobalPhaseSolver(std::move(problem)).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::Limit);
    REQUIRE(!result.solution.has_value());
    REQUIRE(result.stats.dfsNodes == 1);
}

void sharedClockResourceUsesModuloButTrunkAliasUsesAbsoluteEpoch()
{
    const auto periodicReuse = GlobalPhaseSolver(makeSharedResourceProblem(
        4, ClockResourceSharing::PhaseSharedIndependentEpochs, false)).solve();
    REQUIRE(periodicReuse.status == GlobalClockSolveStatus::Sat);
    REQUIRE(periodicReuse.solution->clockResourcePhase.at("shared.cell") == 0);
    REQUIRE(periodicReuse.solution->occurrenceEpoch.at("a.use") == 0);
    REQUIRE(periodicReuse.solution->occurrenceEpoch.at("b.use") == 4);

    const auto phaseConflict = GlobalPhaseSolver(makeSharedResourceProblem(
        1, ClockResourceSharing::PhaseSharedIndependentEpochs, false)).solve();
    REQUIRE(phaseConflict.status == GlobalClockSolveStatus::Unsat);

    const auto trunkConflict = GlobalPhaseSolver(makeSharedResourceProblem(
        4, ClockResourceSharing::ExclusiveOrAliased, true)).solve();
    REQUIRE(trunkConflict.status == GlobalClockSolveStatus::Unsat);

    const auto illegalOverlap = GlobalPhaseSolver(makeSharedResourceProblem(
        4, ClockResourceSharing::ExclusiveOrAliased, false)).solve();
    REQUIRE(illegalOverlap.status == GlobalClockSolveStatus::InvalidInput);
}

void exactSearchMatchesBruteForceRecurrenceOracle()
{
    for (const int ii : {4, 8})
    {
        for (int routeEdges = 0; routeEdges <= 8; ++routeEdges)
        {
            for (int stateLatency = 0; stateLatency <= 4; ++stateLatency)
            {
                for (int maxSamePhaseCells = 1;
                     maxSamePhaseCells <= 4; ++maxSamePhaseCells)
                {
                    const bool expected = bruteForceSingleRouteFeasible(
                        routeEdges, ii, stateLatency, maxSamePhaseCells);
                    const auto result = GlobalPhaseSolver(
                        makeSingleRouteRecurrence(routeEdges, ii, stateLatency,
                                                  maxSamePhaseCells)).solve();
                    REQUIRE(result.status != GlobalClockSolveStatus::Limit);
                    REQUIRE(result.status != GlobalClockSolveStatus::InvalidInput);
                    REQUIRE((result.status == GlobalClockSolveStatus::Sat) ==
                           expected);
                    if (result.solution.has_value())
                    {
                        std::string error;
                        const auto problem = makeSingleRouteRecurrence(
                            routeEdges, ii, stateLatency,
                            maxSamePhaseCells);
                        REQUIRE(GlobalPhaseSolver::validateSolution(
                            problem, result.solution.value(), &error));
                    }
                }
            }
        }
    }
}

void publicValidatorRejectsMalformedProblemWithoutThrowing()
{
    GlobalClockProblem malformed;
    malformed.phaseCount = 0;
    GlobalClockSolution emptySolution;
    std::string error;
    REQUIRE(!GlobalPhaseSolver::validateSolution(
        malformed, emptySolution, &error));
    REQUIRE(error.find("invalid clock problem") != std::string::npos);
}

void routeIterationDistanceUsesPositiveIiAtTheSink()
{
    const auto fourEdges = GlobalPhaseSolver(
        makeIterationDistanceRoute(4)).solve();
    REQUIRE(fourEdges.status == GlobalClockSolveStatus::Sat);
    REQUIRE(fourEdges.solution->occurrenceEpoch.at("route.occ.0") == 0);
    REQUIRE(fourEdges.solution->occurrenceEpoch.at("route.occ.4") == 4);

    const auto threeEdges = GlobalPhaseSolver(
        makeIterationDistanceRoute(3)).solve();
    REQUIRE(threeEdges.status == GlobalClockSolveStatus::Unsat);
}

void excessiveNumericRangeIsInvalidInsteadOfOverflowing()
{
    auto problem = makeToggleProblem(true);
    problem.phaseCount = 2;
    problem.iiCandidates = {std::numeric_limits<int>::max() - 1};
    problem.timingArcs.front().iterationDistance =
        std::numeric_limits<int>::max();
    const auto result = GlobalPhaseSolver(std::move(problem)).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::InvalidInput);
}

void publicValidatorRejectsOverflowingEpochArithmetic()
{
    auto routeProblem = makeIterationDistanceRoute(4);
    routeProblem.anchors = {EpochAnchorSpec{"launch", 0}};
    const auto routeResult = GlobalPhaseSolver(routeProblem).solve();
    REQUIRE(routeResult.status == GlobalClockSolveStatus::Sat);
    auto damagedRouteSolution = routeResult.solution.value();
    damagedRouteSolution.eventEpoch["capture"] =
        std::numeric_limits<std::int64_t>::max();
    std::string error;
    REQUIRE(!GlobalPhaseSolver::validateSolution(
        routeProblem, damagedRouteSolution, &error));

    const auto timingProblem = makeToggleProblem(true);
    const auto timingResult = GlobalPhaseSolver(timingProblem).solve();
    REQUIRE(timingResult.status == GlobalClockSolveStatus::Sat);
    auto damagedTimingSolution = timingResult.solution.value();
    damagedTimingSolution.eventEpoch["inv.a"] =
        std::numeric_limits<std::int64_t>::max();
    REQUIRE(!GlobalPhaseSolver::validateSolution(
        timingProblem, damagedTimingSolution, &error));
}

void publicValidatorRejectsUnexpectedExternalKeys()
{
    const auto problem = makeToggleProblem(true);
    const auto result = GlobalPhaseSolver(problem).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::Sat);
    std::string error;

    auto extraEvent = result.solution.value();
    extraEvent.eventEpoch["injected.event"] = 0;
    REQUIRE(!GlobalPhaseSolver::validateSolution(problem, extraEvent, &error));

    auto extraOccurrence = result.solution.value();
    extraOccurrence.occurrenceEpoch["injected.occurrence"] = 0;
    REQUIRE(!GlobalPhaseSolver::validateSolution(
        problem, extraOccurrence, &error));

    auto extraResource = result.solution.value();
    extraResource.clockResourcePhase["cell.999999.999999"] = 0;
    REQUIRE(!GlobalPhaseSolver::validateSolution(
        problem, extraResource, &error));
}

void routeEdgeContractExtendsPastLegacy160EdgeBound()
{
    const auto problem = makeSingleRouteRecurrence(161, 160, 0, 4);
    const auto result = GlobalPhaseSolver(problem).solve();
    REQUIRE(result.status == GlobalClockSolveStatus::Sat);
    REQUIRE(result.solution.has_value());
    std::string error;
    REQUIRE(GlobalPhaseSolver::validateSolution(
        problem, result.solution.value(), &error));
}

void externalReplayAcceptsMappedProblemsPastLegacy512Cells()
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = 0;
    problem.iiCandidates = {4};
    problem.events = {"source", "sink"};
    problem.anchors = {EpochAnchorSpec{"source", 0}};

    GlobalClockSolution solution;
    solution.phaseCount = 4;
    solution.initiationInterval = 4;
    solution.eventEpoch = {{"source", 0}, {"sink", 0}};
    std::vector<std::string> route;
    for (int index = 0; index < 513; ++index)
    {
        const std::string suffix = std::to_string(index);
        const std::string resource = "qca." + suffix + ".0";
        const std::string occurrence = "route.occ." + suffix;
        const std::string epoch = "route.epoch." + suffix;
        problem.clockResources.push_back(ClockResourceSpec{resource});
        problem.occurrences.push_back(
            RouteOccurrenceSpec{occurrence, resource, epoch});
        route.push_back(occurrence);
        solution.occurrenceEpoch.emplace(occurrence, 0);
        solution.clockResourcePhase.emplace(resource, 0);
    }
    problem.routes.push_back(FixedRouteSpec{
        "mapped_route", "source", "sink", 0, route});

    std::string error;
    REQUIRE(GlobalPhaseSolver::validateSolution(problem, solution, &error));
}

} // namespace

int main()
{
    directFeedbackIsProvenUnsat();
    doglegFeedbackGetsGlobalEpochAndPhaseSolution();
    zeroDistanceCombinationalCycleIsInvalidInput();
    samePhaseRunLimitIsHard();
    searchBudgetDoesNotMasqueradeAsUnsat();
    sharedClockResourceUsesModuloButTrunkAliasUsesAbsoluteEpoch();
    exactSearchMatchesBruteForceRecurrenceOracle();
    publicValidatorRejectsMalformedProblemWithoutThrowing();
    routeIterationDistanceUsesPositiveIiAtTheSink();
    excessiveNumericRangeIsInvalidInsteadOfOverflowing();
    publicValidatorRejectsOverflowingEpochArithmetic();
    publicValidatorRejectsUnexpectedExternalKeys();
    routeEdgeContractExtendsPastLegacy160EdgeBound();
    externalReplayAcceptsMappedProblemsPastLegacy512Cells();
    std::cout << "Sequential global random-clock phase solver tests passed.\n";
    return 0;
}
