#include <autopr/sequential/globalPhaseSolver.h>
#include <autopr/sequential/physicalStateMacro.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace fcngraph::sequential;
using Clock = std::chrono::steady_clock;

struct CommandLine
{
    std::filesystem::path outputDirectory;
    int repetitions = 50;
};

struct TimedSolve
{
    GlobalClockSolveResult result;
    double microseconds = 0.0;
};

struct ScalingSummary
{
    int states = 0;
    std::string expectedStatus;
    int routeEdges = 0;
    int totalRouteEdges = 0;
    int events = 0;
    int occurrences = 0;
    double medianMicroseconds = 0.0;
    double p95Microseconds = 0.0;
    double medianDfsNodes = 0.0;
};

struct EdgeScalingSummary
{
    int routeEdges = 0;
    std::string expectedStatus;
    int initiationInterval = 0;
    int stateLatency = 0;
    std::uint64_t dfsBudget = 0;
    int satRuns = 0;
    int unsatRuns = 0;
    int limitRuns = 0;
    int invalidRuns = 0;
    double medianMicroseconds = 0.0;
    double p95Microseconds = 0.0;
    double medianDfsNodes = 0.0;
};

struct MacroSummary
{
    std::string name;
    int stateBits = 0;
    int nodes = 0;
    int nets = 0;
    int captureNets = 0;
    int initiationInterval = 0;
    std::size_t qcaCells = 0;
    double medianBuildMicroseconds = 0.0;
    double p95BuildMicroseconds = 0.0;
    double medianMappingMicroseconds = 0.0;
    double p95MappingMicroseconds = 0.0;
};

std::string usage()
{
    return "usage: ifcn_sequential_clock_experiment <output-directory> "
           "[--repetitions N]\n";
}

CommandLine parseCommandLine(int argc, char **argv)
{
    if (argc < 2)
    {
        throw std::runtime_error(usage());
    }
    CommandLine command;
    command.outputDirectory = argv[1];
    for (int index = 2; index < argc; ++index)
    {
        const std::string option(argv[index]);
        if (option == "--repetitions")
        {
            if (++index >= argc)
            {
                throw std::runtime_error("--repetitions requires a value");
            }
            command.repetitions = std::stoi(argv[index]);
            if (command.repetitions <= 0 || command.repetitions > 10000)
            {
                throw std::runtime_error(
                    "--repetitions must be in the range 1..10000");
            }
        }
        else
        {
            throw std::runtime_error("unknown option: " + option + "\n" +
                                     usage());
        }
    }
    return command;
}

const char *statusName(GlobalClockSolveStatus status)
{
    switch (status)
    {
    case GlobalClockSolveStatus::Sat:
        return "SAT";
    case GlobalClockSolveStatus::Unsat:
        return "UNSAT";
    case GlobalClockSolveStatus::Limit:
        return "LIMIT";
    case GlobalClockSolveStatus::InvalidInput:
        return "INVALID_INPUT";
    }
    return "UNKNOWN";
}

