#include "VerilogHandler.h"
#include "ui/mainwindow/MainWindow.h"
#include <autopr/graph/legacyGraphvizRenderer.h>
#include <QFileDialog>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QMessageBox>
#include <QImage>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsSvgItem>
#include <QLineEdit>
#include <QLabel>
#include <QSettings>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QSvgRenderer>
#include <QScopedValueRollback>
#include <autopr/algorithms/phase_codec.h>
#include "ui/widgets/GaChessboardInputDialog.h"
#include <algorithm>
#include <array>
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
    int maxSamePhase = 4;
    int maxAttempts = 32;
    int timeBudgetSeconds = 15;
    int compactionRounds = 6;
    bool generateSvg = false;
    bool generateLatex = false;
};

struct JuneRandomClockGraphSettings {
    bool generateSvg = false;
    bool generateLatex = false;
};

struct LegacyGraphvizPreviewTaskResult {
    bool success = false;
    QString error;
    QByteArray svg;
    std::size_t nodeCount = 0;
    std::size_t edgeCount = 0;
    double elapsedMilliseconds = 0.0;
};

struct GcnRlSettings {
    QString engine = "universal";
    QString qualityPreset = "balanced";
    QString checkpoint = "auto";
    QString clockMode = "stochastic-bands";
    QString device = "auto";
    QString startStrategy = "gcn";
    QString startOrientation = "auto";
    QString trainEvalMode = "auto";
    QString parseMode = "auto";
    int phaseCycle = 4;
    int xSpacing = 2;
    int ySpacing = 2;
    int routingPadding = 1;
    int maxSamePhase = 4;
    int clockFieldSamples = 4;
    int policyTrials = 1;
    int retrievalTopK = 4;
    int runs = 2;
    int workers = 2;
    int baseSeed = 7;
    int gcnEpochs = 120;
    int episodes = 80;
    int stepsPerEpisode = 8;
    int ppoEpochs = 4;
    int minibatchSize = 32;
    int finalExactCandidates = 12;
    int exactTimeoutSeconds = 45;
    int legalRepairCandidates = 24;
    int legalRepairMaxPadding = 8;
    int localRefineRounds = 8;
    int localMaxEvaluations = 240;
    int postPrimaryPackRounds = 6;
    int postAreaPackRounds = 10;
    int postPackMaxEvaluations = 320;
    int postPhaseStripPackRounds = 3;
    int postPhaseStripPackMaxEvaluations = 160;
    double areaRewardWeight = 3.0;
    double areaRegressionWeight = 250.0;
    double maxSpanWeight = 8.0;
    double legalRepairTimeoutMultiplier = 4.0;
    bool useLayoutMemory = true;
    bool useActionMemory = true;
    bool finalExactValidation = true;
    bool strictMemoryUpdates = false;
    bool writeTrainingPlots = true;
    bool memoryOnlyInference = false;
    bool clockAlignedStart = true;
    bool stochasticActions = false;
    bool allowExactMemoryRetrieval = true;
};

struct NormalGraphDrawSettings {
    bool generateVisualizations = false;
    bool generateStageSnapshots = false;
    bool generatePhaseLatex = true;
    QString crossingOrderer = QStringLiteral("ogdf");
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
    QString placementEngine;
    unsigned int xSpacing = 0;
    unsigned int ySpacing = 0;
    double searchCost = 0.0;
    int compactionCuts = 0;
    std::map<int, fcngraph::position> nodePositions;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> routes;
    std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> gridCells;
};

struct JuneRandomClockGraphTaskResult {
    bool success = false;
    QString error;
    fcngraph::Parse parse;
    LayoutSearchResult layout;
    int gateNum = 0;
    int inputNum = 0;
    int outputNum = 0;
    int wireNum = 0;
    double elapsedSeconds = 0.0;
};

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

void showLegacyGraphvizPreviewDialog(QWidget *parent,
                                     const QString &sourceFilePath,
                                     const LegacyGraphvizPreviewTaskResult &result)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("legacyGraphvizPreviewDialog"));
    dialog.setWindowTitle(QObject::tr("Legacy Graphviz Graph Draw · June 2025"));
    dialog.resize(1040, 760);
    dialog.setMinimumSize(720, 520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    auto *summary = new QLabel(
        QObject::tr("DOT topology preview for %1 · %2 nodes · %3 edges · %4 ms")
            .arg(QFileInfo(sourceFilePath).fileName())
            .arg(result.nodeCount)
            .arg(result.edgeCount)
            .arg(result.elapsedMilliseconds, 0, 'f', 1),
        &dialog);
    summary->setStyleSheet(QStringLiteral("font-weight: 600; color: #243447;"));
    layout->addWidget(summary);

    auto *notice = new QLabel(
        QObject::tr("View only: the arrows below are Graphviz logical edges. They are not the legacy A* physical routes and this preview is not IO-, phase-, or mapping-certified."),
        &dialog);
    notice->setObjectName(QStringLiteral("legacyGraphvizPreviewNotice"));
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral(
        "background: #fff8e1; border: 1px solid #e5c968; border-radius: 6px; "
        "padding: 8px 10px; color: #644f12;"));
    layout->addWidget(notice);

    auto *scene = new QGraphicsScene(&dialog);
    auto *svgItem = new QGraphicsSvgItem();
    auto *renderer = new QSvgRenderer(result.svg, svgItem);
    svgItem->setSharedRenderer(renderer);
    scene->addItem(svgItem);
    scene->setSceneRect(svgItem->boundingRect().adjusted(-24.0, -24.0, 24.0, 24.0));

    auto *view = new QGraphicsView(scene, &dialog);
    view->setObjectName(QStringLiteral("legacyGraphvizPreviewView"));
    view->setDragMode(QGraphicsView::ScrollHandDrag);
    view->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                         QPainter::SmoothPixmapTransform);
    view->setBackgroundBrush(QColor(250, 251, 253));
    view->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(view, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *fitButton = buttons->addButton(QObject::tr("Fit"), QDialogButtonBox::ActionRole);
    auto *zoomInButton = buttons->addButton(QObject::tr("Zoom In"), QDialogButtonBox::ActionRole);
    auto *zoomOutButton = buttons->addButton(QObject::tr("Zoom Out"), QDialogButtonBox::ActionRole);
    auto *saveButton = buttons->addButton(QObject::tr("Save SVG…"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    const auto fitPreview = [view, svgItem]() {
        if (!svgItem->boundingRect().isEmpty()) {
            view->fitInView(svgItem, Qt::KeepAspectRatio);
        }
    };
    QObject::connect(fitButton, &QPushButton::clicked, &dialog, fitPreview);
    QObject::connect(zoomInButton, &QPushButton::clicked, &dialog,
                     [view]() { view->scale(1.2, 1.2); });
    QObject::connect(zoomOutButton, &QPushButton::clicked, &dialog,
                     [view]() { view->scale(1.0 / 1.2, 1.0 / 1.2); });
    QObject::connect(saveButton, &QPushButton::clicked, &dialog,
                     [&dialog, sourceFilePath, svg = result.svg]() {
        const QFileInfo sourceInfo(sourceFilePath);
        const QString suggestedPath = sourceInfo.dir().filePath(
            sourceInfo.completeBaseName() + QStringLiteral("_legacy_graphviz.svg"));
        QString outputPath = QFileDialog::getSaveFileName(
            &dialog,
            QObject::tr("Save Legacy Graphviz Preview"),
            suggestedPath,
            QObject::tr("SVG files (*.svg)"));
        if (outputPath.isEmpty()) {
            return;
        }
        if (QFileInfo(outputPath).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) != 0) {
            outputPath += QStringLiteral(".svg");
        }

        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly) || output.write(svg) != svg.size() || !output.commit()) {
            QMessageBox::warning(
                &dialog,
                QObject::tr("Save Legacy Graphviz Preview"),
                QObject::tr("Could not save %1\n\n%2")
                    .arg(QDir::toNativeSeparators(outputPath), output.errorString()));
            return;
        }
        QMessageBox::information(
            &dialog,
            QObject::tr("Save Legacy Graphviz Preview"),
            QObject::tr("Saved: %1").arg(QDir::toNativeSeparators(outputPath)));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QTimer::singleShot(0, &dialog, fitPreview);
    dialog.exec();
}

bool readGraphRenderSettings(QWidget *parent, GraphRenderSettings &settings)
{
    // Reproducible/offscreen runs use the same production algorithm and
    // exporters, while avoiding an interactive settings dialog.
    if (!qEnvironmentVariable("IFCN_AUTO_GRAPH_RENDER_FILE").trimmed().isEmpty()) {
        settings.generateSvg = true;
        settings.generateLatex = true;
        return true;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Stochastic Compact Graph P&R"));
    dialog.setMinimumWidth(470);

    auto *description = new QLabel(
        QObject::tr("Non-GCN flow: OGDF crossing order (deterministic barycenter fallback), "
                    "unit-spacing compact placement, "
                    "conflict-driven row/column insertion, four-direction routing, "
                    "and post-route random-phase closure."),
        &dialog);
    description->setWordWrap(true);

    auto *phaseCombo = new QComboBox(&dialog);
    phaseCombo->addItem(QObject::tr("4-phase"), 4);
    phaseCombo->addItem(QObject::tr("3-phase"), 3);
    phaseCombo->setCurrentIndex(0);

    auto *attemptSpin = new QSpinBox(&dialog);
    attemptSpin->setRange(1, 128);
    attemptSpin->setValue(settings.maxAttempts);
    attemptSpin->setSingleStep(4);

    auto *timeBudgetSpin = new QSpinBox(&dialog);
    timeBudgetSpin->setRange(5, 300);
    timeBudgetSpin->setValue(settings.timeBudgetSeconds);
    timeBudgetSpin->setSuffix(QObject::tr(" s"));

    auto *samePhaseSpin = new QSpinBox(&dialog);
    samePhaseSpin->setRange(1, 12);
    samePhaseSpin->setValue(settings.maxSamePhase);

    auto *compactionSpin = new QSpinBox(&dialog);
    compactionSpin->setRange(0, 16);
    compactionSpin->setValue(settings.compactionRounds);

    auto *form = new QFormLayout(&dialog);
    form->addRow(description);
    form->addRow(QObject::tr("Clock phases:"), phaseCombo);
    form->addRow(QObject::tr("Maximum same-phase run:"), samePhaseSpin);
    form->addRow(QObject::tr("Search attempts:"), attemptSpin);
    form->addRow(QObject::tr("Candidate time budget:"), timeBudgetSpin);
    form->addRow(QObject::tr("Fallback compaction rounds:"), compactionSpin);

    auto *outputGroup = new QGroupBox(QObject::tr("Optional output artifacts"), &dialog);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    auto *svgCheck = new QCheckBox(
        QObject::tr("Export final mapped cell-level SVG"), outputGroup);
    svgCheck->setChecked(settings.generateSvg);
    auto *latexCheck = new QCheckBox(
        QObject::tr("Export phase-layout LaTeX (TikZ)"), outputGroup);
    latexCheck->setChecked(settings.generateLatex);
    auto *outputNote = new QLabel(
        QObject::tr("The .ifcn result is always saved because it is required to load and validate the layout."),
        outputGroup);
    outputNote->setWordWrap(true);
    outputLayout->addWidget(svgCheck);
    outputLayout->addWidget(latexCheck);
    outputLayout->addWidget(outputNote);
    form->addRow(outputGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Run"));
    form->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    settings.phaseCount = phaseCombo->currentData().toInt();
    settings.maxSamePhase = samePhaseSpin->value();
    settings.maxAttempts = attemptSpin->value();
    settings.timeBudgetSeconds = timeBudgetSpin->value();
    settings.compactionRounds = compactionSpin->value();
    settings.generateSvg = svgCheck->isChecked();
    settings.generateLatex = latexCheck->isChecked();
    return true;
}

bool readJuneRandomClockGraphSettings(QWidget *parent,
                                      JuneRandomClockGraphSettings &settings)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("June 2025 Random-Clock Graph P&R"));
    dialog.setMinimumWidth(500);

    auto *form = new QFormLayout(&dialog);
    auto *description = new QLabel(
        QObject::tr("Hardened restoration of the June 2025 flow: Graphviz DOT "
                    "placement, source-aware four-direction A* routing, then "
                    "post-route assignment of an irregular 4-phase clock field."),
        &dialog);
    description->setWordWrap(true);
    form->addRow(description);

    auto *contract = new QLabel(
        QObject::tr("The original /40 Graphviz scale is attempted first. One "
                    "roomier /20 fallback is used only when strict routing, "
                    "phase, or cell-level crossover validation fails."),
        &dialog);
    contract->setWordWrap(true);
    contract->setStyleSheet(QStringLiteral(
        "background: #eef6ff; border: 1px solid #b8d3ee; border-radius: 6px; "
        "padding: 8px; color: #294d70;"));
    form->addRow(contract);

    auto *outputGroup = new QGroupBox(QObject::tr("Optional output artifacts"), &dialog);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    auto *svgCheck = new QCheckBox(
        QObject::tr("Export final mapped cell-level SVG"), outputGroup);
    svgCheck->setChecked(settings.generateSvg);
    auto *latexCheck = new QCheckBox(
        QObject::tr("Export random-clock phase-layout LaTeX (TikZ)"), outputGroup);
    latexCheck->setChecked(settings.generateLatex);
    auto *outputNote = new QLabel(
        QObject::tr("The .ifcn layout is always saved and loaded into the UI."),
        outputGroup);
    outputNote->setWordWrap(true);
    outputLayout->addWidget(svgCheck);
    outputLayout->addWidget(latexCheck);
    outputLayout->addWidget(outputNote);
    form->addRow(outputGroup);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Run"));
    form->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    settings.generateSvg = svgCheck->isChecked();
    settings.generateLatex = latexCheck->isChecked();
    return true;
}

bool readNormalGraphDrawSettings(QWidget *parent, NormalGraphDrawSettings &settings)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("2DDWave Fixed-Clock P&R"));
    dialog.setMinimumWidth(470);

    auto *rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(18, 16, 18, 14);
    rootLayout->setSpacing(12);

    auto *header = new QFrame(&dialog);
    header->setObjectName(QStringLiteral("dwaveOptionsHeader"));
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    headerLayout->setSpacing(3);
    auto *titleLabel = new QLabel(QObject::tr("Normal Graph Draw · fixed 2DDWave"), header);
    titleLabel->setObjectName(QStringLiteral("dwaveOptionsTitle"));
    auto *subtitleLabel = new QLabel(
        QObject::tr("This flow is specialized for the fixed 2DDWave clock template; it does not search or accept a random clock field."),
        header);
    subtitleLabel->setObjectName(QStringLiteral("dwaveOptionsSubtitle"));
    subtitleLabel->setWordWrap(true);
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(subtitleLabel);
    rootLayout->addWidget(header);

    auto *clockGroup = new QGroupBox(QObject::tr("Clock contract"), &dialog);
    auto *clockForm = new QFormLayout(clockGroup);
    clockForm->setContentsMargins(12, 12, 12, 10);
    clockForm->setHorizontalSpacing(18);
    clockForm->setVerticalSpacing(8);
    auto *schemeLabel = new QLabel(QObject::tr("2DDWave (fixed)"), clockGroup);
    auto *coverageLabel = new QLabel(
        QObject::tr("Every tile in the final layout rectangle"), clockGroup);
    schemeLabel->setObjectName(QStringLiteral("dwaveContractValue"));
    coverageLabel->setObjectName(QStringLiteral("dwaveContractValue"));
    clockForm->addRow(QObject::tr("Clock scheme:"), schemeLabel);
    clockForm->addRow(QObject::tr("Phase-map coverage:"), coverageLabel);
    rootLayout->addWidget(clockGroup);

    auto *orderingGroup = new QGroupBox(QObject::tr("Within-layer crossing minimization"), &dialog);
    auto *orderingForm = new QFormLayout(orderingGroup);
    orderingForm->setContentsMargins(12, 12, 12, 10);
    auto *orderingCombo = new QComboBox(orderingGroup);
    orderingCombo->addItem(
        QObject::tr("OGDF Sugiyama (fast)"),
        QStringLiteral("ogdf"));
    const int orderingIndex = orderingCombo->findData(settings.crossingOrderer);
    orderingCombo->setCurrentIndex(orderingIndex >= 0 ? orderingIndex : 0);
    orderingCombo->setToolTip(QObject::tr(
        "Only the node order inside fixed logic layers changes; placement, routing, compaction, and legality checks are shared."));
    orderingForm->addRow(QObject::tr("Orderer:"), orderingCombo);
    rootLayout->addWidget(orderingGroup);

    auto *routingGroup = new QGroupBox(QObject::tr("Monotone routing"), &dialog);
    auto *routingForm = new QFormLayout(routingGroup);
    routingForm->setContentsMargins(12, 12, 12, 10);
    auto *routerLabel = new QLabel(
        QObject::tr("Auto: negotiated L/Z Manhattan (up to 128 edges), direct/DP for larger graphs"),
        routingGroup);
    routerLabel->setWordWrap(true);
    routerLabel->setObjectName(QStringLiteral("dwaveContractValue"));
    routingForm->addRow(QObject::tr("Router:"), routerLabel);
    rootLayout->addWidget(routingGroup);

    auto *outputGroup = new QGroupBox(QObject::tr("Output artifacts"), &dialog);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    outputLayout->setContentsMargins(12, 12, 12, 10);
    outputLayout->setSpacing(8);

    auto *phaseLatexCheck = new QCheckBox(
        QObject::tr("Export full-layout 2DDWave phase LaTeX"),
        outputGroup);
    phaseLatexCheck->setChecked(settings.generatePhaseLatex);

    auto *visualCheck = new QCheckBox(
        QObject::tr("Export circuit-layer and SVG figures"),
        outputGroup);
    visualCheck->setChecked(settings.generateVisualizations);

    auto *stageCheck = new QCheckBox(
        QObject::tr("Export intermediate debug TeX snapshots"),
        outputGroup);
    stageCheck->setChecked(settings.generateStageSnapshots);

    outputLayout->addWidget(phaseLatexCheck);
    outputLayout->addWidget(visualCheck);
    outputLayout->addWidget(stageCheck);
    rootLayout->addWidget(outputGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Run"));
    rootLayout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.setStyleSheet(QStringLiteral(
        "QFrame#dwaveOptionsHeader { background: #f0f7f7; border: 1px solid #b8d8d5; border-radius: 8px; }"
        "QLabel#dwaveOptionsTitle { color: #174b50; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#dwaveOptionsSubtitle { color: #50666a; background: transparent; }"
        "QLabel#dwaveContractValue { color: #174b50; font-weight: 600; }"
        "QGroupBox { font-weight: 600; border: 1px solid #d8dee8; border-radius: 7px; margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"));

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    settings.generatePhaseLatex = phaseLatexCheck->isChecked();
    settings.generateVisualizations = visualCheck->isChecked();
    settings.generateStageSnapshots = stageCheck->isChecked();
    settings.crossingOrderer = orderingCombo->currentData().toString();
    return true;
}

