#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <autopr/algorithms/mapping.h>
#include <autopr/io/ifcnMappingMetadata.h>

namespace {

using fcngraph::Mapping;
using fcngraph::MappingMode;
using fcngraph::MappingPositionHash;
using fcngraph::IfcnMappingModeResolver;
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

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: ifcn_mapping_metrics <layout.ifcn> "
                     "[--no-io-contraction] [--timing]\n";
        return 2;
    }

    bool contractIoPorts = true;
    bool printTiming = false;
    for (int index = 2; index < argc; ++index) {
        const std::string option(argv[index]);
        if (option == "--no-io-contraction") {
            contractIoPorts = false;
        } else if (option == "--timing") {
            printTiming = true;
        } else {
            std::cerr << "unknown option: " << option << '\n';
            return 2;
        }
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 3;
    }

    try {
    std::map<int, NodeInfo> nodes;
    std::map<std::pair<int, int>, std::vector<position>> routes;
    std::map<std::pair<int, int>, unsigned int> iterationDistances;
    std::set<std::pair<int, int>> routesWithExplicitDistance;
    std::optional<unsigned int> pendingIterationDistance;
    IfcnMappingModeResolver mappingModeResolver;
    std::string line;
    std::string section;

    const std::regex nodePattern(
        R"(^\s*(\d+)\s*,\s*[^,]+\s*,\s*([^,]+)\s*,\s*\((-?\d+),(-?\d+)\)\s*;)");
    const std::regex routePattern(R"(^\s*\((\d+),(\d+)\)\s*:\s*(.*);)");
    const std::regex coordPattern(R"(\((-?\d+),(-?\d+)\))");
    const std::regex mappingModePattern(
        R"(^\s*#\s*mapping\s+mode\s*:\s*(.*?)\s*$)",
        std::regex::icase);
    const std::regex mappingModeKeyPattern(
        R"(^\s*#\s*mapping\s+mode\b.*$)", std::regex::icase);
    const std::regex flowPattern(
        R"(^\s*#\s*flow\s*:\s*(.*?)\s*$)", std::regex::icase);
    const std::regex iterationDistancePattern(
        R"(^\s*#\s*iteration(?:_|\s+)distance\s*(?:=|:)\s*(\d+)\s*$)",
        std::regex::icase);
    const std::regex iterationDistanceKeyPattern(
        R"(^\s*#\s*iteration(?:_|\s+)distance\b.*$)",
        std::regex::icase);

    const auto rejectDanglingDistance = [&]() {
        if (pendingIterationDistance.has_value()) {
            throw std::runtime_error(
                "iteration_distance is not followed by a route in the paths section");
        }
    };

    while (std::getline(input, line)) {
        std::smatch metadataMatch;
        if (std::regex_match(line, metadataMatch, mappingModePattern)) {
            mappingModeResolver.observeModeValue(metadataMatch[1].str());
            continue;
        }
        if (std::regex_match(line, mappingModeKeyPattern)) {
            throw std::runtime_error("malformed IFCN mapping mode declaration");
        }
        if (std::regex_match(line, metadataMatch, flowPattern)) {
            mappingModeResolver.observeFlowValue(metadataMatch[1].str());
            continue;
        }
        if (std::regex_match(line, iterationDistanceKeyPattern)) {
            if (section != "paths") {
                throw std::runtime_error(
                    "iteration_distance is only valid inside the paths section");
            }
            if (!std::regex_match(line, metadataMatch, iterationDistancePattern)) {
                throw std::runtime_error("malformed IFCN iteration_distance declaration");
            }
            if (pendingIterationDistance.has_value()) {
                throw std::runtime_error(
                    "duplicate iteration_distance before one route");
            }
            const unsigned long long parsedDistance =
                std::stoull(metadataMatch[1].str());
            if (parsedDistance > std::numeric_limits<unsigned int>::max()) {
                throw std::runtime_error("IFCN iteration_distance is out of range");
            }
            pendingIterationDistance = static_cast<unsigned int>(parsedDistance);
            continue;
        }
        if (line.rfind("#nodes info", 0) == 0) {
            rejectDanglingDistance();
            section = (section == "nodes") ? "" : "nodes";
            continue;
        }
        if (line.rfind("#paths info", 0) == 0) {
            rejectDanglingDistance();
            section = (section == "paths") ? "" : "paths";
            continue;
        }

        std::smatch match;
        if (section == "nodes" && std::regex_search(line, match, nodePattern)) {
            position pos;
            if (parsePosition(match[3].str(), match[4].str(), pos)) {
                nodes[std::stoi(match[1])] = {lowerCopy(match[2].str()), pos};
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
            const auto routeKey =
                std::make_pair(std::stoi(match[1]), std::stoi(match[2]));
            if (routes.find(routeKey) != routes.end()) {
                throw std::runtime_error(
                    "duplicate IFCN route " + std::to_string(routeKey.first) +
                    "->" + std::to_string(routeKey.second));
            }
            const unsigned int iterationDistance =
                pendingIterationDistance.value_or(0);
            routes.emplace(routeKey, std::move(path));
            iterationDistances.emplace(routeKey, iterationDistance);
            mappingModeResolver.observeIterationDistance(iterationDistance);
            if (pendingIterationDistance.has_value()) {
                routesWithExplicitDistance.insert(routeKey);
            }
            pendingIterationDistance.reset();
        }
    }
    rejectDanglingDistance();

    const fcngraph::IfcnMappingModeResolution modeResolution =
        mappingModeResolver.resolve();
    const MappingMode mappingMode = modeResolution.mode;
    if (modeResolution.explicitMode &&
        mappingMode == MappingMode::Sequential &&
        routesWithExplicitDistance.size() != routes.size()) {
        throw std::runtime_error(
            "sequential IFCN requires iteration_distance before every route");
    }

    Mapping mapping;
    std::vector<std::vector<position>> routePaths;
    std::vector<unsigned int> routeIterationDistances;
    routePaths.reserve(routes.size());
    routeIterationDistances.reserve(routes.size());
    for (const auto &route : routes) {
        if (route.second.size() >= 2) {
            routePaths.push_back(route.second);
            routeIterationDistances.push_back(
                iterationDistances.at(route.first));
        }
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
        auto &sinkInputs = nodeLinks[{sink.pos, sink.type}].first;
        const position sinkPort = path[path.size() - 2];
        if (std::find(sinkInputs.begin(), sinkInputs.end(), sinkPort) != sinkInputs.end()) {
            throw std::runtime_error(
                "multiple fanins share one physical input port at sink node " +
                std::to_string(key.second));
        }
        sinkInputs.push_back(sinkPort);
    }

    for (auto &entry : nodeLinks) {
        auto &inputs = entry.second.first;
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

        auto &outputs = entry.second.second;
        std::sort(outputs.begin(), outputs.end());
        outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    }

    mapping.node_mapping(nodeLinks, mappingMode);
    auto routeCellsByPath = mapping.mapping_line(
        routePaths, mappingMode, routeIterationDistances);
    std::string crossoverError;
    if (!mapping.validate_crossovers(&crossoverError)) {
        std::cerr << "invalid crossover mapping: " << crossoverError << '\n';
        return 4;
    }
    double ioContractionSeconds = 0.0;
    if (contractIoPorts) {
        const auto contractionStart = std::chrono::steady_clock::now();
        mapping.contract_io_ports(nodeLinks, routeCellsByPath);
        const auto contractionEnd = std::chrono::steady_clock::now();
        ioContractionSeconds =
            std::chrono::duration<double>(contractionEnd - contractionStart).count();
    }
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
    std::cout << cellCount << ' ' << crossCount;
    if (printTiming) {
        std::cout << ' ' << std::fixed << std::setprecision(9) << ioContractionSeconds;
    }
    std::cout << '\n';
    return 0;
    } catch (const std::exception &error) {
        std::cerr << "invalid IFCN mapping input: " << error.what() << '\n';
        return 5;
    }
}
