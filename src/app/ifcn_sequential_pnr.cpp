#include <autopr/algorithms/astar.h>
#include <autopr/algorithms/mapping.h>
#include <autopr/graph/circuitGraph.h>
#include <autopr/graph/parse.h>
#include <autopr/grid/grid.h>
#include <autopr/sequential/globalPhaseSolver.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

using fcngraph::Astar;
using fcngraph::CircuitGraph;
using fcngraph::GridChessboard;
using fcngraph::Mapping;
using fcngraph::NodeLinkMap;
using fcngraph::Parse;
using fcngraph::PhysicalCellSite;
using fcngraph::RouteCellMap;
using fcngraph::position;
using namespace fcngraph::sequential;

struct StateBoundary
{
    std::string dataEvent;
    std::string qEvent;
    int latencyEpochs = 0;
};

struct CommandLine
{
    std::string input;
    std::string output;
    std::string latexOutput;
    std::vector<StateBoundary> states;
    std::vector<int> iiCandidates{4, 8, 12, 16};
    int maxSamePhaseTiles = 4;
    std::uint64_t maxDfsNodes = 250000;
    unsigned int spacing = 5;
};

struct MappedPhysicalLayout
{
    std::set<position> uniqueXySites;
    std::set<PhysicalCellSite> cellSites;
};

std::string usage()
{
    return
        "usage: ifcn_sequential_pnr <cut-dag.v> <output.ifcn> "
        "--state <D-event>:<Q-event> [--state ...] "
        "[--ii 4,8,12,16] [--max-same-phase 4] "
        "[--max-dfs-nodes 250000] [--spacing 5] [--tex output.tex]\n";
}

std::vector<int> parseIntegerList(const std::string &text)
{
    std::vector<int> values;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ','))
    {
        if (token.empty())
        {
            throw std::runtime_error("empty value in integer list: " + text);
        }
        const int value = std::stoi(token);
        if (value <= 0)
        {
            throw std::runtime_error("II values must be positive");
        }
        values.push_back(value);
    }
    if (values.empty())
    {
        throw std::runtime_error("at least one II candidate is required");
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

StateBoundary parseState(const std::string &text)
{
    const auto separator = text.find(':');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= text.size() ||
        text.find(':', separator + 1) != std::string::npos)
    {
        throw std::runtime_error(
            "state boundary must have the form D-event:Q-event: " + text);
    }
    return {text.substr(0, separator), text.substr(separator + 1), 0};
}

CommandLine parseCommandLine(int argc, char **argv)
{
    if (argc < 5)
    {
        throw std::runtime_error(usage());
    }
    CommandLine command{argv[1], argv[2]};
    for (int index = 3; index < argc; ++index)
    {
        const std::string option(argv[index]);
        const auto value = [&](const char *name) -> std::string {
            if (index + 1 >= argc)
            {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++index];
        };
        if (option == "--state")
        {
            command.states.push_back(parseState(value("--state")));
        }
        else if (option == "--ii")
        {
            command.iiCandidates = parseIntegerList(value("--ii"));
        }
        else if (option == "--max-same-phase")
        {
            command.maxSamePhaseTiles = std::stoi(value("--max-same-phase"));
            if (command.maxSamePhaseTiles <= 0 ||
                command.maxSamePhaseTiles > 4)
            {
                throw std::runtime_error(
                    "--max-same-phase must be between 1 and 4 tiles");
            }
        }
        else if (option == "--max-dfs-nodes")
        {
            const std::string raw = value("--max-dfs-nodes");
            std::size_t consumed = 0;
            const unsigned long long parsed = std::stoull(raw, &consumed);
            if (consumed != raw.size() || parsed == 0)
            {
                throw std::runtime_error(
                    "--max-dfs-nodes must be a positive integer");
            }
            command.maxDfsNodes = static_cast<std::uint64_t>(parsed);
        }
        else if (option == "--spacing")
        {
            const int spacing = std::stoi(value("--spacing"));
            if (spacing < 2)
            {
                throw std::runtime_error("--spacing must be at least 2");
            }
            command.spacing = static_cast<unsigned int>(spacing);
        }
        else if (option == "--tex")
        {
            command.latexOutput = value("--tex");
        }
        else
        {
            throw std::runtime_error("unknown option: " + option + "\n" + usage());
        }
    }
    if (command.states.empty())
    {
        throw std::runtime_error("at least one --state D:Q boundary is required");
    }
    return command;
}

