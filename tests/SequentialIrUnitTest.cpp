#include "autopr/sequential/sequentialIr.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

using namespace fcngraph::sequential;

bool hasDependency(const std::vector<SequentialDependency> &dependencies,
                   SequentialDependencyKind kind,
                   const std::string &ownerId)
{
    for (const auto &dependency : dependencies)
    {
        if (dependency.kind == kind && dependency.ownerId == ownerId)
        {
            return true;
        }
    }
    return false;
}

void toggleIsAcceptedAndStateArcIsCutOnlyFromSchedule()
{
    SequentialIR ir;
    ir.gates = {
        SequentialGate{"inv0", "not", {"inv0.a"}, {"inv0.y"}, 1}
    };
    ir.registers = {
        SequentialRegister{"reg0", "reg0.d", "reg0.q", "clk0", 0}
    };
    ir.physicalNets = {
        SequentialPhysicalNet{"q_to_inv", "reg0.q", {"inv0.a"}, 0},
        SequentialPhysicalNet{"inv_to_d", "inv0.y", {"reg0.d"}, 0}
    };

    const auto result = validateAndLayerSequentialIr(ir);
    REQUIRE(result.valid);
    REQUIRE(result.errorCode == SequentialIrErrorCode::None);
    REQUIRE(result.physicalNets.size() == 2);
    REQUIRE(result.resolvedTemporalArcs.size() == 1);
    REQUIRE(result.resolvedTemporalArcs.front().sourceEvent == "reg0.d");
    REQUIRE(result.resolvedTemporalArcs.front().sinkEvent == "reg0.q");
    REQUIRE(result.resolvedTemporalArcs.front().iterationDistance == 1);
    REQUIRE(result.fullDependencies.size() == 4);
    REQUIRE(result.scheduleDependencies.size() == 3);
    REQUIRE(hasDependency(result.fullDependencies,
                          SequentialDependencyKind::RegisterState, "reg0"));
    REQUIRE(!hasDependency(result.scheduleDependencies,
                           SequentialDependencyKind::RegisterState, "reg0"));
    REQUIRE(result.topologicalLayers.size() == 4);
    REQUIRE(result.eventLayer.at("reg0.q") == 0);
    REQUIRE(result.eventLayer.at("inv0.a") == 1);
    REQUIRE(result.eventLayer.at("inv0.y") == 2);
    REQUIRE(result.eventLayer.at("reg0.d") == 3);
}

void zeroDistanceCombinationalCycleIsRejected()
{
    SequentialIR ir;
    ir.gates = {
        SequentialGate{"g0", "buf", {"g0.a"}, {"g0.y"}, 0},
        SequentialGate{"g1", "buf", {"g1.a"}, {"g1.y"}, 0}
    };
    ir.physicalNets = {
        SequentialPhysicalNet{"g0_to_g1", "g0.y", {"g1.a"}, 0},
        SequentialPhysicalNet{"g1_to_g0", "g1.y", {"g0.a"}, 0}
    };

    const auto result = validateAndLayerSequentialIr(ir);
    REQUIRE(!result.valid);
    REQUIRE(result.errorCode == SequentialIrErrorCode::CombinationalCycle);
    REQUIRE(std::string(sequentialIrErrorCodeName(result.errorCode)) ==
            "COMBINATIONAL_CYCLE");
    REQUIRE(!result.relatedIds.empty());
    REQUIRE(result.topologicalLayers.empty());
}

void positiveDistancePhysicalFeedbackIsNeverDeleted()
{
    SequentialIR ir;
    ir.gates = {
        SequentialGate{"loop_buf", "buf", {"loop.a"}, {"loop.y"}, 0}
    };
    ir.physicalNets = {
        SequentialPhysicalNet{"feedback_route", "loop.y", {"loop.a"}, 1}
    };

    const auto result = validateAndLayerSequentialIr(ir);
    REQUIRE(result.valid);
    REQUIRE(result.physicalNets.size() == 1);
    REQUIRE(result.physicalNets.front().id == "feedback_route");
    REQUIRE(result.physicalNets.front().iterationDistance == 1);
    REQUIRE(hasDependency(result.fullDependencies,
                          SequentialDependencyKind::PhysicalNet,
                          "feedback_route"));
    REQUIRE(!hasDependency(result.scheduleDependencies,
                           SequentialDependencyKind::PhysicalNet,
                           "feedback_route"));
    REQUIRE(result.eventLayer.at("loop.a") == 0);
    REQUIRE(result.eventLayer.at("loop.y") == 1);
}

