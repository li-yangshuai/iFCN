#include "autopr/algorithms/astar.h"
#include "autopr/algorithms/genetic.h"
#include "autopr/algorithms/phase_codec.h"
#include "autopr/graph/circuitGraph.h"
#include "autopr/graph/parse.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

using fcngraph::GridCell;
using fcngraph::PositionHash;
using fcngraph::position;

struct LayoutAttempt {
    unsigned int xSpacing = 4;
    unsigned int ySpacing = 4;
    double searchCost = 90.0;
};

struct LayoutBounds {
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int width = 0;
    int height = 0;
    int area = 0;
};

struct PhaseRepeatStats {
    int repeatedAdjacent = 0;
    int totalAdjacent = 0;
    int maxRun = 1;
};

struct LayoutSearchResult {
    LayoutBounds bounds;
    int routeLength = 0;
    PhaseRepeatStats phaseRepeats;
    unsigned int xSpacing = 0;
    unsigned int ySpacing = 0;
    double searchCost = 0.0;
    int attemptIndex = -1;
    std::map<int, position> nodePositions;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;
    std::unordered_map<position, GridCell, PositionHash> gridCells;
};

struct ExperimentRow {
    std::string suite;
    std::string file;
    std::string circuit;
    int phaseCount = 0;
    bool success = false;
    std::string failure;
    int gates = 0;
    int inputs = 0;
    int outputs = 0;
    int edges = 0;
    int attemptsLimit = 0;
    int attemptsEvaluated = 0;
    int routeFail = 0;
    int phaseFail = 0;
    int exceptionFail = 0;
    int successCandidates = 0;
    int bestAttempt = -1;
    unsigned int xSpacing = 0;
    unsigned int ySpacing = 0;
    double searchCost = 0.0;
    int width = 0;
    int height = 0;
    int area = 0;
    int routeLength = 0;
    int totalAdjacent = 0;
    int repeatedAdjacent = 0;
    double repeatRatio = 0.0;
    int maxRun = 0;
    double runtimeSeconds = 0.0;
    int blockSize = 0;
    int paddedWidth = 0;
    int paddedHeight = 0;
    int paddedCells = 0;
    int paddingCells = 0;
    int phaseCells = 0;
    int tileCount = 0;
    int hexChars = 0;
    int packedBytes = 0;
    int packedLineChars = 0;
    int legacyCoordChars = 0;
    double packedLineRatio = 0.0;
    double packedBitsPerLayoutCell = 0.0;
    double packedBitsPerPaddedCell = 0.0;
    bool codecRoundtrip = false;
    int codecMismatches = 0;
    std::string ifcnFile;
    std::string layoutTexFile;
};

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

std::vector<LayoutAttempt> buildLayoutAttempts()
{
    std::vector<LayoutAttempt> attempts;
    for (unsigned int ySpacing = 1; ySpacing <= 14; ++ySpacing) {
        for (unsigned int xSpacing = 1; xSpacing <= 14; ++xSpacing) {
            attempts.push_back({xSpacing, ySpacing, 90.0});
            if (xSpacing <= 8 && ySpacing <= 8) {
                attempts.push_back({xSpacing, ySpacing, 150.0});
            }
            if (xSpacing <= 3 || ySpacing <= 3 || xSpacing >= 7 || ySpacing >= 7) {
                attempts.push_back({xSpacing, ySpacing, 240.0});
            }
            if (xSpacing <= 4 || ySpacing <= 4 || xSpacing >= 7 || ySpacing >= 7) {
                attempts.push_back({xSpacing, ySpacing, 600.0});
            }
        }
    }

    std::sort(attempts.begin(), attempts.end(), [](const LayoutAttempt &lhs, const LayoutAttempt &rhs) {
        const auto lhsArea = lhs.xSpacing * lhs.ySpacing;
        const auto rhsArea = rhs.xSpacing * rhs.ySpacing;
        if (lhsArea != rhsArea) {
            return lhsArea < rhsArea;
        }
        if (lhs.searchCost != rhs.searchCost) {
            return lhs.searchCost < rhs.searchCost;
        }
        if (lhs.ySpacing != rhs.ySpacing) {
            return lhs.ySpacing < rhs.ySpacing;
        }
        return lhs.xSpacing < rhs.xSpacing;
    });
    return attempts;
}

