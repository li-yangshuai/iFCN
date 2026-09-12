#include <algorithm>
#include <cctype>
#include <cmath>
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
#include <tuple>
#include <unordered_set>
#include <vector>

#include <autopr/algorithms/mapping.h>
#include <autopr/io/ifcnMappingMetadata.h>
#include <simon/simon.hpp>

namespace {

using fcngraph::Mapping;
using fcngraph::MappingMode;
using fcngraph::MappingPositionHash;
using fcngraph::PhysicalCellSite;
using fcngraph::IfcnMappingModeResolver;
using fcngraph::position;

struct NodeInfo {
    std::string name;
    std::string type;
    position pos{0, 0};
};

struct LayoutData {
    std::map<int, NodeInfo> nodes;
    std::map<std::pair<int, int>, std::vector<position>> routes;
    std::map<std::pair<int, int>, unsigned int> iterationDistances;
    std::map<position, int> phases;
    std::map<std::tuple<unsigned int, unsigned int, int>, int> physicalPhases;
    bool hasPhysicalPhaseMap = false;
    bool exactPhysicalPhaseTrace = false;
    std::optional<std::string> phaseGranularity;
    MappingMode mappingMode = MappingMode::Combinational;
};

enum class CellFunction {
    Normal,
    Input,
    Output,
    Fixed,
};

enum class CellMode {
    Normal,
    Crossover,
    Vertical,
};

struct Cell {
    position pos{0, 0};
    int layer = 0;
    int phase = 0;
    CellFunction function = CellFunction::Normal;
    CellMode mode = CellMode::Normal;
    std::string name;
    double fixedPolarization = 0.0;
};

struct ShiftedPosition {
    position pos{0, 0};
    bool valid = false;
};

struct EnergyRunOptions {
    double timeStep = 1.0e-17;
    double duration = 8.0e-11;
    double clockSlope = 1.0e-12;
    double clockPeriod = 1.0e-11;
    double inputPeriod = 1.0e-11;
    bool writeWaveform = false;
    bool qcaOnly = false;
    bool contractIoPorts = false;
    std::string vectorTablePath;
    std::vector<std::string> probeNodeNames;
};

constexpr std::size_t kMaxSamePhaseTileRun = 4;

void validateSequentialTilePhases(const LayoutData &data)
{
    for (const auto &[edge, path] : data.routes) {
        if (path.empty()) {
            continue;
        }

        int previousPhase = data.phases.at(path.front());
        std::size_t samePhaseRun = 1;
        for (std::size_t index = 1; index < path.size(); ++index) {
            const position &previousTile = path[index - 1];
            const position &tile = path[index];
            const int phase = data.phases.at(tile);
            const int delta = (phase - previousPhase + 4) % 4;
            if (delta != 0 && delta != 1) {
                throw std::runtime_error(
                    "sequential IFCN tile phase DRC failed on route " +
                    std::to_string(edge.first) + "->" +
                    std::to_string(edge.second) + ": ordered tiles (" +
                    std::to_string(previousTile.first) + "," +
                    std::to_string(previousTile.second) + ") phase " +
                    std::to_string(previousPhase) + " -> (" +
                    std::to_string(tile.first) + "," +
                    std::to_string(tile.second) + ") phase " +
                    std::to_string(phase) +
                    " must be hold or +1 (mod 4)");
            }

            if (phase == previousPhase) {
                ++samePhaseRun;
                if (samePhaseRun > kMaxSamePhaseTileRun) {
                    throw std::runtime_error(
                        "sequential IFCN tile phase DRC failed on route " +
                        std::to_string(edge.first) + "->" +
                        std::to_string(edge.second) +
                        ": more than 4 consecutive same-phase tiles ending "
                        "at ordered tile " + std::to_string(index) + " (" +
                        std::to_string(tile.first) + "," +
                        std::to_string(tile.second) + "), phase " +
                        std::to_string(phase));
                }
            } else {
                samePhaseRun = 1;
            }
            previousPhase = phase;
        }
    }
}

void applyFastEnergyOptions(EnergyRunOptions &options)
{
    options.timeStep = 2.0e-15;
    options.duration = 8.0e-11;
    options.clockSlope = 1.0e-12;
    options.clockPeriod = 1.0e-11;
    options.inputPeriod = 1.0e-11;
}

double parseDoubleArg(const char *name, const char *value)
{
    try {
        return std::stod(value);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string("invalid numeric value for ") + name + ": " + value);
    }
}

