#include "autopr/algorithms/astarwithphase.h"
#include "autopr/algorithms/combinationalClock.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>

namespace {

bool isForwardPhaseStep(int from, int to, int phaseCount)
{
    return to == from || to == (from % phaseCount) + 1;
}

void assertLegalRoute(const fcngraph::PhaseAwareRoute &route,
                      int phaseCount,
                      int maxSamePhase)
{
    assert(route.positions.size() == route.phases.size());
    assert(route.positions.size() >= 2);
    int run = 1;
    for (std::size_t index = 1; index < route.positions.size(); ++index) {
        const int distance =
            std::abs(static_cast<int>(route.positions[index].first) -
                     static_cast<int>(route.positions[index - 1].first)) +
            std::abs(static_cast<int>(route.positions[index].second) -
                     static_cast<int>(route.positions[index - 1].second));
        assert(distance == 1);
        assert(isForwardPhaseStep(route.phases[index - 1],
                                  route.phases[index],
                                  phaseCount));
        run = route.phases[index] == route.phases[index - 1] ? run + 1 : 1;
        assert(run <= maxSamePhase);
    }
}

void routesAndAssignsPhasesInOneSearch()
{
    fcngraph::GridChessboard board;
    const fcngraph::position source{4, 5};
    const fcngraph::position target{12, 9};
    board.addNodeCell(source);
    board.addNodeCell(target);

    fcngraph::PhaseAwareAstar router(board, 4, 3, 80.0);
    const auto route = router.findPath(source, target, 2, false);
    assert(route.has_value());
    assertLegalRoute(route.value(), 4, 3);
    for (std::size_t index = 0; index < route->positions.size(); ++index) {
        const auto cell = board.gridMap.find(route->positions[index]);
        assert(cell != board.gridMap.end());
        assert(cell->second.getPhase() == route->phases[index]);
    }
}

void keepsMultipleFaninsOnDifferentGateSides()
{
    fcngraph::GridChessboard board;
    const fcngraph::position upperSource{4, 4};
    const fcngraph::position lowerSource{4, 12};
    const fcngraph::position gate{12, 8};
    board.addNodeCell(upperSource);
    board.addNodeCell(lowerSource);
    board.addNodeCell(gate);

    fcngraph::PhaseAwareAstar router(board, 4, 4, 120.0);
    const auto upperRoute = router.findPath(upperSource, gate, 1, false);
    const auto lowerRoute = router.findPath(lowerSource, gate, 3, false);
    assert(upperRoute.has_value());
    assert(lowerRoute.has_value());
    assertLegalRoute(upperRoute.value(), 4, 4);
    assertLegalRoute(lowerRoute.value(), 4, 4);
    assert(upperRoute->positions[upperRoute->positions.size() - 2] !=
           lowerRoute->positions[lowerRoute->positions.size() - 2]);
}

void keepsInterSourceCrossingsSingleAndOrthogonal()
{
    fcngraph::GridChessboard board;
    const fcngraph::position left{4, 12};
    const fcngraph::position right{20, 12};
    const fcngraph::position top{12, 4};
    const fcngraph::position bottom{12, 20};
    board.addNodeCell(left);
    board.addNodeCell(right);
    board.addNodeCell(top);
    board.addNodeCell(bottom);

    fcngraph::PhaseAwareAstar router(board, 4, 4, 160.0);
    const auto horizontal = router.findPath(left, right, 1, false);
    const auto vertical = router.findPath(top, bottom, 1, false);
    assert(horizontal.has_value());
    assert(vertical.has_value());

    std::set<fcngraph::position> horizontalInterior(
        std::next(horizontal->positions.begin()),
        std::prev(horizontal->positions.end()));
    std::vector<fcngraph::position> shared;
    for (auto iter = std::next(vertical->positions.begin());
         iter != std::prev(vertical->positions.end()); ++iter) {
        if (horizontalInterior.count(*iter) != 0) {
            shared.push_back(*iter);
        }
    }
    assert(shared.size() == 1);

    const auto isStraightAt = [](const std::vector<fcngraph::position> &path,
                                 const fcngraph::position &cross,
                                 bool horizontalDirection) {
        const auto found = std::find(path.begin(), path.end(), cross);
        assert(found != path.begin() && std::next(found) != path.end());
        return horizontalDirection
            ? std::prev(found)->second == cross.second &&
                  std::next(found)->second == cross.second
            : std::prev(found)->first == cross.first &&
                  std::next(found)->first == cross.first;
    };
    assert(isStraightAt(horizontal->positions, shared.front(), true));
    assert(isStraightAt(vertical->positions, shared.front(), false));
}

void rejectsFullCycleFaninMismatchAndRepairsTheSameGeometry()
{
    fcngraph::CombinationalClockProblem problem;
    problem.tileCount = 8;
    problem.primaryInputs = {0, 1};
    problem.routes = {{0, 2, 3, 4, 5, 7}, {1, 6, 7}};
    // Both paths pass local 0/+1 modulo-phase checks, but reconverge after
    // five and one advances respectively: the same gate sees different tokens.
    std::vector<int> phases{1, 1, 2, 3, 4, 1, 1, 2};
    std::vector<std::int64_t> epochs;
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs));
    assert(epochs[0] == 0 && epochs[1] == 0 && epochs[7] >= 1);
    for (std::size_t i = 0; i < phases.size(); ++i)
        phases[i] = static_cast<int>(epochs[i] % 4) + 1;
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));
}