std::string coordinateId(const position &coordinate)
{
    return "tile." + std::to_string(coordinate.first) + "." +
           std::to_string(coordinate.second);
}

std::string routeId(int source, int sink)
{
    return "net." + std::to_string(source) + "." + std::to_string(sink);
}

std::vector<std::vector<int>> parseLayers(const Parse &parse)
{
    std::vector<std::vector<int>> layers;
    for (const auto &layer : parse.getlayerNodeDivVec())
    {
        layers.emplace_back(layer.begin(), layer.end());
    }
    return layers;
}

void requireStateEvents(const Parse &parse,
                        const std::vector<StateBoundary> &states)
{
    for (const auto &state : states)
    {
        if (parse.getVertexIndex(state.dataEvent) < 0)
        {
            throw std::runtime_error(
                "state D event is absent from cut DAG: " + state.dataEvent);
        }
        if (parse.getVertexIndex(state.qEvent) < 0)
        {
            throw std::runtime_error(
                "state Q event is absent from cut DAG: " + state.qEvent);
        }
    }
}

GlobalClockProblem buildClockProblem(
    Parse &parse,
    const CircuitGraph &graph,
    const std::vector<StateBoundary> &states,
    const std::vector<int> &iiCandidates,
    int maxSamePhaseTiles,
    std::uint64_t maxDfsNodes)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    // Global clock resources and route occurrences are coarse P&R tiles.
    // The solver's historical field name says "Cells", but its unit in this
    // flow is deliberately one tile occurrence, never one mapped QCA cell.
    problem.maxConsecutiveSamePhaseCells = maxSamePhaseTiles;
    problem.iiCandidates = iiCandidates;
    problem.maxDfsNodes = maxDfsNodes;

    for (unsigned int node = 0; node < parse.getm_numVertices(); ++node)
    {
        problem.events.push_back(parse.getNodeName(static_cast<int>(node)));
    }

    std::set<position> coordinates;
    for (const auto &route : graph.routes)
    {
        coordinates.insert(route.second.begin(), route.second.end());
    }
    for (const position &coordinate : coordinates)
    {
        problem.clockResources.push_back(
            ClockResourceSpec{
                coordinateId(coordinate),
                ClockResourceSharing::PhaseSharedIndependentEpochs});
    }

    // Occurrences on a shared fanout trunk carry the same data token and must
    // therefore share an exact epoch variable.  A geometrical crossing owned
    // by another source keeps a distinct epoch variable but shares the tile's
    // modulo-four clock resource.
    std::map<std::pair<int, position>, std::string> fanoutEpochVariable;
    for (const auto &route : graph.routes)
    {
        const int source = static_cast<int>(route.first.first);
        const int sink = static_cast<int>(route.first.second);
        const std::string id = routeId(source, sink);
        std::vector<std::string> occurrenceIds;
        for (std::size_t index = 0; index < route.second.size(); ++index)
        {
            const position coordinate = route.second[index];
            const std::string occurrence =
                id + ".occ." + std::to_string(index);
            std::string epochVariable;
            if (index == 0)
            {
                epochVariable = "event." + parse.getNodeName(source);
            }
            else if (index + 1 == route.second.size())
            {
                epochVariable = "event." + parse.getNodeName(sink);
            }
            else
            {
                auto [epochIt, inserted] = fanoutEpochVariable.emplace(
                    std::make_pair(source, coordinate), occurrence + ".epoch");
                (void)inserted;
                epochVariable = epochIt->second;
            }
            problem.occurrences.push_back(RouteOccurrenceSpec{
                occurrence, coordinateId(coordinate), epochVariable});
            occurrenceIds.push_back(occurrence);
        }
        problem.routes.push_back(FixedRouteSpec{
            id,
            parse.getNodeName(source),
            parse.getNodeName(sink),
            0,
            occurrenceIds});
    }

    for (std::size_t index = 0; index < states.size(); ++index)
    {
        const auto &state = states[index];
        problem.timingArcs.push_back(ExactTimingArcSpec{
            "state." + std::to_string(index),
            state.dataEvent,
            state.qEvent,
            1,
            state.latencyEpochs});
    }

    // All pseudo register-Q ports and primary inputs launch at the common
    // logical tick.  Their downstream paths may hold within a clock zone, but
    // reconvergent fanins bind to one absolute sink event.
    std::set<std::string> anchored;
    for (const auto &state : states)
    {
        if (anchored.insert(state.qEvent).second)
        {
            problem.anchors.push_back(EpochAnchorSpec{state.qEvent, 0});
        }
    }
    for (const auto &inputName : parse.getVec_inputNodeName())
    {
        if (anchored.insert(inputName).second)
        {
            problem.anchors.push_back(EpochAnchorSpec{inputName, 0});
        }
    }
    return problem;
}