void parseEnergyOptions(int argc, char **argv, int startIndex, EnergyRunOptions &options)
{
    for (int i = startIndex; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--fast") {
            applyFastEnergyOptions(options);
        } else if (arg == "--qca-only") {
            options.qcaOnly = true;
        } else if (arg == "--waveform") {
            options.writeWaveform = true;
        } else if (arg == "--io-contraction") {
            options.contractIoPorts = true;
        } else if (arg == "--vectors") {
            options.vectorTablePath = requireValue("--vectors");
        } else if (arg == "--probe-node") {
            options.probeNodeNames.emplace_back(requireValue("--probe-node"));
        } else if (arg == "--time-step") {
            options.timeStep = parseDoubleArg("--time-step", requireValue("--time-step"));
        } else if (arg == "--duration") {
            options.duration = parseDoubleArg("--duration", requireValue("--duration"));
        } else if (arg == "--clock-slope") {
            options.clockSlope = parseDoubleArg("--clock-slope", requireValue("--clock-slope"));
        } else if (arg == "--clock-period") {
            options.clockPeriod = parseDoubleArg("--clock-period", requireValue("--clock-period"));
        } else if (arg == "--input-period") {
            options.inputPeriod = parseDoubleArg("--input-period", requireValue("--input-period"));
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (options.timeStep <= 0.0 || options.duration <= 0.0 || options.clockPeriod <= 0.0 ||
        options.inputPeriod <= 0.0 || options.clockSlope <= 0.0) {
        throw std::runtime_error("energy timing options must be positive");
    }
}

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

ShiftedPosition shiftedPosition(const position &base, int dx, int dy)
{
    const long long x = static_cast<long long>(base.first) + dx;
    const long long y = static_cast<long long>(base.second) + dy;
    const long long maxCoord = static_cast<long long>(std::numeric_limits<unsigned int>::max());
    if (x < 0 || y < 0 || x > maxCoord || y > maxCoord) {
        return {};
    }
    return {{static_cast<unsigned int>(x), static_cast<unsigned int>(y)}, true};
}

bool containsPosition(const std::unordered_set<position, MappingPositionHash> &positions,
                      const ShiftedPosition &candidate)
{
    return candidate.valid && positions.find(candidate.pos) != positions.end();
}

LayoutData parseIfcn(const std::string &filename)
{
    std::ifstream input(filename);
    if (!input) {
        throw std::runtime_error("cannot open " + filename);
    }

    LayoutData data;
    std::string line;
    std::string section;

    const std::regex nodePattern(
        R"(^\s*(\d+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*\((-?\d+),(-?\d+)\)\s*;)");
    const std::regex routePattern(R"(^\s*\((\d+),(\d+)\)\s*:\s*(.*);)");
    const std::regex coordPattern(R"(\((-?\d+),(-?\d+)\))");
    const std::regex phasePattern(R"(\((-?\d+),(-?\d+)\)\s*:\s*(-?\d+)\s*;)");
    const std::regex physicalPhaseSectionPattern(
        R"(^\s*#\s*physical\s+phase\s+map\s*$)", std::regex::icase);
    const std::regex physicalPhaseSectionKeyPattern(
        R"(^\s*#\s*physical\s+phase\s+map\b.*$)", std::regex::icase);
    const std::regex physicalPhasePattern(
        R"(^\s*\((\d+)\s*,\s*(\d+)\s*,\s*(\d+)\)\s*:\s*(\d+)\s*;\s*$)");
    const std::regex physicalPhaseTracePattern(
        R"(^\s*#\s*physical\s+phase\s+trace\s*:\s*layer_aware_xyz\s*$)",
        std::regex::icase);
    const std::regex physicalPhaseTraceKeyPattern(
        R"(^\s*#\s*physical\s+phase\s+trace\b.*$)", std::regex::icase);
    const std::regex mappingModePattern(
        R"(^\s*#\s*mapping\s+mode\s*:\s*(.*?)\s*$)",
        std::regex::icase);
    const std::regex mappingModeKeyPattern(
        R"(^\s*#\s*mapping\s+mode\b.*$)", std::regex::icase);
    const std::regex phaseGranularityPattern(
        R"(^\s*#\s*phase\s+granularity\s*:\s*(.*?)\s*$)",
        std::regex::icase);
    const std::regex phaseGranularityKeyPattern(
        R"(^\s*#\s*phase\s+granularity\b.*$)", std::regex::icase);
    const std::regex flowPattern(
        R"(^\s*#\s*flow\s*:\s*(.*?)\s*$)", std::regex::icase);
    const std::regex iterationDistancePattern(
        R"(^\s*#\s*iteration(?:_|\s+)distance\s*(?:=|:)\s*(\d+)\s*$)",
        std::regex::icase);
    const std::regex iterationDistanceKeyPattern(
        R"(^\s*#\s*iteration(?:_|\s+)distance\b.*$)",
        std::regex::icase);
    std::optional<unsigned int> pendingIterationDistance;
    std::set<std::pair<int, int>> routesWithExplicitDistance;
    IfcnMappingModeResolver mappingModeResolver;
    bool physicalPhaseSectionSeen = false;
    bool physicalPhaseTraceSeen = false;
    bool phaseGranularitySeen = false;
    std::size_t physicalPhaseEntryCount = 0;

    const auto rejectDanglingDistance = [&]() {
        if (pendingIterationDistance.has_value()) {
            throw std::runtime_error(
                "iteration_distance is not followed by a route in the paths section");
        }
    };

    while (std::getline(input, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        std::smatch metadataMatch;
        if (std::regex_match(line, physicalPhaseSectionPattern)) {
            rejectDanglingDistance();
            if (section == "physical_phase") {
                if (physicalPhaseEntryCount == 0) {
                    throw std::runtime_error(
                        "IFCN physical phase map section contains no entries");
                }
                section.clear();
            } else {
                if (!section.empty()) {
                    throw std::runtime_error(
                        "IFCN physical phase map starts before the current section is closed");
                }
                if (physicalPhaseSectionSeen) {
                    throw std::runtime_error(
                        "duplicate IFCN physical phase map section");
                }
                physicalPhaseSectionSeen = true;
                data.hasPhysicalPhaseMap = true;
                section = "physical_phase";
            }
            continue;
        }
        if (std::regex_match(line, physicalPhaseSectionKeyPattern)) {
            throw std::runtime_error("malformed IFCN physical phase map section marker");
        }
        if (section == "physical_phase") {
            const auto firstNonWhitespace = line.find_first_not_of(" \t\r\n");
            if (firstNonWhitespace != std::string::npos &&
                line[firstNonWhitespace] == '#') {
                const std::string lowered = lowerCopy(line.substr(firstNonWhitespace));
                if (lowered.rfind("#nodes info", 0) == 0 ||
                    lowered.rfind("#paths info", 0) == 0 ||
                    lowered.rfind("#phase map", 0) == 0 ||
                    lowered.rfind("#encoded phase map", 0) == 0 ||
                    lowered.rfind("#phase codec", 0) == 0 ||
                    lowered.rfind("#physical phase trace", 0) == 0 ||
                    lowered.rfind("#phase granularity", 0) == 0 ||
                    lowered.rfind("#mapping mode", 0) == 0 ||
                    lowered.rfind("#iteration_distance", 0) == 0 ||
                    lowered.rfind("#iteration distance", 0) == 0) {
                    throw std::runtime_error(
                        "IFCN physical phase map section is not closed");
                }
                continue;
            }

            if (!std::regex_match(line, metadataMatch, physicalPhasePattern)) {
                throw std::runtime_error(
                    "malformed IFCN physical phase map entry: " + line);
            }
            const unsigned long long x = std::stoull(metadataMatch[1].str());
            const unsigned long long y = std::stoull(metadataMatch[2].str());
            const unsigned long long layer = std::stoull(metadataMatch[3].str());
            const unsigned long long phase = std::stoull(metadataMatch[4].str());
            if (x > std::numeric_limits<unsigned int>::max() ||
                y > std::numeric_limits<unsigned int>::max()) {
                throw std::runtime_error(
                    "IFCN physical phase map coordinate is out of range");
            }
            if (layer > 2) {
                throw std::runtime_error(
                    "IFCN physical phase map layer is out of range (expected 0..2)");
            }
            if (phase > 3) {
                throw std::runtime_error(
                    "IFCN physical phase is out of range (expected 0..3)");
            }
            const auto key = std::make_tuple(static_cast<unsigned int>(x),
                                             static_cast<unsigned int>(y),
                                             static_cast<int>(layer));
            const auto [entry, inserted] = data.physicalPhases.emplace(
                key, static_cast<int>(phase));
            if (!inserted && entry->second != static_cast<int>(phase)) {
                throw std::runtime_error(
                    "conflicting duplicate IFCN physical phase map entry");
            }
            ++physicalPhaseEntryCount;
            continue;
        }
        if (std::regex_match(line, physicalPhaseTracePattern)) {
            if (physicalPhaseTraceSeen) {
                throw std::runtime_error(
                    "duplicate IFCN physical phase trace declaration");
            }
            physicalPhaseTraceSeen = true;
            data.exactPhysicalPhaseTrace = true;
            continue;
        }
        if (std::regex_match(line, physicalPhaseTraceKeyPattern)) {
            throw std::runtime_error(
                "malformed or unsupported IFCN physical phase trace declaration");
        }
        if (std::regex_match(line, metadataMatch, phaseGranularityPattern)) {
            if (phaseGranularitySeen) {
                throw std::runtime_error(
                    "duplicate IFCN phase granularity declaration");
            }
            phaseGranularitySeen = true;
            const std::string granularity =
                lowerCopy(metadataMatch[1].str());
            if (granularity.empty()) {
                throw std::runtime_error(
                    "malformed IFCN phase granularity declaration");
            }
            data.phaseGranularity = granularity;
            continue;
        }
        if (std::regex_match(line, phaseGranularityKeyPattern)) {
            throw std::runtime_error(
                "malformed IFCN phase granularity declaration");
        }
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
        if (line.rfind("#phase map", 0) == 0) {
            rejectDanglingDistance();
            section = (section == "phase") ? "" : "phase";
            continue;
        }

        std::smatch match;
        if (section == "nodes" && std::regex_search(line, match, nodePattern)) {
            position pos;
            if (parsePosition(match[4].str(), match[5].str(), pos)) {
                data.nodes[std::stoi(match[1])] = {match[2].str(), lowerCopy(match[3].str()), pos};
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
            if (data.routes.find(routeKey) != data.routes.end()) {
                throw std::runtime_error(
                    "duplicate IFCN route " + std::to_string(routeKey.first) +
                    "->" + std::to_string(routeKey.second));
            }
            const unsigned int iterationDistance =
                pendingIterationDistance.value_or(0);
            data.routes.emplace(routeKey, std::move(path));
            data.iterationDistances.emplace(routeKey, iterationDistance);
            mappingModeResolver.observeIterationDistance(iterationDistance);
            if (pendingIterationDistance.has_value()) {
                routesWithExplicitDistance.insert(routeKey);
            }
            pendingIterationDistance.reset();
        } else if (section == "phase") {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), phasePattern);
                 it != std::sregex_iterator();
                 ++it) {
                position pos;
                if (parsePosition((*it)[1].str(), (*it)[2].str(), pos)) {
                    data.phases[pos] = std::stoi((*it)[3].str());
                }
            }
        }
    }
    rejectDanglingDistance();
    if (section == "physical_phase") {
        throw std::runtime_error("IFCN physical phase map section is not closed");
    }
    const fcngraph::IfcnMappingModeResolution modeResolution =
        mappingModeResolver.resolve();
    data.mappingMode = modeResolution.mode;
    if (modeResolution.explicitMode &&
        data.mappingMode == MappingMode::Sequential &&
        routesWithExplicitDistance.size() != data.routes.size()) {
        throw std::runtime_error(
            "sequential IFCN requires iteration_distance before every route");
    }
    if (data.mappingMode == MappingMode::Sequential) {
        if (modeResolution.explicitMode && !data.phaseGranularity.has_value()) {
            throw std::runtime_error(
                "canonical sequential IFCN requires '#phase granularity: tile'");
        }
        if (data.phaseGranularity.has_value() &&
            *data.phaseGranularity != "tile") {
            throw std::runtime_error(
                "sequential IFCN requires tile phase granularity; stale "
                "qca_cell phase data is unsupported");
        }
        if (data.hasPhysicalPhaseMap || data.exactPhysicalPhaseTrace) {
            throw std::runtime_error(
                "sequential IFCN rejects stale physical phase trace/map; "
                "the coarse tile phase map is authoritative");
        }
        for (const auto &[tile, phase] : data.phases) {
            (void)tile;
            if (phase < 0 || phase > 3) {
                throw std::runtime_error(
                    "sequential IFCN tile phase is out of range (expected 0..3)");
            }
        }
        std::set<position> occupiedTiles;
        for (const auto &[index, node] : data.nodes) {
            (void)index;
            occupiedTiles.insert(node.pos);
        }
        for (const auto &[edge, path] : data.routes) {
            (void)edge;
            occupiedTiles.insert(path.begin(), path.end());
        }
        for (const position &tile : occupiedTiles) {
            if (data.phases.count(tile) == 0) {
                throw std::runtime_error(
                    "sequential IFCN tile phase map is missing occupied tile (" +
                    std::to_string(tile.first) + "," +
                    std::to_string(tile.second) + ")");
            }
        }
        validateSequentialTilePhases(data);
    } else if (data.exactPhysicalPhaseTrace && !data.hasPhysicalPhaseMap) {
        throw std::runtime_error(
            "layer_aware_xyz physical phase trace requires a physical phase map");
    }
    return data;
}