std::optional<LayoutBounds> calculateGridBounds(
    const std::unordered_map<position, GridCell, PositionHash> &gridCells)
{
    bool hasCell = false;
    unsigned int minX = std::numeric_limits<unsigned int>::max();
    unsigned int minY = std::numeric_limits<unsigned int>::max();
    unsigned int maxX = 0;
    unsigned int maxY = 0;

    for (const auto &entry : gridCells) {
        const auto &cell = entry.second;
        if (cell.get_current_weight() == 0 && cell.getPhase() == -1) {
            continue;
        }
        hasCell = true;
        minX = std::min(minX, entry.first.first);
        minY = std::min(minY, entry.first.second);
        maxX = std::max(maxX, entry.first.first);
        maxY = std::max(maxY, entry.first.second);
    }

    if (!hasCell) {
        return std::nullopt;
    }

    LayoutBounds bounds;
    bounds.minX = static_cast<int>(minX);
    bounds.maxX = static_cast<int>(maxX);
    bounds.minY = static_cast<int>(minY);
    bounds.maxY = static_cast<int>(maxY);
    bounds.width = bounds.maxX - bounds.minX + 1;
    bounds.height = bounds.maxY - bounds.minY + 1;
    bounds.area = bounds.width * bounds.height;
    return bounds;
}

int totalRouteLength(const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes)
{
    int length = 0;
    for (const auto &route : routes) {
        length += static_cast<int>(route.second.size());
    }
    return length;
}

PhaseRepeatStats calculatePhaseRepeatStats(
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes,
    const std::unordered_map<position, GridCell, PositionHash> &gridCells)
{
    PhaseRepeatStats stats;
    for (const auto &route : routes) {
        int previousPhase = -1;
        int currentRun = 1;
        for (const auto &pos : route.second) {
            auto cell = gridCells.find(pos);
            const int phase = (cell != gridCells.end()) ? cell->second.getPhase() : -1;
            if (phase >= 1 && previousPhase >= 1) {
                ++stats.totalAdjacent;
                if (phase == previousPhase) {
                    ++stats.repeatedAdjacent;
                    ++currentRun;
                    stats.maxRun = std::max(stats.maxRun, currentRun);
                } else {
                    currentRun = 1;
                }
            } else if (phase < 1) {
                currentRun = 1;
            }
            previousPhase = phase;
        }
    }
    return stats;
}

int phaseRepeatMaxRunLimit(int phaseCount)
{
    return std::max(phaseCount + 2, phaseCount * 2 + 2);
}

bool hasAcceptablePhaseRepeats(const PhaseRepeatStats &stats, int phaseCount)
{
    if (stats.maxRun > phaseRepeatMaxRunLimit(phaseCount)) {
        return false;
    }
    if (stats.totalAdjacent == 0) {
        return true;
    }
    return stats.repeatedAdjacent * 10 <= stats.totalAdjacent * 3;
}

int phaseRepeatPenalty(const PhaseRepeatStats &stats, int phaseCount)
{
    const int maxRunPenalty = std::max(0, stats.maxRun - phaseRepeatMaxRunLimit(phaseCount));
    const int repeatPenalty = (stats.totalAdjacent == 0)
        ? 0
        : std::max(0, stats.repeatedAdjacent * 10 - stats.totalAdjacent * 3);
    return maxRunPenalty * 100000 + repeatPenalty;
}

