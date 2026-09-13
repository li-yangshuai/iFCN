#include "VerilogHandler.h"
#include "ui/mainwindow/MainWindow.h"
#include <QFileDialog>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextStream>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <autopr/algorithms/phase_codec.h>
#include <autopr/algorithms/irregularLayout.h>
#include "ui/widgets/GaChessboardInputDialog.h"
#include <algorithm>
#include <cstddef>
#include <QElapsedTimer>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_set>

namespace {
struct GraphRenderSettings {
    int phaseCount = 4;
    int maxAttempts = 320;
    double timeBudgetSeconds = 120.0;
};

struct NormalGraphDrawSettings {
    bool generateVisualizations = false;
    bool generateStageSnapshots = false;
};

class StatusMessagesMuteGuard
{
public:
    StatusMessagesMuteGuard(CustomStatusBar *statusBar, bool muted)
        : statusBar(statusBar),
          previousMuted(statusBar != nullptr ? statusBar->messagesMuted() : false),
          active(statusBar != nullptr && muted)
    {
        if (active) {
            statusBar->setMessagesMuted(true);
        }
    }

    ~StatusMessagesMuteGuard()
    {
        if (active) {
            statusBar->setMessagesMuted(previousMuted);
        }
    }

private:
    CustomStatusBar *statusBar = nullptr;
    bool previousMuted = false;
    bool active = false;
};

using LayoutBounds = fcngraph::GraphDrawBounds;

struct HeuristicLayoutRequest {
    QString filePath;
    std::string file;
    QString clockSchemeStr;
    fcngraph::CLOCK_SCHEME scheme = fcngraph::CLOCK_SCHEME::USE;
    int width = 0;
    int height = 0;
    int generationSize = 0;
    int populationSize = 0;
};

struct HeuristicLayoutResult {
    bool success = false;
    QString error;
    QString statusMessage;
    fcngraph::Parse parse;
    std::map<unsigned int, fcngraph::position> nodePositions;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> routes;
    std::map<fcngraph::position, int> posPhase;
    LayoutBounds usedBounds;
    int inputNum = 0;
    int gateNum = 0;
    int outputNum = 0;
    int wireNum = 0;
    int hiddenNotNum = 0;
    int removedEdgeNum = 0;
    double elapsedSeconds = 0.0;
};

using HeuristicProgressCallback = std::function<void(const QString &, int, int)>;

bool readGraphRenderSettings(QWidget *parent, GraphRenderSettings &settings)
{
    if (qEnvironmentVariableIsSet("IFCN_IRREGULAR_BATCH")) {
        bool phaseOk = false;
        const int phaseCount = qEnvironmentVariableIntValue("IFCN_IRREGULAR_PHASES", &phaseOk);
        if (phaseOk && phaseCount >= 2) {
            settings.phaseCount = phaseCount;
        }

        bool attemptsOk = false;
        const int maxAttempts = qEnvironmentVariableIntValue("IFCN_IRREGULAR_ATTEMPTS", &attemptsOk);
        if (attemptsOk && maxAttempts > 0) {
            settings.maxAttempts = maxAttempts;
        }
        bool budgetOk = false;
        const double seconds = qEnvironmentVariable("IFCN_IRREGULAR_SECONDS").toDouble(&budgetOk);
        if (budgetOk) settings.timeBudgetSeconds = seconds;
        return true;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Irregular-Clock Graph P&R Options"));

    auto *phaseCombo = new QComboBox(&dialog);
    phaseCombo->addItem(QObject::tr("4-phase"), 4);
    phaseCombo->addItem(QObject::tr("3-phase"), 3);
    phaseCombo->setCurrentIndex(0);

    auto *attemptSpin = new QSpinBox(&dialog);
    attemptSpin->setRange(8, 600);
    attemptSpin->setValue(settings.maxAttempts);
    attemptSpin->setSingleStep(8);

    auto *budgetSpin = new QSpinBox(&dialog);
    budgetSpin->setRange(1, 3600);
    budgetSpin->setValue(static_cast<int>(settings.timeBudgetSeconds));
    budgetSpin->setSuffix(QObject::tr(" s"));

    auto *form = new QFormLayout(&dialog);
    form->addRow(QObject::tr("Phase assignment:"), phaseCombo);
    form->addRow(QObject::tr("Search attempts:"), attemptSpin);
    form->addRow(QObject::tr("Time budget:"), budgetSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    settings.phaseCount = phaseCombo->currentData().toInt();
    settings.maxAttempts = attemptSpin->value();
    settings.timeBudgetSeconds = budgetSpin->value();
    return true;
}

bool readNormalGraphDrawSettings(QWidget *parent, NormalGraphDrawSettings &settings)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Normal Graph P&R Options"));

    auto *visualCheck = new QCheckBox(
        QObject::tr("Generate circuit layer/SVG figures"),
        &dialog);
    visualCheck->setChecked(settings.generateVisualizations);

    auto *stageCheck = new QCheckBox(
        QObject::tr("Generate stage debug TeX snapshots"),
        &dialog);
    stageCheck->setChecked(settings.generateStageSnapshots);

    auto *form = new QFormLayout(&dialog);
    form->addRow(visualCheck);
    form->addRow(stageCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Run"));
    form->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    settings.generateVisualizations = visualCheck->isChecked();
    settings.generateStageSnapshots = stageCheck->isChecked();
    return true;
}

std::optional<LayoutBounds> calculateUsedLayoutBounds(
    const std::map<unsigned int, fcngraph::position> &nodePositions,
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> &routes)
{
    bool hasCoord = false;
    unsigned int minX = std::numeric_limits<unsigned int>::max();
    unsigned int minY = std::numeric_limits<unsigned int>::max();
    unsigned int maxX = 0;
    unsigned int maxY = 0;

    auto includeCoord = [&](const fcngraph::position &pos) {
        hasCoord = true;
        minX = std::min(minX, pos.first);
        minY = std::min(minY, pos.second);
        maxX = std::max(maxX, pos.first);
        maxY = std::max(maxY, pos.second);
    };

    for (const auto &entry : nodePositions) {
        includeCoord(entry.second);
    }
    for (const auto &route : routes) {
        for (const fcngraph::position &pos : route.second) {
            includeCoord(pos);
        }
    }

    if (!hasCoord) {
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

using GateRouteKey = std::pair<unsigned int, unsigned int>;

struct GateLevelIfcnLayout {
    std::map<unsigned int, fcngraph::position> nodePositions;
    std::map<GateRouteKey, std::vector<fcngraph::position>> routes;
    std::map<unsigned int, QString> nodeNames;
    std::map<unsigned int, QString> nodeTypes;
    int restoredHiddenNotNodes = 0;
    int skippedHiddenNotRoutes = 0;
};

QString safeParseNodeName(fcngraph::Parse &parse, unsigned int nodeIndex)
{
    QString name;
    try {
        name = QString::fromStdString(parse.getNodeName(static_cast<int>(nodeIndex))).trimmed();
    } catch (...) {
        name.clear();
    }
    if (name.isEmpty()) {
        try {
            name = QString::fromStdString(parse.getVertexName(static_cast<int>(nodeIndex))).trimmed();
        } catch (...) {
            name.clear();
        }
    }
    return name.isEmpty() ? QStringLiteral("node_%1").arg(nodeIndex) : name;
}

QString safeParseNodeType(fcngraph::Parse &parse, unsigned int nodeIndex, const QString &fallbackType)
{
    QString type;
    try {
        type = QString::fromStdString(parse.getNodeType(static_cast<int>(nodeIndex))).trimmed();
    } catch (...) {
        type.clear();
    }
    return type.isEmpty() ? fallbackType : type;
}

QString layoutCoordText(const fcngraph::position &pos)
{
    // Qt uses a downward-positive Y axis, while TikZ uses an upward-positive
    // Y axis. Convert exactly once at the exporter boundary. Nodes, routes,
    // and phase cells all use this helper, so their coordinates stay aligned.
    const int drawY = -static_cast<int>(pos.second);
    return QStringLiteral("(%1,%2)").arg(pos.first).arg(drawY);
}

int layoutPhaseValue(int rawPhase, int phaseCount)
{
    const int cycle = phaseCount > 0 ? std::min(phaseCount, 4) : 4;
    int phase = rawPhase % cycle;
    if (phase < 0) {
        phase += cycle;
    }
    return std::clamp(phase, 0, 3);
}

std::optional<std::size_t> findRoutePositionIndex(
    const std::vector<fcngraph::position> &path,
    const fcngraph::position &pos)
{
    const auto it = std::find(path.begin(), path.end(), pos);
    if (it == path.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(path.begin(), it));
}

bool isInteriorRouteIndex(const std::vector<fcngraph::position> &path, std::size_t index)
{
    return index > 0 && index + 1 < path.size();
}

std::optional<std::size_t> chooseInteriorRouteIndex(
    const std::vector<fcngraph::position> &path,
    const std::set<fcngraph::position> &occupiedPositions)
{
    for (std::size_t index = 1; index + 1 < path.size(); ++index) {
        if (occupiedPositions.find(path[index]) == occupiedPositions.end()) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<fcngraph::position> chooseSharedInteriorPosition(
    const std::vector<std::vector<fcngraph::position>> &paths,
    const std::set<fcngraph::position> &occupiedPositions)
{
    if (paths.empty() || paths.front().size() < 3) {
        return std::nullopt;
    }

    std::size_t commonCount = paths.front().size();
    for (const auto &path : paths) {
        commonCount = std::min(commonCount, path.size());
    }

    std::size_t index = 0;
    while (index < commonCount) {
        const fcngraph::position &candidate = paths.front()[index];
        bool allSame = true;
        for (const auto &path : paths) {
            if (path[index] != candidate) {
                allSame = false;
                break;
            }
        }
        if (!allSame) {
            break;
        }
        ++index;
    }
    commonCount = index;

    for (std::size_t reverse = commonCount; reverse > 1; --reverse) {
        const std::size_t candidateIndex = reverse - 1;
        bool interiorForAll = true;
        for (const auto &path : paths) {
            if (!isInteriorRouteIndex(path, candidateIndex)) {
                interiorForAll = false;
                break;
            }
        }
        const fcngraph::position &candidate = paths.front()[candidateIndex];
        if (interiorForAll && occupiedPositions.find(candidate) == occupiedPositions.end()) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool splitRouteAtPosition(
    GateLevelIfcnLayout &layout,
    const GateRouteKey &originalKey,
    unsigned int notNodeIndex,
    const fcngraph::position &notPosition)
{
    auto routeIt = layout.routes.find(originalKey);
    if (routeIt == layout.routes.end()) {
        return false;
    }

    const auto splitIndex = findRoutePositionIndex(routeIt->second, notPosition);
    if (!splitIndex.has_value() || !isInteriorRouteIndex(routeIt->second, splitIndex.value())) {
        return false;
    }

    std::vector<fcngraph::position> inputPath;
    std::vector<fcngraph::position> outputPath;
    inputPath.insert(inputPath.end(),
                     routeIt->second.begin(),
                     routeIt->second.begin() + static_cast<std::ptrdiff_t>(splitIndex.value()) + 1);
    outputPath.insert(outputPath.end(),
                      routeIt->second.begin() + static_cast<std::ptrdiff_t>(splitIndex.value()),
                      routeIt->second.end());

    layout.routes.erase(routeIt);
    const GateRouteKey inputKey = {originalKey.first, notNodeIndex};
    if (layout.routes.find(inputKey) == layout.routes.end()) {
        layout.routes[inputKey] = std::move(inputPath);
    }
    layout.routes[{notNodeIndex, originalKey.second}] = std::move(outputPath);
    return true;
}

GateLevelIfcnLayout restoreHiddenNotNodesForIfcn(
    fcngraph::Parse &parse,
    const std::map<unsigned int, fcngraph::position> &nodePositions,
    const std::map<GateRouteKey, std::vector<fcngraph::position>> &routes)
{
    GateLevelIfcnLayout layout;
    layout.nodePositions = nodePositions;
    layout.routes = routes;

    std::set<fcngraph::position> occupiedPositions;
    unsigned int maxNodeIndex = 0;
    for (const auto &entry : layout.nodePositions) {
        maxNodeIndex = std::max(maxNodeIndex, entry.first);
        occupiedPositions.insert(entry.second);
        layout.nodeNames[entry.first] = safeParseNodeName(parse, entry.first);
        layout.nodeTypes[entry.first] = safeParseNodeType(parse, entry.first, QStringLiteral("unknown"));
    }
    for (const auto &entry : parse.hide_not_place_pair) {
        maxNodeIndex = std::max(maxNodeIndex, entry.first);
    }
    unsigned int nextSyntheticIndex = maxNodeIndex + 1;

    auto allocateNotNode = [&](unsigned int preferredIndex,
                               const fcngraph::position &position,
                               const QString &baseName) {
        unsigned int nodeIndex = preferredIndex;
        if (layout.nodePositions.find(nodeIndex) != layout.nodePositions.end()) {
            while (layout.nodePositions.find(nextSyntheticIndex) != layout.nodePositions.end()) {
                ++nextSyntheticIndex;
            }
            nodeIndex = nextSyntheticIndex++;
        }

        layout.nodePositions[nodeIndex] = position;
        layout.nodeNames[nodeIndex] = nodeIndex == preferredIndex
            ? baseName
            : QStringLiteral("%1_copy_%2").arg(baseName).arg(nodeIndex);
        layout.nodeTypes[nodeIndex] = QStringLiteral("not");
        occupiedPositions.insert(position);
        ++layout.restoredHiddenNotNodes;
        return nodeIndex;
    };

    std::map<unsigned int, std::vector<GateRouteKey>> hiddenNotRoutes;
    for (const auto &entry : parse.hide_not_place_pair) {
        hiddenNotRoutes[entry.first].push_back(entry.second);
    }

    for (const auto &group : hiddenNotRoutes) {
        const unsigned int hiddenNotIndex = group.first;
        const QString notName = safeParseNodeName(parse, hiddenNotIndex);

        std::vector<std::pair<GateRouteKey, std::vector<fcngraph::position>>> availableRoutes;
        availableRoutes.reserve(group.second.size());
        for (const GateRouteKey &routeKey : group.second) {
            const auto routeIt = layout.routes.find(routeKey);
            if (routeIt == layout.routes.end() || routeIt->second.size() < 3) {
                ++layout.skippedHiddenNotRoutes;
                continue;
            }
            availableRoutes.push_back({routeKey, routeIt->second});
        }
        if (availableRoutes.empty()) {
            continue;
        }

        std::optional<fcngraph::position> sharedPosition;
        if (availableRoutes.size() > 1) {
            std::vector<std::vector<fcngraph::position>> paths;
            paths.reserve(availableRoutes.size());
            for (const auto &route : availableRoutes) {
                paths.push_back(route.second);
            }
            sharedPosition = chooseSharedInteriorPosition(paths, occupiedPositions);
        }

        if (sharedPosition.has_value()) {
            const unsigned int notNodeIndex = allocateNotNode(hiddenNotIndex, sharedPosition.value(), notName);
            bool restoredAnyRoute = false;
            for (const auto &route : availableRoutes) {
                if (splitRouteAtPosition(layout, route.first, notNodeIndex, sharedPosition.value())) {
                    restoredAnyRoute = true;
                } else {
                    ++layout.skippedHiddenNotRoutes;
                }
            }
            if (!restoredAnyRoute) {
                layout.nodePositions.erase(notNodeIndex);
                layout.nodeNames.erase(notNodeIndex);
                layout.nodeTypes.erase(notNodeIndex);
                occupiedPositions.erase(sharedPosition.value());
                --layout.restoredHiddenNotNodes;
            }
            continue;
        }

        bool firstRestoredRoute = true;
        for (const auto &route : availableRoutes) {
            const auto splitIndex = chooseInteriorRouteIndex(route.second, occupiedPositions);
            if (!splitIndex.has_value()) {
                ++layout.skippedHiddenNotRoutes;
                continue;
            }
            const fcngraph::position notPosition = route.second[splitIndex.value()];
            const unsigned int preferredIndex = firstRestoredRoute ? hiddenNotIndex : nextSyntheticIndex;
            const unsigned int notNodeIndex = allocateNotNode(preferredIndex, notPosition, notName);
            firstRestoredRoute = false;
            if (!splitRouteAtPosition(layout, route.first, notNodeIndex, notPosition)) {
                layout.nodePositions.erase(notNodeIndex);
                layout.nodeNames.erase(notNodeIndex);
                layout.nodeTypes.erase(notNodeIndex);
                occupiedPositions.erase(notPosition);
                --layout.restoredHiddenNotNodes;
                ++layout.skippedHiddenNotRoutes;
            }
        }
    }

    return layout;
}

std::map<fcngraph::position, int> buildClockPhaseMapForBounds(
    fcngraph::GridChessboard &grid,
    const LayoutBounds &bounds)
{
    std::map<fcngraph::position, int> phaseMap;
    if (bounds.width <= 0 || bounds.height <= 0 || bounds.minX < 0 || bounds.minY < 0) {
        return phaseMap;
    }

    for (unsigned long long y = static_cast<unsigned int>(bounds.minY);
         y <= static_cast<unsigned int>(bounds.maxY);
         ++y) {
        for (unsigned long long x = static_cast<unsigned int>(bounds.minX);
             x <= static_cast<unsigned int>(bounds.maxX);
             ++x) {
            const auto ux = static_cast<unsigned int>(x);
            const auto uy = static_cast<unsigned int>(y);
            int phase = static_cast<int>(grid.getCoorPos_Phase(ux, uy)) - 1;
            phase = std::max(0, std::min(3, phase));
            phaseMap[{ux, uy}] = phase;
        }
    }
    return phaseMap;
}

HeuristicLayoutResult runHeuristicLayoutSearch(
    const HeuristicLayoutRequest &request,
    const HeuristicProgressCallback &progress)
{
    HeuristicLayoutResult result;

    if (progress) {
        progress(QObject::tr("Parsing Verilog netlist"), 1, 7);
    }

    fcngraph::Parse parse;
    parse.parseVerilog(request.file, ifcn::verilog::OutputBoundaryMode::Combinational, false);

    if (parse.get_input_num() == 0 || parse.get_output_num() == 0
        || parse.getm_numVertices() < 2 || parse.getm_numEdges() == 0) {
        result.error = QObject::tr("Heuristic P&R requires a nonempty scalar combinational netlist with primary inputs, outputs, and supported Boolean assignments.");
        return result;
    }
    if (request.width <= 0 || request.height <= 0
        || request.generationSize <= 0 || request.populationSize < 2) {
        result.error = QObject::tr("Heuristic P&R requires positive grid dimensions and generations, and a population of at least two.");
        return result;
    }

    result.inputNum = static_cast<int>(parse.get_input_num());
    result.gateNum = static_cast<int>(parse.getm_numVertices()) - result.inputNum;
    result.outputNum = static_cast<int>(parse.get_output_num());
    result.wireNum = static_cast<int>(parse.getm_numEdges()) - result.outputNum;

    if (progress) {
        progress(QObject::tr("Optimizing circuit graph"), 2, 7);
    }

    parse.optimizeAIOG_DRC(2,2,2,2,2,2);
    parse.optimizeBufferNode();
    // Keep NOT gates explicit for heuristic layouts. Hiding them can collapse
    // a NOT edge to a two-point route, leaving no legal grid point to restore.
    parse.caculateSameLayerNodeRoutePair();

    if (parse.getEffectiveNodes().size() < 2 || parse.getEffectiveEdges().empty()) {
        result.error = QObject::tr("Heuristic P&R parsed no connected gate-level circuit to place and route.");
        return result;
    }

    const auto availablePositions = static_cast<uint64_t>(request.width)
        * static_cast<uint64_t>(request.height);
    if (parse.getEffectiveNodes().size() >= availablePositions) {
        result.error = QObject::tr("Heuristic P&R requires more grid positions than circuit nodes so placement and mutation have free space.");
        return result;
    }

    result.hiddenNotNum = static_cast<int>(parse.hideNotNodeIndex.size());
    result.removedEdgeNum = result.wireNum - static_cast<int>(parse.getEffectiveEdges().size());

    if (progress) {
        progress(QObject::tr("Building clock grid and A* router"), 3, 7);
    }

    fcngraph::GridChessboard grid(request.scheme,
                                  {0, 0},
                                  {static_cast<unsigned int>(request.width),
                                   static_cast<unsigned int>(request.height)});
    fcngraph::Astar astar(grid);
    // Different source nets may share a tile only as a legal straight H/V
    // crossover; unrestricted overlap can otherwise short the mapped circuit.
    astar.setAllowInterSourceWireOverlap(false);
    fcngraph::GeneticAlgorithm ga(parse,
                                  grid,
                                  astar,
                                  static_cast<uint64_t>(request.generationSize),
                                  static_cast<uint64_t>(request.populationSize),
                                  0.9,
                                  0.5);

    int routedCandidates = 0;
    ga.setFitnessCallback([&](double fitness) {
        ++routedCandidates;
        if (progress) {
            progress(QObject::tr("Running GA: routed candidate %1, fitness %2")
                         .arg(routedCandidates)
                         .arg(fitness, 0, 'f', 3),
                     -1,
                     0);
        }
    });

    QElapsedTimer timer;
    timer.start();

    if (progress) {
        progress(QObject::tr("Running genetic placement and routing"), -1, 0);
    }

    const bool isSuccess = ga.gaRun();
    result.elapsedSeconds = timer.elapsed() / 1000.0;

    result.statusMessage = request.filePath +
        " \\& " + QString::number(result.gateNum) +
        " \\& " + QString::number(result.inputNum) + " / " + QString::number(result.outputNum) +
        " \\& " + QString::number(result.wireNum) + "&  $ \\times$  = &"
        " \\& " + QString::number(result.hiddenNotNum) +
        " \\& " + QString::number(result.removedEdgeNum) +
        " \\& " + QString::number(request.width) + " $\\times$ " + QString::number(request.height) +
        " \\& " + QString::number(result.elapsedSeconds, 'f', 1) +"& & & & &   $ \\times$  = &  \\\\";

    if (!isSuccess) {
        result.error = QObject::tr("gaRun fail;");
        return result;
    }

    if (ga.best_individuals.empty()) {
        result.error = QObject::tr("ga success, but no layout individual is available for .ifcn export;");
        return result;
    }

    result.nodePositions = ga.getNodePos();
    result.routes = ga.getRoutes();

    if (progress) {
        progress(QObject::tr("Cropping clock area to used layout bounds"), 5, 7);
    }

    const auto usedBounds = calculateUsedLayoutBounds(result.nodePositions, result.routes);
    if (!usedBounds.has_value()) {
        result.error = QObject::tr("Heuristic P&R generated no used layout coordinates for .ifcn export.");
        return result;
    }
    result.usedBounds = usedBounds.value();
    result.posPhase = buildClockPhaseMapForBounds(grid, result.usedBounds);

    result.parse = parse;
    result.success = true;
    return result;
}


bool sceneCoordinates(const fcngraph::position &cellPos, int &xCoord, int &yCoord)
{
    constexpr unsigned int kPitch = 20;
    constexpr unsigned int kOrigin = 200;
    constexpr unsigned int kMaxCellCoord =
        (static_cast<unsigned int>(std::numeric_limits<int>::max()) - kOrigin) / kPitch;

    if (cellPos.first > kMaxCellCoord || cellPos.second > kMaxCellCoord) {
        return false;
    }

    xCoord = static_cast<int>(cellPos.first * kPitch + kOrigin);
    yCoord = static_cast<int>(cellPos.second * kPitch + kOrigin);
    return true;
}

QString projectSourceDir()
{
#ifdef IFCN_PROJECT_SOURCE_DIR
    return QString::fromUtf8(IFCN_PROJECT_SOURCE_DIR);
#else
    return QString();
#endif
}

bool hasNormalGraphDrawScript(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return false;
    }
    return QFileInfo(QDir(rootPath).filePath("src/algorithm/main/test_normal_graph_draw.py")).isFile();
}

bool hasLayoutModule(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return false;
    }

    QDir libDir(QDir(rootPath).filePath("src/algorithm/lib"));
    if (!libDir.exists()) {
        return false;
    }

    return !libDir.entryList(QStringList() << "iFCN_Lab*.so" << "iFCN_Lab*.pyd" << "iFCN_Lab*.dll",
                             QDir::Files).isEmpty();
}

QString bundledLayoutRoot()
{
    const QString sourceDir = projectSourceDir();
    if (sourceDir.isEmpty()) {
        return QString();
    }
    return QDir(sourceDir).filePath("include/layout_backend");
}

QString findLayoutRoot()
{
    const QString envRoot = QString::fromLocal8Bit(qgetenv("IFCN_LAYOUT_ROOT"));
    if (hasNormalGraphDrawScript(envRoot)) {
        return QDir(envRoot).absolutePath();
    }

    const QStringList candidates = {
        bundledLayoutRoot()
    };

    for (const QString &candidate : candidates) {
        if (hasNormalGraphDrawScript(candidate) && hasLayoutModule(candidate)) {
            return QDir(candidate).absolutePath();
        }
    }
    for (const QString &candidate : candidates) {
        if (hasNormalGraphDrawScript(candidate)) {
            return QDir(candidate).absolutePath();
        }
    }
    return QString();
}

QString findLayoutPython(const QString &rootPath)
{
    const QString envPython = QString::fromLocal8Bit(qgetenv("IFCN_LAYOUT_PYTHON"));
    if (!envPython.isEmpty()) {
        return envPython;
    }

    const QStringList candidates = {
        QDir(rootPath).filePath("myenv/bin/python"),
        QStandardPaths::findExecutable("python3"),
        QStandardPaths::findExecutable("python")
    };

    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QStringLiteral("python3");
}

QString pythonPathSeparator()
{
#ifdef Q_OS_WIN
    return QStringLiteral(";");
#else
    return QStringLiteral(":");
#endif
}

void prependPythonPath(QProcessEnvironment &environment, const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    const QString existing = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(QStringLiteral("PYTHONPATH"),
                       existing.isEmpty() ? path : path + pythonPathSeparator() + existing);
}

QDir sourceOutputBaseDir(const QFileInfo &sourceInfo)
{
    return sourceInfo.absoluteDir().exists() ? sourceInfo.absoluteDir() : QDir::current();
}

QString layoutOutputStem(const QFileInfo &sourceInfo, fcngraph::Parse &parse)
{
    return sourceInfo.completeBaseName().isEmpty()
        ? QString::fromStdString(parse.get_moduleName())
        : sourceInfo.completeBaseName();
}

QString layoutOutputDirPath(const QFileInfo &sourceInfo,
                            const QString &outputStem,
                            const QString &dirSuffix)
{
    const QString suffix = dirSuffix.trimmed();
    const QDir baseDir = sourceOutputBaseDir(sourceInfo);
    return suffix.isEmpty()
        ? baseDir.absolutePath()
        : baseDir.filePath(outputStem + suffix);
}

QString processTail(const QString &text, int maxChars = 6000)
{
    if (text.size() <= maxChars) {
        return text.trimmed();
    }
    return text.right(maxChars).trimmed();
}

QString locateNormalGraphDrawIfcn(const QString &outputDirPath, const QString &sourceBaseName)
{
    const QDir outputDir(outputDirPath);
    const QString expected = outputDir.filePath(sourceBaseName + QStringLiteral("_normal_graph_draw.ifcn"));
    if (QFileInfo(expected).isFile()) {
        return expected;
    }

    const QStringList candidates = outputDir.entryList(QStringList() << "*_normal_graph_draw.ifcn"
                                                                     << "*_gate_level_pr.ifcn",
                                                       QDir::Files,
                                                       QDir::Time);
    if (!candidates.isEmpty()) {
        return outputDir.filePath(candidates.first());
    }
    return QString();
}

QString findIfcnMetricsExecutable()
{
    const QString envPath = QString::fromLocal8Bit(qgetenv("IFCN_MAPPING_METRICS_EXE"));
    if (!envPath.isEmpty() && QFileInfo(envPath).isExecutable()) {
        return envPath;
    }

    const QString sourceDir = projectSourceDir();
    if (sourceDir.isEmpty()) {
        return QString();
    }

    const QStringList candidates = {
        QDir(sourceDir).filePath("build/ifcn_mapping_metrics"),
        QDir(sourceDir).filePath("build/src/ifcn_mapping_metrics")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QString();
}

bool writeMappingMetricsToIfcn(const QString &ifcnPath)
{
    if (!QFileInfo(ifcnPath).isFile()) {
        return false;
    }

    const QString executable = findIfcnMetricsExecutable();
    if (executable.isEmpty()) {
        qWarning() << "[Layout] ifcn_mapping_metrics executable was not found.";
        return false;
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments(QStringList() << ifcnPath);
    process.start();
    if (!process.waitForStarted(3000) || !process.waitForFinished(120000)) {
        qWarning() << "[Layout] ifcn_mapping_metrics timed out for" << ifcnPath;
        process.kill();
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning() << "[Layout] ifcn_mapping_metrics failed for" << ifcnPath
                   << QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList parts = output.split(
        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
#else
    const QStringList parts = output.split(
        QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts);
#endif
    if (parts.size() < 2) {
        qWarning() << "[Layout] ifcn_mapping_metrics returned invalid output:" << output;
        return false;
    }

    bool okCell = false;
    bool okCross = false;
    const qulonglong cellCount = parts[0].toULongLong(&okCell);
    const qulonglong crossCount = parts[1].toULongLong(&okCross);
    if (!okCell || !okCross) {
        qWarning() << "[Layout] ifcn_mapping_metrics returned non-numeric output:" << output;
        return false;
    }

    QFile file(ifcnPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString trimmed = line.trimmed().toLower();
        if (trimmed.startsWith(QStringLiteral("#cell count:")) ||
            trimmed.startsWith(QStringLiteral("#cross count:")) ||
            trimmed.startsWith(QStringLiteral("#phase cycle:"))) {
            continue;
        }
        lines.push_back(line);
    }
    file.close();

    int insertAfter = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed().toLower();
        if (trimmed.startsWith(QStringLiteral("#layout area:"))) {
            insertAfter = i;
            break;
        }
        if (insertAfter < 0 &&
            (trimmed.startsWith(QStringLiteral("#total layers:")) ||
             trimmed.startsWith(QStringLiteral("#edges number:")))) {
            insertAfter = i;
        }
    }
    if (insertAfter < 0) {
        insertAfter = 0;
    }

    lines.insert(insertAfter + 1, QStringLiteral("#cross count: %1").arg(crossCount));
    lines.insert(insertAfter + 1, QStringLiteral("#cell count: %1").arg(cellCount));

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    for (const QString &line : lines) {
        out << line << '\n';
    }
    return true;
}

void writeMappingMetricsToLayoutArtifacts(const QString &ifcnPath)
{
    writeMappingMetricsToIfcn(ifcnPath);

    const QFileInfo info(ifcnPath);
    const QString encodedPath = info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral("_encoded.ifcn"));
    if (QFileInfo(encodedPath).isFile()) {
        writeMappingMetricsToIfcn(encodedPath);
    }
}
}



VerilogHandler::VerilogHandler(MainWindow *parent)
    : QObject(parent), mainWindow(parent)
{

}

void VerilogHandler::handleNormalGraphDrawLayout()
{
    NormalGraphDrawSettings settings;
    if (!readNormalGraphDrawSettings(mainWindow, settings)) {
        mainWindow->printToStatusBar(tr("Normal graph draw placement and routing cancelled."));
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        mainWindow,
        tr("Open Verilog File"),
        projectSourceDir().isEmpty() ? QDir::currentPath() : projectSourceDir(),
        tr("Verilog files (*.v);;All file (*)"));

    if (filePath.isEmpty()) {
        mainWindow->printToStatusBar(tr("Normal graph draw placement and routing cancelled."));
        return;
    }
    runNormalGraphDrawLayoutForFile(filePath,
                                    false,
                                    settings.generateVisualizations,
                                    settings.generateStageSnapshots);
}

void VerilogHandler::runNormalGraphDrawLayoutForFile(const QString &filePath,
                                                     bool quietStatusMessages,
                                                     bool generateVisualizations,
                                                     bool generateStageSnapshots)
{
    if (filePath.isEmpty()) {
        mainWindow->printToStatusBar(tr("Normal graph draw placement and routing cancelled."));
        return;
    }
    mainWindow->updateVerilogSourceFile(filePath);

    const QString rootPath = findLayoutRoot();
    if (rootPath.isEmpty() || !hasNormalGraphDrawScript(rootPath)) {
        emit operationFailed(tr("Normal graph draw backend not found. Expected include/layout_backend/src/algorithm/main/test_normal_graph_draw.py."));
        return;
    }

    const QString python = findLayoutPython(rootPath);
    if (python.isEmpty()) {
        emit operationFailed(tr("Python interpreter for normal graph draw backend was not found."));
        return;
    }

    if (!hasLayoutModule(rootPath)) {
        emit operationProgress(
            tr("Normal graph draw Python module was not found under %1; Python will report details if import fails.")
                .arg(QDir::toNativeSeparators(rootPath)),
            0,
            0);
    }

    const QFileInfo sourceInfo(filePath);
    QDir sourceDir = sourceInfo.absoluteDir();
    const QString outputDirPath = sourceDir.filePath(sourceInfo.completeBaseName() + QStringLiteral("_normal_graph_draw"));
    if (!QDir().mkpath(outputDirPath)) {
        emit operationFailed(tr("Cannot create normal graph draw output directory: %1")
                                 .arg(QDir::toNativeSeparators(outputDirPath)));
        return;
    }

    const QString scriptPath = QDir(rootPath).filePath("src/algorithm/main/test_normal_graph_draw.py");
    QStringList arguments;
    arguments << scriptPath
              << QStringLiteral("--benchmark") << filePath
              << QStringLiteral("--output-dir") << outputDirPath;
    if (!generateVisualizations) {
        arguments << QStringLiteral("--skip-figures");
    }
    arguments << QStringLiteral("--skip-latex");
    if (!generateStageSnapshots) {
        arguments << QStringLiteral("--skip-stage-snapshots");
    }

    emit operationStarted(tr("Normal graph draw placement and routing"),
                          tr("Running normal graph draw for %1").arg(QDir::toNativeSeparators(filePath)));
    emit operationProgress(tr("Normal graph draw backend is running"), 0, 0);
    QCoreApplication::processEvents();

    QProcess process;
    process.setProgram(python);
    process.setArguments(arguments);
    process.setWorkingDirectory(rootPath);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    prependPythonPath(environment, QDir(rootPath).filePath("src/algorithm"));
    if (environment.value(QStringLiteral("IFCN_GRAPHVIZ_TIMEOUT")).isEmpty()) {
        environment.insert(QStringLiteral("IFCN_GRAPHVIZ_TIMEOUT"), QStringLiteral("60"));
    }
    if (environment.value(QStringLiteral("IFCN_SIFT_TIMEOUT")).isEmpty()) {
        environment.insert(QStringLiteral("IFCN_SIFT_TIMEOUT"), QStringLiteral("20"));
    }
    if (environment.value(QStringLiteral("IFCN_SIFT_EVALUATIONS")).isEmpty()) {
        environment.insert(QStringLiteral("IFCN_SIFT_EVALUATIONS"), QStringLiteral("200000"));
    }
    environment.insert(QStringLiteral("MPLBACKEND"), QStringLiteral("Agg"));
    process.setProcessEnvironment(environment);

    QString combinedOutput;
    auto drainOutput = [&]() {
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());
        if (!stdOut.isEmpty()) {
            combinedOutput += stdOut;
        }
        if (!stdErr.isEmpty()) {
            combinedOutput += stdErr;
        }
        if (combinedOutput.size() > 20000) {
            combinedOutput = combinedOutput.right(12000);
        }
        QCoreApplication::processEvents();
    };

    process.start();
    if (!process.waitForStarted(5000)) {
        emit operationFailed(tr("Failed to start normal graph draw backend with Python: %1")
                                 .arg(QDir::toNativeSeparators(python)));
        return;
    }

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(250);
        drainOutput();
    }
    drainOutput();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = processTail(combinedOutput);
        emit operationFailed(tr("Normal graph draw placement and routing failed with exit code %1.%2%3")
                                 .arg(process.exitCode())
                                 .arg(detail.isEmpty() ? QString() : QStringLiteral("\n"))
                                 .arg(detail));
        return;
    }

    const QString ifcnPath = locateNormalGraphDrawIfcn(outputDirPath, sourceInfo.completeBaseName());
    if (ifcnPath.isEmpty()) {
        emit operationFailed(tr("Normal graph draw completed, but no generated layout .ifcn was found in %1.")
                                 .arg(QDir::toNativeSeparators(outputDirPath)));
        return;
    }

    writeMappingMetricsToLayoutArtifacts(ifcnPath);
    {
        StatusMessagesMuteGuard muteGuard(mainWindow != nullptr ? mainWindow->customStatusBar : nullptr,
                                          quietStatusMessages);
        mainWindow->mapIfcnFile(ifcnPath, !quietStatusMessages);
    }

    const QDir outputDir(outputDirPath);
    QString svgPath = outputDir.filePath(sourceInfo.completeBaseName() + QStringLiteral("_physical.svg"));
    if (!QFileInfo(svgPath).isFile()) {
        svgPath = outputDir.filePath(sourceInfo.completeBaseName() + QStringLiteral(".svg"));
    }
    QString message = quietStatusMessages
        ? tr("Normal graph draw layout loaded.")
        : tr("Normal graph draw layout loaded: %1").arg(QDir::toNativeSeparators(ifcnPath));
    if (!quietStatusMessages && QFileInfo(svgPath).isFile()) {
        message += tr("; SVG: %1").arg(QDir::toNativeSeparators(svgPath));
    }
    emit operationFinished(message);
}

void VerilogHandler::handleParseVerilogFile()
{
    if (heuristicLayoutRunning) {
        emit operationProgress(tr("Heuristic P&R is already running in the background."), -1, 0);
        return;
    }

    //选择加载文件
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), QDir::currentPath(),
                                                          tr("Verilog files (*.v);;All file (*)"));

    if(filePath.isEmpty()) {
        QString message = "FilePath is empty!";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }else{
        QString message = "open file: " + filePath;
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
    }
    runHeuristicLayoutForFile(filePath);
}

void VerilogHandler::runHeuristicLayoutForFile(const QString &filePath)
{
    if (heuristicLayoutRunning) {
        emit operationProgress(tr("Heuristic P&R is already running in the background."), -1, 0);
        return;
    }
    if (filePath.isEmpty()) {
        QString message = "FilePath is empty!";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }

    mainWindow->updateVerilogSourceFile(filePath);

    HeuristicLayoutRequest request;
    request.filePath = filePath;
    request.file = filePath.toStdString();

    // 弹出参数选择框
    GaChessboardInputDialog inputDialog(mainWindow);
    if (inputDialog.exec() == QDialog::Accepted) {
        request.clockSchemeStr = inputDialog.getClockScheme();
        request.width = inputDialog.getWidth();
        request.height = inputDialog.getHeight();
        request.generationSize = inputDialog.getGeneration();
        request.populationSize = inputDialog.getPopulation();

        if (request.clockSchemeStr == "TDD" || request.clockSchemeStr == "2DDwave") {
            request.scheme = CLOCK_SCHEME::TDD;
        } else if (request.clockSchemeStr == "USE") {
            request.scheme = CLOCK_SCHEME::USE;
        } else if (request.clockSchemeStr == "RES") {
            request.scheme = CLOCK_SCHEME::RES;
        }
    }else{
        QString message = "GA was cancelled or closed.";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }

    heuristicLayoutRunning = true;
    emit operationStarted(tr("Heuristic placement and routing"),
                          tr("Preparing heuristic layout for %1")
                              .arg(QDir::toNativeSeparators(filePath)));
    QCoreApplication::processEvents();

    auto requestPtr = std::make_shared<HeuristicLayoutRequest>(std::move(request));
    QPointer<VerilogHandler> self(this);
    auto postProgress = [self](const QString &detail, int value, int maximum) {
        if (self.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, detail, value, maximum]() {
            if (!self.isNull()) {
                emit self->operationProgress(detail, value, maximum);
            }
        }, Qt::QueuedConnection);
    };

    QThread *thread = QThread::create([self, requestPtr, postProgress]() {
        auto result = std::make_shared<HeuristicLayoutResult>();
        try {
            *result = runHeuristicLayoutSearch(*requestPtr, postProgress);
        } catch (const std::exception &ex) {
            result->error = QString::fromLocal8Bit(ex.what());
        } catch (...) {
            result->error = QObject::tr("Heuristic P&R failed with an unknown error.");
        }

        if (self.isNull()) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, requestPtr, result]() {
            if (self.isNull()) {
                return;
            }

            self->heuristicLayoutRunning = false;
            if (!result->success) {
                const QString message = result->error.isEmpty()
                    ? QObject::tr("Heuristic P&R failed.")
                    : result->error;
                self->mainWindow->customStatusBar->addMessage(message);
                emit self->operationFailed(message);
                return;
            }

            const QString gridMessage = "Clock Scheme: " + requestPtr->clockSchemeStr +
                ", Chessboard size: [" + QString::number(requestPtr->width) +
                " , " + QString::number(requestPtr->height) + "];";
            self->mainWindow->printToStatusBar(gridMessage);
            self->mainWindow->customStatusBar->addMessage("gaRun success;" + result->statusMessage);

            emit self->operationProgress(QObject::tr("Saving heuristic .ifcn layout"), 6, 7);
            QCoreApplication::processEvents();

            const QString ifcnPath = self->saveGateLevelIfcn(requestPtr->filePath,
                                                             result->parse,
                                                             result->nodePositions,
                                                             result->routes,
                                                             result->posPhase,
                                                             4,
                                                             result->gateNum,
                                                             result->inputNum,
                                                             result->outputNum,
                                                             result->wireNum,
                                                             result->usedBounds.width,
                                                             result->usedBounds.height,
                                                             result->elapsedSeconds,
                                                             QStringLiteral("heuristic P&R algorithm"),
                                                             QStringLiteral("_heuristic_pr_layout"),
                                                             QStringLiteral("_heuristic_pr_layout.ifcn"));
            if (ifcnPath.isEmpty()) {
                const QString message = QObject::tr("Heuristic P&R generated a layout, but failed to save .ifcn.");
                emit self->operationFailed(message);
                return;
            }
            self->saveGraphRenderLatex(requestPtr->filePath,
                                       result->parse,
                                       result->nodePositions,
                                       result->routes,
                                       result->posPhase,
                                       4,
                                       result->usedBounds.width,
                                       result->usedBounds.height,
                                       QStringLiteral("_heuristic_pr_layout"),
                                       QStringLiteral("_heuristic_pr_layout.tex"));

            emit self->operationProgress(QObject::tr("Loading heuristic .ifcn into UI"), 7, 7);
            QCoreApplication::processEvents();

            if (!self->mainWindow->mapIfcnFile(ifcnPath)) {
                emit self->operationFailed(QObject::tr("Heuristic P&R produced an .ifcn file, but cell-level mapping failed: %1")
                                               .arg(QDir::toNativeSeparators(ifcnPath)));
                return;
            }
            emit self->operationFinished(QObject::tr("Heuristic P&R layout loaded: %1")
                                             .arg(QDir::toNativeSeparators(ifcnPath)));
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();

}

void VerilogHandler::handleGraphRender()
{

    // 选择加载文件
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), QDir::currentPath(),
                                                          tr("Verilog files (*.v);;All file (*)"));
    if(filePath.isEmpty()) {
        QString message = "FilePath is empty!";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }else{
        QString message = "open file: " + filePath;
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
    }
    runGraphRenderForFile(filePath);
}

void VerilogHandler::runGraphRenderForFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        QString message = "FilePath is empty!";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }
    mainWindow->updateVerilogSourceFile(filePath);