int phaseAt(const LayoutData &data, const position &mappedCell, int layer)
{
    const position sourceTile{mappedCell.first / 5, mappedCell.second / 5};
    if (data.mappingMode == MappingMode::Sequential) {
        const auto tilePhase = data.phases.find(sourceTile);
        if (tilePhase == data.phases.end()) {
            throw std::runtime_error(
                "sequential IFCN tile phase map is missing tile (" +
                std::to_string(sourceTile.first) + "," +
                std::to_string(sourceTile.second) + ") for mapped cell (" +
                std::to_string(mappedCell.first) + "," +
                std::to_string(mappedCell.second) + "," +
                std::to_string(layer) + ")");
        }
        if (tilePhase->second < 0 || tilePhase->second > 3) {
            throw std::runtime_error(
                "sequential IFCN tile phase is out of range at (" +
                std::to_string(sourceTile.first) + "," +
                std::to_string(sourceTile.second) + ")");
        }
        return tilePhase->second;
    }

    if (data.hasPhysicalPhaseMap) {
        const auto it = data.physicalPhases.find(
            std::make_tuple(mappedCell.first, mappedCell.second, layer));
        if (it == data.physicalPhases.end()) {
            throw std::runtime_error(
                "IFCN physical phase map is missing mapped cell (" +
                std::to_string(mappedCell.first) + "," +
                std::to_string(mappedCell.second) + "," +
                std::to_string(layer) + ")");
        }
        return it->second;
    }

    const auto it = data.phases.find(sourceTile);
    if (it == data.phases.end() || it->second < 0) {
        return 0;
    }
    return std::clamp(it->second, 0, 3);
}

