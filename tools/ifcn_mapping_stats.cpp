#include "autopr/algorithms/mapping.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using fcngraph::Mapping;
using fcngraph::MappingPositionHash;
using fcngraph::position;

struct NodeInfo {
    int index = -1;
    std::string name;
    std::string type;
    position pos{0, 0};
};

struct IfcnData {
    std::string path;
    std::string circuit;
    int gates = 0;
    int inputs = 0;
    int outputs = 0;
    int edges = 0;
    int width = 0;
    int height = 0;
    int area = 0;
    std::map<int, NodeInfo> nodes;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;
};

struct MappingStats {
    int nodeCells = 0;
    int routeCells = 0;
    int mappedCells = 0;
    int crosslineGroups = 0;
    int crosslineSegments = 0;
    int uniqueCrossCells = 0;
    std::map<std::string, int> nodeCellByType;
};

std::string trim(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string csvEscape(const std::string &value)
{
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out += ch;
        }
    }
    out += '"';
    return out;
}

void parseNodeLine(const std::string &line, IfcnData &data)
{
    static const std::regex pattern(
        R"(^\s*(\d+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*\((\d+)\s*,\s*(\d+)\)\s*;?\s*$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        return;
    }

    NodeInfo node;
    node.index = std::stoi(match[1].str());
    node.name = trim(match[2].str());
    node.type = trim(match[3].str());
    node.pos = {static_cast<unsigned int>(std::stoul(match[4].str())),
                static_cast<unsigned int>(std::stoul(match[5].str()))};
    data.nodes[node.index] = node;
}

void parsePathLine(const std::string &line, IfcnData &data)
{
    static const std::regex header(R"(^\s*\((\d+)\s*,\s*(\d+)\)\s*:)");
    std::smatch headMatch;
    if (!std::regex_search(line, headMatch, header)) {
        return;
    }

    const auto source = static_cast<unsigned int>(std::stoul(headMatch[1].str()));
    const auto sink = static_cast<unsigned int>(std::stoul(headMatch[2].str()));

    static const std::regex coordPattern(R"(\((\d+)\s*,\s*(\d+)\))");
    std::vector<position> path;
    bool first = true;
    for (std::sregex_iterator it(line.begin(), line.end(), coordPattern), end; it != end; ++it) {
        if (first) {
            first = false;
            continue;
        }
        path.push_back({static_cast<unsigned int>(std::stoul((*it)[1].str())),
                        static_cast<unsigned int>(std::stoul((*it)[2].str()))});
    }
    data.routes[{source, sink}] = std::move(path);
}

IfcnData parseIfcn(const std::string &path)
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }

    IfcnData data;
    data.path = path;
    bool nodeSection = false;
    bool pathSection = false;
    bool phaseSection = false;
    std::string line;

    static const std::regex inputOutputPattern(R"(#input/output:\s*(\d+)\s*/\s*(\d+))");
    static const std::regex areaPattern(R"(#layout area:\s*width:\s*(\d+)\s*,\s*height:\s*(\d+)\s*,\s*area:\s*(\d+))");

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (line.rfind("#circuit name:", 0) == 0) {
            data.circuit = trim(line.substr(std::string("#circuit name:").size()));
            continue;
        }
        if (line.rfind("#gates number:", 0) == 0) {
            data.gates = std::stoi(trim(line.substr(std::string("#gates number:").size())));
            continue;
        }
        if (line.rfind("#edges number:", 0) == 0) {
            data.edges = std::stoi(trim(line.substr(std::string("#edges number:").size())));
            continue;
        }

        std::smatch match;
        if (std::regex_search(line, match, inputOutputPattern)) {
            data.inputs = std::stoi(match[1].str());
            data.outputs = std::stoi(match[2].str());
            continue;
        }
        if (std::regex_search(line, match, areaPattern)) {
            data.width = std::stoi(match[1].str());
            data.height = std::stoi(match[2].str());
            data.area = std::stoi(match[3].str());
            continue;
        }

        if (line.rfind("#nodes info", 0) == 0) {
            nodeSection = true;
            pathSection = phaseSection = false;
            continue;
        }
        if (line.rfind("#paths info", 0) == 0) {
            pathSection = true;
            nodeSection = phaseSection = false;
            continue;
        }
        if (line.rfind("#phase map", 0) == 0) {
            phaseSection = true;
            nodeSection = pathSection = false;
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        if (nodeSection) {
            parseNodeLine(line, data);
        } else if (pathSection) {
            parsePathLine(line, data);
        }
    }

    return data;
}