double percentile(std::vector<double> values, double quantile)
{
    if (values.empty())
    {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double offset = quantile * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(offset));
    const auto upper = static_cast<std::size_t>(std::ceil(offset));
    const double fraction = offset - static_cast<double>(lower);
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

TimedSolve solveTimed(const GlobalClockProblem &problem)
{
    const auto start = Clock::now();
    GlobalClockSolveResult result = GlobalPhaseSolver(problem).solve();
    const auto stop = Clock::now();
    return {
        std::move(result),
        std::chrono::duration<double, std::micro>(stop - start).count()
    };
}

bool sequenceWithAdvancesIsFeasible(int routeEdges,
                                    int advances,
                                    int maxSamePhaseCells)
{
    if (advances < 0 || advances > routeEdges)
    {
        return false;
    }
    if (maxSamePhaseCells <= 0)
    {
        return true;
    }
    const int zeroAdvanceEdges = routeEdges - advances;
    const int zeroCapacity =
        (advances + 1) * (maxSamePhaseCells - 1);
    return zeroAdvanceEdges <= zeroCapacity;
}

bool exactRecurrenceOracle(int routeEdges,
                           int initiationInterval,
                           int stateLatency,
                           int maxSamePhaseCells)
{
    return sequenceWithAdvancesIsFeasible(
        routeEdges,
        initiationInterval - stateLatency,
        maxSamePhaseCells);
}

// This is deliberately a strong modulo-only baseline: it enforces every
// route-local hold/advance rule, the same-phase-run limit, and the state
// recurrence modulo P.  It omits only the absolute epoch/II equality.
bool moduloOnlyRecurrenceOracle(int routeEdges,
                                int phaseCount,
                                int stateLatency,
                                int maxSamePhaseCells)
{
    for (int advances = 0; advances <= routeEdges; ++advances)
    {
        if ((advances + stateLatency) % phaseCount == 0 &&
            sequenceWithAdvancesIsFeasible(
                routeEdges, advances, maxSamePhaseCells))
        {
            return true;
        }
    }
    return false;
}

int minimumAdvancesForRunLimit(int routeEdges, int maxSamePhaseCells)
{
    for (int advances = 0; advances <= routeEdges; ++advances)
    {
        if (sequenceWithAdvancesIsFeasible(
                routeEdges, advances, maxSamePhaseCells))
        {
            return advances;
        }
    }
    throw std::runtime_error("no feasible route-local advance count");
}

int roundUpToMultiple(int value, int multiple)
{
    return ((value + multiple - 1) / multiple) * multiple;
}

bool validateSatSolution(const GlobalClockProblem &problem,
                         const GlobalClockSolveResult &result)
{
    if (result.status != GlobalClockSolveStatus::Sat ||
        !result.solution.has_value())
    {
        return result.status != GlobalClockSolveStatus::Sat;
    }
    std::string error;
    return GlobalPhaseSolver::validateSolution(
        problem, result.solution.value(), &error);
}

GlobalClockProblem makeParallelRecurrenceProblem(int states,
                                                 int routeEdges,
                                                 int initiationInterval,
                                                 int stateLatency,
                                                 int maxSamePhaseCells)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = maxSamePhaseCells;
    problem.iiCandidates = {initiationInterval};
    problem.maxDfsNodes = 1000000;

    for (int state = 0; state < states; ++state)
    {
        const std::string prefix = "s" + std::to_string(state);
        const std::string q = prefix + ".q";
        const std::string d = prefix + ".d";
        problem.events.push_back(q);
        problem.events.push_back(d);
        problem.anchors.push_back(EpochAnchorSpec{q, 0});

        std::vector<std::string> routeOccurrences;
        for (int index = 0; index <= routeEdges; ++index)
        {
            const std::string suffix = std::to_string(index);
            const std::string resource = prefix + ".cell." + suffix;
            const std::string occurrence = prefix + ".occ." + suffix;
            problem.clockResources.push_back(ClockResourceSpec{resource});
            problem.occurrences.push_back(RouteOccurrenceSpec{
                occurrence, resource, prefix + ".epoch." + suffix});
            routeOccurrences.push_back(occurrence);
        }
        problem.routes.push_back(FixedRouteSpec{
            prefix + ".route", q, d, 0, std::move(routeOccurrences)});
        problem.timingArcs.push_back(ExactTimingArcSpec{
            prefix + ".state", d, q, 1, stateLatency});
    }
    return problem;
}

