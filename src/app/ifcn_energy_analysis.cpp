#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include <autopr/algorithms/mapping.h>
#include <simon/simon.hpp>

namespace {

using fcngraph::Mapping;
using fcngraph::MappingPositionHash;
using fcngraph::position;

struct NodeInfo {
    std::string name;
    std::string type;
    position pos{0, 0};
};

struct LayoutData {
    std::map<int, NodeInfo> nodes;
    std::map<std::pair<int, int>, std::vector<position>> routes;
    std::map<position, int> phases;
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
};

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
        } else if (arg == "--waveform") {
            options.writeWaveform = true;
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

    while (std::getline(input, line)) {
        if (line.rfind("#nodes info", 0) == 0) {
            section = (section == "nodes") ? "" : "nodes";
            continue;
        }
        if (line.rfind("#paths info", 0) == 0) {
            section = (section == "paths") ? "" : "paths";
            continue;
        }
        if (line.rfind("#phase map", 0) == 0) {
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
            data.routes[{std::stoi(match[1]), std::stoi(match[2])}] = std::move(path);
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
    return data;
}

int phaseAt(const std::map<position, int> &phaseMap, const position &mappedCell)
{
    const position sourceCell{mappedCell.first / 5, mappedCell.second / 5};
    const auto it = phaseMap.find(sourceCell);
    if (it == phaseMap.end() || it->second < 0) {
        return 0;
    }
    return std::clamp(it->second, 0, 3);
}

void addCell(std::vector<Cell> &cells,
             const std::map<position, int> &phaseMap,
             const position &mappedCell,
             int layer,
             CellFunction function,
             CellMode mode = CellMode::Normal,
             std::string name = {})
{
    cells.push_back({mappedCell, layer, phaseAt(phaseMap, mappedCell), function, mode, std::move(name)});
}

std::vector<Cell> mapIfcnToCells(const LayoutData &data)
{
    Mapping mapping;
    std::vector<std::vector<position>> routePaths;
    routePaths.reserve(data.routes.size());
    for (const auto &route : data.routes) {
        routePaths.push_back(route.second);
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
    const auto nodeCellsByType = mapping.nodecell_list;
    const auto routeCellsByPath = mapping.mapping_line(routePaths);
    const auto crossCellsByPath = mapping.crossline_list;

    std::vector<Cell> cells;
    for (const auto &entry : nodeCellsByType) {
        const std::string &type = entry.first;
        for (const position &cellPos : entry.second) {
            if (type == "input") {
                const position nodePos{cellPos.first / 5, cellPos.second / 5};
                const auto nameIt = nodeNameByPos.find(nodePos);
                addCell(cells,
                        data.phases,
                        cellPos,
                        0,
                        CellFunction::Input,
                        CellMode::Normal,
                        nameIt == nodeNameByPos.end() ? std::string{} : nameIt->second);
            } else if (type == "output") {
                const position nodePos{cellPos.first / 5, cellPos.second / 5};
                const auto nameIt = nodeNameByPos.find(nodePos);
                addCell(cells,
                        data.phases,
                        cellPos,
                        0,
                        CellFunction::Output,
                        CellMode::Normal,
                        nameIt == nodeNameByPos.end() ? std::string{} : nameIt->second);
            } else if (type == "fix0") {
                addCell(cells, data.phases, cellPos, 0, CellFunction::Fixed, CellMode::Normal, "-1.00");
            } else if (type == "fix1") {
                addCell(cells, data.phases, cellPos, 0, CellFunction::Fixed, CellMode::Normal, "1.00");
            } else {
                addCell(cells, data.phases, cellPos, 0, CellFunction::Normal);
            }
        }
    }

    std::unordered_set<position, MappingPositionHash> crossCellSet;
    std::unordered_set<position, MappingPositionHash> verticalCellSet;
    std::map<std::pair<position, position>, std::unordered_set<position, MappingPositionHash>> crossCellsByRoute;
    auto addVerticalStack = [&](const position &cell) {
        addCell(cells, data.phases, cell, 0, CellFunction::Normal, CellMode::Vertical);
        addCell(cells, data.phases, cell, 1, CellFunction::Normal, CellMode::Vertical);
        addCell(cells, data.phases, cell, 2, CellFunction::Normal, CellMode::Vertical);
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
                        addCell(cells, data.phases, *unit, 2, CellFunction::Normal, CellMode::Crossover);
                    } else {
                        addVerticalStack(*unit);
                    }
                } else {
                    addCell(cells, data.phases, *unit, 2, CellFunction::Normal, CellMode::Crossover);
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

                addCell(cells, data.phases, pos, 0, CellFunction::Normal);
            }
        }
    }

    return cells;
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
    for (const auto &offset : offsets) {
        out << "[TYPE:CELL_DOT]\n";
        out << "x=" << x + offset[0] << "\n";
        out << "y=" << y + offset[1] << "\n";
        out << "diameter=5\n";
        out << "charge=0\n";
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
    simon::Result result;
    simon::COSEnergyAnalysisAlgorithm algorithm(option);
    algorithm.run(design, vectorTable, result, simon::SimulationMode::Exhaustive);

    if (runOptions.writeWaveform && !result.outputs.empty() && result.clocks.size() == 4) {
        result.write_text_file(waveformPath);
    }
    result.write_energy_analysis_file(reportPath);

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
                     "[--clock-period seconds] [--input-period seconds] [--clock-slope seconds] [--waveform]\n";
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

        const std::string qcaPath = outputPrefix + "_energy_input.qca";
        const std::string reportPath = outputPrefix + "_energy.txt";
        const std::string waveformPath = outputPrefix + "_energy.rst";

        const LayoutData layout = parseIfcn(ifcnPath);
        const std::vector<Cell> cells = mapIfcnToCells(layout);
        writeQcaFile(qcaPath, cells);
        runEnergyAnalysis(qcaPath, reportPath, waveformPath, runOptions);
        std::cout << "qca=" << qcaPath << '\n';
        std::cout << "report=" << reportPath << '\n';
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "ifcn_energy_analysis failed: " << ex.what() << '\n';
        return 1;
    }
}