QString environmentValueOrDefault(const char *name, const QString &defaultValue)
{
    const QString value = QString::fromLocal8Bit(qgetenv(name)).trimmed();
    return value.isEmpty() ? defaultValue : value;
}

int environmentIntOrDefault(const char *name, int defaultValue)
{
    bool ok = false;
    const int value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toInt(&ok);
    return ok ? value : defaultValue;
}

double environmentDoubleOrDefault(const char *name, double defaultValue)
{
    bool ok = false;
    const double value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toDouble(&ok);
    return ok ? value : defaultValue;
}

bool environmentBoolOrDefault(const char *name, bool defaultValue)
{
    const QString value = QString::fromLocal8Bit(qgetenv(name)).trimmed().toLower();
    if (value.isEmpty()) {
        return defaultValue;
    }
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return defaultValue;
}

void setComboByData(QComboBox *combo, const QString &value)
{
    const int index = combo->findData(value);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    }
}

bool readGcnRlSettings(QWidget *parent, GcnRlSettings &settings)
{
    QSettings persisted;
    persisted.beginGroup(QStringLiteral("gcnRl"));
    settings.engine = persisted.value(QStringLiteral("engine"), settings.engine).toString();
    settings.qualityPreset = persisted.value(QStringLiteral("qualityPreset"), settings.qualityPreset).toString();
    settings.checkpoint = persisted.value(QStringLiteral("checkpoint"), settings.checkpoint).toString();
    settings.clockMode = persisted.value(QStringLiteral("clockMode"), settings.clockMode).toString();
    settings.device = persisted.value(QStringLiteral("device"), settings.device).toString();
    settings.parseMode = persisted.value(QStringLiteral("parseMode"), settings.parseMode).toString();
    settings.phaseCycle = persisted.value(QStringLiteral("phaseCycle"), settings.phaseCycle).toInt();
    settings.baseSeed = persisted.value(QStringLiteral("baseSeed"), settings.baseSeed).toInt();
    settings.clockFieldSamples = persisted.value(QStringLiteral("clockFieldSamples"), settings.clockFieldSamples).toInt();
    settings.policyTrials = persisted.value(QStringLiteral("policyTrials"), settings.policyTrials).toInt();
    settings.stepsPerEpisode = persisted.value(QStringLiteral("stepsPerEpisode"), settings.stepsPerEpisode).toInt();
    settings.exactTimeoutSeconds = persisted.value(QStringLiteral("exactTimeoutSeconds"), settings.exactTimeoutSeconds).toInt();
    settings.routingPadding = persisted.value(QStringLiteral("routingPadding"), settings.routingPadding).toInt();
    settings.maxSamePhase = persisted.value(QStringLiteral("maxSamePhase"), settings.maxSamePhase).toInt();
    settings.retrievalTopK = persisted.value(QStringLiteral("retrievalTopK"), settings.retrievalTopK).toInt();
    settings.clockAlignedStart = persisted.value(QStringLiteral("clockAlignedStart"), settings.clockAlignedStart).toBool();
    settings.stochasticActions = persisted.value(QStringLiteral("stochasticActions"), settings.stochasticActions).toBool();
    settings.allowExactMemoryRetrieval = persisted.value(QStringLiteral("allowExactMemoryRetrieval"), settings.allowExactMemoryRetrieval).toBool();
    settings.runs = persisted.value(QStringLiteral("runs"), settings.runs).toInt();
    settings.workers = persisted.value(QStringLiteral("workers"), settings.workers).toInt();
    settings.gcnEpochs = persisted.value(QStringLiteral("gcnEpochs"), settings.gcnEpochs).toInt();
    settings.episodes = persisted.value(QStringLiteral("episodes"), settings.episodes).toInt();
    settings.ppoEpochs = persisted.value(QStringLiteral("ppoEpochs"), settings.ppoEpochs).toInt();
    settings.minibatchSize = persisted.value(QStringLiteral("minibatchSize"), settings.minibatchSize).toInt();
    settings.memoryOnlyInference = persisted.value(QStringLiteral("memoryOnlyInference"), settings.memoryOnlyInference).toBool();
    persisted.endGroup();

    settings.engine = environmentValueOrDefault("IFCN_GCN_RL_ENGINE", settings.engine);
    settings.qualityPreset = environmentValueOrDefault("IFCN_GCN_RL_PRESET", settings.qualityPreset);
    settings.checkpoint = environmentValueOrDefault("IFCN_GCN_RL_CHECKPOINT", settings.checkpoint);
    settings.clockMode = environmentValueOrDefault("IFCN_GCN_RL_CLOCK_MODE", settings.clockMode);
    settings.device = environmentValueOrDefault("IFCN_GCN_RL_DEVICE", settings.device);
    settings.parseMode = environmentValueOrDefault("IFCN_GCN_RL_PARSE_MODE", settings.parseMode);
    settings.phaseCycle = environmentIntOrDefault("IFCN_GCN_RL_PHASE_CYCLE", settings.phaseCycle);
    settings.baseSeed = environmentIntOrDefault("IFCN_GCN_RL_BASE_SEED", settings.baseSeed);
    settings.clockFieldSamples = environmentIntOrDefault("IFCN_GCN_RL_CLOCK_FIELD_SAMPLES", settings.clockFieldSamples);
    settings.policyTrials = environmentIntOrDefault("IFCN_GCN_RL_POLICY_TRIALS", settings.policyTrials);
    settings.stepsPerEpisode = environmentIntOrDefault("IFCN_GCN_RL_STEPS", settings.stepsPerEpisode);
    settings.exactTimeoutSeconds = environmentIntOrDefault("IFCN_GCN_RL_EXACT_TIMEOUT", settings.exactTimeoutSeconds);
    settings.routingPadding = environmentIntOrDefault("IFCN_GCN_RL_PADDING", settings.routingPadding);
    settings.maxSamePhase = environmentIntOrDefault("IFCN_GCN_RL_MAX_SAME_PHASE", settings.maxSamePhase);
    settings.retrievalTopK = environmentIntOrDefault("IFCN_GCN_RL_RETRIEVAL_TOP_K", settings.retrievalTopK);
    settings.clockAlignedStart = environmentBoolOrDefault("IFCN_GCN_RL_CLOCK_ALIGNED_START", settings.clockAlignedStart);
    settings.stochasticActions = environmentBoolOrDefault("IFCN_GCN_RL_STOCHASTIC_ACTIONS", settings.stochasticActions);
    settings.allowExactMemoryRetrieval = environmentBoolOrDefault("IFCN_GCN_RL_ALLOW_EXACT_MEMORY", settings.allowExactMemoryRetrieval);

    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("gcnRlOptionsDialog"));
    dialog.setWindowTitle(QObject::tr("Universal AI Place & Route"));
    dialog.resize(760, 660);
    dialog.setMinimumSize(650, 520);

    auto *rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(18, 16, 18, 14);
    rootLayout->setSpacing(12);

    auto *header = new QFrame(&dialog);
    header->setObjectName(QStringLiteral("aiOptionsHeader"));
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    headerLayout->setSpacing(3);
    auto *titleLabel = new QLabel(QObject::tr("Memory-enabled stochastic-clock layout"), header);
    titleLabel->setObjectName(QStringLiteral("aiOptionsTitle"));
    auto *subtitleLabel = new QLabel(
        QObject::tr("Use the trained universal agent for fast inference, or switch to the legacy per-circuit PPO workflow."),
        header);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setObjectName(QStringLiteral("aiOptionsSubtitle"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(subtitleLabel);
    rootLayout->addWidget(header);

    auto *setupGroup = new QGroupBox(QObject::tr("Run setup"), &dialog);
    auto *setupForm = new QFormLayout(setupGroup);
    setupForm->setContentsMargins(12, 12, 12, 10);
    setupForm->setHorizontalSpacing(16);
    setupForm->setVerticalSpacing(8);

    auto *engineCombo = new QComboBox(setupGroup);
    engineCombo->addItem(QObject::tr("Universal memory agent (recommended)"), QStringLiteral("universal"));
    engineCombo->addItem(QObject::tr("Legacy online PPO training"), QStringLiteral("legacy"));
    setComboByData(engineCombo, settings.engine);

    auto *presetCombo = new QComboBox(setupGroup);
    presetCombo->addItem(QObject::tr("Fast preview"), QStringLiteral("fast"));
    presetCombo->addItem(QObject::tr("Balanced"), QStringLiteral("balanced"));
    presetCombo->addItem(QObject::tr("High quality"), QStringLiteral("quality"));
    presetCombo->addItem(QObject::tr("Custom"), QStringLiteral("custom"));
    setComboByData(presetCombo, settings.qualityPreset);

    auto *deviceCombo = new QComboBox(setupGroup);
    deviceCombo->addItem(QObject::tr("Auto (CUDA first)"), QStringLiteral("auto"));
    deviceCombo->addItem(QObject::tr("CUDA / GPU"), QStringLiteral("cuda"));
    deviceCombo->addItem(QObject::tr("CPU"), QStringLiteral("cpu"));
    setComboByData(deviceCombo, settings.device);

    auto *parseModeCombo = new QComboBox(setupGroup);
    parseModeCombo->addItem(QObject::tr("Auto (compact/layered)"), QStringLiteral("auto"));
    parseModeCombo->addItem(QObject::tr("Layered"), QStringLiteral("layered"));
    parseModeCombo->addItem(QObject::tr("Compact"), QStringLiteral("compact"));
    setComboByData(parseModeCombo, settings.parseMode);

    auto *phaseCombo = new QComboBox(setupGroup);
    phaseCombo->addItem(QObject::tr("4-phase"), 4);
    phaseCombo->addItem(QObject::tr("3-phase"), 3);
    const int phaseIndex = phaseCombo->findData(settings.phaseCycle);
    if (phaseIndex >= 0) {
        phaseCombo->setCurrentIndex(phaseIndex);
    }

    auto createSpin = [&dialog](int minimum, int maximum, int value, int step = 1) {
        auto *spin = new QSpinBox(&dialog);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setValue(qBound(minimum, value, maximum));
        return spin;
    };
    auto createDoubleSpin = [&dialog](double minimum, double maximum, double value, double step = 0.25) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(minimum, maximum);
        spin->setDecimals(2);
        spin->setSingleStep(step);
        spin->setValue(qBound(minimum, value, maximum));
        return spin;
    };

    auto *seedSpin = createSpin(0, 1000000, settings.baseSeed);
    setupForm->addRow(QObject::tr("Engine:"), engineCombo);
    setupForm->addRow(QObject::tr("Quality preset:"), presetCombo);
    setupForm->addRow(QObject::tr("Compute device:"), deviceCombo);
    setupForm->addRow(QObject::tr("Verilog parse:"), parseModeCombo);
    setupForm->addRow(QObject::tr("Clock phases:"), phaseCombo);
    setupForm->addRow(QObject::tr("Seed:"), seedSpin);
    rootLayout->addWidget(setupGroup);

    auto *engineDescription = new QLabel(&dialog);
    engineDescription->setObjectName(QStringLiteral("engineDescription"));
    engineDescription->setWordWrap(true);
    engineDescription->setTextFormat(Qt::RichText);
    rootLayout->addWidget(engineDescription);

    auto *tabs = new QTabWidget(&dialog);
    tabs->setDocumentMode(true);

    auto *universalScroll = new QScrollArea(tabs);
    universalScroll->setWidgetResizable(true);
    universalScroll->setFrameShape(QFrame::NoFrame);
    auto *universalPage = new QWidget(universalScroll);
    auto *universalForm = new QFormLayout(universalPage);
    universalForm->setContentsMargins(14, 14, 14, 14);
    universalForm->setHorizontalSpacing(18);
    universalForm->setVerticalSpacing(9);

    auto *checkpointEdit = new QLineEdit(settings.checkpoint, universalPage);
    checkpointEdit->setPlaceholderText(QObject::tr("auto — discover the best exact checkpoint"));
    auto *checkpointButton = new QPushButton(QObject::tr("Browse…"), universalPage);
    auto *checkpointRow = new QWidget(universalPage);
    auto *checkpointLayout = new QHBoxLayout(checkpointRow);
    checkpointLayout->setContentsMargins(0, 0, 0, 0);
    checkpointLayout->setSpacing(6);
    checkpointLayout->addWidget(checkpointEdit, 1);
    checkpointLayout->addWidget(checkpointButton);

    auto *clockModeCombo = new QComboBox(universalPage);
    clockModeCombo->addItem(QObject::tr("Stochastic bands (recommended)"), QStringLiteral("stochastic-bands"));
    clockModeCombo->addItem(QObject::tr("Diagonal causal field"), QStringLiteral("diagonal"));
    clockModeCombo->addItem(QObject::tr("Axis-aligned causal field"), QStringLiteral("axis"));
    setComboByData(clockModeCombo, settings.clockMode);

    auto *clockSamplesSpin = createSpin(1, 32, settings.clockFieldSamples);
    auto *policyTrialsSpin = createSpin(1, 8, settings.policyTrials);
    auto *stepsSpin = createSpin(1, 64, settings.stepsPerEpisode);
    auto *exactTimeoutSpin = createSpin(1, 3600, settings.exactTimeoutSeconds, 5);
    auto *paddingSpin = createSpin(0, 16, settings.routingPadding);
    auto *maxSamePhaseSpin = createSpin(0, 32, settings.maxSamePhase);
    auto *retrievalTopKSpin = createSpin(1, 16, settings.retrievalTopK);
    auto *clockAlignedCheck = new QCheckBox(QObject::tr("Align the initial placement to each sampled clock field"), universalPage);
    clockAlignedCheck->setChecked(settings.clockAlignedStart);
    auto *stochasticActionsCheck = new QCheckBox(QObject::tr("Sample policy actions (used for multiple trials)"), universalPage);
    stochasticActionsCheck->setChecked(settings.stochasticActions);
    auto *exactMemoryCheck = new QCheckBox(QObject::tr("Allow exact-topology retrieval from layout memory"), universalPage);
    exactMemoryCheck->setChecked(settings.allowExactMemoryRetrieval);

    universalForm->addRow(QObject::tr("Agent checkpoint:"), checkpointRow);
    universalForm->addRow(QObject::tr("Clock field:"), clockModeCombo);
    universalForm->addRow(QObject::tr("Clock samples:"), clockSamplesSpin);
    universalForm->addRow(QObject::tr("Policy trials / field:"), policyTrialsSpin);
    universalForm->addRow(QObject::tr("Recurrent steps:"), stepsSpin);
    universalForm->addRow(QObject::tr("Exact-route timeout (s):"), exactTimeoutSpin);
    universalForm->addRow(QObject::tr("Routing padding:"), paddingSpin);
    universalForm->addRow(QObject::tr("Max same phase:"), maxSamePhaseSpin);
    universalForm->addRow(QObject::tr("Memory top-k:"), retrievalTopKSpin);
    universalForm->addRow(clockAlignedCheck);
    universalForm->addRow(stochasticActionsCheck);
    universalForm->addRow(exactMemoryCheck);
    universalScroll->setWidget(universalPage);
    tabs->addTab(universalScroll, QObject::tr("Universal agent"));

    auto *legacyScroll = new QScrollArea(tabs);
    legacyScroll->setWidgetResizable(true);
    legacyScroll->setFrameShape(QFrame::NoFrame);
    auto *legacyPage = new QWidget(legacyScroll);
    auto *legacyForm = new QFormLayout(legacyPage);
    legacyForm->setContentsMargins(14, 14, 14, 14);
    legacyForm->setHorizontalSpacing(18);
    legacyForm->setVerticalSpacing(8);

    auto *legacyModeCombo = new QComboBox(legacyPage);
    legacyModeCombo->addItem(QObject::tr("Train and refine this circuit"), false);
    legacyModeCombo->addItem(QObject::tr("Stored layout memory only"), true);
    legacyModeCombo->setCurrentIndex(settings.memoryOnlyInference ? 1 : 0);
    auto *runsSpin = createSpin(1, 16, settings.runs);
    auto *workersSpin = createSpin(1, 16, settings.workers);
    auto *gcnEpochSpin = createSpin(1, 1000, settings.gcnEpochs, 10);
    auto *episodesSpin = createSpin(1, 10000, settings.episodes);
    auto *ppoEpochsSpin = createSpin(1, 1000, settings.ppoEpochs);
    auto *minibatchSpin = createSpin(1, 4096, settings.minibatchSize);
    auto *xSpacingSpin = createSpin(1, 32, settings.xSpacing);
    auto *ySpacingSpin = createSpin(1, 32, settings.ySpacing);
    auto *exactCandidateSpin = createSpin(0, 64, settings.finalExactCandidates);
    auto *legalRepairCandidateSpin = createSpin(0, 128, settings.legalRepairCandidates);
    auto *legalRepairPaddingSpin = createSpin(0, 64, settings.legalRepairMaxPadding);
    auto *localRefineSpin = createSpin(0, 1000, settings.localRefineRounds);
    auto *localEvalSpin = createSpin(0, 100000, settings.localMaxEvaluations);
    auto *postPrimaryPackSpin = createSpin(0, 1000, settings.postPrimaryPackRounds);
    auto *postPackSpin = createSpin(0, 1000, settings.postAreaPackRounds);
    auto *postEvalSpin = createSpin(0, 100000, settings.postPackMaxEvaluations);
    auto *postStripPackSpin = createSpin(0, 1000, settings.postPhaseStripPackRounds);
    auto *postStripEvalSpin = createSpin(0, 100000, settings.postPhaseStripPackMaxEvaluations);
    auto *areaRewardSpin = createDoubleSpin(0.0, 20.0, settings.areaRewardWeight);
    auto *areaRegressionSpin = createDoubleSpin(0.0, 5000.0, settings.areaRegressionWeight, 25.0);
    auto *maxSpanSpin = createDoubleSpin(0.0, 100.0, settings.maxSpanWeight, 1.0);
    auto *layoutMemoryCheck = new QCheckBox(QObject::tr("Use layout memory"), legacyPage);
    layoutMemoryCheck->setChecked(settings.useLayoutMemory);
    auto *actionMemoryCheck = new QCheckBox(QObject::tr("Use shared action memory"), legacyPage);
    actionMemoryCheck->setChecked(settings.useActionMemory);
    auto *trainingPlotsCheck = new QCheckBox(QObject::tr("Write reward-curve SVG"), legacyPage);
    trainingPlotsCheck->setChecked(settings.writeTrainingPlots);

    legacyForm->addRow(QObject::tr("Run mode:"), legacyModeCombo);
    legacyForm->addRow(QObject::tr("Parallel runs:"), runsSpin);
    legacyForm->addRow(QObject::tr("Workers:"), workersSpin);
    legacyForm->addRow(QObject::tr("GCN epochs:"), gcnEpochSpin);
    legacyForm->addRow(QObject::tr("RL episodes:"), episodesSpin);
    legacyForm->addRow(QObject::tr("PPO epochs:"), ppoEpochsSpin);
    legacyForm->addRow(QObject::tr("Minibatch:"), minibatchSpin);
    legacyForm->addRow(QObject::tr("Initial X spacing:"), xSpacingSpin);
    legacyForm->addRow(QObject::tr("Initial Y spacing:"), ySpacingSpin);
    legacyForm->addRow(QObject::tr("Final exact candidates:"), exactCandidateSpin);
    legacyForm->addRow(QObject::tr("Legal repair candidates:"), legalRepairCandidateSpin);
    legacyForm->addRow(QObject::tr("Legal repair max padding:"), legalRepairPaddingSpin);
    legacyForm->addRow(QObject::tr("Local refine rounds:"), localRefineSpin);
    legacyForm->addRow(QObject::tr("Local evaluation budget:"), localEvalSpin);
    legacyForm->addRow(QObject::tr("Primary-pack rounds:"), postPrimaryPackSpin);
    legacyForm->addRow(QObject::tr("Post-pack rounds:"), postPackSpin);
    legacyForm->addRow(QObject::tr("Post-pack evaluation budget:"), postEvalSpin);
    legacyForm->addRow(QObject::tr("Phase-strip rounds:"), postStripPackSpin);
    legacyForm->addRow(QObject::tr("Phase-strip evaluation budget:"), postStripEvalSpin);
    legacyForm->addRow(QObject::tr("Area reward weight:"), areaRewardSpin);
    legacyForm->addRow(QObject::tr("Area regression penalty:"), areaRegressionSpin);
    legacyForm->addRow(QObject::tr("Max span penalty:"), maxSpanSpin);
    legacyForm->addRow(layoutMemoryCheck);
    legacyForm->addRow(actionMemoryCheck);
    legacyForm->addRow(trainingPlotsCheck);
    legacyScroll->setWidget(legacyPage);
    tabs->addTab(legacyScroll, QObject::tr("Legacy PPO (advanced)"));

    rootLayout->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Run layout"));
    rootLayout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QObject::connect(checkpointButton, &QPushButton::clicked, &dialog, [&dialog, checkpointEdit]() {
        const QString selected = QFileDialog::getOpenFileName(
            &dialog,
            QObject::tr("Select universal-agent checkpoint"),
            checkpointEdit->text() == QStringLiteral("auto") ? QString() : checkpointEdit->text(),
            QObject::tr("PyTorch checkpoints (*.pt);;All files (*)"));
        if (!selected.isEmpty()) {
            checkpointEdit->setText(selected);
        }
    });

    QObject::connect(runsSpin, QOverload<int>::of(&QSpinBox::valueChanged), workersSpin, [workersSpin](int runs) {
        workersSpin->setMaximum(std::max(1, runs));
        if (workersSpin->value() > runs) {
            workersSpin->setValue(runs);
        }
    });
    workersSpin->setMaximum(std::max(1, runsSpin->value()));

    bool applyingPreset = false;
    const auto markCustom = [&]() {
        if (!applyingPreset && presetCombo->currentData().toString() != QStringLiteral("custom")) {
            setComboByData(presetCombo, QStringLiteral("custom"));
        }
    };
    const auto applyPreset = [&](const QString &preset) {
        if (preset == QStringLiteral("custom")) {
            return;
        }
        applyingPreset = true;
        if (preset == QStringLiteral("fast")) {
            clockSamplesSpin->setValue(1);
            policyTrialsSpin->setValue(1);
            stepsSpin->setValue(8);
            exactTimeoutSpin->setValue(20);
            stochasticActionsCheck->setChecked(false);
        } else if (preset == QStringLiteral("quality")) {
            clockSamplesSpin->setValue(8);
            policyTrialsSpin->setValue(3);
            stepsSpin->setValue(16);
            exactTimeoutSpin->setValue(90);
            stochasticActionsCheck->setChecked(true);
        } else {
            clockSamplesSpin->setValue(4);
            policyTrialsSpin->setValue(1);
            stepsSpin->setValue(12);
            exactTimeoutSpin->setValue(45);
            stochasticActionsCheck->setChecked(false);
        }
        applyingPreset = false;
    };
    QObject::connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                     [=, &applyPreset](int) { applyPreset(presetCombo->currentData().toString()); });
    const QList<QSpinBox *> presetSpins = {clockSamplesSpin, policyTrialsSpin, stepsSpin, exactTimeoutSpin};
    for (QSpinBox *spin : presetSpins) {
        QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
                         [&, spin](int) { Q_UNUSED(spin); markCustom(); });
    }
    QObject::connect(stochasticActionsCheck, &QCheckBox::toggled, &dialog, [&](bool) { markCustom(); });
    QObject::connect(policyTrialsSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
                     [stochasticActionsCheck](int trials) {
        if (trials > 1) {
            stochasticActionsCheck->setChecked(true);
        }
    });

    const auto applyEngine = [&]() {
        const bool universal = engineCombo->currentData().toString() == QStringLiteral("universal");
        if (tabs->currentIndex() != (universal ? 0 : 1)) {
            tabs->setCurrentIndex(universal ? 0 : 1);
        }
        presetCombo->setEnabled(universal);
        engineDescription->setText(universal
            ? QObject::tr("<b>Pre-trained universal agent:</b> retrieves similar iFCN layouts, keeps recurrent memory across actions, samples frozen clock fields, then exact-routes and exports only a legal result.")
            : QObject::tr("<b>Legacy online PPO:</b> trains or refines a separate policy for this circuit. It is slower and is kept for comparison and continued experiments."));
    };
    QObject::connect(engineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                     [&](int) { applyEngine(); });
    QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, [&](int index) {
        setComboByData(engineCombo, index == 0 ? QStringLiteral("universal") : QStringLiteral("legacy"));
    });
    applyEngine();

    dialog.setStyleSheet(QStringLiteral(
        "QFrame#aiOptionsHeader { background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 8px; }"
        "QLabel#aiOptionsTitle { color: #173a76; font-size: 15px; font-weight: 700; background: transparent; }"
        "QLabel#aiOptionsSubtitle { color: #53627a; background: transparent; }"
        "QLabel#engineDescription { background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 8px 10px; color: #475569; }"
        "QGroupBox { font-weight: 600; border: 1px solid #d8dee8; border-radius: 7px; margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"));

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    settings.engine = engineCombo->currentData().toString();
    settings.qualityPreset = presetCombo->currentData().toString();
    settings.checkpoint = checkpointEdit->text().trimmed().isEmpty()
        ? QStringLiteral("auto")
        : checkpointEdit->text().trimmed();
    settings.clockMode = clockModeCombo->currentData().toString();
    settings.device = deviceCombo->currentData().toString();
    settings.parseMode = parseModeCombo->currentData().toString();
    settings.phaseCycle = phaseCombo->currentData().toInt();
    settings.baseSeed = seedSpin->value();
    settings.clockFieldSamples = clockSamplesSpin->value();
    settings.policyTrials = policyTrialsSpin->value();
    settings.stepsPerEpisode = stepsSpin->value();
    settings.exactTimeoutSeconds = exactTimeoutSpin->value();
    settings.routingPadding = paddingSpin->value();
    settings.maxSamePhase = maxSamePhaseSpin->value();
    settings.retrievalTopK = retrievalTopKSpin->value();
    settings.clockAlignedStart = clockAlignedCheck->isChecked();
    settings.stochasticActions = stochasticActionsCheck->isChecked() || settings.policyTrials > 1;
    settings.allowExactMemoryRetrieval = exactMemoryCheck->isChecked();

    settings.memoryOnlyInference = legacyModeCombo->currentData().toBool();
    settings.runs = runsSpin->value();
    settings.workers = workersSpin->value();
    settings.gcnEpochs = gcnEpochSpin->value();
    settings.episodes = episodesSpin->value();
    settings.ppoEpochs = ppoEpochsSpin->value();
    settings.minibatchSize = minibatchSpin->value();
    settings.xSpacing = xSpacingSpin->value();
    settings.ySpacing = ySpacingSpin->value();
    settings.finalExactCandidates = exactCandidateSpin->value();
    settings.legalRepairCandidates = legalRepairCandidateSpin->value();
    settings.legalRepairMaxPadding = legalRepairPaddingSpin->value();
    settings.localRefineRounds = localRefineSpin->value();
    settings.localMaxEvaluations = localEvalSpin->value();
    settings.postPrimaryPackRounds = postPrimaryPackSpin->value();
    settings.postAreaPackRounds = postPackSpin->value();
    settings.postPackMaxEvaluations = postEvalSpin->value();
    settings.postPhaseStripPackRounds = postStripPackSpin->value();
    settings.postPhaseStripPackMaxEvaluations = postStripEvalSpin->value();
    settings.areaRewardWeight = areaRewardSpin->value();
    settings.areaRegressionWeight = areaRegressionSpin->value();
    settings.maxSpanWeight = maxSpanSpin->value();
    settings.useLayoutMemory = layoutMemoryCheck->isChecked();
    settings.useActionMemory = actionMemoryCheck->isChecked();
    settings.writeTrainingPlots = trainingPlotsCheck->isChecked();

    persisted.beginGroup(QStringLiteral("gcnRl"));
    persisted.setValue(QStringLiteral("engine"), settings.engine);
    persisted.setValue(QStringLiteral("qualityPreset"), settings.qualityPreset);
    persisted.setValue(QStringLiteral("checkpoint"), settings.checkpoint);
    persisted.setValue(QStringLiteral("clockMode"), settings.clockMode);
    persisted.setValue(QStringLiteral("device"), settings.device);
    persisted.setValue(QStringLiteral("parseMode"), settings.parseMode);
    persisted.setValue(QStringLiteral("phaseCycle"), settings.phaseCycle);
    persisted.setValue(QStringLiteral("baseSeed"), settings.baseSeed);
    persisted.setValue(QStringLiteral("clockFieldSamples"), settings.clockFieldSamples);
    persisted.setValue(QStringLiteral("policyTrials"), settings.policyTrials);
    persisted.setValue(QStringLiteral("stepsPerEpisode"), settings.stepsPerEpisode);
    persisted.setValue(QStringLiteral("exactTimeoutSeconds"), settings.exactTimeoutSeconds);
    persisted.setValue(QStringLiteral("routingPadding"), settings.routingPadding);
    persisted.setValue(QStringLiteral("maxSamePhase"), settings.maxSamePhase);
    persisted.setValue(QStringLiteral("retrievalTopK"), settings.retrievalTopK);
    persisted.setValue(QStringLiteral("clockAlignedStart"), settings.clockAlignedStart);
    persisted.setValue(QStringLiteral("stochasticActions"), settings.stochasticActions);
    persisted.setValue(QStringLiteral("allowExactMemoryRetrieval"), settings.allowExactMemoryRetrieval);
    persisted.setValue(QStringLiteral("runs"), settings.runs);
    persisted.setValue(QStringLiteral("workers"), settings.workers);
    persisted.setValue(QStringLiteral("gcnEpochs"), settings.gcnEpochs);
    persisted.setValue(QStringLiteral("episodes"), settings.episodes);
    persisted.setValue(QStringLiteral("ppoEpochs"), settings.ppoEpochs);
    persisted.setValue(QStringLiteral("minibatchSize"), settings.minibatchSize);
    persisted.setValue(QStringLiteral("memoryOnlyInference"), settings.memoryOnlyInference);
    persisted.endGroup();
    return true;
}