void counter2RegistersShareDeterministicTopologicalLayers()
{
    SequentialIR ir;
    ir.gates = {
        SequentialGate{"not0", "not", {"not0.a"}, {"not0.y"}, 1},
        SequentialGate{
            "xor1", "xor", {"xor1.a", "xor1.b"}, {"xor1.y"}, 1}
    };
    ir.registers = {
        SequentialRegister{"reg.q0", "reg.q0.d", "reg.q0.q", "clk0", 0},
        SequentialRegister{"reg.q1", "reg.q1.d", "reg.q1.q", "clk0", 0}
    };
    ir.physicalNets = {
        SequentialPhysicalNet{
            "q0_fanout", "reg.q0.q", {"not0.a", "xor1.b"}, 0},
        SequentialPhysicalNet{"q1_to_xor", "reg.q1.q", {"xor1.a"}, 0},
        SequentialPhysicalNet{"not_to_d0", "not0.y", {"reg.q0.d"}, 0},
        SequentialPhysicalNet{"xor_to_d1", "xor1.y", {"reg.q1.d"}, 0}
    };

    const auto result = validateAndLayerSequentialIr(ir);
    REQUIRE(result.valid);
    REQUIRE(result.resolvedTemporalArcs.size() == 2);
    REQUIRE(result.topologicalLayers.size() == 4);
    REQUIRE(result.eventLayer.at("reg.q0.q") == 0);
    REQUIRE(result.eventLayer.at("reg.q1.q") == 0);
    REQUIRE(result.eventLayer.at("not0.a") == 1);
    REQUIRE(result.eventLayer.at("xor1.a") == 1);
    REQUIRE(result.eventLayer.at("xor1.b") == 1);
    REQUIRE(result.eventLayer.at("not0.y") == 2);
    REQUIRE(result.eventLayer.at("xor1.y") == 2);
    REQUIRE(result.eventLayer.at("reg.q0.d") == 3);
    REQUIRE(result.eventLayer.at("reg.q1.d") == 3);
}

void unknownEventsAndMultipleDriversHaveStableDiagnostics()
{
    SequentialIR unknown;
    unknown.ports = {
        SequentialPort{"input0", SequentialPortDirection::Input, "input0.e"}
    };
    unknown.physicalNets = {
        SequentialPhysicalNet{"bad_net", "input0.e", {"missing.e"}, 0}
    };
    const auto unknownResult = validateAndLayerSequentialIr(unknown);
    REQUIRE(!unknownResult.valid);
    REQUIRE(unknownResult.errorCode == SequentialIrErrorCode::UnknownEvent);
    REQUIRE(std::string(sequentialIrErrorCodeName(unknownResult.errorCode)) ==
            "UNKNOWN_EVENT");

    SequentialIR multiple;
    multiple.ports = {
        SequentialPort{"a", SequentialPortDirection::Input, "a.e"},
        SequentialPort{"b", SequentialPortDirection::Input, "b.e"},
        SequentialPort{"y", SequentialPortDirection::Output, "y.e"}
    };
    multiple.physicalNets = {
        SequentialPhysicalNet{"a_to_y", "a.e", {"y.e"}, 0},
        SequentialPhysicalNet{"b_to_y", "b.e", {"y.e"}, 0}
    };
    const auto multipleResult = validateAndLayerSequentialIr(multiple);
    REQUIRE(!multipleResult.valid);
    REQUIRE(multipleResult.errorCode == SequentialIrErrorCode::MultipleDriver);
    REQUIRE(std::string(sequentialIrErrorCodeName(multipleResult.errorCode)) ==
            "MULTIPLE_DRIVER");
}

} // namespace

int main()
{
    toggleIsAcceptedAndStateArcIsCutOnlyFromSchedule();
    zeroDistanceCombinationalCycleIsRejected();
    positiveDistancePhysicalFeedbackIsNeverDeleted();
    counter2RegistersShareDeterministicTopologicalLayers();
    unknownEventsAndMultipleDriversHaveStableDiagnostics();
    std::cout << "Sequential IR validation and layering tests passed.\n";
    return 0;
}