bool isBetterLayout(const LayoutSearchResult &candidate,
                    const LayoutSearchResult &currentBest,
                    int phaseCount)
{
    const bool candidatePhaseOk = hasAcceptablePhaseRepeats(candidate.phaseRepeats, phaseCount);
    const bool currentPhaseOk = hasAcceptablePhaseRepeats(currentBest.phaseRepeats, phaseCount);
    if (candidatePhaseOk != currentPhaseOk) {
        return candidatePhaseOk;
    }

    if (candidatePhaseOk) {
        const auto score = [](const LayoutSearchResult &layout) {
            return std::make_tuple(layout.bounds.area,
                                   layout.phaseRepeats.maxRun,
                                   layout.phaseRepeats.repeatedAdjacent,
                                   layout.routeLength,
                                   std::max(layout.bounds.width, layout.bounds.height),
                                   layout.bounds.width,
                                   layout.bounds.height,
                                   layout.xSpacing * layout.ySpacing,
                                   layout.searchCost);
        };
        return score(candidate) < score(currentBest);
    }

    const auto score = [phaseCount](const LayoutSearchResult &layout) {
        return std::make_tuple(phaseRepeatPenalty(layout.phaseRepeats, phaseCount),
                               layout.phaseRepeats.maxRun,
                               layout.phaseRepeats.repeatedAdjacent,
                               layout.bounds.area,
                               layout.routeLength,
                               std::max(layout.bounds.width, layout.bounds.height),
                               layout.bounds.width,
                               layout.bounds.height);
    };

    return score(candidate) < score(currentBest);
}

std::string suiteNameForPath(const std::string &file)
{
    const std::filesystem::path path(file);
    for (const auto &part : path) {
        const std::string value = part.string();
        if (value == "TOY" || value == "MAJ" || value == "fontes18") {
            return value;
        }
    }
    return path.parent_path().filename().string();
}

std::string sanitizeName(std::string value)
{
    for (char &ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isalnum(uch) && ch != '_' && ch != '-') {
            ch = '_';
        }
    }
    return value;
}