    GraphRenderSettings settings;
    if (!readGraphRenderSettings(mainWindow, settings)) {
        QString message = "Graph render was cancelled.";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }

    std::string file = filePath.toStdString();

    try {
        emit operationStarted(tr("Irregular-Clock Graph P&R"),
                              tr("Searching for the smallest legal %1-phase layout within %2 seconds")
                                  .arg(settings.phaseCount).arg(settings.timeBudgetSeconds));
        QCoreApplication::processEvents();

        fcngraph::IrregularLayoutOptions searchOptions;
        searchOptions.phaseCount = settings.phaseCount;
        searchOptions.maxAttempts = settings.maxAttempts;
        searchOptions.timeBudgetSeconds = settings.timeBudgetSeconds;
        const auto searchResult = fcngraph::searchIrregularLayout(
            file, searchOptions,
            [&](const fcngraph::GraphDrawSearchProgress& progress) {
                emit operationProgress(QString::fromStdString(progress.message),
                                       progress.attempt, progress.attemptLimit);
                QCoreApplication::processEvents();
            });
        if (!searchResult.success || !searchResult.parse) {
            emit operationFailed(QString::fromStdString(searchResult.error));
            QCoreApplication::processEvents();
            return;
        }
        // Different internal candidates may use different buffering. Export
        // the exact topology owned by the validated winning snapshot.
        auto& parse = *searchResult.parse;
        const auto* bestLayout = &searchResult.layout;
        const int inputNum = static_cast<int>(parse.get_input_num());
        const int outputNum = static_cast<int>(parse.get_output_num());
        const int gateNum = static_cast<int>(bestLayout->nodePositions.size()) - inputNum;
        const int wireNum = static_cast<int>(bestLayout->routes.size());
        const double elapsedSeconds = searchResult.elapsedSeconds;
        const int width = bestLayout->bounds.width;
        const int height = bestLayout->bounds.height;
        QString message = tr("Irregular-clock layout: %1 x %2 = %3 clock tiles; %4 cells; route length %5; %6 s")
            .arg(width).arg(height).arg(bestLayout->bounds.area)
            .arg(static_cast<qulonglong>(searchResult.metrics.physicalCells))
            .arg(bestLayout->routeLength).arg(elapsedSeconds, 0, 'f', 2);
        if (searchResult.budgetExpired)
            message += tr("; time budget reached, retained the best legal layout");

        std::map<unsigned int, position> node_pos;
        for (auto& pair : bestLayout->nodePositions) {
            node_pos[static_cast<unsigned int>(pair.first)] = pair.second;
        }

        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes = bestLayout->routes;
        std::unordered_map<position, GridCell, PositionHash> gridCells = bestLayout->gridCells;

        unsigned int scale = 0;
        if(!node_pos.empty()) {
            position max = node_pos.begin()->second;
            for(auto &v : node_pos) {
                unsigned int y = v.second.second;
                unsigned int ymax = max.second;
                if(y > ymax) {
                    max = v.second;
                }
            }
            scale = max.second + 1;
        } else {
            throw std::runtime_error("No node positions generated");
        }

        std::map<unsigned int, position> node_pos_trans;
        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes_trans;
        std::vector<std::pair<position, position>> pos_trans; // 坐标转换前后对应

        for(auto &v : node_pos) {
            node_pos_trans[v.first] = coordtrans(v.second, scale);
            pos_trans.push_back(std::make_pair(v.second, node_pos_trans[v.first]));
        }

        for(auto &v : routes) {
            std::vector<position> temp = v.second;
            std::vector<position> temp_trans;
            for(auto & route : temp) {
                temp_trans.push_back(coordtrans(route, scale));
                pos_trans.push_back(std::make_pair(route, coordtrans(route, scale)));
            }
            routes_trans[v.first] = temp_trans;
        }

        std::map<position, int> pos_phase;
        for(auto &v : gridCells) {
            if (v.second.getPhase() < 1) {
                continue;
            }
            for(auto &pair : pos_trans) {
                if(v.first == pair.first) {
                    pos_phase[pair.second] = v.second.getPhase()-1;
                    break;
                }
            }
        }

        // Export the selected candidate at the meaningful P&R boundaries.
        // All snapshots use exactly the same winning placement so that
        // differences between figures reflect routing and phase assignment,
        // rather than a different candidate's geometry.
        const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> noRoutes;
        const std::map<position, int> noPhases;
        saveGraphRenderLatex(filePath,
                             parse,
                             node_pos_trans,
                             noRoutes,
                             noPhases,
                             settings.phaseCount,
                             width,
                             height,
                             QStringLiteral("_graph_pr_stages"),
                             QStringLiteral("_01_layered_placement.tex"),
                             false);
        saveGraphRenderLatex(filePath,
                             parse,
                             node_pos_trans,
                             routes_trans,
                             noPhases,
                             settings.phaseCount,
                             width,
                             height,
                             QStringLiteral("_graph_pr_stages"),
                             QStringLiteral("_02_astar_routing.tex"),
                             false);
        saveGraphRenderLatex(filePath,
                             parse,
                             node_pos_trans,
                             routes_trans,
                             pos_phase,
                             settings.phaseCount,
                             width,
                             height,
                             QStringLiteral("_graph_pr_stages"),
                             QStringLiteral("_03_clock_phase_assignment.tex"),
                             false);
        saveGraphRenderLatex(filePath,
                             parse,
                             node_pos_trans,
                             routes_trans,
                             pos_phase,
                             settings.phaseCount,
                             width,
                             height,
                             QStringLiteral("_graph_pr_stages"),
                             QStringLiteral("_05_compact_final.tex"),
                             false);

        const QString ifcnPath = saveGateLevelIfcn(filePath,
                                                   parse,
                                                   node_pos_trans,
                                                   routes_trans,
                                                   pos_phase,
                                                   settings.phaseCount,
                                                   static_cast<int>(gateNum),
                                                   static_cast<int>(inputNum),
                                                   static_cast<int>(outputNum),
                                                   static_cast<int>(wireNum),
                                                   width,
                                                   height,
                                                   elapsedSeconds,
                                                   QStringLiteral("Irregular-Clock Graph P&R"),
                                                   QStringLiteral("_graph_pr_layout"),
                                                   QStringLiteral("_graph_pr_layout.ifcn"));
        saveGraphRenderLatex(filePath,
                             parse,
                             node_pos_trans,
                             routes_trans,
                             pos_phase,
                             settings.phaseCount,
                             width,
                             height,
                             QStringLiteral("_graph_pr_layout"),
                             QStringLiteral("_graph_pr_layout.tex"));
        if (ifcnPath.isEmpty()) {
            throw std::runtime_error("Graph P&R generated a layout, but failed to save .ifcn.");
        }

        if (!mainWindow->mapIfcnFile(ifcnPath)) {
            emit operationFailed(tr("Graph P&R generated a layout, but cell mapping failed: %1")
                                     .arg(QDir::toNativeSeparators(ifcnPath)));
            return;
        }
        emit operationFinished(message + tr(" ; loaded .ifcn: %1").arg(QDir::toNativeSeparators(ifcnPath)));
        QCoreApplication::processEvents();
    } catch (const std::exception &ex) {
        mainWindow->endSceneBatchUpdate(false);
        QString message = QString("布局布线失败: %1").arg(ex.what());
        emit operationFailed(message);
        QCoreApplication::processEvents();
        return;
    }

}

