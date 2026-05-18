#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include <autopr/algorithms/mapping.h>

namespace {

using fcngraph::Mapping;
using fcngraph::MappingPositionHash;
using fcngraph::position;

struct NodeInfo {
    std::string type;
    position pos{0, 0};
};

bool parsePosition(const std::string &xText, const std::string &yText, position &pos)
{
    const long long x = std::stoll(xText);
    const long long y = std::stoll(yText);
    if (x < 0 || y < 0) {
        return false;
    }
    pos = {static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: ifcn_mapping_metrics <layout.ifcn>\n";
        return 2;
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 3;
    }

    std::map<int, NodeInfo> nodes;
    std::map<std::pair<int, int>, std::vector<position>> routes;
    std::string line;
    std::string section;

    const std::regex nodePattern(
        R"(^\s*(\d+)\s*,\s*[^,]+\s*,\s*([^,]+)\s*,\s*\((-?\d+),(-?\d+)\)\s*;)");
    const std::regex routePattern(R"(^\s*\((\d+),(\d+)\)\s*:\s*(.*);)");
    const std::regex coordPattern(R"(\((-?\d+),(-?\d+)\))");

    while (std::getline(input, line)) {
        if (line.rfind("#nodes info", 0) == 0) {
            section = (section == "nodes") ? "" : "nodes";
            continue;
        }
        if (line.rfind("#paths info", 0) == 0) {
            section = (section == "paths") ? "" : "paths";
            continue;
        }

        std::smatch match;
        if (section == "nodes" && std::regex_search(line, match, nodePattern)) {
            position pos;
            if (parsePosition(match[3].str(), match[4].str(), pos)) {
                nodes[std::stoi(match[1])] = {match[2].str(), pos};
            }
        } else if (section == "paths" && std::regex_search(line, match, routePattern)) {
            std::vector<position> path;
            const std::string routeText = match[3].str();
            for (auto it = std::sregex_iterator(routeText.begin(), routeText.end(), coordPattern);
                 it != std::sregex_iterator();
                 ++it) {
                position pos;
                if (parsePosition((*it)[1].str(), (*it)[2].str(), pos)) {
                    path.push_back(pos);
                }
            }
            routes[{std::stoi(match[1]), std::stoi(match[2])}] = std::move(path);
        }
    }

    Mapping mapping;
    std::vector<std::vector<position>> routePaths;
    routePaths.reserve(routes.size());
    for (const auto &route : routes) {
        routePaths.push_back(route.second);
    }

    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> nodeLinks;
    for (const auto &node : nodes) {
        nodeLinks.try_emplace({node.second.pos, node.second.type},
                              std::make_pair(std::vector<position>{}, std::vector<position>{}));
    }

    for (const auto &route : routes) {
        const auto &key = route.first;
        const auto &path = route.second;
        if (path.size() < 2 || !nodes.count(key.first) || !nodes.count(key.second)) {
            continue;
        }

        const NodeInfo &source = nodes[key.first];
        const NodeInfo &sink = nodes[key.second];
        nodeLinks[{source.pos, source.type}].second.push_back(path[1]);
        nodeLinks[{sink.pos, sink.type}].first.push_back(path[path.size() - 2]);
    }

    for (auto &entry : nodeLinks) {
        auto &inputs = entry.second.first;
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

        auto &outputs = entry.second.second;
        std::sort(outputs.begin(), outputs.end());
        outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    }

    mapping.node_mapping(nodeLinks);
    const auto routeCellsByPath = mapping.mapping_line(routePaths);
    const auto crossCellsByPath = mapping.crossline_list;
    const auto nodeCellsByType = mapping.nodecell_list;

    std::vector<position> routeCells;
    for (const auto &pathEntry : routeCellsByPath) {
        for (const auto &segment : pathEntry.second) {
            routeCells.insert(routeCells.end(), segment.begin(), segment.end());
        }
    }

    std::vector<position> nodeCells;
    for (const auto &typeEntry : nodeCellsByType) {
        nodeCells.insert(nodeCells.end(), typeEntry.second.begin(), typeEntry.second.end());
    }

    std::unordered_set<position, MappingPositionHash> crossCellSet;
    std::size_t crossCount = 0;
    for (const auto &pathEntry : crossCellsByPath) {
        crossCount += pathEntry.second.size();
        for (const auto &segment : pathEntry.second) {
            crossCellSet.insert(segment.begin(), segment.end());
        }
    }

    std::vector<position> countedRouteCells;
    countedRouteCells.reserve(routeCells.size());
    std::unordered_set<position, MappingPositionHash> seenRouteCells;
    for (const position &cell : routeCells) {
        if (crossCellSet.find(cell) != crossCellSet.end()) {
            countedRouteCells.push_back(cell);
        } else if (seenRouteCells.insert(cell).second) {
            countedRouteCells.push_back(cell);
        }
    }

    const std::size_t cellCount = countedRouteCells.size() + nodeCells.size();
    std::cout << cellCount << ' ' << crossCount << '\n';
    return 0;
}