void isolatesDrivenLogicOutputsWithoutChangingPrimaryInputLaunch()
{
    fcngraph::CombinationalClockProblem problem;
    problem.tileCount = 6;
    problem.primaryInputs = {0};
    // The first route has a clamped PI; the second starts at a driven gate.
    problem.routes = {{0, 1, 2}, {2, 3, 4, 5}};
    std::vector<int> phases{1, 1, 2, 2, 2, 3};
    std::vector<std::int64_t> epochs;
    std::string error;
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs, &error, &phases));
    assert(error.find("output port") != std::string::npos);
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs));
    assert(epochs[0] == 0 && epochs[1] == 0);
    assert(epochs[2] - epochs[1] == 1); // receiving gate input isolation
    assert(epochs[3] - epochs[2] == 1); // driven gate output isolation
    assert(epochs[5] - epochs[4] == 1);
    for (std::size_t i = 0; i < phases.size(); ++i)
        phases[i] = static_cast<int>(epochs[i] % 4) + 1;
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));
}

void requiresLogicAdvancesSynchronizedInputsAndBoundedHolds()
{
    fcngraph::CombinationalClockProblem problem;
    std::vector<std::int64_t> epochs;
    problem.tileCount = 2;
    problem.primaryInputs = {0};
    problem.routes = {{0, 1}};
    std::vector<int> phases{1, 1};
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs));
    assert(epochs[1] == 1);
    problem.primaryInputs = {0, 1};
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs));

    // A nonzero total route delay is insufficient: the final gate port
    // must be in the preceding clock phase, not the switching gate phase.
    problem.tileCount = 3;
    problem.primaryInputs = {0};
    problem.routes = {{0, 1, 2}};
    phases = {1, 2, 2};
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs));
    assert(epochs[2] - epochs[1] == 1);

    problem.tileCount = 4;
    problem.primaryInputs = {0, 1};
    problem.routes = {{0, 3, 2}, {1, 2}};
    phases = {1, 2, 3, 2};
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));

    problem.tileCount = 6;
    problem.primaryInputs = {0};
    problem.routes = {{0, 1, 2, 3, 4, 5}};
    phases = {1, 1, 1, 1, 1, 2};
    assert(!fcngraph::solveCombinationalClockEpochs(problem, epochs, nullptr, &phases));
    assert(fcngraph::solveCombinationalClockEpochs(problem, epochs));
    assert(epochs[4] - epochs[0] >= 1);
    assert(epochs[5] - epochs[1] >= 1);
}

} // namespace

int main()
{
    rejectsFullCycleFaninMismatchAndRepairsTheSameGeometry();
    requiresLogicAdvancesSynchronizedInputsAndBoundedHolds();
    isolatesDrivenLogicOutputsWithoutChangingPrimaryInputLaunch();
    routesAndAssignsPhasesInOneSearch();
    keepsMultipleFaninsOnDifferentGateSides();
    keepsInterSourceCrossingsSingleAndOrthogonal();
    std::cout << "Phase-aware stochastic router tests passed.\n";
    return 0;
}