MappingStats mapIfcn(const IfcnData &data)
{
    std::vector<std::vector<position>> circleLine;
    circleLine.reserve(data.routes.size());
    for (const auto &route : data.routes) {
        circleLine.push_back(route.second);
    }

    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> nodeLink;
    for (const auto &entry : data.nodes) {
        nodeLink.try_emplace({entry.second.pos, entry.second.type},
                             std::make_pair(std::vector<position>{}, std::vector<position>{}));
    }

    for (const auto &route : data.routes) {
        const auto sourceIt = data.nodes.find(static_cast<int>(route.first.first));
        const auto sinkIt = data.nodes.find(static_cast<int>(route.first.second));
        const auto &path = route.second;
        if (sourceIt == data.nodes.end() || sinkIt == data.nodes.end() || path.size() < 2) {
            continue;
        }

        nodeLink[{sourceIt->second.pos, sourceIt->second.type}].second.push_back(path[1]);
        nodeLink[{sinkIt->second.pos, sinkIt->second.type}].first.push_back(path[path.size() - 2]);
    }

    for (auto &entry : nodeLink) {
        auto &inputs = entry.second.first;
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

        auto &outputs = entry.second.second;
        std::sort(outputs.begin(), outputs.end());
        outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    }

    Mapping mapping;
    mapping.node_mapping(nodeLink);

    MappingStats stats;
    for (const auto &entry : mapping.nodecell_list) {
        const int count = static_cast<int>(entry.second.size());
        stats.nodeCellByType[entry.first] = count;
        stats.nodeCells += count;
    }

    const auto routeExample = mapping.mapping_line(circleLine);
    const auto crossExample = mapping.crossline_list;

    std::vector<position> routeCells;
    for (const auto &route : routeExample) {
        for (const auto &segment : route.second) {
            routeCells.insert(routeCells.end(), segment.begin(), segment.end());
        }
    }

    std::unordered_set<position, MappingPositionHash> crossCells;
    stats.crosslineGroups = static_cast<int>(crossExample.size());
    for (const auto &entry : crossExample) {
        stats.crosslineSegments += static_cast<int>(entry.second.size());
        for (const auto &segment : entry.second) {
            crossCells.insert(segment.begin(), segment.end());
        }
    }
    stats.uniqueCrossCells = static_cast<int>(crossCells.size());

    std::vector<position> dedupedRouteCells;
    dedupedRouteCells.reserve(routeCells.size());
    std::unordered_set<position, MappingPositionHash> seen;
    for (const auto &pos : routeCells) {
        const bool isCross = crossCells.find(pos) != crossCells.end();
        if (isCross || seen.insert(pos).second) {
            dedupedRouteCells.push_back(pos);
        }
    }
    stats.routeCells = static_cast<int>(dedupedRouteCells.size());
    stats.mappedCells = stats.nodeCells + stats.routeCells;
    return stats;
}

int typeCount(const MappingStats &stats, const std::string &type)
{
    const auto it = stats.nodeCellByType.find(type);
    return it == stats.nodeCellByType.end() ? 0 : it->second;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: ifcn_mapping_stats file.ifcn [...]\n";
        return 1;
    }

    std::cout << "ifcn_file,circuit,gates,edges,inputs,outputs,width,height,area,nodes,routes,"
              << "node_cells,route_cells,mapped_cells,crossline_groups,crossline_segments,unique_cross_cells,"
              << "input_cells,output_cells,normal_cells,fix0_cells,fix1_cells\n";

    for (int i = 1; i < argc; ++i) {
        try {
            const auto data = parseIfcn(argv[i]);
            const auto stats = mapIfcn(data);
            std::cout << csvEscape(data.path) << ','
                      << csvEscape(data.circuit) << ','
                      << data.gates << ','
                      << data.edges << ','
                      << data.inputs << ','
                      << data.outputs << ','
                      << data.width << ','
                      << data.height << ','
                      << data.area << ','
                      << data.nodes.size() << ','
                      << data.routes.size() << ','
                      << stats.nodeCells << ','
                      << stats.routeCells << ','
                      << stats.mappedCells << ','
                      << stats.crosslineGroups << ','
                      << stats.crosslineSegments << ','
                      << stats.uniqueCrossCells << ','
                      << typeCount(stats, "input") << ','
                      << typeCount(stats, "output") << ','
                      << typeCount(stats, "normal") << ','
                      << typeCount(stats, "fix0") << ','
                      << typeCount(stats, "fix1") << '\n';
        } catch (const std::exception &ex) {
            std::cerr << argv[i] << ": " << ex.what() << '\n';
        }
    }
    return 0;
}