std::map<position, int> phaseMap(
    const GlobalClockProblem &problem,
    const GlobalClockSolution &solution)
{
    std::map<position, int> phases;
    for (const auto &route : problem.routes)
    {
        for (const auto &occurrenceId : route.occurrences)
        {
            const auto occurrence = std::find_if(
                problem.occurrences.begin(), problem.occurrences.end(),
                [&](const RouteOccurrenceSpec &candidate) {
                    return candidate.id == occurrenceId;
                });
            if (occurrence == problem.occurrences.end())
            {
                throw std::runtime_error("clock solution references an unknown occurrence");
            }
        }
    }
    for (const auto &resource : solution.clockResourcePhase)
    {
        // Resource IDs are generated exclusively by coordinateId().
        const std::string prefix = "tile.";
        if (resource.first.rfind(prefix, 0) != 0)
        {
            throw std::runtime_error("unexpected clock resource id: " + resource.first);
        }
        const auto separator = resource.first.find('.', prefix.size());
        if (separator == std::string::npos)
        {
            throw std::runtime_error("malformed clock resource id: " + resource.first);
        }
        const unsigned long x = std::stoul(
            resource.first.substr(prefix.size(), separator - prefix.size()));
        const unsigned long y = std::stoul(resource.first.substr(separator + 1));
        phases[{static_cast<unsigned int>(x), static_cast<unsigned int>(y)}] =
            resource.second;
    }
    return phases;
}

std::size_t measureMaxSamePhaseTileRun(
    const GlobalClockProblem &problem,
    const GlobalClockSolution &solution)
{
    std::map<std::string, std::string> resourceByOccurrence;
    for (const auto &occurrence : problem.occurrences)
    {
        const auto [unused, inserted] = resourceByOccurrence.emplace(
            occurrence.id, occurrence.clockResource);
        (void)unused;
        if (!inserted)
        {
            throw std::runtime_error(
                "duplicate route occurrence id: " + occurrence.id);
        }
    }

    std::size_t maximum = 0;
    for (const auto &route : problem.routes)
    {
        int previousPhase = -1;
        std::size_t run = 0;
        for (const auto &occurrenceId : route.occurrences)
        {
            const auto resource = resourceByOccurrence.find(occurrenceId);
            if (resource == resourceByOccurrence.end())
            {
                throw std::runtime_error(
                    "route references an unknown occurrence: " + occurrenceId);
            }
            const auto phase =
                solution.clockResourcePhase.find(resource->second);
            if (phase == solution.clockResourcePhase.end())
            {
                throw std::runtime_error(
                    "clock solution is missing tile resource " +
                    resource->second);
            }
            if (phase->second == previousPhase)
            {
                ++run;
            }
            else
            {
                previousPhase = phase->second;
                run = 1;
            }
            maximum = std::max(maximum, run);
        }
    }
    return maximum;
}