void VerilogHandler::mappingCellItem(std::map<unsigned int, position>& _node_pos,
                                    std::map<std::pair<unsigned int, unsigned int>,
                                    std::vector<position>>& _nodepair_route,
                                    Parse _parse, std::map<position, int>& _pos_phase)
{
    Mapping mapping;

    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    for(auto &v: _nodepair_route)
    {
        std::vector<position> unitcell;
        for(auto &pos : v.second)
        {
            unitcell.push_back(pos);
        }
        circle_line.push_back(unitcell);
    }


    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node,type), (扇入，扇出)>
    Nodelink.clear();


    for(auto &v : _nodepair_route)
    {
        std::vector<position> templine = v.second;
        std::string startnodeName = _parse.getNodeType(v.first.first);
        position startpos = templine.front();
        std::string endnodeName = _parse.getNodeType(v.first.second);
        position endpos = templine.back();
        Nodelink[std::make_pair(startpos, startnodeName)];
        Nodelink[std::make_pair(endpos, endnodeName)];
    }
    for(auto &pair : Nodelink)
    {
        for(auto &line : circle_line)
        {
            if(pair.first.first == line.front())
            {
                std::vector<position> &output = pair.second.second;
                output.push_back(*std::next(line.begin()));
            }
            else if(pair.first.first == line.back())
            {
                std::vector<position> &intput = pair.second.first;
                intput.push_back(*std::prev(std::prev(line.end())));
            }
        }
        //避免重复放置输入输出
        if(pair.second.first.size() > 1)
        {
            std::sort(pair.second.first.begin(), pair.second.first.end());
            auto unique_end = std::unique(pair.second.first.begin(), pair.second.first.end());
            pair.second.first.erase(unique_end, pair.second.first.end());
        }
        if(pair.second.second.size() > 1)
        {
            std::sort(pair.second.second.begin(), pair.second.second.end());
            auto unique_end = std::unique(pair.second.second.begin(), pair.second.second.end());
            pair.second.second.erase(unique_end, pair.second.second.end());
        }
    }

    std::vector<position> notcell = {};
    if (!_parse.hide_not_place_pair.empty())
    {
        for (auto &v: _parse.hide_not_place_pair){
            QString message = QString("node gate insert position: (%1 , %2)")
                            .arg(v.second.first)
                            .arg(v.second.second);
            mainWindow->customStatusBar->addMessage(message);
        }

        mapping.not_check(circle_line);
        auto noputplace1 = mapping.temppos_list_examp;
        auto noputplace2 = mapping.oneroutepos_list_examp;
        std::vector<position> crosspos = {};//将所有线路中格子容量已满的保存
        for (auto &line : noputplace1)
        {
            crosspos.push_back(line.second);
        }
        for (auto &line : noputplace2)
        {
            crosspos.push_back(line.second);
        }

        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> not_routes;
        for(auto &line : _parse.hide_not_place_pair)
        {
            not_routes[line.second] = _nodepair_route[line.second];
        }

        std::vector<std::vector<position>> not_line;//放置not的线路（结构同circle_line）
        //std::map<position, int> startpos_num;
        std::map<position, std::vector<std::vector<position>>> startpos_line;
        for(auto &v: not_routes)
        {
            std::vector<position> unitcell;
            for(auto &pos : v.second)
            {
                unitcell.push_back(pos);
            }
            not_line.push_back(unitcell);

            if (startpos_line.find(unitcell.front()) == startpos_line.end())
            {
                startpos_line[unitcell.front()] = {};
                startpos_line[unitcell.front()].push_back(unitcell);
            } else
            {
                startpos_line[unitcell.front()].push_back(unitcell);
            }
        }

        //std::vector<std::vector<position>> not_line_used;
        for(auto &line : startpos_line)
        {
            if(line.second.size() == 1)
            {
                //避免非门插入到复用线路上
                std::vector<std::vector<position>> samestartpos_routes = {};
                std::vector<position> reusepos = {};
                for (auto &v : circle_line)
                {
                    if (line.first == v.front())
                    {
                        samestartpos_routes.push_back(v);
                    }
                }
                if (samestartpos_routes.size() == 2)
                {
                    int i = 0;
                    while(samestartpos_routes.front()[i] == samestartpos_routes.back()[i])
                    {
                        reusepos.push_back(samestartpos_routes.front()[i]);
                        i++;
                    }
                }

                auto single_route = line.second.front();
                for (auto it = single_route.begin(); it != single_route.end(); it++)
                {
                    if (*it == single_route.front())
                    {
                        continue;
                    }
                    else if (*it == single_route.back())
                    {
                        QString message = "NOT gate put fail!";
                        mainWindow->customStatusBar->addMessage(message);
                        return;
                    }
                    else
                    {
                        if ((std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                        ||(std::find(reusepos.begin(), reusepos.end(), *it) != reusepos.end()))
                        {
                            continue;
                        }
                        else
                        {
                            position prevpos = *(std::prev(it));
                            position nextpos = *(std::next(it));
                            Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                            Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                            Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                            notcell.push_back(*it);

                            std::vector<position> front_line;
                            std::vector<position> back_line;
                            front_line.insert(front_line.end(), single_route.begin(), std::next(it));
                            back_line.insert(back_line.end(), it, single_route.end());

                            auto it1 = std::find(circle_line.begin(), circle_line.end(), single_route);
                            circle_line.insert(it1, front_line);
                            auto it2 = std::find(circle_line.begin(), circle_line.end(), single_route);
                            circle_line.insert(it2, back_line);
                            auto delete_line = std::find(circle_line.begin(), circle_line.end(), single_route);
                            circle_line.erase(delete_line);

                            // circle_line.push_back(front_line);
                            // circle_line.push_back(back_line);

                            break;
                        }
                    }
                }

            }
            else//line.second.size() == 2
            {
                auto route1 = line.second.front();
                auto route2 = line.second.back();
                int i = 0;
                while ((i < route1.size()) && (i < route2.size()) && (route1[i] == route2[i]))
                {
                    ++i;
                }
                int fanout = i-1;

                if (fanout == 0)//无复用线路
                {
                    for (auto it = route1.begin(); it != route1.end(); it++)
                    {
                        if (*it == route1.front())
                        {
                            continue;
                        }
                        else if (*it == route1.back())
                        {
                            QString message = "NOT gate put fail!";
                            mainWindow->customStatusBar->addMessage(message);
                            return;
                        }
                        else
                        {
                            if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                            {
                                continue;
                            }
                            else
                            {
                                position prevpos = *(std::prev(it));
                                position nextpos = *(std::next(it));
                                Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                notcell.push_back(*it);

                                std::vector<position> front_line;
                                std::vector<position> back_line;
                                front_line.insert(front_line.end(), route1.begin(), std::next(it));
                                back_line.insert(back_line.end(), it, route1.end());

                                auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                circle_line.insert(it1, front_line);
                                auto it2 = std::find(circle_line.begin(), circle_line.end(), route1);
                                circle_line.insert(it2, back_line);
                                auto delete_line = std::find(circle_line.begin(), circle_line.end(), route1);
                                circle_line.erase(delete_line);

                                break;
                            }
                        }
                    }

                    for (auto it = route2.begin(); it != route2.end(); it++)
                    {
                        if (*it == route2.front())
                        {
                            continue;
                        }
                        else if (*it == route2.back())
                        {
                            QString message = "NOT gate put fail!";
                            mainWindow->customStatusBar->addMessage(message);
                            return;
                        }
                        else
                        {
                            if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                            {
                                continue;
                            }
                            else
                            {
                                position prevpos = *(std::prev(it));
                                position nextpos = *(std::next(it));
                                Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                notcell.push_back(*it);

                                std::vector<position> front_line;
                                std::vector<position> back_line;
                                front_line.insert(front_line.end(), route2.begin(), std::next(it));
                                back_line.insert(back_line.end(), it, route2.end());

                                auto it1 = std::find(circle_line.begin(), circle_line.end(), route2);
                                circle_line.insert(it1, front_line);
                                auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                circle_line.insert(it2, back_line);
                                auto delete_line = std::find(circle_line.begin(), circle_line.end(), route2);
                                circle_line.erase(delete_line);

                                break;
                            }
                        }
                    }
                }
                else//有复用线路，非门优先放置于扇出点
                {
                    auto fanout1 = std::find(route1.begin(), route1.end(), route1[fanout]);
                    auto fanout2 = std::find(route2.begin(), route2.end(), route2[fanout]);
                    position prevpos1 = *(std::prev(fanout1));
                    position prevpos2 = *(std::prev(fanout2));
                    position nextpos1 = *(std::next(fanout1));
                    position nextpos2 = *(std::next(fanout2));
                    if ((prevpos1 == prevpos2)&&(nextpos1 != nextpos2))
                    {
                        if (std::find(crosspos.begin(), crosspos.end(), *fanout1) == crosspos.end())
                        {
                            Nodelink[std::make_pair(*fanout1, "not")] = {{}, {}};
                            Nodelink[std::make_pair(*fanout1, "not")].first.push_back(prevpos1);
                            Nodelink[std::make_pair(*fanout1, "not")].second.push_back(nextpos1);
                            Nodelink[std::make_pair(*fanout1, "not")].second.push_back(nextpos2);
                            notcell.push_back(*fanout1);

                            std::vector<position> reuse_route = {};
                            std::vector<position> route1_back = {};
                            std::vector<position> route2_back = {};

                            reuse_route.insert(reuse_route.end(), route1.begin(), std::next(fanout1));
                            route1_back.insert(route1_back.end(), fanout1, route1.end());
                            route2_back.insert(route2_back.end(), fanout2, route2.end());

                            auto it0 = std::find(circle_line.begin(), circle_line.end(), route1);
                            circle_line.insert(it0, reuse_route);
                            auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                            circle_line.insert(it1, route1_back);
                            auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                            circle_line.insert(it2, route2_back);
                            auto delete_line1 = std::find(circle_line.begin(), circle_line.end(), route1);
                            circle_line.erase(delete_line1);
                            auto delete_line2 = std::find(circle_line.begin(), circle_line.end(), route2);
                            circle_line.erase(delete_line2);
                        }
                        else//扇出点有交叉线不可插入not
                        {
                            bool reusenot = false;
                            for (int i = 1; i < fanout; i++)
                            {
                                if (std::find(crosspos.begin(), crosspos.end(), route1[i]) != crosspos.end())
                                {
                                    continue;
                                }
                                else
                                {
                                    Nodelink[std::make_pair(route1[i], "not")] = {{}, {}};
                                    Nodelink[std::make_pair(route1[i], "not")].first.push_back(route1[i-1]);
                                    Nodelink[std::make_pair(route1[i], "not")].second.push_back(route1[i+1]);
                                    notcell.push_back(route1[i]);

                                    std::vector<position> reuse_route = {};
                                    std::vector<position> route1_back = {};
                                    std::vector<position> route2_back = {};
                                    auto notpos1 = std::find(route1.begin(), route1.end(), route1[i]);
                                    auto notpos2 = std::find(route2.begin(), route2.end(), route2[i]);
                                    reuse_route.insert(reuse_route.end(), route1.begin(), std::next(notpos1));
                                    route1_back.insert(route1_back.end(), notpos1, route1.end());
                                    route2_back.insert(route2_back.end(), notpos2, route2.end());

                                    auto it0 = std::find(circle_line.begin(), circle_line.end(), route1);
                                    circle_line.insert(it0, reuse_route);
                                    auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                    circle_line.insert(it1, route1_back);
                                    auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                    circle_line.insert(it2, route2_back);
                                    auto delete_line1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                    circle_line.erase(delete_line1);
                                    auto delete_line2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                    circle_line.erase(delete_line2);

                                    reusenot = true;
                                    break;
                                }
                            }
                            if (!reusenot)//复用线路里无法插入not
                            {
                                std::vector<position> route1_back = {};
                                std::vector<position> route2_back = {};
                                route1_back.insert(route1_back.end(), fanout1, route1.end());
                                route2_back.insert(route2_back.end(), fanout2, route2.end());

                                for (auto it = route1_back.begin(); it != route1_back.end(); it++)
                                {
                                    if (*it == route1_back.front())
                                    {
                                        continue;
                                    }
                                    else if (*it == route1_back.back())
                                    {
                                        QString message = "NOT gate put fail!";
                                        mainWindow->customStatusBar->addMessage(message);
                                        return;
                                    }
                                    else
                                    {
                                        if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                                        {
                                            continue;
                                        }
                                        else
                                        {
                                            position prevpos = *(std::prev(it));
                                            position nextpos = *(std::next(it));
                                            Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                            Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                            Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                            notcell.push_back(*it);

                                            std::vector<position> front_line;
                                            std::vector<position> back_line;
                                            auto itpos = std::find(route1.begin(), route1.end(), *it);
                                            front_line.insert(front_line.end(), route1.begin(), std::next(itpos));
                                            back_line.insert(back_line.end(), itpos, route1.end());

                                            auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                            circle_line.insert(it1, front_line);
                                            auto it2 = std::find(circle_line.begin(), circle_line.end(), route1);
                                            circle_line.insert(it2, back_line);
                                            auto delete_line = std::find(circle_line.begin(), circle_line.end(), route1);
                                            circle_line.erase(delete_line);

                                            break;
                                        }
                                    }
                                }

                                for (auto it = route2_back.begin(); it != route2_back.end(); it++)
                                {
                                    if (*it == route2_back.front())
                                    {
                                        continue;
                                    }
                                    else if (*it == route2_back.back())
                                    {
                                        QString message = "NOT gate put fail!";
                                        mainWindow->customStatusBar->addMessage(message);
                                        return;
                                    }
                                    else
                                    {
                                        if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                                        {
                                            continue;
                                        }
                                        else
                                        {
                                            position prevpos = *(std::prev(it));
                                            position nextpos = *(std::next(it));
                                            Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                            Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                            Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                            notcell.push_back(*it);

                                            std::vector<position> front_line;
                                            std::vector<position> back_line;
                                            auto itpos = std::find(route2.begin(), route2.end(), *it);
                                            front_line.insert(front_line.end(), route2.begin(), std::next(itpos));
                                            back_line.insert(back_line.end(), itpos, route2.end());

                                            auto it1 = std::find(circle_line.begin(), circle_line.end(), route2);
                                            circle_line.insert(it1, front_line);
                                            auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                            circle_line.insert(it2, back_line);
                                            auto delete_line = std::find(circle_line.begin(), circle_line.end(), route2);
                                            circle_line.erase(delete_line);

                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        QString message = "Fanout_NOT gate put fail!";
                        mainWindow->customStatusBar->addMessage(message);
                        return;
                    }
                }
            }
        }
    }

    if(Nodelink.empty())
    {
        QString message = "Nodelink empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }

    mapping.node_mapping(Nodelink);
    auto nodeexample = mapping.nodecell_list;
    if(nodeexample.empty())
    {
        QString message = "nodeexample empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }
    for(auto &cell : nodeexample)
    {
        auto cellpos_list = cell.second;
        if(cell.first == "input")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                position node_pos = {x_node, y_node};
                QString Iname = "default";
                for (auto &v : _node_pos)
                {
                    if (node_pos == v.second)
                    {
                        std::string index = _parse.getVertexName(v.first);
                        Iname = QString::fromStdString(index);
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::InputCell, _pos_phase, Iname);
            }
        }
        else if (cell.first == "output")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                position node_pos = {x_node, y_node};
                QString Oname = "default";
                for (auto &v : _node_pos)
                {
                    if (node_pos == v.second)
                    {
                        std::string index = _parse.getVertexName(v.first);
                        Oname = QString::fromStdString(index);
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::OutputCell, _pos_phase, Oname);

            }
        }
        else if (cell.first == "normal")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::NormalCell, _pos_phase);

            }
        }
        else if (cell.first == "fix0")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_0, _pos_phase);

            }
        }
        else if (cell.first == "fix1")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_1, _pos_phase);

            }
        }
    }

    auto routeexample = mapping.mapping_line(circle_line);
    auto crossexample = mapping.crossline_list;

    std::vector<position> allroutecells;
    for (auto &pair : routeexample)
    {
        for (auto &v : pair.second)
        {
            allroutecells.insert(allroutecells.end(), v.begin(), v.end());
        }
    }

    //Cross线路元胞放置
    std::vector<position> crosscell;
    std::vector<position> verticalcell;
    if(!crossexample.empty())
    {
        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                crosscell.insert(crosscell.end(), cross.begin(), cross.end());
            }
        }
        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                for(auto unit = cross.begin(); unit != cross.end(); unit++)
                {
                    if((unit == cross.begin()) || (std::next(unit) == cross.end()))
                    {
                        int count = 0;
                        position dir1 = {(*unit).first, (*unit).second + 1};
                        position dir2 = {(*unit).first, (*unit).second - 1};
                        position dir3 = {(*unit).first - 1, (*unit).second};
                        position dir4 = {(*unit).first + 1, (*unit).second};
                        if(std::find(crosscell.begin(), crosscell.end(), dir1) != crosscell.end())
                        {
                            ++count;
                        }
                        if(std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                        {
                            ++count;
                        }
                        if(std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                        {
                            ++count;
                        }
                        if(std::find(crosscell.begin(), crosscell.end(), dir4) != crosscell.end())
                        {
                            ++count;
                        }
                        if (count >= 2)
                        {
                            position cellpos = *unit;
                            putCellItem(cellpos, 2, CellType::CrossoverCell, _pos_phase);

                        }

                        if(count < 2)
                        {
                            //若端点无法直接放置柱点，则跨时钟延伸两个单位元胞
                            if((std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir3) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir4) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos2 = dir1;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos3 = {dir1.first, dir1.second + 1};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, _pos_phase);
                                verticalcell.push_back(cellpos3);


                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else if ((std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir1) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir2) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos2 = dir4;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos3 = {dir4.first + 1, dir4.second};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, _pos_phase);
                                verticalcell.push_back(cellpos3);

                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else//放置交叉线端点三层柱点
                            {
                                position cellpos = *unit;
                                putCellItem(cellpos, 0, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos, 1, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos, 2, CellType::VerticalCell, _pos_phase);
                                verticalcell.push_back(cellpos);
                            }
                        }
                    }
                    else
                    {
                        position cellpos = *unit;
                        putCellItem(cellpos, 2, CellType::CrossoverCell, _pos_phase);


                    }
                }
            }

        }
    }
    //Normal线路元胞放置
    if(!routeexample.empty())
    {
        for(auto &line : routeexample)
        {
            for(auto &unit : line.second)
            {
                for(auto &pos : unit)
                {
                    if(std::find(crosscell.begin(), crosscell.end(), pos) == crosscell.end())
                    {
                        putCellItem(pos, 0, CellType::NormalCell, _pos_phase);

                    }
                    else
                    {
                        std::vector<position> unitroute = unit;
                        std::vector<position> tempcross;
                        for(auto &v : unitroute)
                        {
                            if(std::find(crosscell.begin(), crosscell.end(), v) != crosscell.end())
                            {
                                tempcross.push_back(v);
                            }
                        }
                        bool isvertical = false;
                        for (auto &cell : tempcross)
                        {
                            if (std::find(verticalcell.begin(), verticalcell.end(), cell) != verticalcell.end())
                            {
                                isvertical = true;
                                break;
                            }
                        }
                        if (!isvertical)
                        {
                            for(auto &pos : tempcross)
                            {
                                putCellItem(pos, 0, CellType::NormalCell, _pos_phase);
                            }
                        }

                    }

                }
            }
        }
    }

    for (auto &vpos : verticalcell)
    {
        int pl = 0;
        int posx_node = vpos.first / 5;
        int posy_node = vpos.second / 5;
        position pos_node = {posx_node, posy_node};
        std::vector<position> vtemp = {{vpos.first, vpos.second + 1},
                                    {vpos.first, vpos.second - 1},
                                    {vpos.first - 1, vpos.second},
                                    {vpos.first + 1, vpos.second} };
        for (auto &vcell : vtemp)
        {
            if (std::find(crosscell.begin(), crosscell.end(), vcell) != crosscell.end())
            {
                pl++;
            }
        }
        if ((pl >= 2) || (std::find(notcell.begin(), notcell.end(), pos_node) != notcell.end()))
        {
            QString message = "vertical problem position : ( "+ QString::number(posx_node) + " , "+ QString::number(posy_node) + " )";
            mainWindow->customStatusBar->addMessage(message);
        }
    }

    isOptimizeNOTNode = true;
}