void addCell(std::vector<Cell> &cells,
             const LayoutData &data,
             const position &mappedCell,
             int layer,
             CellFunction function,
             CellMode mode = CellMode::Normal,
             std::string name = {},
             double fixedPolarization = 0.0)
{
    cells.push_back({mappedCell,
                     layer,
                     phaseAt(data, mappedCell, layer),
                     function,
                     mode,
                     std::move(name),
                     fixedPolarization});
}

std::vector<Cell> mapIfcnToCells(const LayoutData &data,
                                 bool contractIoPorts,
                                 const std::vector<std::string> &probeNodeNames)
{
    Mapping mapping;
    std::vector<std::vector<position>> routePaths;
    std::vector<unsigned int> routeIterationDistances;
    routePaths.reserve(data.routes.size());
    routeIterationDistances.reserve(data.routes.size());
    for (const auto &route : data.routes) {
        routePaths.push_back(route.second);
        routeIterationDistances.push_back(data.iterationDistances.at(route.first));
    }

    std::map<position, std::string> nodeNameByPos;
    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> nodeLinks;
    for (const auto &node : data.nodes) {
        nodeNameByPos[node.second.pos] = node.second.name;
        nodeLinks.try_emplace({node.second.pos, node.second.type},
                              std::make_pair(std::vector<position>{}, std::vector<position>{}));
    }

    for (const auto &route : data.routes) {
        const auto &key = route.first;
        const auto &path = route.second;
        if (path.size() < 2 || !data.nodes.count(key.first) || !data.nodes.count(key.second)) {
            continue;
        }

        const NodeInfo &source = data.nodes.at(key.first);
        const NodeInfo &sink = data.nodes.at(key.second);
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

    mapping.node_mapping(nodeLinks, data.mappingMode);
    auto routeCellsByPath = mapping.mapping_line(
        routePaths, data.mappingMode, routeIterationDistances);
    std::string crossoverError;
    if (!mapping.validate_crossovers(&crossoverError)) {
        throw std::runtime_error("invalid crossover mapping: " + crossoverError);
    }
    if (contractIoPorts) {
        mapping.contract_io_ports(nodeLinks, routeCellsByPath);
    }
    const auto nodeCellsByType = mapping.nodecell_list;
    const auto crossCellsByPath = mapping.crossline_list;
    const auto ioTerminalOrigins = mapping.io_terminal_origins();

    const auto ioNameForCell = [&](const position &cellPos) {
        position ownerPos{cellPos.first / 5, cellPos.second / 5};
        const auto originIt = ioTerminalOrigins.find(cellPos);
        if (originIt != ioTerminalOrigins.end()) {
            ownerPos = originIt->second;
        }
        const auto nameIt = nodeNameByPos.find(ownerPos);
        if (nameIt == nodeNameByPos.end()) {
            return std::string{};
        }
        // Verilog escaped identifiers such as "\\1" are stored with their
        // escape prefix by the IFCN parser, while QCADesigner and MNTDesigner
        // label the corresponding primary input as "1".  Emit the common
        // physical-port label so a shared vector table can match both layouts.
        std::string name = nameIt->second;
        if (!name.empty() && name.front() == '\\') {
            name.erase(name.begin());
        }
        return name;
    };

    std::vector<Cell> cells;
    for (const auto &entry : nodeCellsByType) {
        const std::string &type = entry.first;
        for (const position &cellPos : entry.second) {
            if (type == "input") {
                addCell(cells,
                        data,
                        cellPos,
                        0,
                        CellFunction::Input,
                        CellMode::Normal,
                        ioNameForCell(cellPos));
            } else if (type == "output") {
                addCell(cells,
                        data,
                        cellPos,
                        0,
                        CellFunction::Output,
                        CellMode::Normal,
                        ioNameForCell(cellPos));
            } else if (type == "fix0") {
                addCell(cells,
                        data,
                        cellPos,
                        0,
                        CellFunction::Fixed,
                        CellMode::Normal,
                        "-1.00",
                        -1.0);
            } else if (type == "fix1") {
                addCell(cells,
                        data,
                        cellPos,
                        0,
                        CellFunction::Fixed,
                        CellMode::Normal,
                        "1.00",
                        1.0);
            } else {
                addCell(cells, data, cellPos, 0, CellFunction::Normal);
            }
        }
    }

    if (data.mappingMode == MappingMode::Sequential) {
        // Mapping owns the sequential topology contract.  Its ordered physical
        // routes split crossover ownership per route, put a full L0/L1/L2
        // pillar at both ends of every maximal lifted run, and reject an exact
        // layer site shared by routes with different sources.
        std::set<PhysicalCellSite> physicalSites =
            mapping.physicalCellSites(routePaths);

        // I/O contraction edits the public node/route cell sets after the
        // ordered routes have been constructed.  Retain only XY coordinates
        // that survived that optional edit while preserving every crossover
        // corridor and pillar layer at a retained coordinate.
        if (contractIoPorts) {
            std::set<position> retainedXy;
            for (const auto &bucket : nodeCellsByType) {
                retainedXy.insert(bucket.second.begin(), bucket.second.end());
            }
            for (const auto &route : routeCellsByPath) {
                for (const auto &segment : route.second) {
                    retainedXy.insert(segment.begin(), segment.end());
                }
            }
            for (const auto &route : crossCellsByPath) {
                for (const auto &segment : route.second) {
                    retainedXy.insert(segment.begin(), segment.end());
                }
            }
            for (auto site = physicalSites.begin(); site != physicalSites.end();) {
                if (retainedXy.count(site->xy) == 0) {
                    site = physicalSites.erase(site);
                } else {
                    ++site;
                }
            }
        }

        std::map<position, std::set<int>> layersByXy;
        for (const PhysicalCellSite &site : physicalSites) {
            layersByXy[site.xy].insert(site.layer);
        }
        for (const auto &entry : layersByXy) {
            const std::set<int> &layers = entry.second;
            if (layers.count(1) != 0 && layers != std::set<int>{0, 1, 2}) {
                throw std::runtime_error(
                    "invalid sequential vertical stack at (" +
                    std::to_string(entry.first.first) + "," +
                    std::to_string(entry.first.second) + ")");
            }
        }
        for (const PhysicalCellSite &site : physicalSites) {
            const bool isPillar = layersByXy.at(site.xy).count(1) != 0;
            const CellMode mode = isPillar
                                      ? CellMode::Vertical
                                      : (site.layer == 2
                                             ? CellMode::Crossover
                                             : CellMode::Normal);
            addCell(cells, data, site.xy, site.layer,
                    CellFunction::Normal, mode);
        }
    } else {
        // Preserve the historical combinational exporter, including its
        // global-neighbour endpoint treatment.
        std::unordered_set<position, MappingPositionHash> crossCellSet;
        std::unordered_set<position, MappingPositionHash> verticalCellSet;
        std::map<std::pair<position, position>,
                 std::unordered_set<position, MappingPositionHash>>
            crossCellsByRoute;
        auto addVerticalStack = [&](const position &cell) {
            addCell(cells, data, cell, 0, CellFunction::Normal, CellMode::Vertical);
            addCell(cells, data, cell, 1, CellFunction::Normal, CellMode::Vertical);
            addCell(cells, data, cell, 2, CellFunction::Normal, CellMode::Vertical);
            verticalCellSet.insert(cell);
        };
        for (const auto &crossLine : crossCellsByPath) {
            for (const auto &cross : crossLine.second) {
                crossCellSet.insert(cross.begin(), cross.end());
                crossCellsByRoute[crossLine.first].insert(cross.begin(), cross.end());
            }
        }

        for (const auto &crossLine : crossCellsByPath) {
            for (const auto &cross : crossLine.second) {
                for (auto unit = cross.begin(); unit != cross.end(); ++unit) {
                    if (unit == cross.begin() || std::next(unit) == cross.end()) {
                        int count = 0;
                        const position base = *unit;
                        const auto dir1 = shiftedPosition(base, 0, 1);
                        const auto dir2 = shiftedPosition(base, 0, -1);
                        const auto dir3 = shiftedPosition(base, -1, 0);
                        const auto dir4 = shiftedPosition(base, 1, 0);
                        count += containsPosition(crossCellSet, dir1) ? 1 : 0;
                        count += containsPosition(crossCellSet, dir2) ? 1 : 0;
                        count += containsPosition(crossCellSet, dir3) ? 1 : 0;
                        count += containsPosition(crossCellSet, dir4) ? 1 : 0;

                        if (count >= 2) {
                            addCell(cells, data, *unit, 2,
                                    CellFunction::Normal, CellMode::Crossover);
                        } else {
                            addVerticalStack(*unit);
                        }
                    } else {
                        addCell(cells, data, *unit, 2,
                                CellFunction::Normal, CellMode::Crossover);
                    }
                }
            }
        }

        for (const auto &line : routeCellsByPath) {
            const auto routeCrossIt = crossCellsByRoute.find(line.first);
            for (const auto &segment : line.second) {
                for (const position &pos : segment) {
                    if (verticalCellSet.find(pos) != verticalCellSet.end()) {
                        continue;
                    }

                    const bool routeOwnsCrossCell =
                        routeCrossIt != crossCellsByRoute.end()
                        && routeCrossIt->second.find(pos) != routeCrossIt->second.end();
                    if (routeOwnsCrossCell) {
                        continue;
                    }

                    addCell(cells, data, pos, 0, CellFunction::Normal);
                }
            }
        }
    }

    // Node templates and routed wires may both emit the same physical site.
    // Keeping both creates a zero-distance pair and an infinite kink energy in
    // the simulator.  Preserve the first (node cells are emitted first), while
    // enriching an ordinary cell with any later special mode or I/O metadata.
    std::vector<Cell> uniqueCells;
    std::map<std::pair<position, int>, std::size_t> siteIndex;
    for (const Cell &cell : cells) {
        const auto key = std::make_pair(cell.pos, cell.layer);
        const auto [it, inserted] = siteIndex.emplace(key, uniqueCells.size());
        if (inserted) {
            uniqueCells.push_back(cell);
            continue;
        }
        Cell &kept = uniqueCells[it->second];
        if (kept.function == CellFunction::Normal && cell.function != CellFunction::Normal) {
            kept.function = cell.function;
            kept.name = cell.name;
            kept.fixedPolarization = cell.fixedPolarization;
        }
        if (kept.mode == CellMode::Normal && cell.mode != CellMode::Normal) kept.mode = cell.mode;
    }

    if (data.exactPhysicalPhaseTrace) {
        std::set<std::tuple<unsigned int, unsigned int, int>> emittedSites;
        for (const Cell &cell : uniqueCells) {
            emittedSites.emplace(cell.pos.first, cell.pos.second, cell.layer);
        }
        std::set<std::tuple<unsigned int, unsigned int, int>> declaredSites;
        for (const auto &entry : data.physicalPhases) {
            declaredSites.insert(entry.first);
        }
        if (emittedSites != declaredSites) {
            for (const auto &site : emittedSites) {
                if (declaredSites.count(site) == 0) {
                    throw std::runtime_error(
                        "layer_aware_xyz physical phase trace is missing emitted site (" +
                        std::to_string(std::get<0>(site)) + "," +
                        std::to_string(std::get<1>(site)) + "," +
                        std::to_string(std::get<2>(site)) + ")");
                }
            }
            for (const auto &site : declaredSites) {
                if (emittedSites.count(site) == 0) {
                    throw std::runtime_error(
                        "layer_aware_xyz physical phase trace contains extra site (" +
                        std::to_string(std::get<0>(site)) + "," +
                        std::to_string(std::get<1>(site)) + "," +
                        std::to_string(std::get<2>(site)) + ")");
                }
            }
        }
    }

    for (const std::string &probeNodeName : probeNodeNames) {
        const auto nodeIt = std::find_if(data.nodes.begin(), data.nodes.end(),
                                         [&](const auto &entry) {
                                             return entry.second.name == probeNodeName;
                                         });
        if (nodeIt == data.nodes.end()) {
            throw std::runtime_error("probe node not found in IFCN layout: " + probeNodeName);
        }

        position probeCell{nodeIt->second.pos.first * 5U + 2U,
                           nodeIt->second.pos.second * 5U + 2U};
        // For a logic node, observe the mapped port that actually drives its
        // outgoing feedback route.  The majority-gate center is not always
        // the logical terminal (especially when a fixed-polarization arm is
        // present).  A primary-output pseudo node is already observed at its
        // center by the existing mapping ABI.
        if (nodeIt->second.type != "output") {
            const int nodeIndex = nodeIt->first;
            const auto routeIt = std::find_if(data.routes.begin(), data.routes.end(),
                                               [&](const auto &entry) {
                                                   return entry.first.first == nodeIndex &&
                                                          entry.second.size() >= 2;
                                               });
            if (routeIt == data.routes.end()) {
                throw std::runtime_error("probe node has no outgoing physical route: " +
                                         probeNodeName);
            }
            const position next = routeIt->second[1];
            const position coarse = nodeIt->second.pos;
            if (next.first < coarse.first) {
                probeCell = {coarse.first * 5U, coarse.second * 5U + 2U};
            } else if (next.first > coarse.first) {
                probeCell = {coarse.first * 5U + 4U, coarse.second * 5U + 2U};
            } else if (next.second < coarse.second) {
                probeCell = {coarse.first * 5U + 2U, coarse.second * 5U};
            } else if (next.second > coarse.second) {
                probeCell = {coarse.first * 5U + 2U, coarse.second * 5U + 4U};
            }
        }
        const auto cellIt = std::find_if(uniqueCells.begin(), uniqueCells.end(),
                                         [&](const Cell &cell) {
                                             return cell.layer == 0 && cell.pos == probeCell;
                                         });
        if (cellIt == uniqueCells.end()) {
            throw std::runtime_error("mapped output-port cell is unavailable for probe node: " +
                                     probeNodeName);
        }
        if (cellIt->function == CellFunction::Input ||
            cellIt->function == CellFunction::Fixed) {
            throw std::runtime_error("probe node center is not a passive dynamic cell: " +
                                     probeNodeName);
        }
        cellIt->function = CellFunction::Output;
        cellIt->name = "feedback_probe_" + probeNodeName;
    }
    return uniqueCells;
}

const char *functionName(CellFunction function)
{
    switch (function) {
    case CellFunction::Input:
        return "QCAD_CELL_INPUT";
    case CellFunction::Output:
        return "QCAD_CELL_OUTPUT";
    case CellFunction::Fixed:
        return "QCAD_CELL_FIXED";
    case CellFunction::Normal:
    default:
        return "QCAD_CELL_NORMAL";
    }
}

const char *modeName(CellMode mode)
{
    switch (mode) {
    case CellMode::Crossover:
        return "QCAD_CELL_MODE_CROSSOVER";
    case CellMode::Vertical:
        return "QCAD_CELL_MODE_VERTICAL";
    case CellMode::Normal:
    default:
        return "QCAD_CELL_MODE_NORMAL";
    }
}

void writeCellColor(std::ostream &out, CellFunction function, int phase)
{
    if (function == CellFunction::Input) {
        out << "clr.red=0\nclr.green=0\nclr.blue=65535\n";
        return;
    }
    if (function == CellFunction::Output) {
        out << "clr.red=65535\nclr.green=65535\nclr.blue=0\n";
        return;
    }
    if (function == CellFunction::Fixed) {
        out << "clr.red=65535\nclr.green=32768\nclr.blue=0\n";
        return;
    }
    switch (phase) {
    case 0:
        out << "clr.red=0\nclr.green=65535\nclr.blue=0\n";
        break;
    case 1:
        out << "clr.red=65535\nclr.green=0\nclr.blue=65535\n";
        break;
    case 2:
        out << "clr.red=0\nclr.green=65535\nclr.blue=65535\n";
        break;
    case 3:
        out << "clr.red=65535\nclr.green=65535\nclr.blue=65535\n";
        break;
    default:
        out << "clr.red=0\nclr.green=0\nclr.blue=0\n";
        break;
    }
}

void writeQcaCell(std::ostream &out, const Cell &cell)
{
    const double x = static_cast<double>(cell.pos.first * 20U + 200U);
    const double y = static_cast<double>(cell.pos.second * 20U + 200U);
    out << "[TYPE:QCADCell]\n";
    out << "[TYPE:QCADDesignObject]\n";
    out << "x=" << x << "\n";
    out << "y=" << y << "\n";
    out << "bSelected=FALSE\n";
    writeCellColor(out, cell.function, cell.phase);
    out << "bounding_box.xWorld=" << x - 10.0 << "\n";
    out << "bounding_box.yWorld=" << y - 10.0 << "\n";
    out << "bounding_box.cxWorld=20\n";
    out << "bounding_box.cyWorld=20\n";
    out << "[#TYPE:QCADDesignObject]\n";
    out << "cell_options.cxCell=18\n";
    out << "cell_options.cyCell=18\n";
    out << "cell_options.dot_diameter=5.000000\n";
    out << "cell_options.clock=" << cell.phase << "\n";
    out << "cell_options.mode=" << modeName(cell.mode) << "\n";
    out << "cell_function=" << functionName(cell.function) << "\n";
    out << "number_of_dots=4\n";

    const double offsets[4][2] = {{4.5, -4.5}, {4.5, 4.5}, {-4.5, 4.5}, {-4.5, -4.5}};
    for (std::size_t dotIndex = 0; dotIndex < 4; ++dotIndex) {
        const auto &offset = offsets[dotIndex];
        double charge = 0.0;
        if (cell.function == CellFunction::Fixed) {
            if (cell.fixedPolarization != -1.0 && cell.fixedPolarization != 1.0) {
                throw std::runtime_error("fixed QCA cell requires polarization -1 or +1");
            }
            const bool chargedDiagonal = cell.fixedPolarization > 0.0
                                             ? dotIndex % 2 == 0
                                             : dotIndex % 2 == 1;
            if (chargedDiagonal) {
                charge = simon::constants::QCHARGE;
            }
        }
        out << "[TYPE:CELL_DOT]\n";
        out << "x=" << x + offset[0] << "\n";
        out << "y=" << y + offset[1] << "\n";
        out << "diameter=5\n";
        const std::streamsize oldPrecision = out.precision();
        out << "charge=" << std::setprecision(std::numeric_limits<double>::max_digits10)
            << charge << "\n";
        out.precision(oldPrecision);
        out << "spin=0\n";
        out << "potential=0\n";
        out << "[#TYPE:CELL_DOT]\n";
    }

    if (!cell.name.empty()) {
        out << "[TYPE:QCADLabel]\n";
        out << "[TYPE:QCADStretchyObject]\n";
        out << "[TYPE:QCADDesignObject]\n";
        out << "x=" << x << "\n";
        out << "y=" << y - 20.0 << "\n";
        out << "bSelected=FALSE\n";
        writeCellColor(out, cell.function, cell.phase);
        out << "bounding_box.xWorld=" << x - 10.0 << "\n";
        out << "bounding_box.yWorld=" << y - 31.0 << "\n";
        out << "bounding_box.cxWorld=20\n";
        out << "bounding_box.cyWorld=22\n";
        out << "[#TYPE:QCADDesignObject]\n";
        out << "[#TYPE:QCADStretchyObject]\n";
        out << "psz=" << cell.name << "\n";
        out << "[#TYPE:QCADLabel]\n";
    }
    out << "[#TYPE:QCADCell]\n";
}

void writeQcaFile(const std::string &filename, const std::vector<Cell> &cells)
{
    std::ofstream out(filename);
    if (!out) {
        throw std::runtime_error("cannot write " + filename);
    }

    out << "[VERSION]\n";
    out << "qcadesigner_version=2.000000\n";
    out << "[#VERSION]\n";
    out << "[TYPE:DESIGN]\n";
    out << "[TYPE:QCADLayer]\n";
    out << "type=3\nstatus=1\npszDescription=Drawing Layer\n";
    out << "[#TYPE:QCADLayer]\n";
    out << "[TYPE:QCADLayer]\n";
    out << "type=0\nstatus=1\npszDescription=Substrate\n";
    out << "[TYPE:QCADSubstrate]\n";
    out << "[TYPE:QCADStretchyObject]\n";
    out << "[TYPE:QCADDesignObject]\n";
    out << "x=3000.000000\n";
    out << "y=1500.000000\n";
    out << "bSelected=FALSE\n";
    out << "clr.red=65535\nclr.green=65535\nclr.blue=65535\n";
    out << "bounding_box.xWorld=0.000000\n";
    out << "bounding_box.yWorld=0.000000\n";
    out << "bounding_box.cxWorld=6000.000000\n";
    out << "bounding_box.cyWorld=3000.000000\n";
    out << "[#TYPE:QCADDesignObject]\n";
    out << "[#TYPE:QCADStretchyObject]\n";
    out << "grid_spacing=20.000000\n";
    out << "[#TYPE:QCADSubstrate]\n";
    out << "[#TYPE:QCADLayer]\n";

    const char *layerNames[3] = {"Main Cell Layer", "second layer", "third layer"};
    for (int layer = 0; layer < 3; ++layer) {
        out << "[TYPE:QCADLayer]\n";
        out << "type=1\nstatus=0\npszDescription=" << layerNames[layer] << "\n";
        for (const Cell &cell : cells) {
            if (cell.layer == layer) {
                writeQcaCell(out, cell);
            }
        }
        out << "[#TYPE:QCADLayer]\n";
    }
    out << "[#TYPE:DESIGN]\n";
}

std::string replaceSuffix(const std::string &path, const std::string &suffix)
{
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return path + suffix;
    }
    return path.substr(0, dot) + suffix;
}