std::string texEscape(const std::string &value)
{
    std::string out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\textbackslash{}"; break;
            case '_': out += "\\_"; break;
            case '&': out += "\\&"; break;
            case '%': out += "\\%"; break;
            case '$': out += "\\$"; break;
            case '#': out += "\\#"; break;
            case '{': out += "\\{"; break;
            case '}': out += "\\}"; break;
            case '~': out += "\\textasciitilde{}"; break;
            case '^': out += "\\textasciicircum{}"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string nodeLabel(fcngraph::Parse &parse, int node)
{
    const std::string type = parse.getNodeType(node);
    if (type == "input" || type == "output") {
        return parse.getNodeName(node);
    }
    if (type == "maj") {
        return "M";
    }
    if (type == "and") {
        return "\\&";
    }
    if (type == "or") {
        return "\\textbar";
    }
    if (type == "not") {
        return "$\\neg$";
    }
    if (type == "fanout") {
        return "F";
    }
    if (type == "wire") {
        return "w";
    }
    return "";
}

std::string resultStem(const ExperimentRow &row)
{
    const std::filesystem::path input(row.file);
    return sanitizeName(row.suite + "_" + input.stem().string() + "_p" + std::to_string(row.phaseCount));
}

position normalizePos(const position &pos, const LayoutBounds &bounds)
{
    return {
        static_cast<unsigned int>(static_cast<int>(pos.first) - bounds.minX),
        static_cast<unsigned int>(static_cast<int>(pos.second) - bounds.minY)
    };
}

std::map<fcngraph::phase_codec::PhaseCoord, int> normalizedPhaseMap(
    const LayoutSearchResult &layout)
{
    std::map<fcngraph::phase_codec::PhaseCoord, int> phaseMap;
    for (const auto &entry : layout.gridCells) {
        const int phase = entry.second.getPhase();
        if (phase < 1) {
            continue;
        }
        const auto pos = normalizePos(entry.first, layout.bounds);
        phaseMap[{pos.first, pos.second}] = phase - 1;
    }
    return phaseMap;
}

void fillCodecMetrics(ExperimentRow &row, const LayoutSearchResult &layout, int phaseCount)
{
    row.blockSize = (phaseCount == 3) ? 3 : 4;
    row.paddedWidth = ((row.width + row.blockSize - 1) / row.blockSize) * row.blockSize;
    row.paddedHeight = ((row.height + row.blockSize - 1) / row.blockSize) * row.blockSize;
    row.paddedCells = row.paddedWidth * row.paddedHeight;
    row.paddingCells = row.paddedCells - row.area;

    const auto phaseMap = normalizedPhaseMap(layout);
    row.phaseCells = static_cast<int>(phaseMap.size());

    for (const auto &entry : phaseMap) {
        std::ostringstream oss;
        oss << "(" << entry.first.first << "," << entry.first.second << "):" << entry.second << ";\n";
        row.legacyCoordChars += static_cast<int>(oss.str().size());
    }

    const auto tiles = fcngraph::phase_codec::encodePhaseMapToTiles(
        phaseMap,
        phaseCount,
        row.blockSize,
        row.width,
        row.height
    );

    row.tileCount = static_cast<int>(tiles.size());
    for (const auto &tile : tiles) {
        row.hexChars += static_cast<int>(tile.hex.size());
        std::ostringstream oss;
        oss << "tile(" << tile.tileX << "," << tile.tileY << "):0x" << tile.hex << ";\n";
        row.packedLineChars += static_cast<int>(oss.str().size());
    }
    row.packedBytes = row.hexChars / 2;
    row.packedLineRatio = row.legacyCoordChars > 0
        ? static_cast<double>(row.packedLineChars) / static_cast<double>(row.legacyCoordChars)
        : 0.0;
    row.packedBitsPerLayoutCell = row.area > 0
        ? (8.0 * static_cast<double>(row.packedBytes)) / static_cast<double>(row.area)
        : 0.0;
    row.packedBitsPerPaddedCell = row.paddedCells > 0
        ? (8.0 * static_cast<double>(row.packedBytes)) / static_cast<double>(row.paddedCells)
        : 0.0;

    std::map<fcngraph::phase_codec::PhaseCoord, int> decoded;
    try {
        for (const auto &tile : tiles) {
            const auto matrix = fcngraph::phase_codec::decodePackedHexToMatrix(
                tile.hex,
                phaseCount,
                row.blockSize
            );
            for (int r = 0; r < row.blockSize; ++r) {
                for (int c = 0; c < row.blockSize; ++c) {
                    decoded[{tile.tileX * static_cast<unsigned int>(row.blockSize) + static_cast<unsigned int>(c),
                             tile.tileY * static_cast<unsigned int>(row.blockSize) + static_cast<unsigned int>(r)}] =
                        matrix[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
                }
            }
        }

        for (int y = 0; y < row.paddedHeight; ++y) {
            for (int x = 0; x < row.paddedWidth; ++x) {
                const auto coord = std::make_pair(static_cast<unsigned int>(x), static_cast<unsigned int>(y));
                const auto expectedIt = phaseMap.find(coord);
                const auto actualIt = decoded.find(coord);
                const int expected = expectedIt != phaseMap.end() ? expectedIt->second : 0;
                const int actual = actualIt != decoded.end() ? actualIt->second : -1;
                if (expected != actual) {
                    ++row.codecMismatches;
                }
            }
        }
        row.codecRoundtrip = row.codecMismatches == 0;
    } catch (const std::exception &) {
        row.codecRoundtrip = false;
        row.codecMismatches = row.paddedCells;
    }
}

void writeIfcnFile(const std::filesystem::path &outputDir,
                   ExperimentRow &row,
                   fcngraph::Parse &parse,
                   const LayoutSearchResult &layout)
{
    const std::filesystem::path outPath = outputDir / (resultStem(row) + ".ifcn");
    std::ofstream out(outPath);
    if (!out) {
        throw std::runtime_error("cannot write " + outPath.string());
    }

    const int blockSize = (row.phaseCount == 3) ? 3 : 4;
    const auto phaseMap = normalizedPhaseMap(layout);
    const auto encodedTiles = fcngraph::phase_codec::encodePhaseMapToTiles(
        phaseMap,
        row.phaseCount,
        blockSize,
        layout.bounds.width,
        layout.bounds.height
    );

    out << "#circuit name: " << std::filesystem::path(row.file).filename().string() << "\n\n";
    out << "#designed by graph render algorithm with encoded "
        << row.phaseCount << "-phase clock tiles.\n\n";
    out << "#gate level placement and routing infomation\n";
    out << "#gates number: " << row.gates << "\n";
    out << "#input/output: " << row.inputs << " / " << row.outputs << "\n";
    out << "#edges number: " << row.edges << "\n";
    out << "#total layers: " << static_cast<int>(parse.getlayerNodeDivVec().size()) << "\n";
    out << "#layout area: width: " << layout.bounds.width
        << ", height: " << layout.bounds.height
        << ", area: " << layout.bounds.area << "\n";
    out << "#phase origin: top-left=(" << layout.bounds.minX << "," << layout.bounds.minY
        << "), saved coordinates are normalized to (0,0)\n";
    out << "#phase count: " << row.phaseCount << "\n";
    out << "#runtime: " << std::fixed << std::setprecision(3) << row.runtimeSeconds << "s\n\n";

    out << "#nodes info \n";
    out << "### nodeIndex, nodeName, nodeType, nodePosition ###\n";
    for (const auto &entry : layout.nodePositions) {
        const auto pos = normalizePos(entry.second, layout.bounds);
        out << entry.first << ", "
            << parse.getNodeName(entry.first) << ", "
            << parse.getNodeType(entry.first) << ", "
            << "(" << pos.first << "," << pos.second << ");\n";
    }
    out << "#nodes info \n\n";

    out << "#paths info\n";
    out << "### {node1, node2} : path ###\n";
    for (const auto &route : layout.routes) {
        out << "(" << route.first.first << "," << route.first.second << "): ";
        for (std::size_t i = 0; i < route.second.size(); ++i) {
            const auto pos = normalizePos(route.second[i], layout.bounds);
            if (i > 0) {
                out << ",";
            }
            out << "(" << pos.first << "," << pos.second << ")";
        }
        out << ";\n";
    }
    out << "#paths info\n";

    out << "#phase map\n";
    out << "#phase codec: phase_count=" << row.phaseCount
        << ", block_size=" << blockSize
        << ", encoding=packed_hex_2bit_row_major\n";
    out << "### tile(x,y) : packed_hex for a "
        << blockSize << "x" << blockSize << " phase block ###\n";
    for (const auto &tile : encodedTiles) {
        out << "tile(" << tile.tileX << "," << tile.tileY << "):0x"
            << tile.hex << ";\n";
    }
    out << "#phase map\n";

    row.ifcnFile = outPath.string();
}

void writeLayoutTexFile(const std::filesystem::path &outputDir,
                        ExperimentRow &row,
                        fcngraph::Parse &parse,
                        const LayoutSearchResult &layout)
{
    const std::filesystem::path outPath = outputDir / (resultStem(row) + "_layout.tex");
    const std::filesystem::path tempDir =
        std::filesystem::absolute(outputDir / (resultStem(row) + "_latex_tmp"));
    const std::filesystem::path previousPath = std::filesystem::current_path();

    std::map<unsigned int, position> normalizedNodePositions;
    for (const auto &entry : layout.nodePositions) {
        normalizedNodePositions[static_cast<unsigned int>(entry.first)] =
            normalizePos(entry.second, layout.bounds);
    }

    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> normalizedRoutes;
    for (const auto &route : layout.routes) {
        auto &path = normalizedRoutes[route.first];
        path.reserve(route.second.size());
        for (const auto &pos : route.second) {
            path.push_back(normalizePos(pos, layout.bounds));
        }
    }

    try {
        std::filesystem::remove_all(tempDir);
        std::filesystem::create_directories(tempDir);
        std::filesystem::current_path(tempDir);

        fcngraph::GridChessboard latexChessboard;
        fcngraph::Astar latexAstar(latexChessboard);
        fcngraph::GeneticAlgorithm latexPrinter(parse, latexChessboard, latexAstar, 1, 1, 0.0, 0.0);
        const auto clockScheme = (row.phaseCount == 3)
            ? fcngraph::CLOCK_SCHEME::BANCS
            : fcngraph::CLOCK_SCHEME::USE;
        const auto phaseMap = normalizedPhaseMap(layout);

        latexPrinter.printLaTex(clockScheme,
                                {0, 0},
                                {static_cast<unsigned int>(layout.bounds.width),
                                 static_cast<unsigned int>(layout.bounds.height)},
                                normalizedNodePositions,
                                normalizedRoutes,
                                {},
                                phaseMap);

        const std::filesystem::path generatedPath =
            tempDir / (parse.get_moduleName() + ".tex");
        if (!std::filesystem::exists(generatedPath)) {
            throw std::runtime_error("existing LaTeX exporter did not create " + generatedPath.string());
        }

        std::filesystem::current_path(previousPath);
        std::filesystem::copy_file(generatedPath,
                                   outPath,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove_all(tempDir);
    } catch (...) {
        std::filesystem::current_path(previousPath);
        std::filesystem::remove_all(tempDir);
        throw;
    }

    row.layoutTexFile = outPath.string();
}

ExperimentRow runOne(const std::string &file,
                     int phaseCount,
                     int maxAttempts,
                     const std::filesystem::path &layoutDir)
{
    ExperimentRow row;
    row.file = file;
    row.suite = suiteNameForPath(file);
    row.phaseCount = phaseCount;
    row.attemptsLimit = maxAttempts;

    auto start = std::chrono::steady_clock::now();
    std::string lastFailure;

    try {
        fcngraph::Parse parse;
        parse.parseVerilog(file);
        parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);

        row.circuit = std::filesystem::path(file).filename().string();
        row.gates = static_cast<int>(parse.getm_numVertices());
        row.inputs = static_cast<int>(parse.get_input_num());
        row.outputs = static_cast<int>(parse.get_output_num());
        row.edges = static_cast<int>(parse.getm_numEdges());

        parse.optimizeBufferNode();
        parse.caculateSameLayerNodeRoutePair();

        std::optional<LayoutSearchResult> bestLayout;
        const auto attempts = buildLayoutAttempts();
        row.attemptsLimit = std::min(maxAttempts, static_cast<int>(attempts.size()));

        const auto &layerNodes = parse.getlayerNodeDivVec();
        std::size_t maxLayerWidth = 0;
        for (const auto &layer : layerNodes) {
            maxLayerWidth = std::max(maxLayerWidth, layer.size());
        }
        const auto placementAreaLowerBound = [maxLayerWidth, layerCount = layerNodes.size()](const LayoutAttempt &attempt) -> int {
            if (maxLayerWidth == 0 || layerCount == 0) {
                return 0;
            }
            const int width = static_cast<int>((maxLayerWidth - 1) * attempt.xSpacing + 1);
            const int height = static_cast<int>((layerCount - 1) * attempt.ySpacing + 1);
            return width * height;
        };

        for (int attemptIndex = 0; attemptIndex < row.attemptsLimit; ++attemptIndex) {
            const auto &attempt = attempts[static_cast<std::size_t>(attemptIndex)];
            const bool bestPhaseOk = bestLayout.has_value()
                && hasAcceptablePhaseRepeats(bestLayout->phaseRepeats, phaseCount);
            if (bestPhaseOk && placementAreaLowerBound(attempt) > bestLayout->bounds.area) {
                continue;
            }
            ++row.attemptsEvaluated;

            fcngraph::GridChessboard chessboard;
            fcngraph::Astar astar(chessboard, false, attempt.searchCost);
            fcngraph::CircuitGraph graph(parse, file, chessboard, astar);

            try {
                graph.processAndGenerateGraph(false, true, true, true);
                graph.sortNodesByLayeredGrid(attempt.xSpacing, attempt.ySpacing);

                if (!graph.placeAndRoute()) {
                    ++row.routeFail;
                    lastFailure = "route failed";
                    continue;
                }

                if (!graph.assignPhases(phaseCount)) {
                    ++row.phaseFail;
                    lastFailure = "phase assignment failed";
                    continue;
                }

                const auto bounds = calculateGridBounds(chessboard.getGridMap());
                if (!bounds.has_value()) {
                    lastFailure = "empty layout";
                    continue;
                }

                LayoutSearchResult result;
                result.bounds = bounds.value();
                result.routeLength = totalRouteLength(graph.routes);
                result.phaseRepeats = calculatePhaseRepeatStats(graph.routes, chessboard.getGridMap());
                result.xSpacing = attempt.xSpacing;
                result.ySpacing = attempt.ySpacing;
                result.searchCost = attempt.searchCost;
                result.attemptIndex = attemptIndex + 1;
                result.nodePositions = graph.nodeIndex_pos;
                result.routes = graph.routes;
                result.gridCells = chessboard.getGridMap();

                ++row.successCandidates;
                if (!bestLayout.has_value() || isBetterLayout(result, bestLayout.value(), phaseCount)) {
                    bestLayout = std::move(result);
                }
            } catch (const std::exception &ex) {
                ++row.exceptionFail;
                lastFailure = ex.what();
            }
        }

        if (bestLayout.has_value()) {
            auto now = std::chrono::steady_clock::now();
            row.runtimeSeconds = std::chrono::duration<double>(now - start).count();
            row.success = true;
            row.bestAttempt = bestLayout->attemptIndex;
            row.xSpacing = bestLayout->xSpacing;
            row.ySpacing = bestLayout->ySpacing;
            row.searchCost = bestLayout->searchCost;
            row.width = bestLayout->bounds.width;
            row.height = bestLayout->bounds.height;
            row.area = bestLayout->bounds.area;
            row.routeLength = bestLayout->routeLength;
            row.totalAdjacent = bestLayout->phaseRepeats.totalAdjacent;
            row.repeatedAdjacent = bestLayout->phaseRepeats.repeatedAdjacent;
            row.repeatRatio = row.totalAdjacent > 0
                ? static_cast<double>(row.repeatedAdjacent) / static_cast<double>(row.totalAdjacent)
                : 0.0;
            row.maxRun = bestLayout->phaseRepeats.maxRun;
            fillCodecMetrics(row, bestLayout.value(), phaseCount);
            writeIfcnFile(layoutDir, row, parse, bestLayout.value());
            writeLayoutTexFile(layoutDir, row, parse, bestLayout.value());
        } else {
            row.failure = lastFailure.empty() ? "no feasible layout" : lastFailure;
        }
    } catch (const std::exception &ex) {
        row.failure = ex.what();
    }

    auto end = std::chrono::steady_clock::now();
    if (row.runtimeSeconds == 0.0) {
        row.runtimeSeconds = std::chrono::duration<double>(end - start).count();
    }
    return row;
}

void writeHeader(std::ostream &out)
{
    out << "suite,file,circuit,phase,success,failure,gates,inputs,outputs,edges,"
        << "attempts_limit,attempts_evaluated,route_fail,phase_fail,exception_fail,success_candidates,best_attempt,"
        << "x_spacing,y_spacing,search_cost,width,height,area,route_length,total_adjacent,repeated_adjacent,"
        << "repeat_ratio,max_run,runtime_s,block_size,padded_width,padded_height,padded_cells,padding_cells,"
        << "phase_cells,tile_count,hex_chars,packed_bytes,packed_line_chars,legacy_coord_chars,packed_line_ratio,"
        << "packed_bits_per_layout_cell,packed_bits_per_padded_cell,codec_roundtrip,codec_mismatches,"
        << "ifcn_file,layout_tex_file\n";
}

void writeRow(std::ostream &out, const ExperimentRow &row)
{
    out << csvEscape(row.suite) << ','
        << csvEscape(row.file) << ','
        << csvEscape(row.circuit) << ','
        << row.phaseCount << ','
        << (row.success ? "yes" : "no") << ','
        << csvEscape(row.failure) << ','
        << row.gates << ','
        << row.inputs << ','
        << row.outputs << ','
        << row.edges << ','
        << row.attemptsLimit << ','
        << row.attemptsEvaluated << ','
        << row.routeFail << ','
        << row.phaseFail << ','
        << row.exceptionFail << ','
        << row.successCandidates << ','
        << row.bestAttempt << ','
        << row.xSpacing << ','
        << row.ySpacing << ','
        << std::fixed << std::setprecision(1) << row.searchCost << ','
        << row.width << ','
        << row.height << ','
        << row.area << ','
        << row.routeLength << ','
        << row.totalAdjacent << ','
        << row.repeatedAdjacent << ','
        << std::fixed << std::setprecision(6) << row.repeatRatio << ','
        << row.maxRun << ','
        << std::fixed << std::setprecision(6) << row.runtimeSeconds << ','
        << row.blockSize << ','
        << row.paddedWidth << ','
        << row.paddedHeight << ','
        << row.paddedCells << ','
        << row.paddingCells << ','
        << row.phaseCells << ','
        << row.tileCount << ','
        << row.hexChars << ','
        << row.packedBytes << ','
        << row.packedLineChars << ','
        << row.legacyCoordChars << ','
        << std::fixed << std::setprecision(6) << row.packedLineRatio << ','
        << std::fixed << std::setprecision(6) << row.packedBitsPerLayoutCell << ','
        << std::fixed << std::setprecision(6) << row.packedBitsPerPaddedCell << ','
        << (row.codecRoundtrip ? "yes" : "no") << ','
        << row.codecMismatches << ','
        << csvEscape(row.ifcnFile) << ','
        << csvEscape(row.layoutTexFile)
        << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    int maxAttempts = 320;
    std::string outputPath = "phase_experiment_results.csv";
    std::filesystem::path layoutDir = "phase_experiment_layouts";
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--max-attempts" && i + 1 < argc) {
            maxAttempts = std::stoi(argv[++i]);
        } else if (arg == "--csv" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--layout-dir" && i + 1 < argc) {
            layoutDir = argv[++i];
        } else {
            inputs.push_back(arg);
        }
    }

    if (inputs.empty()) {
        std::cerr << "Usage: " << argv[0] << " [--max-attempts N] [--csv out.csv] file1.v ...\n";
        return 2;
    }

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "Cannot open output CSV: " << outputPath << "\n";
        return 2;
    }
    writeHeader(out);
    std::filesystem::create_directories(layoutDir);

    for (const auto &file : inputs) {
        for (int phaseCount : {4, 3}) {
            std::cerr << "[run] " << file << " phase=" << phaseCount
                      << " max_attempts=" << maxAttempts << "\n";
            const auto row = runOne(file, phaseCount, maxAttempts, layoutDir);
            writeRow(out, row);
            out.flush();
            std::cerr << "      " << (row.success ? "success" : "failed")
                      << " area=" << row.area
                      << " time=" << std::fixed << std::setprecision(2) << row.runtimeSeconds
                      << "s"
                      << (row.failure.empty() ? "" : (" reason=" + row.failure))
                      << "\n";
        }
    }

    return 0;
}