position VerilogHandler::coordtrans(const position& pos, unsigned int scale)
{
    (void)scale;
    unsigned int x = pos.first;
    unsigned int y = pos.second;
    return {x, y};
}

QString VerilogHandler::saveGateLevelIfcn(
    const QString &sourceFilePath,
    Parse &parse,
    const std::map<unsigned int, position> &nodePositions,
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes,
    const std::map<position, int> &posPhase,
    int phaseCount,
    int gateNum,
    int inputNum,
    int outputNum,
    int wireNum,
    int width,
    int height,
    double elapsedSeconds,
    const QString &algorithmLabel,
    const QString &outputDirSuffix,
    const QString &outputFileSuffix)
{
    const int blockSize = 4;
    const QFileInfo sourceInfo(sourceFilePath);
    const QString circuitFileName = sourceInfo.fileName().isEmpty()
        ? QString::fromStdString(parse.get_moduleName())
        : sourceInfo.fileName();
    const QString outputStem = layoutOutputStem(sourceInfo, parse);
    const QString suffix = outputFileSuffix.trimmed().isEmpty()
        ? QStringLiteral("_gate_level_pr.ifcn")
        : outputFileSuffix;
    const QString outputDirPath = layoutOutputDirPath(sourceInfo, outputStem, outputDirSuffix);
    if (!QDir().mkpath(outputDirPath)) {
        mainWindow->printToStatusBar("Failed to create layout output directory: " +
                                     QDir::toNativeSeparators(outputDirPath));
        return QString();
    }
    const QString outputPath = QDir(outputDirPath).filePath(outputStem + suffix);
    const QString label = algorithmLabel.trimmed().isEmpty()
        ? QStringLiteral("placement and routing algorithm")
        : algorithmLabel.trimmed();
    const GateLevelIfcnLayout restoredLayout = restoreHiddenNotNodesForIfcn(parse, nodePositions, routes);

    bool hasCoord = false;
    unsigned int originX = 0;
    unsigned int originY = 0;
    unsigned int maxX = 0;
    unsigned int maxY = 0;
    auto includeCoord = [&](const position &pos) {
        if (!hasCoord) {
            originX = maxX = pos.first;
            originY = maxY = pos.second;
            hasCoord = true;
            return;
        }
        originX = std::min(originX, pos.first);
        originY = std::min(originY, pos.second);
        maxX = std::max(maxX, pos.first);
        maxY = std::max(maxY, pos.second);
    };

    for (const auto &entry : posPhase) {
        includeCoord(entry.first);
    }
    for (const auto &entry : restoredLayout.nodePositions) {
        includeCoord(entry.second);
    }
    for (const auto &route : restoredLayout.routes) {
        for (const position &pos : route.second) {
            includeCoord(pos);
        }
    }

    if (!hasCoord) {
        mainWindow->printToStatusBar("No graph layout coordinates to save.");
        return QString();
    }

    const int normalizedWidth = std::max(width, static_cast<int>(maxX - originX + 1));
    const int normalizedHeight = std::max(height, static_cast<int>(maxY - originY + 1));

    auto normalizePos = [&](const position &pos) -> position {
        return {pos.first - originX, pos.second - originY};
    };

    std::map<unsigned int, position> normalizedNodePositions;
    for (const auto &entry : restoredLayout.nodePositions) {
        normalizedNodePositions[entry.first] = normalizePos(entry.second);
    }

    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> normalizedRoutes;
    for (const auto &route : restoredLayout.routes) {
        auto &path = normalizedRoutes[route.first];
        path.reserve(route.second.size());
        for (const position &pos : route.second) {
            path.push_back(normalizePos(pos));
        }
    }

    std::map<position, int> normalizedPosPhase;
    for (const auto &entry : posPhase) {
        if (entry.first.first < originX || entry.first.first > maxX ||
            entry.first.second < originY || entry.first.second > maxY) {
            continue;
        }
        normalizedPosPhase[normalizePos(entry.first)] = entry.second;
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mainWindow->printToStatusBar("Failed to save .ifcn: " + QDir::toNativeSeparators(outputPath));
        return QString();
    }

    QTextStream out(&file);
    out << "#circuit name: " << circuitFileName << "\n\n";
    out << "#designed by " << label << " with encoded "
        << phaseCount << "-phase clock tiles.\n\n";

    out << "#gate level placement and routing infomation\n";
    out << "#algorithm: " << label << "\n";
    out << "#gates number: " << gateNum << "\n";
    out << "#input/output: " << inputNum << " / " << outputNum << "\n";
    out << "#primary output nodes: ";
    bool firstPrimaryOutput = true;
    for (const auto nodeIndex : parse.getOutputNodesIndex()) {
        if (normalizedNodePositions.count(nodeIndex) == 0) continue;
        if (!firstPrimaryOutput) out << ",";
        out << nodeIndex;
        firstPrimaryOutput = false;
    }
    out << "\n";
    out << "#edges number: " << wireNum << "\n";
    out << "#total layers: " << static_cast<int>(parse.getlayerNodeDivVec().size()) << "\n";
    out << "#layout area: width: " << normalizedWidth << ", height: " << normalizedHeight
        << ", area: " << normalizedWidth * normalizedHeight << "\n";
    out << "#phase origin: top-left=(" << originX << "," << originY
        << "), saved coordinates are normalized to (0,0)\n";
    out << "#phase count: " << phaseCount << "\n";
    out << "#runtime: " << QString::number(elapsedSeconds, 'f', 3) << "s\n\n";
    if (!parse.hide_not_place_pair.empty()) {
        out << "#hidden NOT restored: " << restoredLayout.restoredHiddenNotNodes
            << ", skipped routes: " << restoredLayout.skippedHiddenNotRoutes << "\n\n";
    }

    out << "#nodes info \n";
    out << "### nodeIndex, nodeName, nodeType, nodePosition ###\n";
    for (const auto &entry : normalizedNodePositions) {
        const unsigned int nodeIndex = entry.first;
        const auto nameIt = restoredLayout.nodeNames.find(nodeIndex);
        const auto typeIt = restoredLayout.nodeTypes.find(nodeIndex);
        const QString nodeName = nameIt != restoredLayout.nodeNames.end()
            ? nameIt->second
            : safeParseNodeName(parse, nodeIndex);
        const QString nodeType = typeIt != restoredLayout.nodeTypes.end()
            ? typeIt->second
            : safeParseNodeType(parse, nodeIndex, QStringLiteral("unknown"));
        out << nodeIndex << ", "
            << nodeName << ", "
            << nodeType << ", "
            << "(" << entry.second.first << "," << entry.second.second << ");\n";
    }
    out << "#nodes info \n\n";

    out << "#paths info\n";
    out << "### {node1, node2} : path ###\n";
    for (const auto &route : normalizedRoutes) {
        out << "(" << route.first.first << "," << route.first.second << "): ";
        for (std::size_t i = 0; i < route.second.size(); ++i) {
            if (i > 0) {
                out << ",";
            }
            out << "(" << route.second[i].first << "," << route.second[i].second << ")";
        }
        out << ";\n";
    }
    out << "#paths info\n";

    out << "#phase map\n";
    out << "#phase codec: phase_count=" << phaseCount
        << ", block_size=" << blockSize
        << ", encoding=packed_hex_2bit_row_major\n";
    out << "### tile(x,y) : packed_hex for a "
        << blockSize << "x" << blockSize << " phase block ###\n";

    try {
        const auto encodedTiles = fcngraph::phase_codec::encodePhaseMapToTiles(
            normalizedPosPhase,
            phaseCount,
            blockSize,
            normalizedWidth,
            normalizedHeight
        );
        for (const auto &tile : encodedTiles) {
            out << "tile(" << tile.tileX << "," << tile.tileY << "):0x"
                << QString::fromStdString(tile.hex) << ";\n";
        }
    } catch (const std::exception &ex) {
        mainWindow->printToStatusBar(QString("Failed to encode phase map: %1").arg(ex.what()));
        file.close();
        QFile::remove(outputPath);
        return QString();
    }
    out << "#phase map\n";
    file.close();

    mainWindow->printToStatusBar(label + " .ifcn saved: " + QDir::toNativeSeparators(outputPath));
    return outputPath;
}

