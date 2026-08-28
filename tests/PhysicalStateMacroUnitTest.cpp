#include "autopr/sequential/physicalStateMacro.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

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

std::size_t countText(const std::string &text, const std::string &needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string::npos)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}

} // namespace

int main(int argc, char **argv)
{
    using namespace fcngraph::sequential;

    const PhysicalStateMacro macro = makePhysicalT1ToggleMacro({4, 4});
    const PhysicalStateValidationResult validation =
        validatePhysicalStateMacro(macro);
    REQUIRE(validation.valid);
    REQUIRE(macro.nodes.size() == 5);
    REQUIRE(macro.nets.size() == 5);
    REQUIRE(macro.stateRing.size() == 4);
    REQUIRE(macro.clockSolution.has_value());
    REQUIRE(macro.clockSolution->initiationInterval == 4);

    int inputNodes = 0;
    int inverterNodes = 0;
    int feedbackNets = 0;
    for (const auto &node : macro.nodes)
    {
        inputNodes += node.mappingType == "input" ? 1 : 0;
        inverterNodes += node.mappingType == "not" ? 1 : 0;
    }
    for (const auto &net : macro.nets)
    {
        feedbackNets += net.iterationDistance == 1 ? 1 : 0;
    }
    REQUIRE(inputNodes == 0);
    REQUIRE(inverterNodes == 1);
    REQUIRE(feedbackNets == 1);

    std::set<int> phases;
    for (const int nodeIndex : macro.stateRing)
    {
        for (const auto &node : macro.nodes)
        {
            if (node.index == nodeIndex)
            {
                phases.insert(macro.phaseByCoordinate.at(node.coordinate));
            }
        }
    }
    REQUIRE(phases == std::set<int>({0, 1, 2, 3}));

    const PhysicalStateMappingResult mapping =
        validatePhysicalStateMapping(macro);
    REQUIRE(mapping.valid);
    REQUIRE(mapping.uniqueQcaCells > 0);

    // Translation must not change the timing witness or mapped topology.
    const PhysicalStateMacro translated =
        makePhysicalT1ToggleMacro({80, 120});
    REQUIRE(validatePhysicalStateMacro(translated).valid);
    const PhysicalStateMappingResult translatedMapping =
        validatePhysicalStateMapping(translated);
    REQUIRE(translatedMapping.valid);
    REQUIRE(translatedMapping.uniqueQcaCells == mapping.uniqueQcaCells);

    PhysicalStateMacro abstractBoundary = macro;
    abstractBoundary.nets[3].path.clear();
    REQUIRE(!validatePhysicalStateMacro(abstractBoundary).valid);

    PhysicalStateMacro missingFeedback = macro;
    missingFeedback.nets[3].iterationDistance = 0;
    REQUIRE(!validatePhysicalStateMacro(missingFeedback).valid);

    PhysicalStateMacro pseudoInput = macro;
    pseudoInput.nodes.front().mappingType = "input";
    REQUIRE(!validatePhysicalStateMacro(pseudoInput).valid);

    PhysicalStateMacro collapsedPhase = macro;
    collapsedPhase.phaseByCoordinate[collapsedPhase.nodes[2].coordinate] = 1;
    REQUIRE(!validatePhysicalStateMacro(collapsedPhase).valid);

    PhysicalStateMacro duplicateNet = macro;
    duplicateNet.nets.push_back(duplicateNet.nets.front());
    REQUIRE(!validatePhysicalStateMacro(duplicateNet).valid);

    const PhysicalStateMacro resetMacro =
        makePhysicalResetToggleMacro({4, 4});
    REQUIRE(validatePhysicalStateMacro(resetMacro).valid);
    REQUIRE(resetMacro.nodes.size() == 6);
    REQUIRE(resetMacro.nets.size() == 6);
    REQUIRE(resetMacro.stateRing == std::vector<int>({0, 2, 3, 4}));
    REQUIRE(resetMacro.clockSolution.has_value());
    REQUIRE(resetMacro.clockSolution->initiationInterval == 4);
    REQUIRE(resetMacro.clockSolution->eventEpoch.at("state.q") == 0);
    REQUIRE(resetMacro.clockSolution->eventEpoch.at("rst") == 0);
    REQUIRE(resetMacro.clockSolution->eventEpoch.at("next.or") == 1);
    REQUIRE(resetMacro.clockSolution->eventEpoch.at("next.not") == 2);
    REQUIRE(resetMacro.clockSolution->eventEpoch.at("state.hold") == 3);
    REQUIRE(resetMacro.clockSolution->eventEpoch.at("q") == 1);

    int resetInputs = 0;
    int resetOutputs = 0;
    for (const auto &node : resetMacro.nodes)
    {
        if (node.mappingType == "input")
        {
            ++resetInputs;
            REQUIRE(node.id == "rst");
        }
        if (node.mappingType == "output")
        {
            ++resetOutputs;
            REQUIRE(node.id == "q");
        }
    }
    REQUIRE(resetInputs == 1);
    REQUIRE(resetOutputs == 1);
    REQUIRE(resetMacro.nodes.front().id == "state.q");
    REQUIRE(resetMacro.nodes.front().mappingType == "fanout");
    std::set<std::tuple<int, int, int>> resetTopology;
    for (const auto &net : resetMacro.nets)
    {
        resetTopology.emplace(
            net.sourceNode, net.sinkNode, net.iterationDistance);
    }
    const std::set<std::tuple<int, int, int>> expectedResetTopology{
        {0, 2, 0}, {1, 2, 0}, {2, 3, 0},
        {3, 4, 0}, {4, 0, 1}, {0, 5, 0}};
    REQUIRE(resetTopology == expectedResetTopology);
    const auto nextState = [](bool q, bool rst) { return !(q || rst); };
    REQUIRE(nextState(false, false));
    REQUIRE(!nextState(true, false));
    REQUIRE(!nextState(false, true));
    REQUIRE(!nextState(true, true));
    std::set<int> resetRingPhases;
    for (const int nodeIndex : resetMacro.stateRing)
    {
        for (const auto &node : resetMacro.nodes)
        {
            if (node.index == nodeIndex)
            {
                resetRingPhases.insert(
                    resetMacro.phaseByCoordinate.at(node.coordinate));
            }
        }
    }
    REQUIRE(resetRingPhases == std::set<int>({0, 1, 2, 3}));
    const PhysicalStateMappingResult resetMapping =
        validatePhysicalStateMapping(resetMacro);
    REQUIRE(resetMapping.valid);
    REQUIRE(resetMapping.uniqueQcaCells > 0);

    const PhysicalStateMacro johnson2 =
        makePhysicalResetJohnson2Macro({4, 4});
    REQUIRE(validatePhysicalStateMacro(johnson2).valid);
    REQUIRE(johnson2.nodes.size() == 20);
    REQUIRE(johnson2.nets.size() == 21);
    REQUIRE(johnson2.stateNodes == std::vector<int>({0, 1}));
    REQUIRE(johnson2.stateTransitions.size() == 2);
    REQUIRE(johnson2.clockSolution.has_value());
    REQUIRE(johnson2.clockSolution->initiationInterval == 8);
    REQUIRE(johnson2.clockSolution->eventEpoch.at("state.q0") == 0);
    REQUIRE(johnson2.clockSolution->eventEpoch.at("state.q1") == 0);
    REQUIRE(johnson2.clockSolution->eventEpoch.at("rst") == 0);
    int johnson2Captures = 0;
    for (const auto &net : johnson2.nets)
    {
        johnson2Captures += net.iterationDistance == 1 ? 1 : 0;
    }
    REQUIRE(johnson2Captures == 2);
    const auto nextJohnson2 = [](unsigned int state, bool reset) {
        if (reset)
        {
            return 0U;
        }
        const unsigned int q0 = state & 1U;
        const unsigned int q1 = (state >> 1U) & 1U;
        return (q0 << 1U) | (q1 ^ 1U);
    };
    unsigned int johnson2State = 0;
    const std::vector<unsigned int> johnson2Expected{1, 3, 2, 0};
    for (const unsigned int expected : johnson2Expected)
    {
        johnson2State = nextJohnson2(johnson2State, false);
        REQUIRE(johnson2State == expected);
    }
    REQUIRE(nextJohnson2(3, true) == 0);
    const PhysicalStateMappingResult johnson2Mapping =
        validatePhysicalStateMapping(johnson2);
    REQUIRE(johnson2Mapping.valid);
    REQUIRE(johnson2Mapping.uniqueQcaCells > 0);

    const PhysicalStateMacro johnson4 = makePhysicalJohnson4Macro({4, 4});
    REQUIRE(validatePhysicalStateMacro(johnson4).valid);
    REQUIRE(johnson4.nodes.size() == 20);
    REQUIRE(johnson4.nets.size() == 20);
    REQUIRE(johnson4.stateNodes == std::vector<int>({0, 1, 2, 3}));
    REQUIRE(johnson4.stateTransitions.size() == 4);
    REQUIRE(johnson4.clockSolution.has_value());
    REQUIRE(johnson4.clockSolution->initiationInterval == 4);
    int johnson4Captures = 0;
    for (const auto &net : johnson4.nets)
    {
        johnson4Captures += net.iterationDistance == 1 ? 1 : 0;
    }
    REQUIRE(johnson4Captures == 4);
    const auto nextJohnson4 = [](unsigned int state) {
        return ((state << 1U) & 0xEU) |
               ((((state >> 3U) & 1U) ^ 1U) & 1U);
    };
    std::set<unsigned int> johnson4Cycle;
    unsigned int johnson4State = 0;
    for (int tick = 0; tick < 8; ++tick)
    {
        REQUIRE(johnson4Cycle.insert(johnson4State).second);
        johnson4State = nextJohnson4(johnson4State);
    }
    REQUIRE(johnson4State == 0);
    REQUIRE(johnson4Cycle.size() == 8);
    const PhysicalStateMappingResult johnson4Mapping =
        validatePhysicalStateMapping(johnson4);
    REQUIRE(johnson4Mapping.valid);
    REQUIRE(johnson4Mapping.uniqueQcaCells > 0);

    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    const bool keepLatex = argc > 1;
    const std::filesystem::path latexPath = keepLatex
        ? std::filesystem::path(argv[1])
        : std::filesystem::temp_directory_path() /
              ("ifcn_physical_t1_" + std::to_string(nonce) + ".tex");
    if (!latexPath.parent_path().empty())
    {
        std::filesystem::create_directories(latexPath.parent_path());
    }
    std::string latexError;
    REQUIRE(writePhysicalStateMacroLatex(
        latexPath.string(), resetMacro, &latexError));
    std::ifstream latexInput(latexPath);
    std::ostringstream latexBuffer;
    latexBuffer << latexInput.rdbuf();
    const std::string latex = latexBuffer.str();
    REQUIRE(countText(latex, "\\node(") == 6);
    REQUIRE(countText(latex, ") [v]") == 6);
    REQUIRE(countText(latex, "\\draw[route]") == 6);
    REQUIRE(latex.find("\\node[c1]") != std::string::npos);
    REQUIRE(latex.find("\\node[c2]") != std::string::npos);
    REQUIRE(latex.find("\\node[c3]") != std::string::npos);
    REQUIRE(latex.find("\\node[c4]") != std::string::npos);
    REQUIRE(latex.find("\\draw[route] (4) -- (0);") !=
            std::string::npos);
    REQUIRE(latex.find("{rst};") != std::string::npos);
    REQUIRE(latex.find("{q};") != std::string::npos);
    REQUIRE(latex.find("DFF") == std::string::npos);
    REQUIRE(latex.find("D(t)") == std::string::npos);
    REQUIRE(latex.find("Q(t+1)") == std::string::npos);
    REQUIRE(latex.find("statearc") == std::string::npos);
    if (!keepLatex)
    {
        std::filesystem::remove(latexPath);
    }

    for (const auto &fixture : std::vector<std::pair<PhysicalStateMacro,
                                                     std::pair<int, int>>>{
             {johnson2, {20, 21}}, {johnson4, {20, 20}}})
    {
        const std::filesystem::path complexLatexPath =
            std::filesystem::temp_directory_path() /
            (fixture.first.id + "_" + std::to_string(nonce) + ".tex");
        REQUIRE(writePhysicalStateMacroLatex(
            complexLatexPath.string(), fixture.first, &latexError));
        std::ifstream complexInput(complexLatexPath);
        std::ostringstream complexBuffer;
        complexBuffer << complexInput.rdbuf();
        const std::string complexLatex = complexBuffer.str();
        REQUIRE(countText(complexLatex, ") [v]") ==
                static_cast<std::size_t>(fixture.second.first));
        REQUIRE(countText(complexLatex, "\\draw[route]") ==
                static_cast<std::size_t>(fixture.second.second));
        REQUIRE(complexLatex.find("xiaonode") == std::string::npos);
        std::filesystem::remove(complexLatexPath);
    }

    std::cout << "physical_t1_state_macro=valid"
              << " ring_stages=" << macro.stateRing.size()
              << " phases=0,1,2,3"
              << " II=" << macro.initiationInterval
              << " mapped_qca_cells=" << mapping.uniqueQcaCells
              << " reset_toggle_mapped_qca_cells="
              << resetMapping.uniqueQcaCells
              << " johnson2_mapped_qca_cells="
              << johnson2Mapping.uniqueQcaCells
              << " johnson4_mapped_qca_cells="
              << johnson4Mapping.uniqueQcaCells << '\n';
    return 0;
}