std::vector<LayoutAttempt> buildLayoutAttempts()
{
    // Establish a routable baseline first, then move inward.  Starting from
    // (1,1) is counterproductive with the mandatory one-cell gate-port halo:
    // those placements are structurally unroutable and used to consume the
    // majority of the search time before a useful candidate was attempted.
    std::vector<LayoutAttempt> attempts = {
        {8, 8, 480.0},
        {6, 6, 360.0},
        {6, 4, 300.0}, {4, 6, 300.0},
        {5, 5, 320.0},
        {5, 4, 360.0}, {4, 5, 360.0},
        {4, 4, 420.0},
        {6, 3, 480.0}, {3, 6, 480.0},
        {5, 3, 540.0}, {3, 5, 540.0},
        {4, 3, 600.0}, {3, 4, 600.0},
        {3, 3, 720.0}
    };
    for (unsigned int ySpacing = 3; ySpacing <= 10; ++ySpacing) {
        for (unsigned int xSpacing = 3; xSpacing <= 10; ++xSpacing) {
            const bool duplicate = std::any_of(
                attempts.begin(), attempts.end(), [&](const LayoutAttempt &candidate) {
                    return candidate.xSpacing == xSpacing &&
                           candidate.ySpacing == ySpacing;
                });
            if (!duplicate) {
                attempts.push_back({xSpacing, ySpacing,
                    (xSpacing <= 4 || ySpacing <= 4) ? 600.0 : 420.0});
            }
        }
    }
    return attempts;
}

std::optional<LayoutBounds> calculateGridBounds(
    const std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> &gridCells)
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

QString rlStyleCoord(const fcngraph::position &pos, int gridHeight)
{
    const int drawY = gridHeight - static_cast<int>(pos.second) - 1;
    return QStringLiteral("(%1,%2)").arg(pos.first).arg(drawY);
}