std::string latexMacroName(const std::string &name)
{
    if (name == "t1_toggle")
        return "T1 toggle";
    if (name == "reset_toggle")
        return "Reset toggle";
    if (name == "johnson2_reset")
        return "Johnson-2 reset";
    if (name == "johnson4")
        return "Johnson-4";
    return name;
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const CommandLine command = parseCommandLine(argc, argv);
        std::filesystem::create_directories(command.outputDirectory);

        std::ofstream recurrenceCsv(
            command.outputDirectory / "recurrence_sweep.csv");
        if (!recurrenceCsv)
        {
            throw std::runtime_error("cannot create recurrence_sweep.csv");
        }
        recurrenceCsv
            << "route_edges,ii,state_latency,max_same_phase_cells,"
               "modulo_only,exact_oracle,solver_status,false_accept,"
               "false_reject,time_us,dfs_nodes,decisions,forced_edges,"
               "conflicts\n";

        std::uint64_t sweepCases = 0;
        std::uint64_t exactSat = 0;
        std::uint64_t exactUnsat = 0;
        std::uint64_t moduloSat = 0;
        std::uint64_t falseAccept = 0;
        std::uint64_t falseReject = 0;
        std::uint64_t solverMismatch = 0;
        std::uint64_t solverLimit = 0;
        // maxSamePhaseCells == 1 is the strict tile-to-tile progression
        // sub-family.  Every physical edge advances exactly one clock number,
        // matching the local modulo-clock model used by Walter et al. (2024).
        // The comparison is constraint-level; it is not a runtime comparison
        // against their fiction implementation.
        std::uint64_t strictCases = 0;
        std::uint64_t strictExactSat = 0;
        std::uint64_t strictExactUnsat = 0;
        std::uint64_t strictModuloFalseAccept = 0;
        std::uint64_t strictModuloFalseReject = 0;
        std::vector<double> sweepTimes;

        recurrenceCsv << std::fixed << std::setprecision(3);
        for (int routeEdges = 0; routeEdges <= 12; ++routeEdges)
        {
            for (const int ii : {4, 8, 12, 16})
            {
                for (int stateLatency = 0; stateLatency <= 4;
                     ++stateLatency)
                {
                    for (int maxSamePhaseCells = 1;
                         maxSamePhaseCells <= 4;
                         ++maxSamePhaseCells)
                    {
                        const bool exact = exactRecurrenceOracle(
                            routeEdges, ii, stateLatency,
                            maxSamePhaseCells);
                        const bool modulo = moduloOnlyRecurrenceOracle(
                            routeEdges, 4, stateLatency,
                            maxSamePhaseCells);
                        const GlobalClockProblem problem =
                            makeParallelRecurrenceProblem(
                                1, routeEdges, ii, stateLatency,
                                maxSamePhaseCells);
                        const TimedSolve timed = solveTimed(problem);
                        const bool solverSat =
                            timed.result.status == GlobalClockSolveStatus::Sat;
                        const bool isLimit =
                            timed.result.status == GlobalClockSolveStatus::Limit;
                        const bool mismatch = !isLimit &&
                            timed.result.status !=
                                GlobalClockSolveStatus::InvalidInput &&
                            solverSat != exact;

                        ++sweepCases;
                        exact ? ++exactSat : ++exactUnsat;
                        moduloSat += modulo ? 1U : 0U;
                        falseAccept += modulo && !exact ? 1U : 0U;
                        falseReject += !modulo && exact ? 1U : 0U;
                        solverMismatch += mismatch ? 1U : 0U;
                        solverLimit += isLimit ? 1U : 0U;
                        sweepTimes.push_back(timed.microseconds);

                        if (maxSamePhaseCells == 1)
                        {
                            ++strictCases;
                            exact ? ++strictExactSat : ++strictExactUnsat;
                            strictModuloFalseAccept +=
                                modulo && !exact ? 1U : 0U;
                            strictModuloFalseReject +=
                                !modulo && exact ? 1U : 0U;
                        }

                        recurrenceCsv
                            << routeEdges << ',' << ii << ',' << stateLatency
                            << ',' << maxSamePhaseCells << ','
                            << (modulo ? 1 : 0) << ',' << (exact ? 1 : 0)
                            << ',' << statusName(timed.result.status) << ','
                            << (modulo && !exact ? 1 : 0) << ','
                            << (!modulo && exact ? 1 : 0) << ','
                            << timed.microseconds << ','
                            << timed.result.stats.dfsNodes << ','
                            << timed.result.stats.decisions << ','
                            << timed.result.stats.forcedEdges << ','
                            << timed.result.stats.conflicts << '\n';
                    }
                }
            }
        }
        if (solverMismatch != 0 || solverLimit != 0)
        {
            throw std::runtime_error(
                "reference solver disagreed with the exact oracle or hit LIMIT");
        }

        std::ofstream iiAblationCsv(
            command.outputDirectory / "ii_ablation.csv");
        if (!iiAblationCsv)
        {
            throw std::runtime_error("cannot create ii_ablation.csv");
        }
        iiAblationCsv
            << "route_edges,state_latency,max_same_phase_cells,"
               "adaptive_candidates,fixed_ii,adaptive_oracle,fixed_oracle,"
               "adaptive_solver_status,fixed_solver_status,adaptive_ii,"
               "adaptive_only,fixed_only,adaptive_validator,fixed_validator,"
               "adaptive_time_us,fixed_time_us,adaptive_dfs_nodes,"
               "fixed_dfs_nodes\n";
        iiAblationCsv << std::fixed << std::setprecision(3);

        const std::vector<int> adaptiveIiCandidates{4, 8, 12, 16};
        std::uint64_t iiAblationCases = 0;
        std::uint64_t adaptiveSat = 0;
        std::uint64_t fixedIi4Sat = 0;
        std::uint64_t adaptiveOnlySat = 0;
        std::uint64_t fixedOnlySat = 0;
        std::uint64_t iiAblationMismatch = 0;
        std::uint64_t iiAblationLimit = 0;
        for (int routeEdges = 0; routeEdges <= 12; ++routeEdges)
        {
            for (int stateLatency = 0; stateLatency <= 4; ++stateLatency)
            {
                for (int maxSamePhaseCells = 1;
                     maxSamePhaseCells <= 4; ++maxSamePhaseCells)
                {
                    bool adaptiveOracle = false;
                    for (const int ii : adaptiveIiCandidates)
                    {
                        adaptiveOracle = adaptiveOracle ||
                            exactRecurrenceOracle(
                                routeEdges, ii, stateLatency,
                                maxSamePhaseCells);
                    }
                    const bool fixedOracle = exactRecurrenceOracle(
                        routeEdges, 4, stateLatency,
                        maxSamePhaseCells);

                    GlobalClockProblem adaptiveProblem =
                        makeParallelRecurrenceProblem(
                            1, routeEdges, 4, stateLatency,
                            maxSamePhaseCells);
                    adaptiveProblem.iiCandidates = adaptiveIiCandidates;
                    const TimedSolve adaptiveTimed =
                        solveTimed(adaptiveProblem);
                    const GlobalClockProblem fixedProblem =
                        makeParallelRecurrenceProblem(
                            1, routeEdges, 4, stateLatency,
                            maxSamePhaseCells);
                    const TimedSolve fixedTimed = solveTimed(fixedProblem);

                    const bool adaptiveSolverSat =
                        adaptiveTimed.result.status ==
                        GlobalClockSolveStatus::Sat;
                    const bool fixedSolverSat =
                        fixedTimed.result.status ==
                        GlobalClockSolveStatus::Sat;
                    const bool adaptiveValidator = validateSatSolution(
                        adaptiveProblem, adaptiveTimed.result);
                    const bool fixedValidator = validateSatSolution(
                        fixedProblem, fixedTimed.result);
                    const bool isLimit =
                        adaptiveTimed.result.status ==
                            GlobalClockSolveStatus::Limit ||
                        fixedTimed.result.status ==
                            GlobalClockSolveStatus::Limit;
                    const bool mismatch =
                        adaptiveTimed.result.status ==
                            GlobalClockSolveStatus::InvalidInput ||
                        fixedTimed.result.status ==
                            GlobalClockSolveStatus::InvalidInput ||
                        (!isLimit &&
                         (adaptiveSolverSat != adaptiveOracle ||
                          fixedSolverSat != fixedOracle)) ||
                        !adaptiveValidator || !fixedValidator;

                    ++iiAblationCases;
                    adaptiveSat += adaptiveOracle ? 1U : 0U;
                    fixedIi4Sat += fixedOracle ? 1U : 0U;
                    adaptiveOnlySat +=
                        adaptiveOracle && !fixedOracle ? 1U : 0U;
                    fixedOnlySat +=
                        !adaptiveOracle && fixedOracle ? 1U : 0U;
                    iiAblationLimit += isLimit ? 1U : 0U;
                    iiAblationMismatch += mismatch ? 1U : 0U;

                    const int selectedIi =
                        adaptiveTimed.result.solution.has_value()
                        ? adaptiveTimed.result.solution->initiationInterval
                        : 0;
                    iiAblationCsv
                        << routeEdges << ',' << stateLatency << ','
                        << maxSamePhaseCells << ",4|8|12|16,4,"
                        << (adaptiveOracle ? 1 : 0) << ','
                        << (fixedOracle ? 1 : 0) << ','
                        << statusName(adaptiveTimed.result.status) << ','
                        << statusName(fixedTimed.result.status) << ','
                        << selectedIi << ','
                        << (adaptiveOracle && !fixedOracle ? 1 : 0) << ','
                        << (!adaptiveOracle && fixedOracle ? 1 : 0) << ','
                        << (adaptiveValidator ? 1 : 0) << ','
                        << (fixedValidator ? 1 : 0) << ','
                        << adaptiveTimed.microseconds << ','
                        << fixedTimed.microseconds << ','
                        << adaptiveTimed.result.stats.dfsNodes << ','
                        << fixedTimed.result.stats.dfsNodes << '\n';
                }
            }
        }
        if (iiAblationMismatch != 0 || iiAblationLimit != 0)
        {
            throw std::runtime_error(
                "II ablation disagreed with the exact oracle or hit LIMIT");
        }

        std::ofstream scalingCsv(
            command.outputDirectory / "scaling_runs.csv");
        if (!scalingCsv)
        {
            throw std::runtime_error("cannot create scaling_runs.csv");
        }
        scalingCsv
            << "states,expected_status,route_edges,total_route_edges,events,"
               "occurrences,repetition,solver_status,time_us,dfs_nodes,"
               "decisions,forced_edges,conflicts\n";
        scalingCsv << std::fixed << std::setprecision(3);

        std::vector<ScalingSummary> scalingSummaries;
        for (const int states : {1, 2, 4, 8, 16, 32})
        {
            for (const auto &[expectedStatus, routeEdges] :
                 std::vector<std::pair<std::string, int>>{
                     {"SAT", 4}, {"UNSAT", 3}})
            {
                const GlobalClockProblem problem =
                    makeParallelRecurrenceProblem(
                        states, routeEdges, 4, 0, 4);
                std::vector<double> times;
                std::vector<double> dfsNodes;
                for (int repetition = 0;
                     repetition < command.repetitions;
                     ++repetition)
                {
                    const TimedSolve timed = solveTimed(problem);
                    if (statusName(timed.result.status) != expectedStatus)
                    {
                        throw std::runtime_error(
                            "scaling fixture returned an unexpected status");
                    }
                    times.push_back(timed.microseconds);
                    dfsNodes.push_back(
                        static_cast<double>(timed.result.stats.dfsNodes));
                    scalingCsv
                        << states << ',' << expectedStatus << ','
                        << routeEdges << ',' << states * routeEdges << ','
                        << problem.events.size() << ','
                        << problem.occurrences.size() << ',' << repetition
                        << ',' << statusName(timed.result.status) << ','
                        << timed.microseconds << ','
                        << timed.result.stats.dfsNodes << ','
                        << timed.result.stats.decisions << ','
                        << timed.result.stats.forcedEdges << ','
                        << timed.result.stats.conflicts << '\n';
                }
                scalingSummaries.push_back(ScalingSummary{
                    states,
                    expectedStatus,
                    routeEdges,
                    states * routeEdges,
                    static_cast<int>(problem.events.size()),
                    static_cast<int>(problem.occurrences.size()),
                    percentile(times, 0.50),
                    percentile(times, 0.95),
                    percentile(dfsNodes, 0.50)});
            }
        }

        std::ofstream edgeScalingCsv(
            command.outputDirectory / "edge_scaling_runs.csv");
        if (!edgeScalingCsv)
        {
            throw std::runtime_error("cannot create edge_scaling_runs.csv");
        }
        edgeScalingCsv
            << "route_edges,fixture,expected_status,ii,state_latency,"
               "max_same_phase_cells,dfs_budget,exact_oracle,repetition,"
               "solver_status,internal_validator,time_us,dfs_nodes,"
               "decisions,forced_edges,conflicts\n";
        edgeScalingCsv << std::fixed << std::setprecision(3);

        std::vector<EdgeScalingSummary> edgeScalingSummaries;
        const std::vector<int> edgeCounts{8, 16, 32, 64, 96, 128, 160};
        for (const int routeEdges : edgeCounts)
        {
            const int maxSamePhaseCells = 4;
            const int minimumAdvances = minimumAdvancesForRunLimit(
                routeEdges, maxSamePhaseCells);
            const int satIi = std::max(
                4, roundUpToMultiple(minimumAdvances, 4));
            const int satLatency = satIi - minimumAdvances;

            struct EdgeFixture
            {
                const char *name;
                const char *expectedStatus;
                int ii;
                int stateLatency;
                std::uint64_t dfsBudget;
            };
            // SAT is chosen on the tight route-local run-length boundary.
            // UNSAT asks E binary route edges to realize E+1 advances and is
            // therefore an exact cardinality contradiction.  LIMIT reuses
            // the feasible SAT instance with an intentionally one-node DFS
            // budget so that budget exhaustion is exercised independently
            // from an infeasibility proof.
            const std::vector<EdgeFixture> fixtures{
                {"tight_sat", "SAT", satIi, satLatency, 5000000},
                {"cardinality_unsat", "UNSAT", routeEdges + 4, 3,
                 5000000},
                {"budget_limit", "LIMIT", satIi, satLatency, 1}
            };

            for (const auto &fixture : fixtures)
            {
                GlobalClockProblem problem = makeParallelRecurrenceProblem(
                    1, routeEdges, fixture.ii, fixture.stateLatency,
                    maxSamePhaseCells);
                problem.maxDfsNodes = fixture.dfsBudget;
                const bool exactOracle = exactRecurrenceOracle(
                    routeEdges, fixture.ii, fixture.stateLatency,
                    maxSamePhaseCells);
                std::vector<double> times;
                std::vector<double> dfsNodes;
                int satRuns = 0;
                int unsatRuns = 0;
                int limitRuns = 0;
                int invalidRuns = 0;
                for (int repetition = 0;
                     repetition < command.repetitions; ++repetition)
                {
                    const TimedSolve timed = solveTimed(problem);
                    const std::string actualStatus =
                        statusName(timed.result.status);
                    const bool internalValidator = validateSatSolution(
                        problem, timed.result);
                    if (actualStatus != fixture.expectedStatus ||
                        !internalValidator)
                    {
                        throw std::runtime_error(
                            "edge-scaling fixture returned an unexpected "
                            "status or invalid SAT witness");
                    }
                    satRuns += timed.result.status ==
                        GlobalClockSolveStatus::Sat ? 1 : 0;
                    unsatRuns += timed.result.status ==
                        GlobalClockSolveStatus::Unsat ? 1 : 0;
                    limitRuns += timed.result.status ==
                        GlobalClockSolveStatus::Limit ? 1 : 0;
                    invalidRuns += timed.result.status ==
                        GlobalClockSolveStatus::InvalidInput ? 1 : 0;
                    times.push_back(timed.microseconds);
                    dfsNodes.push_back(static_cast<double>(
                        timed.result.stats.dfsNodes));
                    edgeScalingCsv
                        << routeEdges << ',' << fixture.name << ','
                        << fixture.expectedStatus << ',' << fixture.ii << ','
                        << fixture.stateLatency << ','
                        << maxSamePhaseCells << ',' << fixture.dfsBudget << ','
                        << (exactOracle ? 1 : 0) << ',' << repetition << ','
                        << actualStatus << ','
                        << (internalValidator ? 1 : 0) << ','
                        << timed.microseconds << ','
                        << timed.result.stats.dfsNodes << ','
                        << timed.result.stats.decisions << ','
                        << timed.result.stats.forcedEdges << ','
                        << timed.result.stats.conflicts << '\n';
                }
                edgeScalingSummaries.push_back(EdgeScalingSummary{
                    routeEdges,
                    fixture.expectedStatus,
                    fixture.ii,
                    fixture.stateLatency,
                    fixture.dfsBudget,
                    satRuns,
                    unsatRuns,
                    limitRuns,
                    invalidRuns,
                    percentile(times, 0.50),
                    percentile(times, 0.95),
                    percentile(dfsNodes, 0.50)});
            }
        }

        struct MacroFactory
        {
            std::string name;
            std::function<PhysicalStateMacro()> make;
        };
        const std::vector<MacroFactory> factories{
            {"t1_toggle", [] { return makePhysicalT1ToggleMacro({4, 4}); }},
            {"reset_toggle", [] {
                 return makePhysicalResetToggleMacro({4, 4});
             }},
            {"johnson2_reset", [] {
                 return makePhysicalResetJohnson2Macro({4, 4});
             }},
            {"johnson4", [] { return makePhysicalJohnson4Macro({4, 4}); }}
        };

        std::ofstream macroCsv(
            command.outputDirectory / "physical_macro_runs.csv");
        if (!macroCsv)
        {
            throw std::runtime_error("cannot create physical_macro_runs.csv");
        }
        macroCsv
            << "macro,state_bits,nodes,nets,capture_nets,ii,qca_cells,"
               "repetition,build_time_us,mapping_time_us,structural_valid,"
               "mapping_valid\n";
        macroCsv << std::fixed << std::setprecision(3);

        std::vector<MacroSummary> macroSummaries;
        for (const auto &factory : factories)
        {
            std::vector<double> buildTimes;
            std::vector<double> mappingTimes;
            std::optional<MacroSummary> summary;
            for (int repetition = 0;
                 repetition < command.repetitions;
                 ++repetition)
            {
                const auto buildStart = Clock::now();
                const PhysicalStateMacro macro = factory.make();
                const auto buildStop = Clock::now();
                const PhysicalStateValidationResult validation =
                    validatePhysicalStateMacro(macro);
                const auto mappingStart = Clock::now();
                const PhysicalStateMappingResult mapping =
                    validatePhysicalStateMapping(macro);
                const auto mappingStop = Clock::now();
                if (!validation.valid || !mapping.valid)
                {
                    throw std::runtime_error(
                        "physical macro failed structural validation: " +
                        factory.name);
                }

                const double buildMicroseconds =
                    std::chrono::duration<double, std::micro>(
                        buildStop - buildStart).count();
                const double mappingMicroseconds =
                    std::chrono::duration<double, std::micro>(
                        mappingStop - mappingStart).count();
                buildTimes.push_back(buildMicroseconds);
                mappingTimes.push_back(mappingMicroseconds);

                int captures = 0;
                for (const auto &net : macro.nets)
                {
                    captures += net.iterationDistance == 1 ? 1 : 0;
                }
                if (!summary.has_value())
                {
                    summary = MacroSummary{
                        factory.name,
                        static_cast<int>(macro.stateNodes.size()),
                        static_cast<int>(macro.nodes.size()),
                        static_cast<int>(macro.nets.size()),
                        captures,
                        macro.initiationInterval,
                        mapping.uniqueQcaCells};
                }
                macroCsv
                    << factory.name << ',' << macro.stateNodes.size() << ','
                    << macro.nodes.size() << ',' << macro.nets.size() << ','
                    << captures << ',' << macro.initiationInterval << ','
                    << mapping.uniqueQcaCells << ',' << repetition << ','
                    << buildMicroseconds << ',' << mappingMicroseconds
                    << ",1,1\n";
            }
            summary->medianBuildMicroseconds = percentile(buildTimes, 0.50);
            summary->p95BuildMicroseconds = percentile(buildTimes, 0.95);
            summary->medianMappingMicroseconds =
                percentile(mappingTimes, 0.50);
            summary->p95MappingMicroseconds =
                percentile(mappingTimes, 0.95);
            macroSummaries.push_back(summary.value());
        }

        const double falseAcceptRate = exactUnsat == 0
            ? 0.0
            : static_cast<double>(falseAccept) /
                  static_cast<double>(exactUnsat);
        const double sweepP50 = percentile(sweepTimes, 0.50);
        const double sweepP95 = percentile(sweepTimes, 0.95);
        const double strictFalseAcceptRate = strictExactUnsat == 0
            ? 0.0
            : static_cast<double>(strictModuloFalseAccept) /
                  static_cast<double>(strictExactUnsat);

        std::ofstream summaryJson(command.outputDirectory / "summary.json");
        if (!summaryJson)
        {
            throw std::runtime_error("cannot create summary.json");
        }
        summaryJson << std::fixed << std::setprecision(6);
        summaryJson
            << "{\n"
            << "  \"schema\": \"ifcn.sequential_clock_experiment.v2\",\n"
            << "  \"configuration\": {\"phase_count\": 4, "
               "\"repetitions\": " << command.repetitions
            << ", \"sweep_max_route_edges\": 12, "
               "\"bounded_total_route_edges\": 1024},\n"
            << "  \"correctness\": {\n"
            << "    \"cases\": " << sweepCases << ",\n"
            << "    \"exact_sat\": " << exactSat << ",\n"
            << "    \"exact_unsat\": " << exactUnsat << ",\n"
            << "    \"modulo_sat\": " << moduloSat << ",\n"
            << "    \"modulo_false_accept\": " << falseAccept << ",\n"
            << "    \"modulo_false_reject\": " << falseReject << ",\n"
            << "    \"false_accept_rate_among_exact_unsat\": "
            << falseAcceptRate << ",\n"
            << "    \"solver_oracle_mismatches\": " << solverMismatch
            << ",\n"
            << "    \"solver_limits\": " << solverLimit << ",\n"
            << "    \"solver_time_us_p50\": " << sweepP50 << ",\n"
            << "    \"solver_time_us_p95\": " << sweepP95 << "\n"
            << "  },\n"
            << "  \"recent_paper_model_comparison\": {\n"
            << "    \"reference\": \"Walter et al., IEEE NANO 2024\",\n"
            << "    \"comparison_type\": \"constraint-level reproduction; "
               "not implementation-runtime comparison\",\n"
            << "    \"family\": \"strict next-phase cyclic routes\",\n"
            << "    \"cases\": " << strictCases << ",\n"
            << "    \"exact_sat\": " << strictExactSat << ",\n"
            << "    \"exact_unsat\": " << strictExactUnsat << ",\n"
            << "    \"local_modulo_false_accept\": "
            << strictModuloFalseAccept << ",\n"
            << "    \"local_modulo_false_reject\": "
            << strictModuloFalseReject << ",\n"
            << "    \"false_accept_rate_among_exact_unsat\": "
            << strictFalseAcceptRate << "\n"
            << "  },\n"
            << "  \"ii_ablation\": {\n"
            << "    \"cases\": " << iiAblationCases << ",\n"
            << "    \"adaptive_candidates\": [4, 8, 12, 16],\n"
            << "    \"fixed_ii\": 4,\n"
            << "    \"adaptive_sat\": " << adaptiveSat << ",\n"
            << "    \"fixed_ii4_sat\": " << fixedIi4Sat << ",\n"
            << "    \"adaptive_only_sat\": " << adaptiveOnlySat << ",\n"
            << "    \"fixed_only_sat\": " << fixedOnlySat << ",\n"
            << "    \"solver_oracle_mismatches\": "
            << iiAblationMismatch << ",\n"
            << "    \"solver_limits\": " << iiAblationLimit << "\n"
            << "  },\n"
            << "  \"scaling\": [\n";
        for (std::size_t index = 0; index < scalingSummaries.size(); ++index)
        {
            const auto &entry = scalingSummaries[index];
            summaryJson
                << "    {\"states\": " << entry.states
                << ", \"status\": \"" << entry.expectedStatus
                << "\", \"route_edges\": " << entry.routeEdges
                << ", \"total_route_edges\": " << entry.totalRouteEdges
                << ", \"events\": " << entry.events
                << ", \"occurrences\": " << entry.occurrences
                << ", \"time_us_p50\": " << entry.medianMicroseconds
                << ", \"time_us_p95\": " << entry.p95Microseconds
                << ", \"dfs_nodes_p50\": " << entry.medianDfsNodes
                << "}" << (index + 1 == scalingSummaries.size() ? "\n" : ",\n");
        }
        summaryJson << "  ],\n  \"edge_scaling\": [\n";
        for (std::size_t index = 0;
             index < edgeScalingSummaries.size(); ++index)
        {
            const auto &entry = edgeScalingSummaries[index];
            summaryJson
                << "    {\"route_edges\": " << entry.routeEdges
                << ", \"expected_status\": \"" << entry.expectedStatus
                << "\", \"ii\": " << entry.initiationInterval
                << ", \"state_latency\": " << entry.stateLatency
                << ", \"dfs_budget\": " << entry.dfsBudget
                << ", \"sat_runs\": " << entry.satRuns
                << ", \"unsat_runs\": " << entry.unsatRuns
                << ", \"limit_runs\": " << entry.limitRuns
                << ", \"invalid_runs\": " << entry.invalidRuns
                << ", \"time_us_p50\": " << entry.medianMicroseconds
                << ", \"time_us_p95\": " << entry.p95Microseconds
                << ", \"dfs_nodes_p50\": " << entry.medianDfsNodes
                << "}"
                << (index + 1 == edgeScalingSummaries.size() ? "\n" : ",\n");
        }
        summaryJson << "  ],\n  \"physical_macros\": [\n";
        for (std::size_t index = 0; index < macroSummaries.size(); ++index)
        {
            const auto &entry = macroSummaries[index];
            summaryJson
                << "    {\"name\": \"" << entry.name
                << "\", \"state_bits\": " << entry.stateBits
                << ", \"nodes\": " << entry.nodes
                << ", \"nets\": " << entry.nets
                << ", \"capture_nets\": " << entry.captureNets
                << ", \"ii\": " << entry.initiationInterval
                << ", \"qca_cells\": " << entry.qcaCells
                << ", \"build_time_us_p50\": "
                << entry.medianBuildMicroseconds
                << ", \"mapping_time_us_p50\": "
                << entry.medianMappingMicroseconds
                << "}" << (index + 1 == macroSummaries.size() ? "\n" : ",\n");
        }
        summaryJson
            << "  ],\n"
            << "  \"scope\": \"fixed-geometry clock closure and structural "
               "QCA mapping; not generic RTL-driven sequential P&R or "
               "cell-level state-function sign-off\"\n"
            << "}\n";

        std::ofstream latex(command.outputDirectory / "preliminary_tables.tex");
        if (!latex)
        {
            throw std::runtime_error("cannot create preliminary_tables.tex");
        }
        latex << std::fixed << std::setprecision(2);
        latex
            << "% Generated by ifcn_sequential_clock_experiment.\n"
            << "\\begin{table}[t]\n"
            << "\\centering\n"
            << "\\caption{Correctness of fixed-geometry clock closure.}\n"
            << "\\begin{tabular}{lr}\n\\hline\n"
            << "Metric & Value " << "\\\\" << '\n'
            << "\\hline\n"
            << "Parameterized cases & " << sweepCases << " " << "\\\\"
            << '\n'
            << "Exact SAT / UNSAT & " << exactSat << " / " << exactUnsat
            << " " << "\\\\" << '\n'
            << "Modulo-only false accepts & " << falseAccept << " ("
            << 100.0 * falseAcceptRate << "\\%) " << "\\\\" << '\n'
            << "Exact-solver/oracle mismatches & " << solverMismatch
            << " " << "\\\\" << '\n'
            << "Solver LIMIT results & " << solverLimit << " " << "\\\\"
            << '\n'
            << "Solver time p50 / p95 ($\\mu$s) & " << sweepP50 << " / "
            << sweepP95 << " " << "\\\\" << '\n'
            << "\\hline\n\\end{tabular}\n\\end{table}\n\n"
            << "\\begin{table}[t]\n"
            << "\\centering\n"
            << "\\caption{Constraint-level comparison with the local "
               "modulo clock-number model of Walter et al. (2024).}\n"
            << "\\begin{tabular}{lr}\n\\hline\n"
            << "Metric & Value " << "\\\\" << '\n'
            << "\\hline\n"
            << "Strict next-phase cyclic cases & " << strictCases << " "
            << "\\\\" << '\n'
            << "Exact SAT / UNSAT & " << strictExactSat << " / "
            << strictExactUnsat << " " << "\\\\" << '\n'
            << "Local-modulo false accepts & " << strictModuloFalseAccept
            << " (" << 100.0 * strictFalseAcceptRate << "\\%) "
            << "\\\\" << '\n'
            << "Local-modulo false rejects & " << strictModuloFalseReject
            << " " << "\\\\" << '\n'
            << "\\hline\n\\end{tabular}\n"
            << "\\end{table}\n\n"
            << "\\begin{table}[t]\n"
            << "\\centering\n"
            << "\\caption{Initiation-interval selection ablation.}\n"
            << "\\begin{tabular}{lr}\n\\hline\n"
            << "Metric & Value " << "\\\\" << '\n'
            << "\\hline\n"
            << "Parameterized geometries & " << iiAblationCases << " "
            << "\\\\" << '\n'
            << "Adaptive II SAT & " << adaptiveSat << " " << "\\\\"
            << '\n'
            << "Fixed II=4 SAT & " << fixedIi4Sat << " " << "\\\\"
            << '\n'
            << "Recovered only by adaptive II & " << adaptiveOnlySat << " "
            << "\\\\" << '\n'
            << "Fixed-only SAT & " << fixedOnlySat << " " << "\\\\"
            << '\n'
            << "Solver/oracle mismatches & " << iiAblationMismatch << " "
            << "\\\\" << '\n'
            << "\\hline\n\\end{tabular}\n\\end{table}\n\n"
            << "\\begin{table}[t]\n"
            << "\\centering\n"
            << "\\caption{Route-edge scaling with distinct SAT, UNSAT, "
               "and LIMIT outcomes.}\n"
            << "\\begin{tabular}{lrrrrrr}\n\\hline\n"
            << "$E$ & Status & II & $L_s$ & p50 & p95 & DFS p50 "
            << "\\\\" << '\n'
            << "\\hline\n";
        for (const auto &entry : edgeScalingSummaries)
        {
            latex << entry.routeEdges << " & " << entry.expectedStatus
                  << " & " << entry.initiationInterval << " & "
                  << entry.stateLatency << " & "
                  << entry.medianMicroseconds << " & "
                  << entry.p95Microseconds << " & "
                  << entry.medianDfsNodes << " " << "\\\\" << '\n';
        }
        latex
            << "\\hline\n\\end{tabular}\n\\end{table}\n\n"
            << "\\begin{table}[t]\n"
            << "\\centering\n"
            << "\\caption{Structural mapping of sequential feedback fixtures.}\n"
            << "\\begin{tabular}{lrrrrrr}\n\\hline\n"
            << "Fixture & State & Nodes & Nets & Capt. & II & QCA cells "
            << "\\\\" << '\n'
            << "\\hline\n";
        for (const auto &entry : macroSummaries)
        {
            latex << latexMacroName(entry.name) << " & " << entry.stateBits
                  << " & " << entry.nodes << " & " << entry.nets << " & "
                  << entry.captureNets << " & " << entry.initiationInterval
                  << " & " << entry.qcaCells << " " << "\\\\" << '\n';
        }
        latex << "\\hline\n\\end{tabular}\n\\end{table}\n";

        std::cout << std::fixed << std::setprecision(3)
                  << "sequential_clock_experiment=success"
                  << " cases=" << sweepCases
                  << " exact_sat=" << exactSat
                  << " exact_unsat=" << exactUnsat
                  << " modulo_false_accept=" << falseAccept
                  << " false_accept_rate=" << falseAcceptRate
                  << " solver_mismatch=" << solverMismatch
                  << " solver_limit=" << solverLimit
                  << " ii_adaptive_only_sat=" << adaptiveOnlySat
                  << " edge_scaling_rows="
                  << edgeScalingSummaries.size()
                  << " output=" << command.outputDirectory.string() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ifcn_sequential_clock_experiment failed: "
                  << error.what() << '\n';
        return 1;
    }
}