void VerilogHandler::saveGraphRenderLatex(
    const QString &sourceFilePath,
    Parse &parse,
    const std::map<unsigned int, position> &nodePositions,
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes,
    const std::map<position, int> &posPhase,
    int phaseCount,
    int width,
    int height,
    const QString &outputDirSuffix,
    const QString &outputFileSuffix,
    bool showNullLabels)
{
    const QFileInfo sourceInfo(sourceFilePath);
    const QString outputStem = layoutOutputStem(sourceInfo, parse);
    const QString outputDirPath = layoutOutputDirPath(sourceInfo,
                                                     outputStem,
                                                     outputDirSuffix);
    if (!QDir().mkpath(outputDirPath)) {
        mainWindow->printToStatusBar("Failed to create layout LaTeX directory: " +
                                     QDir::toNativeSeparators(outputDirPath));
        return;
    }
    const QString fileSuffix = outputFileSuffix.trimmed().isEmpty()
        ? QStringLiteral("_layout.tex")
        : outputFileSuffix;
    const QString outputPath = QDir(outputDirPath).filePath(outputStem + fileSuffix);

    bool hasCoord = false;
    unsigned int originX = 0;
    unsigned int originY = 0;
    unsigned int maxX = 0;
    unsigned int maxY = 0;
    auto includeCoord = [&](const position &pos) {
        if (!hasCoord) {
            originX = maxX = pos.first;
            originY = maxY = pos.second;
            hasCoord = true;
            return;
        }
        originX = std::min(originX, pos.first);
        originY = std::min(originY, pos.second);
        maxX = std::max(maxX, pos.first);
        maxY = std::max(maxY, pos.second);
    };

    for (const auto &entry : nodePositions) {
        includeCoord(entry.second);
    }
    for (const auto &route : routes) {
        for (const position &pos : route.second) {
            includeCoord(pos);
        }
    }
    if (!hasCoord) {
        for (const auto &entry : posPhase) {
            includeCoord(entry.first);
        }
    }

    if (!hasCoord) {
        mainWindow->printToStatusBar("No layout coordinates to save as LaTeX.");
        return;
    }

    Q_UNUSED(width);
    Q_UNUSED(height);
    const int normalizedWidth = static_cast<int>(maxX - originX + 1);
    const int normalizedHeight = static_cast<int>(maxY - originY + 1);
    auto normalizePos = [&](const position &pos) -> position {
        return {pos.first - originX, pos.second - originY};
    };

    std::map<unsigned int, position> normalizedNodePositions;
    for (const auto &entry : nodePositions) {
        normalizedNodePositions[entry.first] = normalizePos(entry.second);
    }

    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> normalizedRoutes;
    for (const auto &route : routes) {
        auto &path = normalizedRoutes[route.first];
        path.reserve(route.second.size());
        for (const position &pos : route.second) {
            path.push_back(normalizePos(pos));
        }
    }

    std::map<position, int> normalizedPosPhase;
    for (const auto &entry : posPhase) {
        if (entry.first.first < originX || entry.first.first > maxX ||
            entry.first.second < originY || entry.first.second > maxY) {
            continue;
        }
        normalizedPosPhase[normalizePos(entry.first)] = entry.second;
    }

    std::set<position> usedPositions;
    for (const auto &entry : normalizedNodePositions) {
        usedPositions.insert(entry.second);
    }
    for (const auto &route : normalizedRoutes) {
        for (const position &pos : route.second) {
            usedPositions.insert(pos);
        }
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        mainWindow->printToStatusBar("Failed to save layout LaTeX: " + QDir::toNativeSeparators(outputPath));
        return;
    }

    QTextStream out(&file);
    out << "\\documentclass[tikz]{standalone}\n"
        << "\\usetikzlibrary{calc,arrows.meta}\n"
        << "\\newcommand{\\phasecell}[1]{\\vbox to 1cm{\\vfil\\hbox{\\hspace{1pt}\\scriptsize #1}\\vspace{1pt}}}\n"
        << "\\begin{document}\n"
        << "\\begin{tikzpicture}[\n"
        << "scale=0.5,transform shape,\n"
        << "cell/.style={rectangle, minimum size=1cm, inner sep=0pt, text width=1cm, align=left},\n"
        << "c-1/.style={cell, fill=white, text=black},\n"
        << "c0/.style={cell, fill=lightgray!50, text=black},\n"
        << "c1/.style={cell, fill=lightgray, text=black},\n"
        << "c2/.style={cell, fill=gray, text=black},\n"
        << "c3/.style={cell, fill=darkgray!90, text=white},\n"
        << "v/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.7cm},\n"
        << "vi/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.5cm,text=red},\n"
        << "vo/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.5cm,text=blue},\n"
        << "route/.style={->, >={Stealth[]},line width=0.8pt, blue!50}\n"
        << "]\n";

    for (int y = 0; y < normalizedHeight; ++y) {
        for (int x = 0; x < normalizedWidth; ++x) {
            const position pos{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
            const QString coord = layoutCoordText(pos);
            if (usedPositions.find(pos) == usedPositions.end()) {
                out << "\\node[c-1] at " << coord
                    << (showNullLabels ? "{\\phasecell{null}};\n" : "{};\n");
                continue;
            }

            const auto phaseIt = normalizedPosPhase.find(pos);
            if (phaseIt == normalizedPosPhase.end()) {
                out << "\\node[c-1] at " << coord
                    << (showNullLabels ? "{\\phasecell{null}};\n" : "{};\n");
                continue;
            }
            const int phase = layoutPhaseValue(phaseIt->second, phaseCount);
            out << "\\node[c" << phase << "] at " << coord
                << "{\\phasecell{" << phase << "}};\n";
        }
    }

    for (const auto &entry : normalizedNodePositions) {
        out << "\\node[v] (" << entry.first << ") at "
            << layoutCoordText(entry.second)
            << "{" << entry.first << "};\n";
    }

    for (const auto &route : normalizedRoutes) {
        if (route.second.empty()) {
            continue;
        }
        out << "\\draw[route](" << route.first.first << ")--";
        for (std::size_t i = 1; i + 1 < route.second.size(); ++i) {
            out << layoutCoordText(route.second[i]) << "--";
        }
        out << "(" << route.first.second << ");\n";
    }

    out << "\\end{tikzpicture}\n\\end{document}\n";
    file.close();

    mainWindow->printToStatusBar("Layout LaTeX saved: " + QDir::toNativeSeparators(outputPath));
}

void VerilogHandler::putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<position, int>& _pos_phase, QString _name)
{
    int x_coord = 0;
    int y_coord = 0;
    if (!sceneCoordinates(_cellpos, x_coord, y_coord)) {
        qWarning() << "[VerilogHandler] Skip mapped cell with invalid scene coordinate:"
                   << _cellpos.first << _cellpos.second;
        return;
    }

    int cell_layer = _celllayer;
    position node_pos = {_cellpos.first / 5, _cellpos.second / 5};
    auto phase_it = _pos_phase.find(node_pos);
    int phase = (phase_it != _pos_phase.end()) ? phase_it->second : -1;

    QCADCellItem *cellItem = new QCADCellItem(x_coord, y_coord, cell_layer, phase, _cellType, _name);
    mainWindow->checkCellInserted(mainWindow->layers, cellItem, cell_layer, x_coord, y_coord);
}

