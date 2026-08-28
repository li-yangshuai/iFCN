#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace fcngraph::sequential
{

enum class SequentialPortDirection
{
    Input,
    Output
};

struct SequentialPort
{
    std::string id;
    SequentialPortDirection direction = SequentialPortDirection::Input;
    std::string event;
};

struct SequentialGate
{
    std::string id;
    std::string operation;
    std::vector<std::string> inputEvents;
    std::vector<std::string> outputEvents;
    int latencyEpochs = 0;
};

struct SequentialRegister
{
    std::string id;
    std::string dataEvent;
    std::string qEvent;
    std::string clockDomain;
    int latencyEpochs = 0;
};

// Physical nets are never removed from this IR.  A positive iteration
// distance cuts only the scheduling dependency; the net remains available to
// placement, routing, export, and physical DRC.
struct SequentialPhysicalNet
{
    std::string id;
    std::string sourceEvent;
    std::vector<std::string> sinkEvents;
    int iterationDistance = 0;
};

struct SequentialTemporalArc
{
    std::string id;
    std::string sourceEvent;
    std::string sinkEvent;
    int iterationDistance = 0;
    int latencyEpochs = 0;
};

struct SequentialIR
{
    std::vector<SequentialPort> ports;
    std::vector<SequentialGate> gates;
    std::vector<SequentialRegister> registers;
    std::vector<SequentialPhysicalNet> physicalNets;
    std::vector<SequentialTemporalArc> temporalArcs;
};

enum class SequentialDependencyKind
{
    Gate,
    PhysicalNet,
    TemporalArc,
    RegisterState
};

struct SequentialDependency
{
    // A stable, derived branch identifier.  ownerId identifies the gate, net,
    // explicit temporal arc, or register that produced this dependency.
    std::string id;
    std::string ownerId;
    std::string sourceEvent;
    std::string sinkEvent;
    int iterationDistance = 0;
    int latencyEpochs = 0;
    SequentialDependencyKind kind = SequentialDependencyKind::PhysicalNet;
};

enum class SequentialIrErrorCode
{
    None,
    EmptyDesign,
    EmptyIdentifier,
    DuplicateIdentifier,
    DuplicateEvent,
    InvalidComponent,
    InvalidIterationDistance,
    InvalidLatency,
    UnknownEvent,
    MultipleDriver,
    CombinationalCycle
};

const char *sequentialIrErrorCodeName(SequentialIrErrorCode code) noexcept;

struct SequentialIrValidationResult
{
    bool valid = false;
    SequentialIrErrorCode errorCode = SequentialIrErrorCode::None;
    std::string message;
    std::vector<std::string> relatedIds;

    // fullDependencies contains every logical dependency.  scheduleDependencies
    // is the d=0 view consumed by DAG placement and layering.
    std::vector<SequentialDependency> fullDependencies;
    std::vector<SequentialDependency> scheduleDependencies;
    std::vector<SequentialTemporalArc> resolvedTemporalArcs;

    // Kept as a value copy so callers cannot accidentally confuse a scheduling
    // cut with deletion of the corresponding routed net.
    std::vector<SequentialPhysicalNet> physicalNets;

    std::vector<std::vector<std::string>> topologicalLayers;
    std::map<std::string, std::size_t> eventLayer;
};

SequentialIrValidationResult validateAndLayerSequentialIr(
    const SequentialIR &ir);

} // namespace fcngraph::sequential