MappedPhysicalLayout mapPhysicalLayout(Parse &parse,
                                       const CircuitGraph &graph)
{
    NodeLinkMap nodeLinks;
    for (const auto &node : graph.nodeIndex_pos)
    {
        nodeLinks.try_emplace(
            std::make_pair(node.second, parse.getNodeType(node.first)),
            std::make_pair(std::vector<position>{}, std::vector<position>{}));
    }

    std::vector<std::vector<position>> routePaths;
    std::vector<unsigned int> routeIterationDistances;
    for (const auto &route : graph.routes)
    {
        if (route.second.size() < 2)
        {
            throw std::runtime_error("P&R emitted a route shorter than two cells");
        }
        const int source = static_cast<int>(route.first.first);
        const int sink = static_cast<int>(route.first.second);
        const position sourcePosition = graph.nodeIndex_pos.at(source);
        const position sinkPosition = graph.nodeIndex_pos.at(sink);
        nodeLinks[{sourcePosition, parse.getNodeType(source)}].second.push_back(
            route.second[1]);
        nodeLinks[{sinkPosition, parse.getNodeType(sink)}].first.push_back(
            route.second[route.second.size() - 2]);
        routePaths.push_back(route.second);
        routeIterationDistances.push_back(0);
    }

    for (auto &node : nodeLinks)
    {
        for (auto *ports : {&node.second.first, &node.second.second})
        {
            std::sort(ports->begin(), ports->end());
            ports->erase(std::unique(ports->begin(), ports->end()), ports->end());
        }
    }

    Mapping mapping;
    mapping.node_mapping(nodeLinks, fcngraph::MappingMode::Sequential);
    const auto routeCells = mapping.mapping_line(
        routePaths,
        fcngraph::MappingMode::Sequential,
        routeIterationDistances);
    std::string mappingError;
    if (!mapping.validate_crossovers(&mappingError))
    {
        throw std::runtime_error("QCA Mapping DRC failed: " + mappingError);
    }

    MappedPhysicalLayout layout;
    const auto orderedRoutes =
        mapping.orderedLayerAwarePhysicalRoutes(routePaths);
    if (orderedRoutes.size() != routePaths.size())
    {
        throw std::runtime_error(
            "QCA Mapping returned the wrong number of ordered physical routes");
    }
    std::set<position> orderedRouteCells;
    for (const auto &route : orderedRoutes)
    {
        for (const PhysicalCellSite &site : route)
        {
            orderedRouteCells.insert(site.xy);
        }
    }

    std::set<position> mappedRouteCells;
    const auto includeRouteCells = [&](const RouteCellMap &routes) {
        for (const auto &route : routes)
        {
            for (const auto &segment : route.second)
            {
                mappedRouteCells.insert(segment.begin(), segment.end());
            }
        }
    };
    includeRouteCells(routeCells);
    includeRouteCells(mapping.crossline_list);
    for (const position &cell : mappedRouteCells)
    {
        if (orderedRouteCells.count(cell) == 0)
        {
            throw std::runtime_error(
                "ordered physical routes do not cover mapped route cell (" +
                std::to_string(cell.first) + "," +
                std::to_string(cell.second) + ")");
        }
    }

    std::map<position, std::string> nodeEventByTile;
    for (const auto &node : graph.nodeIndex_pos)
    {
        const auto [entry, inserted] = nodeEventByTile.emplace(
            node.second, parse.getNodeName(node.first));
        if (!inserted && entry->second != parse.getNodeName(node.first))
        {
            throw std::runtime_error("multiple node events occupy one mapped tile");
        }
    }
    std::map<position, std::string> nodeEventByCell;
    for (const auto &type : mapping.nodecell_list)
    {
        for (const position &cell : type.second)
        {
            const position tile{cell.first / 5, cell.second / 5};
            const auto event = nodeEventByTile.find(tile);
            if (event == nodeEventByTile.end())
            {
                throw std::runtime_error(
                    "mapped node cell has no owning node event at (" +
                    std::to_string(cell.first) + "," +
                    std::to_string(cell.second) + ")");
            }
            const auto [entry, inserted] =
                nodeEventByCell.emplace(cell, event->second);
            if (!inserted && entry->second != event->second)
            {
                throw std::runtime_error(
                    "mapped node cell is owned by conflicting node events");
            }
        }
    }
    layout.cellSites = mapping.physicalCellSites(routePaths);

    for (const auto &site : layout.cellSites)
    {
        layout.uniqueXySites.insert(site.xy);
    }
    return layout;
}

std::string jsonEscape(const std::string &value)
{
    std::ostringstream escaped;
    for (const char character : value)
    {
        if (character == '\\' || character == '"')
        {
            escaped << '\\';
        }
        escaped << character;
    }
    return escaped.str();
}