int rlStylePhase(int rawPhase, int phaseCount)
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
    std::map<fcngraph::position, std::set<unsigned int>> routeSourcesByPosition;
    for (const auto &route : layout.routes) {
        for (const fcngraph::position &routePosition : route.second) {
            routeSourcesByPosition[routePosition].insert(route.first.first);
        }
    }
    for (const auto &usage : routeSourcesByPosition) {
        if (usage.second.size() > 1) {
            // A restored gate must never replace an inter-source crossover
            // cell.  The route-only mapper cannot diagnose that node/cross
            // collision until after the scene has begun to load.
            occupiedPositions.insert(usage.first);
        }
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
    parse.parseVerilog(request.file);

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

int totalRouteLength(const std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> &routes)
{
    int length = 0;
    for (const auto &route : routes) {
        length += static_cast<int>(route.second.size());
    }
    return length;
}

PhaseRepeatStats calculatePhaseRepeatStats(
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> &routes,
    const std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> &gridCells)
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

using JuneProgressCallback = std::function<void(const QString &, int, int)>;

JuneRandomClockGraphTaskResult runJuneRandomClockGraphSearch(
    const QString &filePath,
    const JuneProgressCallback &progress)
{
    JuneRandomClockGraphTaskResult task;
    QElapsedTimer timer;
    timer.start();

    try {
        const std::string source = filePath.toStdString();
        Parse parse;
        parse.parseVerilog(source);
        if (parse.getm_numVertices() == 0) {
            throw std::runtime_error("The Verilog file contains no placeable circuit nodes.");
        }

        task.gateNum = parse.getm_numVertices();
        task.inputNum = parse.get_input_num();
        task.outputNum = parse.get_output_num();
        task.wireNum = parse.getm_numEdges();

        if (progress) {
            progress(QObject::tr("Parsing and optimizing the June graph pipeline"), 5, 100);
        }
        parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
        // The July 14 Graph Draw revision intentionally kept buffer nodes
        // in the DOT topology.  Removing them here changes both the Graphviz
        // geometry and the compact-area characteristics of that algorithm.
        parse.addLayerRedundancyNode();
        parse.caculateSameLayerNodeRoutePair();

        struct JuneAttempt {
            double gridSize;
            double searchCost;
            int routeOrderRetries;
        };
        // The first candidate keeps the June /40 Graphviz geometry and fast
        // bounded search.  The roomier candidate is used only when strict
        // source ownership or phase validation rejects that result.
        const std::array<JuneAttempt, 2> attempts{{
            {40.0, 40.0, 4},
            {20.0, 320.0, 24},
        }};

        std::optional<LayoutSearchResult> selectedLayout;
        QString lastFailure = QObject::tr("no legal candidate was produced");
        for (std::size_t attemptIndex = 0; attemptIndex < attempts.size(); ++attemptIndex) {
            const JuneAttempt &attempt = attempts[attemptIndex];
            if (progress) {
                progress(
                    QObject::tr("June candidate %1/%2: Graphviz scale /%3, A* cost %4")
                        .arg(attemptIndex + 1)
                        .arg(attempts.size())
                        .arg(attempt.gridSize, 0, 'f', 0)
                        .arg(attempt.searchCost, 0, 'f', 0),
                    12 + static_cast<int>(attemptIndex) * 30,
                    100);
            }

            GridChessboard board;
            Astar router(board, false, attempt.searchCost);
            router.setAllowInterSourceWireOverlap(false);
            CircuitGraph graph(parse, source, board, router);
            graph.setFitnessCallback([&lastFailure](const std::string &detail) {
                if (detail.rfind("June random-clock graph P&R:", 0) == 0 ||
                    detail.rfind("A* routing failed", 0) == 0) {
                    lastFailure = QString::fromStdString(detail);
                }
            });

            if (!graph.placeAndRouteJuneRandomClock(
                    4, attempt.gridSize, attempt.routeOrderRetries)) {
                continue;
            }

            std::map<unsigned int, position> candidateNodePositions;
            for (const auto &node : graph.nodeIndex_pos) {
                candidateNodePositions[static_cast<unsigned int>(node.first)] = node.second;
            }
            const GateLevelIfcnLayout restoredCandidate = restoreHiddenNotNodesForIfcn(
                parse, candidateNodePositions, graph.routes);
            if (restoredCandidate.skippedHiddenNotRoutes > 0) {
                lastFailure = QObject::tr("hidden-NOT restoration skipped %1 route(s)")
                    .arg(restoredCandidate.skippedHiddenNotRoutes);
                continue;
            }

            std::vector<std::vector<position>> restoredRouteGeometry;
            restoredRouteGeometry.reserve(restoredCandidate.routes.size());
            for (const auto &route : restoredCandidate.routes) {
                restoredRouteGeometry.push_back(route.second);
            }
            Mapping restoredMapping;
            restoredMapping.mapping_line(restoredRouteGeometry);
            std::string restoredCrossoverError;
            if (!restoredMapping.validate_crossovers(&restoredCrossoverError)) {
                lastFailure = QObject::tr("hidden-NOT restoration produced an invalid crossover: %1")
                    .arg(QString::fromStdString(restoredCrossoverError));
                continue;
            }

            const auto bounds = calculateGridBounds(board.getGridMap());
            if (!bounds.has_value()) {
                lastFailure = QObject::tr("the routed board is empty");
                continue;
            }

            LayoutSearchResult result;
            result.bounds = bounds.value();
            result.routeLength = totalRouteLength(graph.routes);
            result.phaseRepeats = calculatePhaseRepeatStats(graph.routes, board.getGridMap());
            result.searchCost = attempt.searchCost;
            result.nodePositions = graph.nodeIndex_pos;
            result.routes = graph.routes;
            result.gridCells = board.getGridMap();
            selectedLayout = std::move(result);
            break;
        }

        if (!selectedLayout.has_value()) {
            throw std::runtime_error(
                QObject::tr("June Graphviz/A* pipeline failed both bounded candidates. Last status: %1")
                    .arg(lastFailure)
                    .toStdString());
        }

        task.parse = std::move(parse);
        task.layout = std::move(selectedLayout.value());
        task.success = true;
    } catch (const std::exception &ex) {
        task.error = QString::fromLocal8Bit(ex.what());
    } catch (...) {
        task.error = QObject::tr("unknown June Graphviz/A* failure");
    }
    task.elapsedSeconds = timer.elapsed() / 1000.0;
    return task;
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

struct FixedLayerOrderResult
{
    std::vector<std::vector<int>> layers;
    QString engine;
    int crossings = -1;
};

int countAdjacentLayerCrossings(const std::vector<std::vector<int>> &layers,
                                const std::vector<std::pair<int, int>> &edges)
{
    std::map<int, std::pair<int, int>> positionByNode;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        for (std::size_t order = 0; order < layers[layer].size(); ++order) {
            positionByNode[layers[layer][order]] = {
                static_cast<int>(layer), static_cast<int>(order)};
        }
    }

    int crossings = 0;
    for (std::size_t left = 0; left < edges.size(); ++left) {
        const auto leftSource = positionByNode.find(edges[left].first);
        const auto leftTarget = positionByNode.find(edges[left].second);
        if (leftSource == positionByNode.end() || leftTarget == positionByNode.end() ||
            leftTarget->second.first != leftSource->second.first + 1) {
            continue;
        }
        for (std::size_t right = left + 1; right < edges.size(); ++right) {
            const auto rightSource = positionByNode.find(edges[right].first);
            const auto rightTarget = positionByNode.find(edges[right].second);
            if (rightSource == positionByNode.end() || rightTarget == positionByNode.end() ||
                rightSource->second.first != leftSource->second.first ||
                rightTarget->second.first != leftTarget->second.first) {
                continue;
            }
            const int sourceDelta = leftSource->second.second - rightSource->second.second;
            const int targetDelta = leftTarget->second.second - rightTarget->second.second;
            if (sourceDelta != 0 && targetDelta != 0 &&
                ((sourceDelta < 0) != (targetDelta < 0))) {
                ++crossings;
            }
        }
    }
    return crossings;
}

std::vector<std::vector<int>> barycenterFixedLayerOrder(
    const std::vector<std::vector<int>> &originalLayers,
    const std::vector<std::pair<int, int>> &edges)
{
    std::vector<std::vector<int>> layers = originalLayers;
    std::map<int, std::vector<int>> predecessors;
    std::map<int, std::vector<int>> successors;
    std::map<int, int> layerOf;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        for (int node : layers[layer]) {
            layerOf[node] = static_cast<int>(layer);
        }
    }
    for (const auto &edge : edges) {
        const auto sourceLayer = layerOf.find(edge.first);
        const auto targetLayer = layerOf.find(edge.second);
        if (sourceLayer == layerOf.end() || targetLayer == layerOf.end() ||
            targetLayer->second != sourceLayer->second + 1) {
            continue;
        }
        successors[edge.first].push_back(edge.second);
        predecessors[edge.second].push_back(edge.first);
    }

    const auto reorder = [](std::vector<int> &nodes,
                            const std::vector<int> &reference,
                            const std::map<int, std::vector<int>> &neighbors) {
        std::map<int, int> referenceOrder;
        for (std::size_t index = 0; index < reference.size(); ++index) {
            referenceOrder[reference[index]] = static_cast<int>(index);
        }
        std::map<int, int> oldOrder;
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            oldOrder[nodes[index]] = static_cast<int>(index);
        }
        const auto barycenter = [&](int node) {
            const auto found = neighbors.find(node);
            if (found == neighbors.end() || found->second.empty()) {
                return static_cast<double>(oldOrder[node]);
            }
            double sum = 0.0;
            int count = 0;
            for (int adjacent : found->second) {
                const auto order = referenceOrder.find(adjacent);
                if (order != referenceOrder.end()) {
                    sum += order->second;
                    ++count;
                }
            }
            return count > 0 ? sum / count : static_cast<double>(oldOrder[node]);
        };
        std::stable_sort(nodes.begin(), nodes.end(), [&](int left, int right) {
            const double leftCenter = barycenter(left);
            const double rightCenter = barycenter(right);
            if (leftCenter != rightCenter) {
                return leftCenter < rightCenter;
            }
            return oldOrder[left] < oldOrder[right];
        });
    };

    std::vector<std::vector<int>> best = layers;
    int bestCrossings = countAdjacentLayerCrossings(best, edges);
    for (int pass = 0; pass < 8; ++pass) {
        for (std::size_t layer = 1; layer < layers.size(); ++layer) {
            reorder(layers[layer], layers[layer - 1], predecessors);
        }
        for (std::size_t offset = 1; offset < layers.size(); ++offset) {
            const std::size_t layer = layers.size() - 1 - offset;
            reorder(layers[layer], layers[layer + 1], successors);
        }
        const int crossings = countAdjacentLayerCrossings(layers, edges);
        if (crossings < bestCrossings) {
            bestCrossings = crossings;
            best = layers;
        }
    }
    return best;
}

FixedLayerOrderResult fixedLayerCrossingOrder(fcngraph::Parse &parse)
{
    FixedLayerOrderResult result;
    const auto &parserLayers = parse.getlayerNodeDivVec();
    result.layers.reserve(parserLayers.size());
    for (const auto &layer : parserLayers) {
        result.layers.emplace_back(layer.begin(), layer.end());
    }
    const auto edges = parse.getEffectiveEdges();

    QStringList candidates;
    const QString configured = qEnvironmentVariable("IFCN_OGDF_ORDERER").trimmed();
    if (!configured.isEmpty()) {
        candidates << configured;
    }
    const QString sourceRoot = projectSourceDir();
    if (!sourceRoot.isEmpty()) {
        candidates << QDir(sourceRoot).filePath(QStringLiteral("build/ifcn_ogdf_layer_order"))
                   << QDir(sourceRoot).filePath(QStringLiteral("build-release/ifcn_ogdf_layer_order"))
                   << QDir(sourceRoot).filePath(QStringLiteral("build-ogdf/ifcn_ogdf_layer_order"));
    }
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("ifcn_ogdf_layer_order"));

    QString executable;
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            executable = info.absoluteFilePath();
            break;
        }
    }

    if (!executable.isEmpty()) {
        std::map<int, int> layerOf;
        QString payload;
        QTextStream stream(&payload);
        int adjacentEdgeCount = 0;
        for (std::size_t layer = 0; layer < result.layers.size(); ++layer) {
            for (int node : result.layers[layer]) {
                layerOf[node] = static_cast<int>(layer);
            }
        }
        for (const auto &edge : edges) {
            if (layerOf.count(edge.first) != 0 && layerOf.count(edge.second) != 0 &&
                layerOf[edge.second] == layerOf[edge.first] + 1) {
                ++adjacentEdgeCount;
            }
        }
        int nodeCount = 0;
        for (const auto &layer : result.layers) {
            nodeCount += static_cast<int>(layer.size());
        }
        stream << nodeCount << ' ' << adjacentEdgeCount << ' '
               << result.layers.size() << '\n';
        for (std::size_t layer = 0; layer < result.layers.size(); ++layer) {
            for (std::size_t order = 0; order < result.layers[layer].size(); ++order) {
                stream << result.layers[layer][order] << ' ' << layer << ' ' << order << '\n';
            }
        }
        for (const auto &edge : edges) {
            if (layerOf.count(edge.first) != 0 && layerOf.count(edge.second) != 0 &&
                layerOf[edge.second] == layerOf[edge.first] + 1) {
                stream << edge.first << ' ' << edge.second << '\n';
            }
        }

        QProcess process;
        process.start(executable);
        if (process.waitForStarted(2000)) {
            process.write(payload.toUtf8());
            process.closeWriteChannel();
            if (process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit &&
                process.exitCode() == 0) {
                const QStringList lines = QString::fromUtf8(process.readAllStandardOutput())
                    .split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
                std::vector<std::vector<int>> ordered(result.layers.size());
                bool valid = !lines.isEmpty() && lines.front().startsWith(QStringLiteral("OGDF "));
                for (int lineIndex = 1; valid && lineIndex < lines.size(); ++lineIndex) {
                    const QStringList fields = lines[lineIndex].trimmed().split(
                        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    if (fields.size() != 3) {
                        valid = false;
                        break;
                    }
                    bool layerOk = false, orderOk = false, nodeOk = false;
                    const int layer = fields[0].toInt(&layerOk);
                    const int order = fields[1].toInt(&orderOk);
                    const int node = fields[2].toInt(&nodeOk);
                    if (!layerOk || !orderOk || !nodeOk || layer < 0 ||
                        layer >= static_cast<int>(ordered.size()) ||
                        order != static_cast<int>(ordered[layer].size())) {
                        valid = false;
                        break;
                    }
                    ordered[layer].push_back(node);
                }
                if (valid) {
                    for (std::size_t layer = 0; layer < ordered.size(); ++layer) {
                        std::set<int> expected(result.layers[layer].begin(), result.layers[layer].end());
                        std::set<int> received(ordered[layer].begin(), ordered[layer].end());
                        if (expected != received) {
                            valid = false;
                            break;
                        }
                    }
                }
                if (valid) {
                    result.layers = std::move(ordered);
                    result.engine = QStringLiteral("OGDF Sugiyama fixed-layer");
                    result.crossings = countAdjacentLayerCrossings(result.layers, edges);
                    return result;
                }
            } else if (process.state() != QProcess::NotRunning) {
                process.kill();
                process.waitForFinished(1000);
            }
        }
    }

    result.layers = barycenterFixedLayerOrder(result.layers, edges);
    result.engine = QStringLiteral("deterministic barycenter fixed-layer fallback");
    result.crossings = countAdjacentLayerCrossings(result.layers, edges);
    return result;
}

bool hasGcnRlScript(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return false;
    }
    return QFileInfo(QDir(rootPath).filePath("src/algorithm/main/train_layout_ppo.py")).isFile();
}

bool hasNormalGraphDrawScript(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return false;
    }
    return QFileInfo(QDir(rootPath).filePath("src/algorithm/main/test_normal_graph_draw.py")).isFile();
}

bool hasGcnRlModule(const QString &rootPath)
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

QString bundledGcnRlRoot()
{
    const QString sourceDir = projectSourceDir();
    if (sourceDir.isEmpty()) {
        return QString();
    }
    return QDir(sourceDir).filePath("include/gcn_rl_layout");
}

QString findGcnRlRoot()
{
    const QString envRoot = QString::fromLocal8Bit(qgetenv("IFCN_GCN_RL_ROOT"));
    if (hasGcnRlScript(envRoot)) {
        return QDir(envRoot).absolutePath();
    }

    const QStringList candidates = {
        bundledGcnRlRoot()
    };

    for (const QString &candidate : candidates) {
        if (hasGcnRlScript(candidate) && hasGcnRlModule(candidate)) {
            return QDir(candidate).absolutePath();
        }
    }
    for (const QString &candidate : candidates) {
        if (hasGcnRlScript(candidate)) {
            return QDir(candidate).absolutePath();
        }
    }
    return QString();
}