void VerilogHandler::putClock(std::map<position, int>& _pos_phase)
{
    for(auto &v : _pos_phase)
    {
        auto pos = v.first;
        int x = ((pos.first*5) + 2) * 20 + 200;
        int y = ((pos.second*5) + 2) * 20 + 200;
        if((v.second >= 0) && (v.second <= 3))
        {
            QCADClockScheme *item = new QCADClockScheme(v.second);
            item->setPos(x, y);
            item->setZValue(-1);
            mainWindow->scene->addItem(item);
        }
    }
}

void VerilogHandler::generateSVG()
{
    QRectF itemsBoundingRect = mainWindow->scene->exportContentBounds();

    if (!itemsBoundingRect.isValid() || itemsBoundingRect.isEmpty()) {
        QString message = "No cell-level layout to save.";
        mainWindow->printToStatusBar(message);
        return;
    }

    itemsBoundingRect = itemsBoundingRect.normalized();

    const QFileInfo currentFileInfo(mainWindow->currentFilePath());
    const QString circuitName = currentFileInfo.completeBaseName().isEmpty()
        ? QStringLiteral("cell_level_layout")
        : currentFileInfo.completeBaseName();
    const QDir outputDir = currentFileInfo.absoluteDir().exists()
        ? currentFileInfo.absoluteDir()
        : QDir::current();
    QString selectedFilter;
    QString outputPath = QFileDialog::getSaveFileName(mainWindow,
                                                      QObject::tr("Save Cell-Level Layout"),
                                                      outputDir.absoluteFilePath(circuitName + "_cell_level_layout.pdf"),
                                                      QObject::tr("PDF files (*.pdf);;SVG files (*.svg)"),
                                                      &selectedFilter);
    if (outputPath.isEmpty()) {
        return;
    }

    QFileInfo outputInfo(outputPath);
    QString suffix = outputInfo.suffix().toLower();
    if (suffix.isEmpty()) {
        suffix = selectedFilter.contains(QStringLiteral("SVG"), Qt::CaseInsensitive)
            ? QStringLiteral("svg")
            : QStringLiteral("pdf");
        outputPath += QLatin1Char('.') + suffix;
    } else if (suffix != QStringLiteral("svg") && suffix != QStringLiteral("pdf")) {
        suffix = selectedFilter.contains(QStringLiteral("SVG"), Qt::CaseInsensitive)
            ? QStringLiteral("svg")
            : QStringLiteral("pdf");
        outputPath += QLatin1Char('.') + suffix;
    }

    const QSizeF contentSize(qMax<qreal>(1.0, itemsBoundingRect.width()),
                             qMax<qreal>(1.0, itemsBoundingRect.height()));
    const QSize exportSize(qMax(1, static_cast<int>(std::ceil(contentSize.width()))),
                           qMax(1, static_cast<int>(std::ceil(contentSize.height()))));
    const QSizeF figureSize(exportSize);

    if (suffix == QStringLiteral("pdf")) {
        QPdfWriter pdfWriter(outputPath);
        pdfWriter.setResolution(72);
        pdfWriter.setTitle(QStringLiteral("iFCN cell-level layout"));
        pdfWriter.setCreator(QStringLiteral("iFCN"));
        const QPageSize pageSize(figureSize,
                                 QPageSize::Point,
                                 QStringLiteral("iFCN cell-level layout"));
        QPageLayout pageLayout(pageSize,
                               QPageLayout::Portrait,
                               QMarginsF(0.0, 0.0, 0.0, 0.0),
                               QPageLayout::Point);
        pageLayout.setMode(QPageLayout::FullPageMode);
        pdfWriter.setPageLayout(pageLayout);

        QPainter painter(&pdfWriter);
        if (!painter.isActive()) {
            QString message = "Failed to save cell-level layout: " + QDir::toNativeSeparators(outputPath);
            mainWindow->printToStatusBar(message);
            return;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        mainWindow->scene->renderForExport(&painter,
                                           QRectF(QPointF(0.0, 0.0), figureSize),
                                           itemsBoundingRect,
                                           Qt::IgnoreAspectRatio);
        painter.end();

        QString message = QString("Cell-level layout saved as cropped PDF: %1 (%2x%3 pt)")
            .arg(QDir::toNativeSeparators(outputPath))
            .arg(qRound(figureSize.width()))
            .arg(qRound(figureSize.height()));
        mainWindow->printToStatusBar(message);
        return;
    }

    const QSize svgSize = exportSize;

    QSvgGenerator svgGenerator;
    svgGenerator.setFileName(outputPath);
    svgGenerator.setSize(svgSize);
    svgGenerator.setViewBox(QRect(QPoint(0, 0), svgSize));
    svgGenerator.setTitle(QStringLiteral("iFCN cell-level layout"));
    svgGenerator.setDescription(QStringLiteral("Vector cell-level layout exported by iFCN."));

    QPainter painter(&svgGenerator);
    if (!painter.isActive()) {
        QString message = "Failed to save cell-level layout: " + QDir::toNativeSeparators(outputPath);
        mainWindow->printToStatusBar(message);
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    mainWindow->scene->renderForExport(&painter,
                                       QRectF(QPointF(0.0, 0.0), figureSize),
                                       itemsBoundingRect,
                                       Qt::IgnoreAspectRatio);
    painter.end();

    //打印信息
    QString message = QString("Cell-level layout saved as SVG: %1 (%2x%3)")
        .arg(QDir::toNativeSeparators(outputPath))
        .arg(svgSize.width())
        .arg(svgSize.height());
    mainWindow->printToStatusBar(message);

}