void writeIfcn(const std::string &path,
               Parse &parse,
               const CircuitGraph &graph,
               const std::map<position, int> &phases,
               const GlobalClockSolution &solution,
               const std::vector<StateBoundary> &states,
               std::size_t mappedUniqueXySites,
               std::size_t mappedLayerCellRecords,
               int maxSamePhaseTiles,
               std::size_t maxObservedSamePhaseTileRun)
{
    std::filesystem::path outputPath(path);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream output(path);
    if (!output)
    {
        throw std::runtime_error("cannot write IFCN layout: " + path);
    }

    output << "#circuit name: " << parse.get_moduleName() << "\n";
    output << "#mapping mode: sequential\n";
    output << "#flow: sequential register-cut P&R v0\n";
    output << "#status: sequential-aware abstract state boundary\n";
    output << "#phase count: 4\n";
    output << "#phase granularity: tile\n";
    output << "#tile phase drc scope: ordered_route_tiles\n";
    output << "#max same phase tiles: "
           << maxSamePhaseTiles << "\n";
    output << "#observed max same phase tile run: "
           << maxObservedSamePhaseTileRun << "\n";
    output << "#initiation interval epochs: "
           << solution.initiationInterval << "\n";
    output << "#mapped unique xy sites: " << mappedUniqueXySites << "\n";
    output << "#mapped qca cells: " << mappedUniqueXySites << "\n";
    output << "#mapped layer cell records: "
           << mappedLayerCellRecords << "\n";
    for (const auto &state : states)
    {
        output << "#state arc: " << state.dataEvent << "(t) -> "
               << state.qEvent << "(t+1), distance=1, latency="
               << state.latencyEpochs << "\n";
    }
    output << "\n#nodes info\n";
    output << "### nodeIndex, nodeName, nodeType, nodePosition ###\n";
    for (const auto &node : graph.nodeIndex_pos)
    {
        output << node.first << ", " << parse.getNodeName(node.first) << ", "
               << parse.getNodeType(node.first) << ", ("
               << node.second.first << ',' << node.second.second << ");\n";
    }
    output << "#nodes info\n\n#paths info\n";
    output << "### {node1, node2} : path ###\n";
    for (const auto &route : graph.routes)
    {
        output << "#iteration_distance=0\n"
               << '(' << route.first.first << ',' << route.first.second << "): ";
        for (std::size_t index = 0; index < route.second.size(); ++index)
        {
            if (index != 0)
            {
                output << ',';
            }
            output << '(' << route.second[index].first << ','
                   << route.second[index].second << ')';
        }
        output << ";\n";
    }
    output << "#paths info\n\n#phase map\n";
    output << "### authoritative tile clock map; every mapped QCA cell "
              "inherits its tile phase ###\n";
    output << "### tile clock resource (x,y) : zero-based phase ###\n";
    for (const auto &phase : phases)
    {
        output << '(' << phase.first.first << ',' << phase.first.second
               << "): " << phase.second << ";\n";
    }
    output << "#phase map\n";
    if (!output)
    {
        throw std::runtime_error("failed while writing IFCN layout: " + path);
    }

    std::ofstream report(path + ".json");
    if (!report)
    {
        throw std::runtime_error("cannot write sequential P&R report");
    }
    report << "{\n"
           << "  \"schema\": \"ifcn.sequential-pnr-report.v0\",\n"
           << "  \"module\": \"" << jsonEscape(parse.get_moduleName()) << "\",\n"
           << "  \"mapping_mode\": \"sequential\",\n"
           << "  \"phase_granularity\": \"tile\",\n"
           << "  \"tile_phase_drc_scope\": \"ordered_route_tiles\",\n"
           << "  \"tile_clock_resources\": "
           << solution.clockResourcePhase.size() << ",\n"
           << "  \"max_same_phase_tiles\": " << maxSamePhaseTiles << ",\n"
           << "  \"max_observed_same_phase_tile_run\": "
           << maxObservedSamePhaseTileRun << ",\n"
           << "  \"tile_phase_drc\": true,\n"
           << "  \"status\": \"success_abstract_state_macro\",\n"
           << "  \"initiation_interval\": " << solution.initiationInterval << ",\n"
           << "  \"nodes\": " << graph.nodeIndex_pos.size() << ",\n"
           << "  \"routes\": " << graph.routes.size() << ",\n"
           << "  \"mapped_unique_xy_sites\": " << mappedUniqueXySites << ",\n"
           << "  \"mapped_qca_cells\": " << mappedUniqueXySites << ",\n"
           << "  \"mapped_layer_cell_records\": "
           << mappedLayerCellRecords << ",\n"
           << "  \"state_boundaries\": " << states.size() << "\n"
           << "}\n";
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

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const CommandLine command = parseCommandLine(argc, argv);
        Parse parse;
        parse.parseVerilog(command.input);
        if (parse.getm_numVertices() == 0)
        {
            throw std::runtime_error("legacy DAG parser produced an empty graph");
        }
        requireStateEvents(parse, command.states);
        parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
        parse.caculateSameLayerNodeRoutePair();

        GridChessboard board;
        Astar router(board, false, 600.0);
        router.setAllowInterSourceWireOverlap(false);
        CircuitGraph graph(parse, command.input, board, router);
        const auto layers = parseLayers(parse);

        bool routed = false;
        for (const unsigned int spacing :
             {command.spacing, command.spacing + 2, command.spacing + 4})
        {
            graph.sortNodesByFixedLayerOrder(layers, spacing, spacing);
            if (graph.placeAndRouteLegacyFast())
            {
                routed = true;
                break;
            }
        }
        if (!routed)
        {
            throw std::runtime_error("phase-blind placement/routing failed");
        }

        const GlobalClockProblem tileProblem = buildClockProblem(
            parse, graph, command.states, command.iiCandidates,
            command.maxSamePhaseTiles, command.maxDfsNodes);
        const MappedPhysicalLayout mappedLayout =
            mapPhysicalLayout(parse, graph);

        const GlobalClockSolveResult tileResult =
            GlobalPhaseSolver(tileProblem).solve();
        if (tileResult.status != GlobalClockSolveStatus::Sat ||
            !tileResult.solution.has_value())
        {
            throw std::runtime_error(
                std::string("tile phase/epoch solve ") +
                statusName(tileResult.status) +
                ": " + tileResult.message +
                " (dfs_nodes=" +
                std::to_string(tileResult.stats.dfsNodes) +
                ", decisions=" +
                std::to_string(tileResult.stats.decisions) +
                ", conflicts=" +
                std::to_string(tileResult.stats.conflicts) +
                ", ii_tried=" +
                std::to_string(tileResult.stats.iiCandidatesTried) + ")");
        }
        std::string validationError;
        if (!GlobalPhaseSolver::validateSolution(
                tileProblem,
                tileResult.solution.value(),
                &validationError))
        {
            throw std::runtime_error(
                "tile phase/epoch validation failed: " +
                validationError);
        }
        const std::size_t maxObservedSamePhaseTileRun =
            measureMaxSamePhaseTileRun(
                tileProblem, tileResult.solution.value());
        if (maxObservedSamePhaseTileRun >
            static_cast<std::size_t>(command.maxSamePhaseTiles))
        {
            throw std::runtime_error(
                "tile same-phase run exceeds configured limit");
        }
        const auto phases = phaseMap(
            tileProblem, tileResult.solution.value());
        for (const auto &phase : phases)
        {
            // GlobalPhaseSolver uses 0..3; the legacy GridCell/LaTeX renderer
            // uses the established 1..4 encoding.
            board.gridMap[phase.first].setPhase(phase.second + 1);
        }
        writeIfcn(command.output, parse, graph, phases,
                  tileResult.solution.value(), command.states,
                  mappedLayout.uniqueXySites.size(),
                  mappedLayout.cellSites.size(),
                  command.maxSamePhaseTiles,
                  maxObservedSamePhaseTileRun);
        if (!command.latexOutput.empty())
        {
            const std::filesystem::path latexPath(command.latexOutput);
            if (!latexPath.parent_path().empty())
            {
                std::filesystem::create_directories(latexPath.parent_path());
            }
            // Reuse the established P&R renderer unchanged.  This flow only
            // supplies its generated coordinates, routes, and clock phases.
            graph.printLaTex(command.latexOutput);
        }

        std::cout << "sequential_pnr=success"
                  << " module=" << parse.get_moduleName()
                  << " nodes=" << graph.nodeIndex_pos.size()
                  << " routes=" << graph.routes.size()
                  << " state_boundaries=" << command.states.size()
                  << " II=" << tileResult.solution->initiationInterval
                  << " phase_granularity=tile"
                  << " max_same_phase_tile_run="
                  << maxObservedSamePhaseTileRun
                  << " mapped_unique_xy_sites="
                  << mappedLayout.uniqueXySites.size()
                  << " mapped_layer_cell_records="
                  << mappedLayout.cellSites.size()
                  << " output=" << command.output;
        if (!command.latexOutput.empty())
        {
            std::cout << " tex=" << command.latexOutput;
        }
        std::cout << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ifcn_sequential_pnr failed: " << error.what() << '\n';
        return 1;
    }
}
