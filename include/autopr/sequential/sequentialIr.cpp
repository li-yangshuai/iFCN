#include "autopr/sequential/sequentialIr.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace fcngraph::sequential
{
namespace
{

SequentialIrValidationResult fail(SequentialIrValidationResult result,
                                  SequentialIrErrorCode code,
                                  std::string message,
                                  std::vector<std::string> relatedIds = {})
{
    result.valid = false;
    result.errorCode = code;
    result.message = std::move(message);
    std::sort(relatedIds.begin(), relatedIds.end());
    relatedIds.erase(std::unique(relatedIds.begin(), relatedIds.end()),
                     relatedIds.end());
    result.relatedIds = std::move(relatedIds);
    return result;
}

bool insertNonEmptyIdentifier(std::set<std::string> &identifiers,
                              const std::string &identifier,
                              SequentialIrValidationResult &result,
                              const std::string &kind)
{
    if (identifier.empty())
    {
        result = fail(std::move(result),
                      SequentialIrErrorCode::EmptyIdentifier,
                      kind + " identifier is empty");
        return false;
    }
    if (!identifiers.insert(identifier).second)
    {
        result = fail(std::move(result),
                      SequentialIrErrorCode::DuplicateIdentifier,
                      "duplicate " + kind + " identifier: " + identifier,
                      {identifier});
        return false;
    }
    return true;
}

bool declareEvent(std::map<std::string, std::string> &eventOwners,
                  const std::string &event,
                  const std::string &owner,
                  SequentialIrValidationResult &result)
{
    if (event.empty())
    {
        result = fail(std::move(result),
                      SequentialIrErrorCode::EmptyIdentifier,
                      "event identifier is empty at " + owner,
                      {owner});
        return false;
    }
    const auto [position, inserted] = eventOwners.emplace(event, owner);
    if (!inserted)
    {
        result = fail(
            std::move(result), SequentialIrErrorCode::DuplicateEvent,
            "event " + event + " is declared by both " + position->second +
                " and " + owner,
            {event, position->second, owner});
        return false;
    }
    return true;
}

std::vector<std::string> findCyclicComponent(
    const std::map<std::string, std::vector<std::string>> &adjacency)
{
    std::map<std::string, int> index;
    std::map<std::string, int> lowlink;
    std::vector<std::string> stack;
    std::set<std::string> onStack;
    std::vector<std::vector<std::string>> cyclicComponents;
    int nextIndex = 0;

    std::function<void(const std::string &)> visit =
        [&](const std::string &event) {
            index[event] = nextIndex;
            lowlink[event] = nextIndex;
            ++nextIndex;
            stack.push_back(event);
            onStack.insert(event);

            for (const auto &sink : adjacency.at(event))
            {
                if (index.count(sink) == 0)
                {
                    visit(sink);
                    lowlink[event] = std::min(lowlink[event], lowlink[sink]);
                }
                else if (onStack.count(sink) != 0)
                {
                    lowlink[event] = std::min(lowlink[event], index[sink]);
                }
            }

            if (lowlink[event] != index[event])
            {
                return;
            }

            std::vector<std::string> component;
            while (!stack.empty())
            {
                const std::string member = stack.back();
                stack.pop_back();
                onStack.erase(member);
                component.push_back(member);
                if (member == event)
                {
                    break;
                }
            }
            std::sort(component.begin(), component.end());

            bool cyclic = component.size() > 1;
            if (!cyclic && !component.empty())
            {
                const auto &edges = adjacency.at(component.front());
                cyclic = std::find(edges.begin(), edges.end(),
                                   component.front()) != edges.end();
            }
            if (cyclic)
            {
                cyclicComponents.push_back(std::move(component));
            }
        };

    for (const auto &[event, unused] : adjacency)
    {
        (void)unused;
        if (index.count(event) == 0)
        {
            visit(event);
        }
    }

    if (cyclicComponents.empty())
    {
        return {};
    }
    std::sort(cyclicComponents.begin(), cyclicComponents.end());
    return cyclicComponents.front();
}

} // namespace

const char *sequentialIrErrorCodeName(SequentialIrErrorCode code) noexcept
{
    switch (code)
    {
    case SequentialIrErrorCode::None:
        return "NONE";
    case SequentialIrErrorCode::EmptyDesign:
        return "EMPTY_DESIGN";
    case SequentialIrErrorCode::EmptyIdentifier:
        return "EMPTY_IDENTIFIER";
    case SequentialIrErrorCode::DuplicateIdentifier:
        return "DUPLICATE_IDENTIFIER";
    case SequentialIrErrorCode::DuplicateEvent:
        return "DUPLICATE_EVENT";
    case SequentialIrErrorCode::InvalidComponent:
        return "INVALID_COMPONENT";
    case SequentialIrErrorCode::InvalidIterationDistance:
        return "INVALID_ITERATION_DISTANCE";
    case SequentialIrErrorCode::InvalidLatency:
        return "INVALID_LATENCY";
    case SequentialIrErrorCode::UnknownEvent:
        return "UNKNOWN_EVENT";
    case SequentialIrErrorCode::MultipleDriver:
        return "MULTIPLE_DRIVER";
    case SequentialIrErrorCode::CombinationalCycle:
        return "COMBINATIONAL_CYCLE";
    }
    return "UNKNOWN_ERROR";
}

SequentialIrValidationResult validateAndLayerSequentialIr(
    const SequentialIR &ir)
{
    SequentialIrValidationResult result;
    result.physicalNets = ir.physicalNets;

    if (ir.ports.empty() && ir.gates.empty() && ir.registers.empty())
    {
        return fail(std::move(result), SequentialIrErrorCode::EmptyDesign,
                    "sequential IR declares no ports, gates, or registers");
    }

    std::set<std::string> componentIds;
    std::set<std::string> relationIds;
    std::map<std::string, std::string> eventOwners;

    for (const auto &port : ir.ports)
    {
        if (!insertNonEmptyIdentifier(componentIds, port.id, result, "component") ||
            !declareEvent(eventOwners, port.event, "port:" + port.id, result))
        {
            return result;
        }
    }

    for (const auto &gate : ir.gates)
    {
        if (!insertNonEmptyIdentifier(componentIds, gate.id, result, "component"))
        {
            return result;
        }
        if (gate.operation.empty() || gate.outputEvents.empty())
        {
            return fail(std::move(result),
                        SequentialIrErrorCode::InvalidComponent,
                        "gate " + gate.id +
                            " requires an operation and at least one output",
                        {gate.id});
        }
        if (gate.latencyEpochs < 0)
        {
            return fail(std::move(result), SequentialIrErrorCode::InvalidLatency,
                        "gate " + gate.id + " has negative latency",
                        {gate.id});
        }
        for (std::size_t index = 0; index < gate.inputEvents.size(); ++index)
        {
            if (!declareEvent(eventOwners, gate.inputEvents[index],
                              "gate:" + gate.id + ":input:" +
                                  std::to_string(index),
                              result))
            {
                return result;
            }
        }
        for (std::size_t index = 0; index < gate.outputEvents.size(); ++index)
        {
            if (!declareEvent(eventOwners, gate.outputEvents[index],
                              "gate:" + gate.id + ":output:" +
                                  std::to_string(index),
                              result))
            {
                return result;
            }
        }
    }

    for (const auto &reg : ir.registers)
    {
        if (!insertNonEmptyIdentifier(componentIds, reg.id, result, "component"))
        {
            return result;
        }
        if (reg.clockDomain.empty() || reg.dataEvent == reg.qEvent)
        {
            return fail(std::move(result),
                        SequentialIrErrorCode::InvalidComponent,
                        "register " + reg.id +
                            " requires a clock domain and distinct D/Q events",
                        {reg.id});
        }
        if (reg.latencyEpochs < 0)
        {
            return fail(std::move(result), SequentialIrErrorCode::InvalidLatency,
                        "register " + reg.id + " has negative latency",
                        {reg.id});
        }
        if (!declareEvent(eventOwners, reg.dataEvent,
                          "register:" + reg.id + ":D", result) ||
            !declareEvent(eventOwners, reg.qEvent,
                          "register:" + reg.id + ":Q", result))
        {
            return result;
        }
    }

    for (const auto &net : ir.physicalNets)
    {
        if (!insertNonEmptyIdentifier(relationIds, net.id, result, "relation"))
        {
            return result;
        }
        if (net.iterationDistance < 0)
        {
            return fail(std::move(result),
                        SequentialIrErrorCode::InvalidIterationDistance,
                        "physical net " + net.id +
                            " has negative iteration distance",
                        {net.id});
        }
        if (net.sinkEvents.empty())
        {
            return fail(std::move(result),
                        SequentialIrErrorCode::InvalidComponent,
                        "physical net " + net.id + " has no sinks",
                        {net.id});
        }
    }

    for (const auto &arc : ir.temporalArcs)
    {
        if (!insertNonEmptyIdentifier(relationIds, arc.id, result, "relation"))
        {
            return result;
        }
        if (arc.iterationDistance < 0)
        {
            return fail(std::move(result),
                        SequentialIrErrorCode::InvalidIterationDistance,
                        "temporal arc " + arc.id +
                            " has negative iteration distance",
                        {arc.id});
        }
        if (arc.latencyEpochs < 0)
        {
            return fail(std::move(result), SequentialIrErrorCode::InvalidLatency,
                        "temporal arc " + arc.id + " has negative latency",
                        {arc.id});
        }
    }

    const auto knownEvent = [&eventOwners](const std::string &event) {
        return eventOwners.count(event) != 0;
    };
    for (const auto &net : ir.physicalNets)
    {
        if (!knownEvent(net.sourceEvent))
        {
            return fail(std::move(result), SequentialIrErrorCode::UnknownEvent,
                        "physical net " + net.id +
                            " refers to unknown source event " + net.sourceEvent,
                        {net.id, net.sourceEvent});
        }
        for (const auto &sink : net.sinkEvents)
        {
            if (!knownEvent(sink))
            {
                return fail(std::move(result),
                            SequentialIrErrorCode::UnknownEvent,
                            "physical net " + net.id +
                                " refers to unknown sink event " + sink,
                            {net.id, sink});
            }
        }
    }
    for (const auto &arc : ir.temporalArcs)
    {
        if (!knownEvent(arc.sourceEvent) || !knownEvent(arc.sinkEvent))
        {
            const std::string unknown = !knownEvent(arc.sourceEvent)
                ? arc.sourceEvent
                : arc.sinkEvent;
            return fail(std::move(result), SequentialIrErrorCode::UnknownEvent,
                        "temporal arc " + arc.id+
                            " refers to unknown event " + unknown,
                        {arc.id, unknown});
        }
    }

    std::map<std::string, std::string> physicalDriver;
    for (const auto &net : ir.physicalNets)
    {
        for (const auto &sink : net.sinkEvents)
        {
            const auto [position, inserted] = physicalDriver.emplace(sink, net.id);
            if (!inserted)
            {
                return fail(
                    std::move(result), SequentialIrErrorCode::MultipleDriver,
                    "event " + sink + " is driven by physical nets " +
                        position->second + " and " + net.id,
                    {sink, position->second, net.id});
            }
        }
    }

    const auto appendDependency = [&result](SequentialDependency dependency) {
        result.fullDependencies.push_back(dependency);
        if (dependency.iterationDistance == 0)
        {
            result.scheduleDependencies.push_back(std::move(dependency));
        }
    };

    for (const auto &gate : ir.gates)
    {
        for (std::size_t input = 0; input < gate.inputEvents.size(); ++input)
        {
            for (std::size_t output = 0; output < gate.outputEvents.size(); ++output)
            {
                appendDependency(SequentialDependency{
                    "gate:" + gate.id + ":" + std::to_string(input) + ":" +
                        std::to_string(output),
                    gate.id,
                    gate.inputEvents[input],
                    gate.outputEvents[output],
                    0,
                    gate.latencyEpochs,
                    SequentialDependencyKind::Gate
                });
            }
        }
    }

    for (const auto &net : ir.physicalNets)
    {
        for (std::size_t sink = 0; sink < net.sinkEvents.size(); ++sink)
        {
            appendDependency(SequentialDependency{
                "net:" + net.id + ":" + std::to_string(sink),
                net.id,
                net.sourceEvent,
                net.sinkEvents[sink],
                net.iterationDistance,
                0,
                SequentialDependencyKind::PhysicalNet
            });
        }
    }

    result.resolvedTemporalArcs = ir.temporalArcs;
    for (const auto &arc : ir.temporalArcs)
    {
        appendDependency(SequentialDependency{
            "temporal:" + arc.id,
            arc.id,
            arc.sourceEvent,
            arc.sinkEvent,
            arc.iterationDistance,
            arc.latencyEpochs,
            SequentialDependencyKind::TemporalArc
        });
    }
    for (const auto &reg : ir.registers)
    {
        const SequentialTemporalArc stateArc{
            "register_state:" + reg.id,
            reg.dataEvent,
            reg.qEvent,
            1,
            reg.latencyEpochs
        };
        result.resolvedTemporalArcs.push_back(stateArc);
        appendDependency(SequentialDependency{
            stateArc.id,
            reg.id,
            stateArc.sourceEvent,
            stateArc.sinkEvent,
            stateArc.iterationDistance,
            stateArc.latencyEpochs,
            SequentialDependencyKind::RegisterState
        });
    }

    std::map<std::string, std::vector<std::string>> adjacency;
    std::map<std::string, std::size_t> indegree;
    for (const auto &[event, unused] : eventOwners)
    {
        (void)unused;
        adjacency[event];
        indegree[event] = 0;
    }
    for (const auto &dependency : result.scheduleDependencies)
    {
        adjacency[dependency.sourceEvent].push_back(dependency.sinkEvent);
        ++indegree[dependency.sinkEvent];
    }
    for (auto &[event, sinks] : adjacency)
    {
        (void)event;
        std::sort(sinks.begin(), sinks.end());
    }

    std::vector<std::string> currentLayer;
    for (const auto &[event, degree] : indegree)
    {
        if (degree == 0)
        {
            currentLayer.push_back(event);
        }
    }

    std::size_t processed = 0;
    while (!currentLayer.empty())
    {
        result.topologicalLayers.push_back(currentLayer);
        processed += currentLayer.size();
        std::set<std::string> nextLayer;
        for (const auto &source : currentLayer)
        {
            for (const auto &sink : adjacency[source])
            {
                if (--indegree[sink] == 0)
                {
                    nextLayer.insert(sink);
                }
            }
        }
        currentLayer.assign(nextLayer.begin(), nextLayer.end());
    }

    if (processed != eventOwners.size())
    {
        const std::vector<std::string> component = findCyclicComponent(adjacency);
        std::ostringstream message;
        message << "zero-iteration-distance combinational cycle";
        if (!component.empty())
        {
            message << " contains";
            for (const auto &event : component)
            {
                message << ' ' << event;
            }
        }
        result.topologicalLayers.clear();
        result.eventLayer.clear();
        return fail(std::move(result),
                    SequentialIrErrorCode::CombinationalCycle,
                    message.str(), component);
    }

    for (std::size_t layer = 0; layer < result.topologicalLayers.size(); ++layer)
    {
        for (const auto &event : result.topologicalLayers[layer])
        {
            result.eventLayer[event] = layer;
        }
    }
    result.valid = true;
    result.errorCode = SequentialIrErrorCode::None;
    result.message = "sequential IR is valid";
    return result;
}

} // namespace fcngraph::sequential