QString findGcnRlPython(const QString &rootPath)
{
    const QString envPython = QString::fromLocal8Bit(qgetenv("IFCN_GCN_RL_PYTHON"));
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

QString copyLayoutArtifact(const QDir &outputDir,
                           const QString &sourceBaseName,
                           const QString &sourceSuffix,
                           const QString &targetSuffix)
{
    const QString sourcePath = outputDir.filePath(sourceBaseName + sourceSuffix);
    if (!QFileInfo(sourcePath).isFile()) {
        return QString();
    }

    const QString targetPath = outputDir.filePath(sourceBaseName + targetSuffix);
    if (QFileInfo(sourcePath).absoluteFilePath() == QFileInfo(targetPath).absoluteFilePath()) {
        return targetPath;
    }

    if (QFileInfo(targetPath).exists()) {
        QFile::remove(targetPath);
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        qWarning() << "[LayoutOutput] Failed to copy artifact" << sourcePath << "to" << targetPath;
        return QString();
    }
    return targetPath;
}

QString ensureGcnRlClassifiedArtifacts(const QString &outputDirPath, const QString &sourceBaseName)
{
    const QDir outputDir(outputDirPath);
    const QString classifiedIfcn = copyLayoutArtifact(outputDir,
                                                      sourceBaseName,
                                                      QStringLiteral("_rl_layout.ifcn"),
                                                      QStringLiteral("_gcn_rl_layout.ifcn"));
    copyLayoutArtifact(outputDir,
                       sourceBaseName,
                       QStringLiteral("_rl_layout_encoded.ifcn"),
                       QStringLiteral("_gcn_rl_layout_encoded.ifcn"));
    copyLayoutArtifact(outputDir,
                       sourceBaseName,
                       QStringLiteral("_rl_layout.svg"),
                       QStringLiteral("_gcn_rl_layout.svg"));
    copyLayoutArtifact(outputDir,
                       sourceBaseName,
                       QStringLiteral("_rl_layout.tex"),
                       QStringLiteral("_gcn_rl_layout.tex"));
    return classifiedIfcn;
}

QString processTail(const QString &text, int maxChars = 6000)
{
    if (text.size() <= maxChars) {
        return text.trimmed();
    }
    return text.right(maxChars).trimmed();
}

QString locateGcnRlIfcn(const QString &outputDirPath, const QString &sourceBaseName)
{
    const QDir outputDir(outputDirPath);
    const QString classifiedExpected = outputDir.filePath(sourceBaseName + QStringLiteral("_gcn_rl_layout.ifcn"));
    if (QFileInfo(classifiedExpected).isFile()) {
        return classifiedExpected;
    }

    const QString rlExpected = outputDir.filePath(sourceBaseName + QStringLiteral("_rl_layout.ifcn"));
    if (QFileInfo(rlExpected).isFile()) {
        return rlExpected;
    }

    const QString expected = outputDir.filePath(sourceBaseName + QStringLiteral("_phase_layout.ifcn"));
    if (QFileInfo(expected).isFile()) {
        return expected;
    }

    const QStringList candidates = outputDir.entryList(QStringList() << "*_gcn_rl_layout.ifcn"
                                                                     << "*_rl_layout.ifcn"
                                                                     << "*_phase_layout.ifcn",
                                                       QDir::Files,
                                                       QDir::Time);
    if (!candidates.isEmpty()) {
        return outputDir.filePath(candidates.first());
    }
    return QString();
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

QString locateNormalGraphDrawLatex(const QString &outputDirPath)
{
    const QDir outputDir(outputDirPath);
    const QStringList candidates = outputDir.entryList(QStringList() << "*.tex",
                                                       QDir::Files,
                                                       QDir::Time);
    return candidates.isEmpty() ? QString() : outputDir.filePath(candidates.first());
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
        qWarning() << "[GCN+RL] ifcn_mapping_metrics executable was not found.";
        return false;
    }

    QProcess process;
    process.setProgram(executable);
    QStringList arguments{ifcnPath, QStringLiteral("--no-io-contraction")};
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(3000) || !process.waitForFinished(120000)) {
        qWarning() << "[GCN+RL] ifcn_mapping_metrics timed out for" << ifcnPath;
        process.kill();
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning() << "[GCN+RL] ifcn_mapping_metrics failed for" << ifcnPath
                   << QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QStringList parts = output.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        qWarning() << "[GCN+RL] ifcn_mapping_metrics returned invalid output:" << output;
        return false;
    }

    bool okCell = false;
    bool okCross = false;
    const qulonglong cellCount = parts[0].toULongLong(&okCell);
    const qulonglong crossCount = parts[1].toULongLong(&okCross);
    if (!okCell || !okCross) {
        qWarning() << "[GCN+RL] ifcn_mapping_metrics returned non-numeric output:" << output;
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

void writeMappingMetricsToGcnRlArtifacts(const QString &ifcnPath)
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

void VerilogHandler::handleGcnRlLayout()
{
    const QString filePath = QFileDialog::getOpenFileName(
        mainWindow,
        tr("Open Verilog File"),
        projectSourceDir().isEmpty() ? QDir::currentPath() : projectSourceDir(),
        tr("Verilog files (*.v);;All file (*)"));

    if (filePath.isEmpty()) {
        mainWindow->printToStatusBar(tr("GCN+RL placement and routing cancelled."));
        return;
    }
    runGcnRlLayoutForFile(filePath);
}

void VerilogHandler::handleNormalGraphDrawLayout()
{
    NormalGraphDrawSettings settings;
    if (!readNormalGraphDrawSettings(mainWindow, settings)) {
        mainWindow->printToStatusBar(tr("2DDWave fixed-clock placement and routing cancelled."));
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        mainWindow,
        tr("Open Verilog File"),
        projectSourceDir().isEmpty() ? QDir::currentPath() : projectSourceDir(),
        tr("Verilog files (*.v);;All file (*)"));

    if (filePath.isEmpty()) {
        mainWindow->printToStatusBar(tr("2DDWave fixed-clock placement and routing cancelled."));
        return;
    }
    runNormalGraphDrawLayoutForFile(filePath,
                                    false,
                                    settings.generateVisualizations,
                                    settings.generateStageSnapshots,
                                    settings.generatePhaseLatex,
                                    settings.crossingOrderer);
}

void VerilogHandler::runNormalGraphDrawLayoutForFile(const QString &filePath,
                                                     bool quietStatusMessages,
                                                     bool generateVisualizations,
                                                     bool generateStageSnapshots,
                                                     bool generatePhaseLatex,
                                                     const QString &crossingOrderer)
{
    if (filePath.isEmpty()) {
        mainWindow->printToStatusBar(tr("2DDWave fixed-clock placement and routing cancelled."));
        return;
    }
    mainWindow->updateVerilogSourceFile(filePath);

    const QString rootPath = findGcnRlRoot();
    if (rootPath.isEmpty() || !hasNormalGraphDrawScript(rootPath)) {
        emit operationFailed(tr("Normal graph draw backend not found. Expected include/gcn_rl_layout/src/algorithm/main/test_normal_graph_draw.py."));
        return;
    }

    const QString python = findGcnRlPython(rootPath);
    if (python.isEmpty()) {
        emit operationFailed(tr("Python interpreter for normal graph draw backend was not found."));
        return;
    }

    if (!hasGcnRlModule(rootPath)) {
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
    const QString normalizedOrderer = QStringLiteral("ogdf");
    const QString orderingLabel = tr("OGDF Sugiyama");
    const QString routerMode = QStringLiteral("auto");
    QStringList arguments;
    arguments << scriptPath
              << QStringLiteral("--benchmark") << filePath
              << QStringLiteral("--output-dir") << outputDirPath
              << QStringLiteral("--crossing-orderer")
              << normalizedOrderer
              << QStringLiteral("--router")
              << routerMode;
    if (!generateVisualizations) {
        arguments << QStringLiteral("--skip-figures")
                  << QStringLiteral("--skip-training-curve");
    }
    if (!generatePhaseLatex) {
        arguments << QStringLiteral("--skip-latex");
    }
    if (!generateStageSnapshots) {
        arguments << QStringLiteral("--skip-stage-snapshots");
    }

    emit operationStarted(tr("2DDWave fixed-clock placement and routing"),
                          tr("Running %1 ordering and adaptive Manhattan routing with the fixed 2DDWave clock for %2")
                              .arg(orderingLabel,
                                   QDir::toNativeSeparators(filePath)));
    emit operationProgress(tr("Starting fixed-clock 2DDWave backend"), 1, 100);
    QCoreApplication::processEvents();

    QProcess process;
    process.setProgram(python);
    process.setArguments(arguments);
    process.setWorkingDirectory(rootPath);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    prependPythonPath(environment, QDir(rootPath).filePath("src/algorithm"));
    if (environment.value(QStringLiteral("IFCN_GCN_EPOCHS")).isEmpty()) {
        environment.insert(QStringLiteral("IFCN_GCN_EPOCHS"), QStringLiteral("60"));
    }
    environment.insert(QStringLiteral("MPLBACKEND"), QStringLiteral("Agg"));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    process.setProcessEnvironment(environment);

    QString combinedOutput;
    QString progressOutputBuffer;
    const QRegularExpression progressRecord(
        QStringLiteral("^\\[IFCN-PROGRESS\\]\\s+(\\d{1,3})\\|(.*)$"));
    auto consumeProgressRecords = [&](const QString &chunk, bool flushRemainder = false) {
        progressOutputBuffer += chunk;
        while (true) {
            const int lineEnd = progressOutputBuffer.indexOf(QLatin1Char('\n'));
            if (lineEnd < 0 && !flushRemainder) {
                break;
            }
            QString line;
            if (lineEnd < 0) {
                line = progressOutputBuffer;
                progressOutputBuffer.clear();
            } else {
                line = progressOutputBuffer.left(lineEnd);
                progressOutputBuffer.remove(0, lineEnd + 1);
            }
            line = line.trimmed();
            const QRegularExpressionMatch match = progressRecord.match(line);
            if (match.hasMatch()) {
                const int value = qBound(0, match.captured(1).toInt(), 100);
                emit operationProgress(match.captured(2).trimmed(), value, 100);
            }
            if (lineEnd < 0) {
                break;
            }
        }
    };
    auto drainOutput = [&]() {
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());
        if (!stdOut.isEmpty()) {
            combinedOutput += stdOut;
            consumeProgressRecords(stdOut);
        }
        if (!stdErr.isEmpty()) {
            combinedOutput += stdErr;
            consumeProgressRecords(stdErr);
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
    consumeProgressRecords(QString(), true);

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = processTail(combinedOutput);
        emit operationFailed(tr("2DDWave fixed-clock placement and routing failed with exit code %1.%2%3")
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

    writeMappingMetricsToGcnRlArtifacts(ifcnPath);
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
        ? tr("2DDWave fixed-clock layout loaded.")
        : tr("2DDWave fixed-clock layout loaded (%1 ordering): %2")
              .arg(orderingLabel,
                   QDir::toNativeSeparators(ifcnPath));
    if (!quietStatusMessages && QFileInfo(svgPath).isFile()) {
        message += tr("; SVG: %1").arg(QDir::toNativeSeparators(svgPath));
    }
    if (!quietStatusMessages && generatePhaseLatex) {
        const QString latexPath = locateNormalGraphDrawLatex(outputDirPath);
        if (!latexPath.isEmpty()) {
            message += tr("; full-layout phase TeX: %1")
                           .arg(QDir::toNativeSeparators(latexPath));
        }
    }
    emit operationFinished(message);
}

void VerilogHandler::runGcnRlLayoutForFile(const QString &filePath,
                                           bool quietStatusMessages,
                                           bool forceLiveTraining)
{
    if (gcnRlLayoutRunning) {
        emit operationProgress(tr("Universal AI P&R is already running."), -1, 0);
        return;
    }
    if (filePath.isEmpty()) {
        mainWindow->printToStatusBar(tr("GCN+RL placement and routing cancelled."));
        return;
    }
    mainWindow->updateVerilogSourceFile(filePath);

    const QString rootPath = findGcnRlRoot();
    if (rootPath.isEmpty()) {
        emit operationFailed(tr("GCN+RL backend not found. Expected include/gcn_rl_layout or set IFCN_GCN_RL_ROOT."));
        return;
    }

    const QString python = findGcnRlPython(rootPath);
    if (python.isEmpty()) {
        emit operationFailed(tr("Python interpreter for GCN+RL backend was not found."));
        return;
    }

    if (!hasGcnRlModule(rootPath)) {
        emit operationProgress(
            tr("GCN+RL Python module was not found under %1; Python will report details if import fails.")
                .arg(QDir::toNativeSeparators(rootPath)),
            0,
            0);
    }

    GcnRlSettings settings;
    if (!readGcnRlSettings(mainWindow, settings)) {
        mainWindow->printToStatusBar(tr("GCN+RL placement and routing cancelled."));
        return;
    }
    if (forceLiveTraining) {
        settings.engine = QStringLiteral("legacy");
        settings.memoryOnlyInference = false;
    }
    const bool useUniversalAgent = settings.engine == QStringLiteral("universal");
    const QFileInfo sourceInfo(filePath);
    QDir sourceDir = sourceInfo.absoluteDir();
    const QString outputDirPath = sourceDir.filePath(
        sourceInfo.completeBaseName() + QStringLiteral("_gcn_rl_layout"));
    if (!QDir().mkpath(outputDirPath)) {
        emit operationFailed(tr("Cannot create GCN+RL output directory: %1")
                                 .arg(QDir::toNativeSeparators(outputDirPath)));
        return;
    }

    const QDateTime taskStartedUtc = QDateTime::currentDateTimeUtc();
    const QString sourceStem = sourceInfo.completeBaseName();
    const QDir outputDir(outputDirPath);
    if (useUniversalAgent) {
        // A failed stochastic-clock run must never fall back to an artifact
        // left by an earlier successful invocation.
        const QStringList staleArtifacts = {
            sourceStem + QStringLiteral("_rl_layout.ifcn"),
            sourceStem + QStringLiteral("_rl_layout_encoded.ifcn"),
            sourceStem + QStringLiteral("_rl_layout.svg"),
            sourceStem + QStringLiteral("_rl_layout.tex"),
            sourceStem + QStringLiteral("_gcn_rl_layout.ifcn"),
            sourceStem + QStringLiteral("_gcn_rl_layout_encoded.ifcn"),
            sourceStem + QStringLiteral("_gcn_rl_layout.svg"),
            sourceStem + QStringLiteral("_gcn_rl_layout.tex"),
            sourceStem + QStringLiteral("_rl_summary.json")
        };
        for (const QString &artifact : staleArtifacts) {
            QFile::remove(outputDir.filePath(artifact));
        }
    }

    const QString layoutMemoryDir = QDir(rootPath).filePath(QStringLiteral("results/layout_memory"));
    const QString experiencePath = QDir(layoutMemoryDir).filePath("rl_action_experience.json");
    QStringList trainArguments;
    trainArguments << QStringLiteral("--device") << settings.device
                   << QStringLiteral("--phase-cycle") << QString::number(settings.phaseCycle)
                   << QStringLiteral("--x-spacing") << QString::number(settings.xSpacing)
                   << QStringLiteral("--y-spacing") << QString::number(settings.ySpacing)
                   << QStringLiteral("--padding") << QString::number(settings.routingPadding)
                   << QStringLiteral("--max-same-phase") << QString::number(settings.maxSamePhase)
                   << QStringLiteral("--start-layout-strategy") << settings.startStrategy
                   << QStringLiteral("--start-layout-orientation") << settings.startOrientation
                   << QStringLiteral("--parse-mode") << settings.parseMode
                   << QStringLiteral("--episodes") << QString::number(settings.episodes)
                   << QStringLiteral("--steps-per-episode") << QString::number(settings.stepsPerEpisode)
                   << QStringLiteral("--ppo-epochs") << QString::number(settings.ppoEpochs)
                   << QStringLiteral("--minibatch-size") << QString::number(settings.minibatchSize)
		                   << QStringLiteral("--train-eval-mode") << settings.trainEvalMode
			                   << QStringLiteral("--final-exact-validation-candidates") << QString::number(settings.finalExactCandidates)
			                   << QStringLiteral("--exact-eval-timeout-sec") << QString::number(settings.exactTimeoutSeconds)
			                   << QStringLiteral("--require-legal-final")
	                   << QStringLiteral("--legal-repair-candidates") << QString::number(settings.legalRepairCandidates)
	                   << QStringLiteral("--legal-repair-max-padding") << QString::number(settings.legalRepairMaxPadding)
                   << QStringLiteral("--legal-repair-timeout-multiplier") << QString::number(settings.legalRepairTimeoutMultiplier, 'f', 2)
                   << QStringLiteral("--local-refine-rounds") << QString::number(settings.localRefineRounds)
                   << QStringLiteral("--local-max-evaluations") << QString::number(settings.localMaxEvaluations)
	                   << QStringLiteral("--post-primary-pack-rounds") << QString::number(settings.postPrimaryPackRounds)
	                   << QStringLiteral("--post-area-pack-rounds") << QString::number(settings.postAreaPackRounds)
	                   << QStringLiteral("--post-pack-max-evaluations") << QString::number(settings.postPackMaxEvaluations)
	                   << QStringLiteral("--post-phase-strip-pack-rounds") << QString::number(settings.postPhaseStripPackRounds)
	                   << QStringLiteral("--post-phase-strip-pack-max-evaluations") << QString::number(settings.postPhaseStripPackMaxEvaluations)
	                   << QStringLiteral("--best-selection-mode") << QStringLiteral("legal-area")
	                   << QStringLiteral("--area-reward-weight") << QString::number(settings.areaRewardWeight, 'f', 2)
	                   << QStringLiteral("--area-regression-weight") << QString::number(settings.areaRegressionWeight, 'f', 2)
                   << QStringLiteral("--max-span-weight") << QString::number(settings.maxSpanWeight, 'f', 2)
                   << QStringLiteral("--log-interval") << QStringLiteral("2")
                   << QStringLiteral("--disable-step-log");

    if (!settings.writeTrainingPlots) {
        trainArguments << QStringLiteral("--disable-training-plots");
    }
    if (settings.memoryOnlyInference) {
        trainArguments << QStringLiteral("--memory-only-inference");
    }

    if (!settings.useLayoutMemory) {
        trainArguments << QStringLiteral("--disable-layout-memory");
    }
    if (!settings.useActionMemory) {
        trainArguments << QStringLiteral("--disable-rl-experience");
    }
    trainArguments << (settings.finalExactValidation
        ? QStringLiteral("--final-exact-validation")
        : QStringLiteral("--no-final-exact-validation"));
    trainArguments << (settings.strictMemoryUpdates
        ? QStringLiteral("--strict-memory-updates")
        : QStringLiteral("--no-strict-memory-updates"));

    const QString legacyRunnerPath = QDir(rootPath).filePath("scripts/gui_gcn_rl_runner.py");
    const QString universalRunnerPath = QDir(rootPath).filePath("scripts/gui_universal_agent_runner.py");
    if (useUniversalAgent && !QFileInfo(universalRunnerPath).isFile()) {
        emit operationFailed(tr("Universal-agent GUI runner was not found: %1")
                                 .arg(QDir::toNativeSeparators(universalRunnerPath)));
        return;
    }

    const bool useLegacyRunner = !useUniversalAgent &&
                                 QFileInfo(legacyRunnerPath).isFile() &&
                                 !settings.memoryOnlyInference;
    const QString scriptPath = useUniversalAgent
        ? universalRunnerPath
        : (useLegacyRunner
            ? legacyRunnerPath
            : QDir(rootPath).filePath("src/algorithm/main/train_layout_ppo.py"));
    QStringList arguments;
    if (useUniversalAgent) {
        arguments << scriptPath
                  << QStringLiteral("--benchmark") << filePath
                  << QStringLiteral("--output-dir") << outputDirPath
                  << QStringLiteral("--checkpoint") << settings.checkpoint
                  << QStringLiteral("--device") << settings.device
                  << QStringLiteral("--seed") << QString::number(settings.baseSeed)
                  << QStringLiteral("--parse-mode") << settings.parseMode
                  << QStringLiteral("--phase-count") << QString::number(settings.phaseCycle)
                  << QStringLiteral("--clock-mode") << settings.clockMode
                  << QStringLiteral("--clock-field-samples") << QString::number(settings.clockFieldSamples)
                  << QStringLiteral("--policy-trials") << QString::number(settings.policyTrials)
                  << QStringLiteral("--steps-per-episode") << QString::number(settings.stepsPerEpisode)
                  << QStringLiteral("--exact-eval-timeout-sec") << QString::number(settings.exactTimeoutSeconds)
                  << QStringLiteral("--padding") << QString::number(settings.routingPadding)
                  << QStringLiteral("--max-same-phase") << QString::number(settings.maxSamePhase)
                  << QStringLiteral("--retrieval-top-k") << QString::number(settings.retrievalTopK)
                  << (settings.stochasticActions
                        ? QStringLiteral("--no-deterministic")
                        : QStringLiteral("--deterministic"))
                  << (settings.clockAlignedStart
                        ? QStringLiteral("--clock-aligned-start")
                        : QStringLiteral("--no-clock-aligned-start"))
                  << (settings.allowExactMemoryRetrieval
                        ? QStringLiteral("--allow-exact-memory-retrieval")
                        : QStringLiteral("--no-allow-exact-memory-retrieval"))
                  << QStringLiteral("--require-legal");
    } else if (useLegacyRunner) {
        const QString runCount = QString::number(settings.runs);
        arguments << scriptPath
                  << QStringLiteral("--benchmark") << filePath
                  << QStringLiteral("--output-dir") << outputDirPath
                  << QStringLiteral("--runs") << runCount
                  << QStringLiteral("--max-workers") << QString::number(settings.workers)
                  << QStringLiteral("--base-seed") << QString::number(settings.baseSeed)
                  << QStringLiteral("--rl-experience-path") << experiencePath
                  << QStringLiteral("--");
        arguments << trainArguments;
    } else {
        arguments << scriptPath
                  << QStringLiteral("--benchmark") << filePath
                  << QStringLiteral("--output-dir") << outputDirPath
                  << QStringLiteral("--seed") << QString::number(settings.baseSeed)
                  << QStringLiteral("--rl-experience-path") << experiencePath;
        arguments << trainArguments;
    }

    QScopedValueRollback<bool> runningGuard(gcnRlLayoutRunning, true);

    emit operationStarted(useUniversalAgent
                              ? tr("Universal AI placement and routing")
                              : tr("Legacy GCN+RL placement and routing"),
                          useUniversalAgent
                              ? tr("Loading the trained memory agent for %1").arg(QDir::toNativeSeparators(filePath))
                              : (settings.memoryOnlyInference
                                  ? tr("Loading stored layout memory for %1").arg(QDir::toNativeSeparators(filePath))
                                  : tr("Running per-circuit PPO refinement for %1").arg(QDir::toNativeSeparators(filePath))));
    emit operationProgress(useUniversalAgent
                               ? tr("Preparing universal agent and circuit graph")
                               : tr("Legacy GCN+RL backend is running"),
                           0,
                           useUniversalAgent ? 100 : 0);
    QCoreApplication::processEvents();

    QProcess process;
    process.setProgram(python);
    process.setArguments(arguments);
    process.setWorkingDirectory(rootPath);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    prependPythonPath(environment, QDir(rootPath).filePath("src/algorithm"));
    environment.insert(QStringLiteral("IFCN_GCN_EPOCHS"), QString::number(settings.gcnEpochs));
    environment.insert(QStringLiteral("IFCN_GCN_RL_LAYOUT_MEMORY_DIR"), layoutMemoryDir);
    environment.insert(QStringLiteral("PYTHONHASHSEED"), QStringLiteral("0"));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    environment.insert(QStringLiteral("MPLBACKEND"), QStringLiteral("Agg"));
    process.setProcessEnvironment(environment);

    QString combinedOutput;
    QString progressLineBuffer;
    const auto processProgressLine = [&](const QString &rawLine) {
        const QString line = rawLine.trimmed();
        const QString prefix = QStringLiteral("IFCN_PROGRESS ");
        if (!useUniversalAgent || !line.startsWith(prefix)) {
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line.mid(prefix.size()).toUtf8());
        if (!document.isObject()) {
            return;
        }
        const QJsonObject progress = document.object();
        const QString detail = progress.value(QStringLiteral("message")).toString(
            tr("Universal agent is running"));
        const int value = qBound(0,
                                 static_cast<int>(std::lround(
                                     progress.value(QStringLiteral("progress")).toDouble() * 100.0)),
                                 100);
        emit operationProgress(detail, value, 100);
    };
    const auto consumeProgressOutput = [&](const QString &chunk) {
        progressLineBuffer += chunk;
        int newline = progressLineBuffer.indexOf(QLatin1Char('\n'));
        while (newline >= 0) {
            processProgressLine(progressLineBuffer.left(newline));
            progressLineBuffer.remove(0, newline + 1);
            newline = progressLineBuffer.indexOf(QLatin1Char('\n'));
        }
    };
    auto drainOutput = [&]() {
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());
        if (!stdOut.isEmpty()) {
            combinedOutput += stdOut;
            consumeProgressOutput(stdOut);
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
        emit operationFailed(tr("Failed to start GCN+RL backend with Python: %1")
                                 .arg(QDir::toNativeSeparators(python)));
        return;
    }

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(250);
        drainOutput();
    }
    drainOutput();
    if (!progressLineBuffer.trimmed().isEmpty()) {
        processProgressLine(progressLineBuffer);
        progressLineBuffer.clear();
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (useUniversalAgent && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 2) {
            QFile summaryFile(outputDir.filePath(sourceStem + QStringLiteral("_rl_summary.json")));
            if (summaryFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QJsonObject summary = QJsonDocument::fromJson(summaryFile.readAll()).object();
                if (!summary.isEmpty()) {
                    emit operationFailed(
                        tr("No strictly legal layout was found in %1 candidates. Best candidate: %2 failed edges, %3 direction violations, %4 clock violations. Try High quality, a larger timeout, or the axis clock field.")
                            .arg(summary.value(QStringLiteral("candidate_count")).toInt())
                            .arg(summary.value(QStringLiteral("best_failed_edges")).toInt())
                            .arg(summary.value(QStringLiteral("best_direction_violation_count")).toInt())
                            .arg(summary.value(QStringLiteral("best_clock_violations")).toInt()));
                    return;
                }
            }
        }
        const QString detail = processTail(combinedOutput);
        emit operationFailed(tr("%1 failed with exit code %2.%3%4")
                                 .arg(useUniversalAgent
                                          ? tr("Universal AI placement and routing")
                                          : tr("Legacy GCN+RL placement and routing"))
                                 .arg(process.exitCode())
                                 .arg(detail.isEmpty() ? QString() : QStringLiteral("\n"))
                                 .arg(detail));
        return;
    }

    QJsonObject universalSummary;
    if (useUniversalAgent) {
        const QString summaryPath = outputDir.filePath(sourceStem + QStringLiteral("_rl_summary.json"));
        QFile summaryFile(summaryPath);
        if (!summaryFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit operationFailed(tr("Universal agent completed without a readable summary: %1")
                                     .arg(QDir::toNativeSeparators(summaryPath)));
            return;
        }
        const QJsonDocument summaryDocument = QJsonDocument::fromJson(summaryFile.readAll());
        universalSummary = summaryDocument.object();
        if (!summaryDocument.isObject() ||
            !universalSummary.value(QStringLiteral("strict_success")).toBool(false)) {
            emit operationFailed(tr("Universal agent found no fully routed layout that is legal under the sampled clock fields."));
            return;
        }
        const QString rawIfcnPath = outputDir.filePath(sourceStem + QStringLiteral("_rl_layout.ifcn"));
        const QFileInfo rawIfcnInfo(rawIfcnPath);
        if (!rawIfcnInfo.isFile() ||
            rawIfcnInfo.lastModified().toUTC() < taskStartedUtc.addSecs(-1)) {
            emit operationFailed(tr("Universal agent summary is successful, but the current run did not produce a fresh .ifcn artifact."));
            return;
        }
    }

    const QString classifiedIfcnPath = ensureGcnRlClassifiedArtifacts(outputDirPath,
                                                                      sourceInfo.completeBaseName());
    const QString ifcnPath = classifiedIfcnPath.isEmpty()
        ? locateGcnRlIfcn(outputDirPath, sourceInfo.completeBaseName())
        : classifiedIfcnPath;
    if (ifcnPath.isEmpty()) {
        emit operationFailed(tr("GCN+RL completed, but no generated layout .ifcn was found in %1.")
                                 .arg(QDir::toNativeSeparators(outputDirPath)));
        return;
    }

    writeMappingMetricsToGcnRlArtifacts(ifcnPath);
    {
        StatusMessagesMuteGuard muteGuard(mainWindow != nullptr ? mainWindow->customStatusBar : nullptr,
                                          quietStatusMessages);
        mainWindow->mapIfcnFile(ifcnPath, !quietStatusMessages);
    }

    QString svgPath = outputDir.filePath(sourceInfo.completeBaseName() + QStringLiteral("_gcn_rl_layout.svg"));
    if (!QFileInfo(svgPath).isFile()) {
        svgPath = outputDir.filePath(sourceInfo.completeBaseName() + QStringLiteral("_rl_layout.svg"));
    }
    if (!QFileInfo(svgPath).isFile()) {
        svgPath = outputDir.filePath(sourceInfo.completeBaseName() + QStringLiteral("_phase_layout.svg"));
    }
    QString message;
    if (useUniversalAgent) {
        const int width = universalSummary.value(QStringLiteral("best_width")).toInt();
        const int height = universalSummary.value(QStringLiteral("best_height")).toInt();
        const double area = universalSummary.value(QStringLiteral("best_area")).toDouble();
        message = quietStatusMessages
            ? tr("Universal AI layout loaded (%1 × %2, area %3).")
                  .arg(width).arg(height).arg(area, 0, 'f', 0)
            : tr("Universal AI layout loaded (%1 × %2, area %3): %4")
                  .arg(width).arg(height).arg(area, 0, 'f', 0)
                  .arg(QDir::toNativeSeparators(ifcnPath));
    } else {
        message = quietStatusMessages
            ? tr("Legacy GCN+RL layout loaded.")
            : tr("Legacy GCN+RL layout loaded: %1").arg(QDir::toNativeSeparators(ifcnPath));
    }
    if (!quietStatusMessages && QFileInfo(svgPath).isFile()) {
        message += tr("; SVG: %1").arg(QDir::toNativeSeparators(svgPath));
    }
    const QString curvePath = outputDir.filePath(sourceInfo.completeBaseName() + QStringLiteral("_rl_training_curves.svg"));
    if (!quietStatusMessages && QFileInfo(curvePath).isFile()) {
        message += tr("; Reward curve: %1").arg(QDir::toNativeSeparators(curvePath));
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
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), "/home/lys/projects/github/iFCN", 
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

        if (request.clockSchemeStr == "TDD") {
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

            self->mainWindow->mapIfcnFile(ifcnPath);
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
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), "/home/lys/projects/github/iFCN", 
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

void VerilogHandler::handleJuneRandomClockGraphLayout()
{
    const QString filePath = QFileDialog::getOpenFileName(
        mainWindow,
        tr("Open Verilog for June Random-Clock Graph P&R"),
        QDir::currentPath(),
        tr("Verilog files (*.v);;All files (*)"));
    if (filePath.isEmpty()) {
        mainWindow->customStatusBar->addMessage(
            tr("June random-clock Graph P&R was cancelled."));
        return;
    }
    runJuneRandomClockGraphLayoutForFile(filePath);
}

void VerilogHandler::handleLegacyGraphvizGraphDraw()
{
    const QString filePath = QFileDialog::getOpenFileName(
        mainWindow,
        tr("Open Verilog for Legacy Graphviz Graph Draw"),
        QDir::currentPath(),
        tr("Verilog files (*.v);;All files (*)"));
    if (filePath.isEmpty()) {
        mainWindow->customStatusBar->addMessage(
            tr("Legacy Graphviz Graph Draw was cancelled."));
        return;
    }
    runLegacyGraphvizGraphDrawForFile(filePath);
}

void VerilogHandler::runLegacyGraphvizGraphDrawForFile(const QString &filePath)
{
    if (legacyGraphvizDrawRunning) {
        mainWindow->customStatusBar->addMessage(
            tr("A Legacy Graphviz Graph Draw request is already running."));
        return;
    }

    const QFileInfo sourceInfo(filePath);
    if (filePath.isEmpty() || !sourceInfo.isFile() || !sourceInfo.isReadable()) {
        const QString message = tr("Cannot read the Verilog file for Legacy Graphviz Graph Draw: %1")
            .arg(QDir::toNativeSeparators(filePath));
        emit operationFailed(message);
        return;
    }

    mainWindow->updateVerilogSourceFile(filePath);
    legacyGraphvizDrawRunning = true;
    emit operationStarted(
        tr("Legacy Graphviz Graph Draw"),
        tr("Recreating the June 2025 DOT topology preview for %1")
            .arg(QDir::toNativeSeparators(filePath)));
    emit operationProgress(tr("Parsing and optimizing the legacy logical graph"), 10, 100);

    QPointer<VerilogHandler> self(this);
    QThread *thread = QThread::create([self, filePath]() {
        auto taskResult = std::make_shared<LegacyGraphvizPreviewTaskResult>();
        QElapsedTimer timer;
        timer.start();

        try {
            fcngraph::Parse parse;
            parse.parseVerilog(filePath.toStdString());
            if (parse.getm_numVertices() == 0) {
                throw std::runtime_error("The Verilog file contains no drawable circuit nodes.");
            }
            parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
            parse.optimizeBufferNode();
            parse.addLayerRedundancyNode();
            parse.caculateSameLayerNodeRoutePair();

            fcngraph::LegacyGraphvizOptions options;
            options.showCircuitLabels = true;
            options.boxNodes = true;
            options.orthogonalEdges = true;
            const fcngraph::LegacyGraphvizResult rendered =
                fcngraph::renderLegacyGraphviz(parse, options);

            taskResult->success = rendered.success;
            taskResult->error = QString::fromStdString(rendered.error);
            taskResult->svg = QByteArray(rendered.svg.data(),
                                         static_cast<int>(rendered.svg.size()));
            taskResult->nodeCount = rendered.nodeCount;
            taskResult->edgeCount = rendered.edgeCount;
        } catch (const std::exception &ex) {
            taskResult->error = QString::fromLocal8Bit(ex.what());
        } catch (...) {
            taskResult->error = QObject::tr(
                "Legacy Graphviz Graph Draw failed with an unknown error.");
        }
        taskResult->elapsedMilliseconds = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;

        if (self.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, filePath, taskResult]() {
            if (self.isNull()) {
                return;
            }

            if (!taskResult->success || taskResult->svg.isEmpty()) {
                self->legacyGraphvizDrawRunning = false;
                const QString message = taskResult->error.isEmpty()
                    ? QObject::tr("Legacy Graphviz returned no SVG data.")
                    : taskResult->error;
                emit self->operationFailed(message);
                return;
            }

            QSvgRenderer validator(taskResult->svg);
            if (!validator.isValid()) {
                self->legacyGraphvizDrawRunning = false;
                emit self->operationFailed(
                    QObject::tr("Graphviz returned an invalid SVG document."));
                return;
            }

            emit self->operationProgress(
                QObject::tr("DOT layout complete; opening the interactive preview"), 100, 100);
            const QString readyMessage = QObject::tr(
                "Legacy Graphviz preview ready: %1 nodes, %2 edges, %3 ms (view only)")
                .arg(taskResult->nodeCount)
                .arg(taskResult->edgeCount)
                .arg(taskResult->elapsedMilliseconds, 0, 'f', 1);
            showLegacyGraphvizPreviewDialog(self->mainWindow, filePath, *taskResult);
            if (self.isNull()) {
                return;
            }
            self->legacyGraphvizDrawRunning = false;
            emit self->operationFinished(readyMessage);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VerilogHandler::runJuneRandomClockGraphLayoutForFile(const QString &filePath)
{
    if (juneRandomClockGraphLayoutRunning) {
        mainWindow->customStatusBar->addMessage(
            tr("A June random-clock Graph P&R request is already running."));
        return;
    }

    const QFileInfo sourceInfo(filePath);
    if (filePath.isEmpty() || !sourceInfo.isFile() || !sourceInfo.isReadable()) {
        const QString message = tr("Cannot read the Verilog file for June random-clock Graph P&R: %1")
            .arg(QDir::toNativeSeparators(filePath));
        emit operationFailed(message);
        return;
    }

    JuneRandomClockGraphSettings settings;
    if (!readJuneRandomClockGraphSettings(mainWindow, settings)) {
        mainWindow->customStatusBar->addMessage(
            tr("June random-clock Graph P&R was cancelled."));
        return;
    }

    juneRandomClockGraphLayoutRunning = true;
    mainWindow->updateVerilogSourceFile(filePath);
    emit operationStarted(
        tr("June 2025 random-clock Graph placement and routing"),
        tr("Running the hardened Graphviz placement, source-aware four-direction A*, and post-route 4-phase assignment for %1")
            .arg(QDir::toNativeSeparators(filePath)));

    QPointer<VerilogHandler> self(this);
    QThread *thread = QThread::create([self, filePath, settings]() {
        const auto progress = [self](const QString &detail, int value, int maximum) {
            if (self.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(self, [self, detail, value, maximum]() {
                if (!self.isNull()) {
                    emit self->operationProgress(detail, value, maximum);
                }
            }, Qt::QueuedConnection);
        };
        auto taskResult = std::make_shared<JuneRandomClockGraphTaskResult>(
            runJuneRandomClockGraphSearch(filePath, progress));

        if (self.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, filePath, settings, taskResult]() {
            if (self.isNull()) {
                return;
            }

            const auto fail = [self](const QString &detail) {
                if (self.isNull()) {
                    return;
                }
                self->juneRandomClockGraphLayoutRunning = false;
                emit self->operationFailed(
                    QObject::tr("June random-clock Graph P&R failed: %1").arg(detail));
            };

            if (!taskResult->success) {
                fail(taskResult->error.isEmpty()
                         ? QObject::tr("the background search returned no legal layout")
                         : taskResult->error);
                return;
            }

            try {
                Parse &parse = taskResult->parse;
                LayoutSearchResult &selectedLayout = taskResult->layout;
                emit self->operationProgress(
                    QObject::tr("A* routes and random-clock phases are legal; writing IFCN"),
                    76,
                    100);

                std::map<unsigned int, position> nodePositions;
                for (const auto &node : selectedLayout.nodePositions) {
                    nodePositions[static_cast<unsigned int>(node.first)] = node.second;
                }
                const auto routes = selectedLayout.routes;
                std::map<position, int> positionPhases;
                for (const auto &cell : selectedLayout.gridCells) {
                    const int phase = cell.second.getPhase();
                    if (phase >= 1 && phase <= 4) {
                        positionPhases[cell.first] = phase - 1;
                    }
                }

                const QString outputDirSuffix =
                    QStringLiteral("_june_random_clock_graph_pr");
                const QString ifcnPath = self->saveGateLevelIfcn(
                    filePath,
                    parse,
                    nodePositions,
                    routes,
                    positionPhases,
                    4,
                    taskResult->gateNum,
                    taskResult->inputNum,
                    taskResult->outputNum,
                    taskResult->wireNum,
                    selectedLayout.bounds.width,
                    selectedLayout.bounds.height,
                    taskResult->elapsedSeconds,
                    QStringLiteral("hardened restoration of the June 2025 random-clock Graphviz/A* P&R"),
                    outputDirSuffix,
                    QStringLiteral("_june_random_clock_graph_pr.ifcn"));
                if (ifcnPath.isEmpty()) {
                    throw std::runtime_error(
                        "The legal June random-clock layout could not be saved as IFCN.");
                }

                QStringList generatedArtifacts;
                generatedArtifacts << QObject::tr("IFCN: %1")
                    .arg(QDir::toNativeSeparators(ifcnPath));
                if (settings.generateLatex) {
                    const QString latexPath = self->saveGraphRenderLatex(
                        filePath,
                        parse,
                        nodePositions,
                        routes,
                        positionPhases,
                        4,
                        selectedLayout.bounds.width,
                        selectedLayout.bounds.height,
                        outputDirSuffix,
                        QStringLiteral("_june_random_clock_graph_pr.tex"));
                    if (!latexPath.isEmpty()) {
                        generatedArtifacts << QObject::tr("LaTeX: %1")
                            .arg(QDir::toNativeSeparators(latexPath));
                    }
                }

                emit self->operationProgress(
                    QObject::tr("Loading the hardened June layout into the cell-level view"),
                    90,
                    100);
                if (!self->mainWindow->mapIfcnFile(ifcnPath, false)) {
                    throw std::runtime_error(
                        "The generated IFCN failed cell-level mapping validation.");
                }

                if (settings.generateSvg) {
                    const QString svgPath = QFileInfo(ifcnPath).dir().filePath(
                        QFileInfo(ifcnPath).completeBaseName() + QStringLiteral(".svg"));
                    if (self->exportCellLevelLayout(svgPath)) {
                        generatedArtifacts << QObject::tr("SVG: %1")
                            .arg(QDir::toNativeSeparators(svgPath));
                    }
                }

                const QString summary = QObject::tr(
                    "June random-clock Graph P&R complete: %1x%2=%3, %4 routes, %5 s")
                    .arg(selectedLayout.bounds.width)
                    .arg(selectedLayout.bounds.height)
                    .arg(selectedLayout.bounds.area)
                    .arg(selectedLayout.routes.size())
                    .arg(taskResult->elapsedSeconds, 0, 'f', 3);
                emit self->operationProgress(
                    QObject::tr("June random-clock layout is ready"), 100, 100);
                self->juneRandomClockGraphLayoutRunning = false;
                emit self->operationFinished(summary);
                QMessageBox::information(
                    self->mainWindow,
                    QObject::tr("June 2025 Random-Clock Graph P&R complete"),
                    QObject::tr("The hardened June-derived physical layout is loaded and ready.\n\n%1")
                        .arg(generatedArtifacts.join(QLatin1Char('\n'))));
            } catch (const std::exception &ex) {
                fail(QString::fromLocal8Bit(ex.what()));
            } catch (...) {
                fail(QObject::tr("unknown IFCN save or load failure"));
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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

    fcngraph::Parse parse;
    parse.parseVerilog(file);

    try {
        parse.optimizeAIOG_DRC(2,2,2,2,2,2);

        auto gateNum = parse.getm_numVertices();
        auto inputNum = parse.get_input_num();
        auto outputNum = parse.get_output_num();
        auto wireNum = parse.getm_numEdges();

        parse.optimizeBufferNode();
        parse.caculateSameLayerNodeRoutePair();

        // Include crossing minimization in the reported algorithm runtime.
        QElapsedTimer timer;
        timer.start();

        emit operationStarted(
            tr("Stochastic compact graph placement and routing"),
            tr("Preparing crossing-minimized layer order for compact-first routing and %1-phase closure")
                .arg(settings.phaseCount));
        QCoreApplication::processEvents();

        const FixedLayerOrderResult fixedOrder = fixedLayerCrossingOrder(parse);
        emit operationProgress(
            tr("%1: %2 crossings; placing the ordered layers at minimum spacing before routing")
                .arg(fixedOrder.engine)
                .arg(fixedOrder.crossings),
            5,
            100);
        QCoreApplication::processEvents();

        std::optional<LayoutSearchResult> bestLayout;
        std::string lastFailure;
        const auto attempts = buildLayoutAttempts();
        const std::size_t effectiveEdgeCount = parse.getEffectiveEdges().size();
        const int complexityCap = effectiveEdgeCount > 300 ? 2 : static_cast<int>(attempts.size());
        const int attemptLimit = std::min({settings.maxAttempts,
                                           static_cast<int>(attempts.size()),
                                           complexityCap});
        const qint64 candidateBudgetMilliseconds =
            static_cast<qint64>(settings.timeBudgetSeconds) * 1000;
        bool candidateBudgetReached = false;
        bool usedGraphvizFallback = false;
        bool usedAdaptiveCompact = false;
        constexpr int kCandidateProgressStart = 6;
        constexpr int kCandidateProgressEnd = 68;
        const auto candidateProgress = [attemptLimit](int completedAttempts) {
            if (attemptLimit <= 0) {
                return kCandidateProgressEnd;
            }
            const int completed = std::clamp(completedAttempts, 0, attemptLimit);
            return kCandidateProgressStart +
                   (kCandidateProgressEnd - kCandidateProgressStart) * completed /
                       attemptLimit;
        };
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

        struct GraphvizFallbackAttempt {
            double gridSizeX;
            double gridSizeY;
            double searchCost;
            int routeOrderRetries;
        };
        // Graphviz coordinates are divided by the quantisation value.  The old
        // sparse schedule searched from /64 down to /40, which actually makes
        // a small graph progressively larger.  Very small graphs get their own
        // compact-first schedule.  Deep MAJ graphs keep enough X port capacity
        // and contract Y in four-phase-period increments; shallow graphs may
        // safely compact both axes together.
        const bool compactSmallGraphvizSearch = effectiveEdgeCount <= 24;
        const bool compactNarrowDeepGraphvizSearch =
            compactSmallGraphvizSearch && maxLayerWidth <= 2 && layerNodes.size() >= 6;
        const bool compactVeryDeepGraphvizSearch =
            compactSmallGraphvizSearch && !compactNarrowDeepGraphvizSearch &&
            layerNodes.size() >= 9;
        const bool exploreFineGraphvizScales = effectiveEdgeCount <= 70;
        const std::vector<GraphvizFallbackAttempt> graphvizAttempts =
            compactNarrowDeepGraphvizSearch
            ? std::vector<GraphvizFallbackAttempt>{
                  {40.0, 68.0, 420.0, 8}, {44.0, 68.0, 420.0, 8},
                  {36.0, 68.0, 420.0, 8}, {52.0, 64.0, 360.0, 10},
                  {48.0, 64.0, 360.0, 10}, {52.0, 52.0, 300.0, 10}}
            : compactSmallGraphvizSearch && layerNodes.size() <= 5
            ? std::vector<GraphvizFallbackAttempt>{
                  {96.0, 96.0, 240.0, 8}, {88.0, 88.0, 300.0, 12},
                  {80.0, 80.0, 240.0, 8}, {72.0, 72.0, 240.0, 8},
                  {64.0, 64.0, 240.0, 12}, {60.0, 64.0, 300.0, 10},
                  {56.0, 68.0, 420.0, 10}, {56.0, 72.0, 420.0, 10},
                  {40.0, 68.0, 420.0, 8}, {52.0, 52.0, 300.0, 10},
                  {48.0, 48.0, 300.0, 10}}
            : compactVeryDeepGraphvizSearch
            ? std::vector<GraphvizFallbackAttempt>{
                  {64.0, 64.0, 240.0, 8}, {60.0, 64.0, 300.0, 8},
                  {56.0, 72.0, 420.0, 8}, {52.0, 68.0, 420.0, 8},
                  {52.0, 72.0, 480.0, 10}, {48.0, 64.0, 420.0, 8}}
            : compactSmallGraphvizSearch
            ? std::vector<GraphvizFallbackAttempt>{
                  {64.0, 64.0, 240.0, 8}, {60.0, 64.0, 300.0, 8},
                  {56.0, 68.0, 420.0, 8}, {52.0, 72.0, 480.0, 10},
                  {52.0, 68.0, 420.0, 8}, {56.0, 72.0, 420.0, 8},
                  {56.0, 76.0, 420.0, 8},
                  {52.0, 64.0, 360.0, 10}, {40.0, 68.0, 420.0, 8},
                  {52.0, 52.0, 300.0, 10}, {48.0, 60.0, 360.0, 10},
                  {48.0, 48.0, 300.0, 10}, {44.0, 48.0, 360.0, 10}}
            : exploreFineGraphvizScales
            ? std::vector<GraphvizFallbackAttempt>{
                  {64.0, 64.0, 100.0, 6}, {60.0, 60.0, 100.0, 6},
                  {58.0, 58.0, 100.0, 6}, {56.0, 56.0, 100.0, 6},
                  {54.0, 54.0, 100.0, 6}, {52.0, 52.0, 100.0, 6},
                  {48.0, 48.0, 100.0, 6}, {46.0, 46.0, 100.0, 6},
                  {44.0, 44.0, 100.0, 6}, {42.0, 42.0, 100.0, 6},
                  {40.0, 40.0, 100.0, 6}, {37.0, 37.0, 140.0, 6},
                  {32.0, 32.0, 180.0, 6}, {20.0, 20.0, 360.0, 6}}
            : std::vector<GraphvizFallbackAttempt>{
                  // Dense circuits start with a measured phase-cycle squeeze:
                  // X/44 removes the excess horizontal spread while Y/36 is
                  // one four-phase period tighter than the routable /32 base.
                  // The remaining entries are transactional safety fallbacks.
                  {44.0, 36.0, 420.0, 24}, {44.0, 32.0, 360.0, 24},
                  {32.0, 32.0, 180.0, 6}, {40.0, 40.0, 100.0, 6},
                  {37.0, 37.0, 140.0, 6}, {20.0, 20.0, 360.0, 6}};
        double bestGraphvizGridX = 0.0;
        double bestGraphvizGridY = 0.0;
        const auto evaluateGraphvizAttempt = [&](const GraphvizFallbackAttempt &fallback,
                                                  int progressIndex,
                                                  int progressTotal) {
            const bool anisotropic = std::abs(fallback.gridSizeX - fallback.gridSizeY) > 1e-9;
            const QString quantisation = anisotropic
                ? tr("X/%1, Y/%2")
                      .arg(fallback.gridSizeX, 0, 'f', 0)
                      .arg(fallback.gridSizeY, 0, 'f', 0)
                : tr("/%1").arg(fallback.gridSizeX, 0, 'f', 0);
            emit operationProgress(
                tr("Graphviz compact seed %1/%2: quantisation %3")
                    .arg(progressIndex + 1)
                    .arg(progressTotal)
                    .arg(quantisation),
                6 + progressIndex * 3,
                100);
            QCoreApplication::processEvents();

            GridChessboard fallbackBoard;
            Astar fallbackRouter(fallbackBoard, false, fallback.searchCost);
            fallbackRouter.setAllowInterSourceWireOverlap(false);
            // Crossings remain source-aware and DRC-checked; a zero legal
            // crossing penalty keeps compact seeds from detouring outside
            // their placement solely because maxSearchCost exceeds 100.
            fallbackRouter.setOccupiedWirePenalty(0.0);
            CircuitGraph fallbackGraph(parse, file, fallbackBoard, fallbackRouter);
            fallbackGraph.setFitnessCallback([&lastFailure](const std::string &detail) {
                lastFailure = detail;
            });
            const bool routed = anisotropic
                ? fallbackGraph.placeAndRouteJuneRandomClockAnisotropic(
                      settings.phaseCount,
                      fallback.gridSizeX,
                      fallback.gridSizeY,
                      fallback.routeOrderRetries)
                : fallbackGraph.placeAndRouteJuneRandomClock(
                      settings.phaseCount,
                      fallback.gridSizeX,
                      fallback.routeOrderRetries);
            if (!routed) {
                return std::make_pair(false, false);
            }
            const auto bounds = calculateGridBounds(fallbackBoard.getGridMap());
            if (!bounds.has_value()) {
                lastFailure = "Graphviz seed produced an empty layout";
                return std::make_pair(false, false);
            }
            LayoutSearchResult result;
            result.bounds = bounds.value();
            result.routeLength = totalRouteLength(fallbackGraph.routes);
            result.phaseRepeats = calculatePhaseRepeatStats(
                fallbackGraph.routes, fallbackBoard.getGridMap());
            result.placementEngine = anisotropic
                ? QStringLiteral("Graphviz DOT source-aware compact seed X/%1 Y/%2")
                      .arg(fallback.gridSizeX, 0, 'f', 0)
                      .arg(fallback.gridSizeY, 0, 'f', 0)
                : QStringLiteral("Graphviz DOT source-aware compact seed /%1")
                      .arg(fallback.gridSizeX, 0, 'f', 0);
            result.searchCost = fallback.searchCost;
            result.nodePositions = fallbackGraph.nodeIndex_pos;
            result.routes = fallbackGraph.routes;
            result.gridCells = fallbackBoard.getGridMap();
            const bool improved = !bestLayout.has_value() ||
                isBetterLayout(result, bestLayout.value(), settings.phaseCount);
            if (improved) {
                bestLayout = std::move(result);
                bestGraphvizGridX = fallback.gridSizeX;
                bestGraphvizGridY = fallback.gridSizeY;
            }
            usedGraphvizFallback = true;
            return std::make_pair(true, improved);
        };
        const auto tryCompactGraphvizSeed = [&]() {
            bool foundLegalSeed = false;
            int nonImprovingLegalSeeds = 0;
            for (std::size_t fallbackIndex = 0;
                 fallbackIndex < graphvizAttempts.size();
                 ++fallbackIndex) {
                if (fallbackIndex > 0 && timer.elapsed() >= candidateBudgetMilliseconds) {
                    candidateBudgetReached = true;
                    break;
                }
                const GraphvizFallbackAttempt &fallback = graphvizAttempts[fallbackIndex];
                const auto evaluated = evaluateGraphvizAttempt(
                    fallback,
                    static_cast<int>(fallbackIndex),
                    static_cast<int>(graphvizAttempts.size()) + 1);
                if (!evaluated.first) {
                    continue;
                }
                if (evaluated.second) {
                    nonImprovingLegalSeeds = 0;
                } else {
                    ++nonImprovingLegalSeeds;
                }
                foundLegalSeed = true;
                const int nonImprovingLimit = compactSmallGraphvizSearch ? 1 : 2;
                if (!exploreFineGraphvizScales ||
                    nonImprovingLegalSeeds >= nonImprovingLimit) {
                    break;
                }
            }
            if (foundLegalSeed && exploreFineGraphvizScales &&
                !compactSmallGraphvizSearch &&
                bestGraphvizGridY > 0.0 &&
                timer.elapsed() < candidateBudgetMilliseconds) {
                const double horizontalBoost = maxLayerWidth >= 10 ? 10.0
                    : (maxLayerWidth >= 6 ? 6.0 : 4.0);
                const GraphvizFallbackAttempt horizontalSqueeze{
                    bestGraphvizGridX + horizontalBoost,
                    bestGraphvizGridY,
                    std::max(160.0, bestLayout->searchCost),
                    12};
                evaluateGraphvizAttempt(
                    horizontalSqueeze,
                    static_cast<int>(graphvizAttempts.size()),
                    static_cast<int>(graphvizAttempts.size()) + 2);
                if (timer.elapsed() < candidateBudgetMilliseconds) {
                    // A full clock period can be contracted without changing
                    // endpoint phase equivalence. Generate a Y-compressed
                    // candidate one phase cycle tighter, then reroute and
                    // reassign every phase before it may replace the baseline.
                    const GraphvizFallbackAttempt phaseCycleSqueeze{
                        bestGraphvizGridX,
                        bestGraphvizGridY + static_cast<double>(settings.phaseCount),
                        std::max(180.0, bestLayout->searchCost),
                        18};
                    evaluateGraphvizAttempt(
                        phaseCycleSqueeze,
                        static_cast<int>(graphvizAttempts.size()) + 1,
                        static_cast<int>(graphvizAttempts.size()) + 2);
                }
            }
            return foundLegalSeed;
        };

        // Primary strategy: OGDF/barycenter supplies only the crossing-aware
        // order.  Coordinates start at the minimum integer spacing and gain
        // sparse row/column capacity only where structured routing failures
        // prove it is necessary.  Graphviz scaling remains a safety fallback,
        // not the source of unconditional whitespace.
        {
            const int adaptiveExpansionRounds =
                effectiveEdgeCount <= 24 ? 8
                : (effectiveEdgeCount <= 70 ? 5
                   : (effectiveEdgeCount <= 128 ? 3 : 0));
            const int adaptiveRouteRetries =
                effectiveEdgeCount <= 70 ? 2 : 0;
            const double adaptiveSearchCost =
                effectiveEdgeCount <= 24 ? 120.0
                : (effectiveEdgeCount <= 70 ? 180.0 : 240.0);

            emit operationProgress(
                tr("Compact-first seed: %1 order at unit spacing; routing conflicts insert sparse capacity")
                    .arg(fixedOrder.engine),
                6,
                100);
            QCoreApplication::processEvents();

            GridChessboard compactBoard;
            Astar compactRouter(compactBoard, false, adaptiveSearchCost);
            compactRouter.setAllowInterSourceWireOverlap(false);
            compactRouter.setOccupiedWirePenalty(0.0);
            CircuitGraph compactGraph(parse, file, compactBoard, compactRouter);
            compactGraph.setFitnessCallback([&lastFailure](const std::string &detail) {
                lastFailure = detail;
            });
            compactGraph.sortNodesByFixedLayerOrder(
                fixedOrder.layers, 1, 1);

            if (compactGraph.routeCompactRandomClockWithExpansion(
                    settings.phaseCount,
                    adaptiveRouteRetries,
                    adaptiveExpansionRounds,
                    adaptiveSearchCost,
                    settings.maxSamePhase)) {
                const auto bounds = calculateGridBounds(
                    compactBoard.getGridMap());
                if (bounds.has_value()) {
                    const auto &expansion =
                        compactGraph.getAdaptiveExpansionStats();
                    LayoutSearchResult result;
                    result.bounds = bounds.value();
                    result.routeLength =
                        totalRouteLength(compactGraph.routes);
                    result.phaseRepeats = calculatePhaseRepeatStats(
                        compactGraph.routes,
                        compactBoard.getGridMap());
                    result.placementEngine =
                        QStringLiteral("%1 compact-first (+%2R,+%3C,-%4R,-%5C)")
                            .arg(fixedOrder.engine)
                            .arg(expansion.insertedRows)
                            .arg(expansion.insertedColumns)
                            .arg(expansion.removedRows)
                            .arg(expansion.removedColumns);
                    result.xSpacing = 1;
                    result.ySpacing = 1;
                    result.searchCost = adaptiveSearchCost;
                    result.nodePositions = compactGraph.nodeIndex_pos;
                    result.routes = compactGraph.routes;
                    result.gridCells = compactBoard.getGridMap();
                    bestLayout = std::move(result);
                    usedAdaptiveCompact = true;
                    usedGraphvizFallback = false;
                    emit operationProgress(
                        tr("Compact-first routing complete: %1x%2=%3, inserted %4 row(s) and %5 column(s)")
                            .arg(bestLayout->bounds.width)
                            .arg(bestLayout->bounds.height)
                            .arg(bestLayout->bounds.area)
                            .arg(expansion.insertedRows)
                            .arg(expansion.insertedColumns),
                        68,
                        100);
                    QCoreApplication::processEvents();
                }
            }
        }

        if (!bestLayout.has_value()) {
            tryCompactGraphvizSeed();
        }

        for (int attemptIndex = 0;
             !usedAdaptiveCompact && attemptIndex < attemptLimit;
             ++attemptIndex) {
            if (attemptIndex > 0 && !bestLayout.has_value()) {
                // The 8x8 feasibility anchor failed.  Smaller fixed-layer
                // placements cannot improve port capacity, so switch to the
                // Graphviz geometry fallback instead of burning every compact
                // candidate on the same routing deadlock.
                break;
            }
            if (attemptIndex > 0 && timer.elapsed() >= candidateBudgetMilliseconds) {
                candidateBudgetReached = true;
                break;
            }
            const auto &attempt = attempts[attemptIndex];
            const bool bestPhaseOk = bestLayout.has_value()
                && hasAcceptablePhaseRepeats(bestLayout->phaseRepeats, settings.phaseCount);
            if (bestPhaseOk && placementAreaLowerBound(attempt) > bestLayout->bounds.area) {
                continue;
            }
            GridChessboard chessboard;
            Astar astar(chessboard, false, attempt.searchCost);
            CircuitGraph graph(parse, file, chessboard, astar);
            graph.setFitnessCallback([&lastFailure](const std::string &detail) {
                lastFailure = detail;
            });

            {
                QString progress = QString("Candidate %1/%2: spacing=(%3,%4), search cost=%5, phase-aware=%6")
                    .arg(attemptIndex + 1)
                    .arg(attemptLimit)
                    .arg(attempt.xSpacing)
                    .arg(attempt.ySpacing)
                    .arg(attempt.searchCost)
                    .arg(settings.phaseCount);
                if (bestLayout.has_value()) {
                    progress += QString("; best=%1x%2 area=%3 route=%4 max repeat=%5")
                                    .arg(bestLayout->bounds.width)
                                    .arg(bestLayout->bounds.height)
                                    .arg(bestLayout->bounds.area)
                                    .arg(bestLayout->routeLength)
                                    .arg(bestLayout->phaseRepeats.maxRun);
                }
                emit operationProgress(progress, candidateProgress(attemptIndex), 100);
                QCoreApplication::processEvents();
            }

            try {
                graph.sortNodesByFixedLayerOrder(fixedOrder.layers,
                                                 attempt.xSpacing,
                                                 attempt.ySpacing);

                if (!graph.placeAndRoutePhaseAware(settings.phaseCount,
                                                   settings.maxSamePhase,
                                                   attempt.searchCost)) {
                    if (lastFailure.empty()) {
                        lastFailure = "phase-aware route failed";
                    }
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
                result.placementEngine = fixedOrder.engine;
                result.xSpacing = attempt.xSpacing;
                result.ySpacing = attempt.ySpacing;
                result.searchCost = attempt.searchCost;
                result.nodePositions = graph.nodeIndex_pos;
                result.routes = graph.routes;
                result.gridCells = chessboard.getGridMap();

                const bool isBetter = !bestLayout.has_value()
                    || isBetterLayout(result, bestLayout.value(), settings.phaseCount);
                if (isBetter) {
                    bestLayout = std::move(result);
                    usedGraphvizFallback = false;
                    const bool phaseOk = hasAcceptablePhaseRepeats(bestLayout->phaseRepeats, settings.phaseCount);
                    const double repeatRatio = bestLayout->phaseRepeats.totalAdjacent > 0
                        ? static_cast<double>(bestLayout->phaseRepeats.repeatedAdjacent) / bestLayout->phaseRepeats.totalAdjacent
                        : 0.0;
                    QString message = QString("Candidate %1/%2 success: %3x%4=%5, phase=%6, spacing=(%7,%8), max repeat=%9, repeat ratio=%10% (%11)")
                        .arg(attemptIndex + 1)
                        .arg(attemptLimit)
                        .arg(bestLayout->bounds.width)
                        .arg(bestLayout->bounds.height)
                        .arg(bestLayout->bounds.area)
                        .arg(settings.phaseCount)
                        .arg(bestLayout->xSpacing)
                        .arg(bestLayout->ySpacing)
                        .arg(bestLayout->phaseRepeats.maxRun)
                        .arg(repeatRatio * 100.0, 0, 'f', 1)
                        .arg(phaseOk ? "phase-ok" : "phase-repeat-high");
                    emit operationProgress(message, candidateProgress(attemptIndex + 1), 100);
                    QCoreApplication::processEvents();
                }
            } catch (const std::exception &ex) {
                lastFailure = ex.what();
                continue;
            }
        }

        if (!bestLayout.has_value()) {
            emit operationProgress(
                tr("Compact Graphviz and fixed-layer routing were blocked; checking the buffered safety topology"),
                69,
                100);
            QCoreApplication::processEvents();
            const bool preferBufferedTopology =
                layerNodes.size() >= 18 && maxLayerWidth >= 10;
            if (!bestLayout.has_value() && preferBufferedTopology) {
                emit operationProgress(
                    tr("Compact topology remained blocked; restoring July layer buffers for a legal fallback"),
                    70,
                    100);
                QCoreApplication::processEvents();

                Parse bufferedParse;
                bufferedParse.parseVerilog(file);
                bufferedParse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
                bufferedParse.addLayerRedundancyNode();
                bufferedParse.caculateSameLayerNodeRoutePair();

                GridChessboard bufferedBoard;
                Astar bufferedRouter(bufferedBoard, false, 360.0);
                bufferedRouter.setAllowInterSourceWireOverlap(false);
                CircuitGraph bufferedGraph(
                    bufferedParse, file, bufferedBoard, bufferedRouter);
                bufferedGraph.setFitnessCallback([&lastFailure](const std::string &detail) {
                    lastFailure = detail;
                });
                if (bufferedGraph.placeAndRouteJuneRandomClock(
                        settings.phaseCount,
                        20.0,
                        24)) {
                    const auto bounds = calculateGridBounds(bufferedBoard.getGridMap());
                    if (bounds.has_value()) {
                        LayoutSearchResult result;
                        result.bounds = bounds.value();
                        result.routeLength = totalRouteLength(bufferedGraph.routes);
                        result.phaseRepeats = calculatePhaseRepeatStats(
                            bufferedGraph.routes, bufferedBoard.getGridMap());
                        result.placementEngine = QStringLiteral(
                            "Graphviz DOT buffered-topology fallback");
                        result.searchCost = 360.0;
                        result.nodePositions = bufferedGraph.nodeIndex_pos;
                        result.routes = bufferedGraph.routes;
                        result.gridCells = bufferedBoard.getGridMap();
                        bestLayout = std::move(result);
                        usedGraphvizFallback = true;
                        parse = std::move(bufferedParse);
                        gateNum = parse.getm_numVertices();
                        wireNum = parse.getm_numEdges();
                    }
                }
            }
            if (!bestLayout.has_value()) {
                QString message = QString(
                    "Compact Graph Draw failed in both fixed-layer and Graphviz fallback routing. Last failure: %1")
                    .arg(QString::fromStdString(lastFailure));
                emit operationFailed(message);
                QCoreApplication::processEvents();
                return;
            }
        }

        if (candidateBudgetReached) {
            emit operationProgress(
                tr("Candidate budget reached; keeping the best legal layout found"),
                69,
                100);
        }

        const int effectiveCompactionRounds =
            effectiveEdgeCount > 300 || usedGraphvizFallback || usedAdaptiveCompact
            ? 0 : settings.compactionRounds;
        emit operationProgress(
            effectiveCompactionRounds > 0
                ? tr("Candidate search complete; preparing phase-aware compaction")
                : tr("Legal compact layout selected; preparing output data"),
            70,
            100);
        QCoreApplication::processEvents();
        if (effectiveCompactionRounds > 0) {
            emit operationProgress(
                tr("Phase-aware compaction: rerouting each accepted geometric cut"),
                72,
                100);
            QCoreApplication::processEvents();

            GridChessboard compactBoard;
            Astar legacyRouter(compactBoard, false,
                               std::max(240.0, bestLayout->searchCost));
            CircuitGraph compactGraph(parse, file, compactBoard, legacyRouter);
            compactGraph.nodeIndex_pos = bestLayout->nodePositions;
            if (compactGraph.placeAndRoutePhaseAware(settings.phaseCount,
                                                     settings.maxSamePhase,
                                                     std::max(240.0, bestLayout->searchCost))) {
                const int acceptedCuts = compactGraph.compactPhaseAware(
                    settings.phaseCount,
                    settings.maxSamePhase,
                    std::max(300.0, bestLayout->searchCost),
                    effectiveCompactionRounds);
                const auto compactBounds = calculateGridBounds(compactBoard.getGridMap());
                if (compactBounds.has_value()) {
                    LayoutSearchResult compactResult;
                    compactResult.bounds = compactBounds.value();
                    compactResult.routeLength = totalRouteLength(compactGraph.routes);
                    compactResult.phaseRepeats = calculatePhaseRepeatStats(
                        compactGraph.routes, compactBoard.getGridMap());
                    compactResult.placementEngine = bestLayout->placementEngine;
                    compactResult.xSpacing = bestLayout->xSpacing;
                    compactResult.ySpacing = bestLayout->ySpacing;
                    compactResult.searchCost = std::max(300.0, bestLayout->searchCost);
                    compactResult.compactionCuts = acceptedCuts;
                    compactResult.nodePositions = compactGraph.nodeIndex_pos;
                    compactResult.routes = compactGraph.routes;
                    compactResult.gridCells = compactBoard.getGridMap();
                    if (isBetterLayout(compactResult, bestLayout.value(), settings.phaseCount)) {
                        bestLayout = std::move(compactResult);
                    }
                }
            }
        }

        emit operationProgress(
            tr("Placement, routing, and compaction complete; preparing output data"),
            82,
            100);
        QCoreApplication::processEvents();

        double elapsedSeconds = timer.elapsed() / 1000.0;
        int width = bestLayout->bounds.width;
        int height = bestLayout->bounds.height;

        QString elapsedStr = filePath + " \& " + QString::number(gateNum) +
                                        " \& " + QString::number(inputNum) + " / " + QString::number(outputNum) +
                                        " \& " + QString::number(wireNum) + 
                                        " \& " + QString::number(width)+ " $\\times$ " + QString::number(height) + " = " + QString::number(width*height) +
                                        " \& phase " + QString::number(settings.phaseCount) +
                                        " \& " + QString::number(elapsedSeconds, 'f', 1) ;
        //测试时间

        const bool phaseQualityOk = hasAcceptablePhaseRepeats(bestLayout->phaseRepeats, settings.phaseCount);
        const double repeatRatio = bestLayout->phaseRepeats.totalAdjacent > 0
            ? static_cast<double>(bestLayout->phaseRepeats.repeatedAdjacent) / bestLayout->phaseRepeats.totalAdjacent
            : 0.0;
        QString placementSummary = QString(" ; placement=%1")
            .arg(bestLayout->placementEngine);
        if (!usedGraphvizFallback) {
            placementSummary += QString(" (%1 fixed-layer crossings)")
                .arg(fixedOrder.crossings);
        }
        const QString geometrySummary = usedAdaptiveCompact
            ? QString(" ; unit-spacing seed with adaptive capacity, route length=%1")
                  .arg(bestLayout->routeLength)
            : (usedGraphvizFallback
                   ? QString(" ; route length=%1").arg(bestLayout->routeLength)
                   : QString(" ; candidate spacing=(%1,%2), route length=%3")
                         .arg(bestLayout->xSpacing)
                         .arg(bestLayout->ySpacing)
                         .arg(bestLayout->routeLength));
        QString message =  "Stochastic compact graph layout success! " + elapsedStr +
                           placementSummary + geometrySummary +
                           QString(" ; max phase repeat=%1 ; repeat ratio=%2% ; phase quality=%3")
                               .arg(bestLayout->phaseRepeats.maxRun)
                               .arg(repeatRatio * 100.0, 0, 'f', 1)
                               .arg(phaseQualityOk ? "ok" : "high-repeat-warning") +
                           QString(" ; accepted phase-aware cuts=%1")
                               .arg(bestLayout->compactionCuts);

        std::map<unsigned int, position> node_pos;
        for (auto& pair : bestLayout->nodePositions) {
            node_pos[static_cast<unsigned int>(pair.first)] = pair.second;  
        } 
        
        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes = bestLayout->routes;
        std::unordered_map<position, GridCell, PositionHash> gridCells = bestLayout->gridCells;

        if (node_pos.empty()) {
            throw std::runtime_error("No node positions generated");
        }

        // Keep the routed coordinate system unchanged in every exported
        // artifact.  The save routines may translate the minimum coordinate
        // to the origin, but must not rotate or reflect the final layout.
        std::unordered_set<position, PositionHash> occupiedPositions;
        occupiedPositions.reserve(node_pos.size() + gridCells.size());
        for (const auto &node : node_pos) {
            occupiedPositions.insert(node.second);
        }
        for(const auto &v : routes) {
            for(const position &route : v.second) {
                occupiedPositions.insert(route);
            }
        }

        std::map<position, int> pos_phase;
        for (const auto &v : gridCells) {
            if (v.second.getPhase() < 1) {
                continue;
            }
            if (occupiedPositions.find(v.first) != occupiedPositions.end()) {
                pos_phase[v.first] = v.second.getPhase() - 1;
            }
        }

        emit operationProgress(tr("Saving required .ifcn layout"), 86, 100);
        QCoreApplication::processEvents();

        const QString crossingOrderLabel =
            fixedOrder.engine.startsWith(QStringLiteral("OGDF"))
                ? QStringLiteral("OGDF crossing minimization")
                : fixedOrder.engine;
        const QString algorithmLabel = usedAdaptiveCompact
            ? QStringLiteral("compact Graph Draw: %1 + unit-spacing placement + conflict-driven row/column expansion + random-phase closure")
                  .arg(crossingOrderLabel)
            : (usedGraphvizFallback
                   ? QStringLiteral("compact Graph Draw: Graphviz safety fallback + four-direction routing + random-phase closure")
                   : QStringLiteral("compact Graph Draw: crossing-ordered fixed-layer fallback + phase-aware routing"));
        const QString ifcnPath = saveGateLevelIfcn(filePath,
                                                   parse,
                                                   node_pos,
                                                   routes,
                                                   pos_phase,
                                                   settings.phaseCount,
                                                   static_cast<int>(gateNum),
                                                   static_cast<int>(inputNum),
                                                   static_cast<int>(outputNum),
                                                   static_cast<int>(wireNum),
                                                   width,
                                                   height,
                                                   elapsedSeconds,
                                                   algorithmLabel,
                                                   QStringLiteral("_graph_pr_layout"),
                                                   QStringLiteral("_graph_pr_layout.ifcn"));
        if (ifcnPath.isEmpty()) {
            throw std::runtime_error("Graph P&R generated a layout, but failed to save .ifcn.");
        }

        QStringList generatedArtifacts;
        generatedArtifacts << tr("IFCN: %1").arg(QDir::toNativeSeparators(ifcnPath));

        if (settings.generateLatex) {
            emit operationProgress(tr("Writing optional phase-layout LaTeX"), 90, 100);
            QCoreApplication::processEvents();
            const QString latexPath = saveGraphRenderLatex(
                filePath,
                parse,
                node_pos,
                routes,
                pos_phase,
                settings.phaseCount,
                width,
                height,
                QStringLiteral("_graph_pr_layout"),
                QStringLiteral("_graph_pr_layout.tex"));
            if (!latexPath.isEmpty()) {
                generatedArtifacts << tr("LaTeX: %1").arg(
                    QDir::toNativeSeparators(latexPath));
            } else {
                generatedArtifacts << tr("LaTeX export failed (the IFCN layout is still valid)");
            }
        }

        emit operationProgress(tr("Loading generated .ifcn into the cell-level view"), 94, 100);
        QCoreApplication::processEvents();
        // Suppress the parser's intermediate success dialog.  The true
        // completion dialog is shown after mapping and optional exports.
        mainWindow->mapIfcnFile(ifcnPath, false);

        if (settings.generateSvg) {
            emit operationProgress(tr("Writing optional final cell-level SVG"), 98, 100);
            QCoreApplication::processEvents();
            const QFileInfo ifcnInfo(ifcnPath);
            const QString svgPath = ifcnInfo.dir().filePath(
                ifcnInfo.completeBaseName() + QStringLiteral(".svg"));
            if (exportCellLevelLayout(svgPath)) {
                generatedArtifacts << tr("SVG: %1").arg(
                    QDir::toNativeSeparators(svgPath));
            } else {
                generatedArtifacts << tr("SVG export failed (the IFCN layout is still valid)");
            }
        }

        const QString loadedMessage = message +
            tr(" ; loaded .ifcn: %1").arg(QDir::toNativeSeparators(ifcnPath));
        emit operationFinished(loadedMessage);
        QCoreApplication::processEvents();
        if (qEnvironmentVariable("IFCN_AUTO_GRAPH_RENDER_FILE").trimmed().isEmpty()) {
            QMessageBox::information(
                mainWindow,
                tr("Stochastic Compact Graph P&R complete"),
                tr("The layout is loaded and ready.\n\n%1")
                    .arg(generatedArtifacts.join(QLatin1Char('\n'))));
        }
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
    auto routeexample = mapping.mapping_line(circle_line);
    std::string crossoverError;
    if (!mapping.validate_crossovers(&crossoverError)) {
        const QString message = QStringLiteral("Cell mapping rejected: invalid crossover: %1")
                                    .arg(QString::fromStdString(crossoverError));
        qWarning().noquote() << message;
        mainWindow->customStatusBar->addMessage(message);
        return;
    }
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
                position node_pos = {cellpos.first / 5, cellpos.second / 5};
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
                position node_pos = {cellpos.first / 5, cellpos.second / 5};
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
    const int blockSize = phaseCount == 3 ? 3 : 4;
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
    if (restoredLayout.skippedHiddenNotRoutes > 0) {
        mainWindow->printToStatusBar(
            tr("Refusing to save a layout with %1 unrestored hidden-NOT route(s).")
                .arg(restoredLayout.skippedHiddenNotRoutes));
        return QString();
    }

    std::vector<std::vector<position>> restoredRouteGeometry;
    restoredRouteGeometry.reserve(restoredLayout.routes.size());
    for (const auto &route : restoredLayout.routes) {
        restoredRouteGeometry.push_back(route.second);
    }
    Mapping restoredMapping;
    restoredMapping.mapping_line(restoredRouteGeometry);
    std::string restoredCrossoverError;
    if (!restoredMapping.validate_crossovers(&restoredCrossoverError)) {
        mainWindow->printToStatusBar(
            tr("Refusing to save an invalid crossover mapping: %1")
                .arg(QString::fromStdString(restoredCrossoverError)));
        return QString();
    }

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

    QSaveFile file(outputPath);
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
        file.cancelWriting();
        return QString();
    }
    out << "#phase map\n";
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit()) {
        mainWindow->printToStatusBar(
            "Failed to commit .ifcn: " + QDir::toNativeSeparators(outputPath));
        return QString();
    }

    mainWindow->printToStatusBar(label + " .ifcn saved: " + QDir::toNativeSeparators(outputPath));
    return outputPath;
}

QString VerilogHandler::saveGraphRenderLatex(
    const QString &sourceFilePath,
    Parse &parse,
    const std::map<unsigned int, position> &nodePositions,
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes,
    const std::map<position, int> &posPhase,
    int phaseCount,
    int width,
    int height,
    const QString &outputDirSuffix,
    const QString &outputFileSuffix)
{
    const QFileInfo sourceInfo(sourceFilePath);
    const QString outputStem = layoutOutputStem(sourceInfo, parse);
    const QString outputDirPath = layoutOutputDirPath(sourceInfo,
                                                     outputStem,
                                                     outputDirSuffix);
    if (!QDir().mkpath(outputDirPath)) {
        mainWindow->printToStatusBar("Failed to create layout LaTeX directory: " +
                                     QDir::toNativeSeparators(outputDirPath));
        return QString();
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
        return QString();
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
        return QString();
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
            const QString coord = rlStyleCoord(pos, normalizedHeight);
            if (usedPositions.find(pos) == usedPositions.end()) {
                out << "\\node[c-1] at " << coord << "{\\phasecell{null}};\n";
                continue;
            }

            const auto phaseIt = normalizedPosPhase.find(pos);
            const int phase = rlStylePhase(phaseIt != normalizedPosPhase.end() ? phaseIt->second : 0,
                                           phaseCount);
            out << "\\node[c" << phase << "] at " << coord
                << "{\\phasecell{" << phase << "}};\n";
        }
    }

    for (const auto &entry : normalizedNodePositions) {
        out << "\\node[v] (" << entry.first << ") at "
            << rlStyleCoord(entry.second, normalizedHeight)
            << "{" << entry.first << "};\n";
    }

    for (const auto &route : normalizedRoutes) {
        if (route.second.empty()) {
            continue;
        }
        out << "\\draw[route](" << route.first.first << ")--";
        for (std::size_t i = 1; i + 1 < route.second.size(); ++i) {
            out << rlStyleCoord(route.second[i], normalizedHeight) << "--";
        }
        out << "(" << route.first.second << ");\n";
    }

    out << "\\end{tikzpicture}\n\\end{document}\n";
    file.close();

    mainWindow->printToStatusBar("Layout LaTeX saved: " + QDir::toNativeSeparators(outputPath));
    return outputPath;
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

    exportCellLevelLayout(outputPath, selectedFilter);
}

bool VerilogHandler::exportCellLevelLayout(const QString &requestedOutputPath,
                                           const QString &selectedFilter)
{
    QRectF itemsBoundingRect = mainWindow->scene->exportContentBounds();

    if (!itemsBoundingRect.isValid() || itemsBoundingRect.isEmpty()) {
        const QString message = QStringLiteral("No cell-level layout to save.");
        mainWindow->printToStatusBar(message);
        return false;
    }

    if (requestedOutputPath.trimmed().isEmpty()) {
        return false;
    }

    itemsBoundingRect = itemsBoundingRect.normalized();
    QString outputPath = requestedOutputPath;

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
            return false;
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
        return true;
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
        return false;
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
    return true;
}
