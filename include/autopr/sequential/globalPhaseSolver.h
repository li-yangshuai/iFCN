#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fcngraph::sequential
{

enum class ClockResourceSharing
{
    ExclusiveOrAliased,
    PhaseSharedIndependentEpochs
};

struct ClockResourceSpec
{
    std::string id;
    ClockResourceSharing sharing = ClockResourceSharing::ExclusiveOrAliased;
};

struct RouteOccurrenceSpec
{
    std::string id;
    std::string clockResource;
    // Occurrences on a same-token fanout trunk reuse this identifier and are
    // therefore constrained to the same absolute epoch.
    std::string epochVariable;
};

struct FixedRouteSpec
{
    std::string id;
    std::string sourceEvent;
    std::string sinkEvent;
    int iterationDistance = 0;
    // Includes the source and sink occurrences in data-flow order.  The
    // caller defines their granularity; sequential P&R uses ordered coarse
    // clock tiles. Shared fanout prefixes are repeated with aliased
    // epochVariable identifiers.
    std::vector<std::string> occurrences;
};

struct ExactTimingArcSpec
{
    std::string id;
    std::string sourceEvent;
    std::string sinkEvent;
    int iterationDistance = 0;
    int latencyEpochs = 0;
};

struct EpochAnchorSpec
{
    std::string event;
    int epoch = 0;
};

struct GlobalClockProblem
{
    // Bounded v0 reference-backend contract: 2..8 phases, at most 256
    // events, 4096 resources/occurrences, 128 routes, 8192 route steps, 512
    // exact arcs, and 16 II candidates.  Inputs beyond this contract return
    // InvalidInput; exhausting maxDfsNodes on an in-contract problem returns
    // Limit and is never reported as Unsat.
    int phaseCount = 4;
    // Number of consecutive same-phase route occurrences allowed.  The
    // caller defines the occurrence granularity (for the sequential P&R
    // flows it is one coarse clock tile, never one mapped QCA cell).
    // A value <= 0 disables this route-local constraint.
    int maxConsecutiveSamePhaseCells = 4;
    std::vector<int> iiCandidates{4};
    std::uint64_t maxDfsNodes = 250000;

    std::vector<std::string> events;
    std::vector<ClockResourceSpec> clockResources;
    std::vector<RouteOccurrenceSpec> occurrences;
    std::vector<FixedRouteSpec> routes;
    std::vector<ExactTimingArcSpec> timingArcs;
    std::vector<EpochAnchorSpec> anchors;
};

enum class GlobalClockSolveStatus
{
    Sat,
    Unsat,
    Limit,
    InvalidInput
};

struct GlobalClockSolveStats
{
    std::uint64_t dfsNodes = 0;
    std::uint64_t decisions = 0;
    std::uint64_t forcedEdges = 0;
    std::uint64_t conflicts = 0;
    int iiCandidatesTried = 0;
};

struct GlobalClockSolution
{
    int phaseCount = 4;
    int initiationInterval = 4;
    std::map<std::string, std::int64_t> eventEpoch;
    std::map<std::string, std::int64_t> occurrenceEpoch;
    std::map<std::string, int> clockResourcePhase;
};

struct GlobalClockSolveResult
{
    GlobalClockSolveStatus status = GlobalClockSolveStatus::InvalidInput;
    std::optional<GlobalClockSolution> solution;
    GlobalClockSolveStats stats;
    std::string message;
};

class GlobalPhaseSolver
{
public:
    explicit GlobalPhaseSolver(GlobalClockProblem problem);

    GlobalClockSolveResult solve() const;

    static bool validateSolution(const GlobalClockProblem &problem,
                                 const GlobalClockSolution &solution,
                                 std::string *error = nullptr);

private:
    GlobalClockProblem problem_;
};

} // namespace fcngraph::sequential
