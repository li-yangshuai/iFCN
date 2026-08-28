#pragma once

#include "autopr/grid/grid.h"
#include "autopr/sequential/globalPhaseSolver.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fcngraph::sequential
{

// A state macro is deliberately represented as physical clock resources and
// routed nets.  It is not a D/Q scheduling boundary.  The only primary port in
// the minimal T=1 toggle below is an observation output; state is retained by
// the closed, four-phase loop.
struct PhysicalStateNode
{
    int index = -1;
    std::string id;
    std::string mappingType;
    position coordinate{0, 0};
};

struct PhysicalStateNet
{
    std::string id;
    int sourceNode = -1;
    int sinkNode = -1;
    int iterationDistance = 0;
    std::vector<position> path;
};

// One synchronous state update path.  stages contains the launch-state node,
// every integer-epoch propagation node, and the capture-state node in order.
// The last physical net has iterationDistance=1; every preceding net has
// iterationDistance=0.  Reusing a state node at the two ends is valid for a
// one-bit loop, while different endpoints model a shift/counter transition.
struct PhysicalStateTransition
{
    std::string id;
    std::vector<int> stages;
};

struct PhysicalStateMacro
{
    std::string id;
    int phaseCount = 4;
    int initiationInterval = 4;

    // stateRing is retained as the compact single-bit view used by the first
    // two fixtures.  Multi-register circuits use stateNodes/stateTransitions.
    std::vector<int> stateRing;
    std::vector<int> stateNodes;
    std::vector<PhysicalStateTransition> stateTransitions;
    std::vector<PhysicalStateNode> nodes;
    std::vector<PhysicalStateNet> nets;

    GlobalClockProblem clockProblem;
    std::optional<GlobalClockSolution> clockSolution;
    std::map<position, int> phaseByCoordinate;
};

struct PhysicalStateValidationResult
{
    bool valid = false;
    std::string message;
};

struct PhysicalStateMappingResult
{
    bool valid = false;
    std::size_t uniqueQcaCells = 0;
    std::string message;
};

// Construct the smallest library macro currently supported by iFCN's normal
// Mapping templates:
//
//   state/fanout(phi0) -> NOT(phi1) -> hold2(phi2)
//          ^                              |
//          |-------- hold3(phi3) <--------|
//
// The fanout's second branch drives q.  There is no pseudo state input
// and no D port.  The return net is a real routed net with distance one.
PhysicalStateMacro makePhysicalT1ToggleMacro(position northWest = {4, 4});

// Mapping-compatible gate-level state macro for the benchmark recurrence
// q(t+1) = !(q(t) | rst(t)).  Unlike the register-cut DAG, state.q is an
// internal fanout clock tile.  rst is the only driven input and q is a
// non-state observation sink.  Cell-level functional characterization is a
// separate sign-off step and is not implied by constructing this macro.
PhysicalStateMacro makePhysicalResetToggleMacro(
    position northWest = {4, 4});

// Two-bit synchronous Johnson counter with active-high reset:
//   q0' = !(q1 | rst), q1' = q0 & !rst.
// Both state bits launch together and capture together at II=8.
PhysicalStateMacro makePhysicalResetJohnson2Macro(
    position northWest = {4, 4});

// Four-bit Johnson counter laid out as a closed rectangular state track:
//   q0' = !q3, q1' = q0, q2' = q1, q3' = q2.
// It contains four simultaneous state launches/captures and four complete
// distance-one feedback paths, all sharing II=4.
PhysicalStateMacro makePhysicalJohnson4Macro(
    position northWest = {4, 4});

// Checks both geometry and the phase/epoch witness.  In particular, the state
// ring must be closed, four-connected, contain exactly one inverter, traverse
// all four phases, advance by exactly one epoch per edge, and return to the
// same physical state resource exactly one II later.
PhysicalStateValidationResult validatePhysicalStateMacro(
    const PhysicalStateMacro &macro);

// Exercise the existing combinational Mapping library's structural DRC on the
// complete cyclic topology.  This is intentionally separate from CircuitGraph,
// whose layering API requires a DAG.  Success does not replace cell-level
// functional characterization of the feedback storage structure.
PhysicalStateMappingResult validatePhysicalStateMapping(
    const PhysicalStateMacro &macro);

// Render the macro with CircuitGraph::printLaTex's default combinational P&R
// template.  Only the clock-tile coordinates, gate labels, and physical nets
// come from the state macro; no abstract DFF box or dashed temporal arc is
// introduced.
bool writePhysicalStateMacroLatex(const std::string &outputPath,
                                  const PhysicalStateMacro &macro,
                                  std::string *error = nullptr);

} // namespace fcngraph::sequential