void runEnergyAnalysis(const std::string &qcaPath,
                       const std::string &reportPath,
                       const std::string &waveformPath,
                       const EnergyRunOptions &runOptions)
{
    simon::QCADesign design;
    if (!simon::parse_design(qcaPath, design)) {
        throw std::runtime_error("cannot parse generated qca " + qcaPath);
    }

    simon::EnergyAnalysisOption option;
    option.time_step = runOptions.timeStep;
    option.duration = runOptions.duration;
    option.clock_slope = runOptions.clockSlope;
    option.clock_period = runOptions.clockPeriod;
    option.input_period = runOptions.inputPeriod;

    simon::VectorTable vectorTable;
    simon::SimulationMode mode = simon::SimulationMode::Exhaustive;
    if (!runOptions.vectorTablePath.empty()) {
        if (!simon::parse_vector_table(runOptions.vectorTablePath, vectorTable)) {
            throw std::runtime_error("cannot parse vector table: " +
                                     runOptions.vectorTablePath);
        }
        mode = simon::SimulationMode::Selective;
    }
    simon::Result result;
    simon::COSEnergyAnalysisAlgorithm algorithm(option);
    if (mode == simon::SimulationMode::Selective) {
        // QCA files may enumerate primary inputs in different geometric
        // orders.  Reorder both the cell pointers and vector columns by their
        // labels, otherwise equal vector tables drive different signals.  An
        // explicit input order also selects the simulator's permutation path,
        // so retain the discovered output names instead of accidentally
        // replacing them with an empty list.
        std::vector<std::string> outputNames;
        for (const auto &layer : design) {
            for (const auto &cell : layer) {
                if (simon::function(cell) == simon::FCNCellFunction::OUTPUT) {
                    outputNames.push_back(simon::name(cell));
                }
            }
        }
        algorithm.run(design,
                      vectorTable,
                      result,
                      mode,
                      vectorTable.names,
                      std::move(outputNames));
    } else {
        algorithm.run(design, vectorTable, result, mode);
    }

    if (runOptions.writeWaveform && !result.outputs.empty() && result.clocks.size() == 4) {
        result.write_text_file(waveformPath);
    }
    result.write_energy_analysis_file(reportPath);
    {
        std::ofstream report(reportPath, std::ios::app);
        report << "[ENERGY_RUN_OPTIONS]\n"
               << "time_step_s=" << option.time_step << '\n'
               << "duration_s=" << option.duration << '\n'
               << "clock_period_s=" << option.clock_period << '\n'
               << "input_period_s=" << option.input_period << '\n'
               << "clock_slope_s=" << option.clock_slope << '\n'
               << "io_contraction=" << (runOptions.contractIoPorts ? "TRUE" : "FALSE") << '\n'
               << "simulation_mode="
               << (mode == simon::SimulationMode::Selective ? "SELECTIVE" : "EXHAUSTIVE")
               << '\n'
               << "vector_table=" << runOptions.vectorTablePath << '\n'
               << "probe_nodes=";
        for (std::size_t index = 0; index < runOptions.probeNodeNames.size(); ++index) {
            if (index != 0) report << ',';
            report << runOptions.probeNodeNames[index];
        }
        report << '\n'
               << "probe_semantics="
               << (runOptions.probeNodeNames.empty()
                       ? "NONE"
                       : "EXISTING_DYNAMIC_OUTPUT_PORT_CELL_RECLASSIFIED_FOR_OBSERVATION")
               << '\n'
               << "[#ENERGY_RUN_OPTIONS]\n";
    }

    const auto &energy = result.energy_analysis;
    std::cout << "available=" << (energy.available ? "true" : "false")
              << " cycles=" << energy.cycle_count
              << " cells=" << energy.cells.size()
              << " total_error_eV=" << energy.total_error_eV
              << " total_bath_clock_eV=" << energy.total_bath_clock_eV
              << " time_step=" << option.time_step
              << " duration=" << option.duration
              << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: ifcn_energy_analysis <layout.ifcn> [output-prefix] "
                     "[--fast] [--time-step seconds] [--duration seconds] "
                     "[--clock-period seconds] [--input-period seconds] [--clock-slope seconds] "
                     "[--waveform] [--qca-only] [--io-contraction] [--vectors vectors.vt] "
                     "[--probe-node node-name]\n";
        return 2;
    }

    try {
        const std::string ifcnPath = argv[1];
        int optionStart = 2;
        std::string outputPrefix = replaceSuffix(ifcnPath, "");
        if (argc >= 3 && std::string(argv[2]).rfind("-", 0) != 0) {
            outputPrefix = argv[2];
            optionStart = 3;
        }
        EnergyRunOptions runOptions;
        parseEnergyOptions(argc, argv, optionStart, runOptions);

        const bool sourceIsQca = ifcnPath.size() >= 4 &&
            ifcnPath.substr(ifcnPath.size() - 4) == ".qca";
        const std::string qcaPath = sourceIsQca ? ifcnPath : outputPrefix + "_energy_input.qca";
        const std::string reportPath = outputPrefix + "_energy.txt";
        const std::string waveformPath = outputPrefix + "_energy.rst";

        if (!sourceIsQca) {
            const LayoutData layout = parseIfcn(ifcnPath);
            const std::vector<Cell> cells = mapIfcnToCells(layout,
                                                           runOptions.contractIoPorts,
                                                           runOptions.probeNodeNames);
            writeQcaFile(qcaPath, cells);
        } else if (runOptions.contractIoPorts || !runOptions.probeNodeNames.empty()) {
            throw std::runtime_error(
                "--io-contraction and --probe-node apply only to IFCN input");
        }
        if (runOptions.qcaOnly) {
            std::cout << "qca=" << qcaPath << '\n';
            return 0;
        }
        runEnergyAnalysis(qcaPath, reportPath, waveformPath, runOptions);
        std::cout << "qca=" << qcaPath << '\n';
        std::cout << "report=" << reportPath << '\n';
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "ifcn_energy_analysis failed: " << ex.what() << '\n';
        return 1;
    }
}
