#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QtGlobal>
#include <cmath>
#include <stdexcept>
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/TabbedMainWindow.h"
#include "ui/widgets/CircuitSchematicView.h"
#include "ui/widgets/LayeredStructure3DView.h"

namespace {
bool consoleLoggingEnabled()
{
    if (qEnvironmentVariableIsSet("IFCN_UI_SCREENSHOT_INPUT")
        || qEnvironmentVariableIsSet("IFCN_UI_SCREENSHOT_VIEW")) {
        return true;
    }
    const char *value = std::getenv("IFCN_ENABLE_CONSOLE_LOG");
    if (value == nullptr) {
        return false;
    }
    const QByteArray flag(value);
    return flag == "1" || flag.toLower() == "true" || flag.toLower() == "yes";
}

void silentQtMessageHandler(QtMsgType type,
                            const QMessageLogContext &,
                            const QString &)
{
    if (type == QtFatalMsg) {
        std::abort();
    }
}

void silenceProcessConsole()
{
#ifdef Q_OS_WIN
    constexpr const char *nullDevice = "NUL";
#else
    constexpr const char *nullDevice = "/dev/null";
#endif
    (void)std::freopen(nullDevice, "w", stdout);
    (void)std::freopen(nullDevice, "w", stderr);
}

QString engineeringStyleSheet()
{
    return QString::fromLatin1(R"IFCN(
QMainWindow {
  background: #e9eef5;
}
QWidget {
  color: #172033;
  background-color: #f5f7fb;
}
QMenuBar {
  background-color: #172033;
  color: #f8fafc;
  border-bottom: 1px solid #0f172a;
  padding: 3px 8px;
}
QMenuBar::item {
  background: transparent;
  padding: 5px 10px;
}
QMenuBar::item:selected {
  background-color: #2d3a50;
}
QMenu {
  background-color: #ffffff;
  border: 1px solid #cbd5e1;
  padding: 5px;
}
QMenu::item {
  padding: 7px 28px 7px 12px;
}
QMenu::item:selected {
  background-color: #eaf2ff;
  color: #1d4ed8;
}
QToolBar {
  background-color: #ffffff;
  border: 0;
  border-bottom: 1px solid #d8dee8;
  padding: 4px 7px;
  spacing: 5px;
}
QToolBar::separator {
  background: #d8dee8;
  width: 1px;
  margin: 3px 5px;
}
QToolBar QLabel {
  background: transparent;
}
QToolBar#viewToolBar {
  border-bottom: 0;
}
QPushButton,
QToolButton {
  background-color: #ffffff;
  border: 1px solid #cbd5e1;
  border-radius: 6px;
  padding: 4px 10px;
  min-height: 26px;
}
QPushButton:hover,
QToolButton:hover {
  background-color: #f1f5f9;
  border-color: #94a3b8;
}
QPushButton:pressed,
QToolButton:pressed {
  background-color: #e2e8f0;
}
QPushButton:checked,
QToolButton:checked {
  background-color: #eaf2ff;
  border-color: #3b82f6;
  color: #1d4ed8;
}
QToolButton#primaryAlgorithmButton {
  background-color: #2563eb;
  border-color: #1d4ed8;
  color: #ffffff;
  font-weight: 600;
  padding: 4px 12px;
}
QToolButton#primaryAlgorithmButton:hover {
  background-color: #1d4ed8;
  border-color: #1e40af;
}
QCheckBox#workspaceIoContractCheckBox {
  background-color: #ffffff;
  border: 1px solid #cbd5e1;
  border-radius: 6px;
  padding: 6px 10px;
  min-height: 22px;
  font-weight: 600;
}
QCheckBox#workspaceIoContractCheckBox:hover {
  background-color: #f1f5f9;
  border-color: #94a3b8;
}
QCheckBox#workspaceIoContractCheckBox:checked {
  background-color: #eaf2ff;
  border-color: #3b82f6;
  color: #1d4ed8;
}
QCheckBox#workspaceIoContractCheckBox::indicator {
  width: 15px;
  height: 15px;
}
QPushButton:focus,
QToolButton:focus,
QComboBox:focus,
QLineEdit:focus,
QSpinBox:focus,
QDoubleSpinBox:focus {
  border: 1px solid #2563eb;
}
QPushButton:disabled,
QToolButton:disabled,
QComboBox:disabled,
QSpinBox:disabled,
QDoubleSpinBox:disabled {
  background: #eef2f7;
  color: #94a3b8;
  border-color: #d8dee8;
}
QComboBox,
QLineEdit,
QSpinBox,
QDoubleSpinBox {
  background-color: #ffffff;
  border: 1px solid #cbd5e1;
  border-radius: 6px;
  padding: 3px 8px;
  min-height: 26px;
}
QComboBox:hover,
QLineEdit:hover,
QSpinBox:hover,
QDoubleSpinBox:hover {
  border-color: #94a3b8;
}
QComboBox::drop-down {
  border-left: 1px solid #c4cad3;
  width: 20px;
}
QComboBox QAbstractItemView {
  background-color: #ffffff;
  border: 1px solid #9fa9b5;
  selection-background-color: #dce6f2;
  selection-color: #1f252d;
}
QTabWidget::pane {
  border: 1px solid #d8dee8;
  background: #ffffff;
}
QTabBar::tab {
  background: #eef2f7;
  border: 1px solid #d8dee8;
  border-bottom: 0;
  padding: 7px 16px;
  margin-right: 1px;
  min-width: 110px;
}
QTabBar::tab:selected {
  background: #ffffff;
  color: #172033;
}
QTabBar::tab:!selected {
  color: #52606e;
}
QDockWidget {
  background: #eef2f7;
  color: #172033;
  font-weight: 600;
}
QDockWidget::title {
  background: #eef2f7;
  border-bottom: 1px solid #d8dee8;
  padding: 7px 9px;
  text-align: left;
}
QToolBox {
  background-color: #dfe4ea;
  border: 1px solid #b8c0ca;
}
QToolBox::tab {
  background: #eef1f4;
  border: 1px solid #b8c0ca;
  padding: 9px 10px;
  margin: 1px;
  min-height: 46px;
  min-width: 0;
  font-weight: 600;
  font-size: 13px;
}
QToolBox::tab:selected {
  background: #dce6f2;
  border-color: #7f9fbd;
}
QGraphicsView {
  background-color: #ffffff;
  border: 1px solid #aeb7c2;
}
QTableView,
QTableWidget,
QListView,
QTreeView {
  background-color: #ffffff;
  alternate-background-color: #f3f5f7;
  border: 1px solid #b8c0ca;
  gridline-color: #d4d9df;
  selection-background-color: #dce6f2;
  selection-color: #1f252d;
}
QHeaderView::section {
  background-color: #e1e5ea;
  border: 0;
  border-right: 1px solid #c4cad3;
  border-bottom: 1px solid #c4cad3;
  padding: 4px 7px;
  font-weight: 600;
}
QTextEdit,
QPlainTextEdit {
  background-color: #ffffff;
  border: 1px solid #aeb7c2;
  border-radius: 3px;
  padding: 5px;
  selection-background-color: #cddff2;
}
QScrollArea {
  background: #f2f4f6;
  border: 0;
}
QScrollBar:vertical,
QScrollBar:horizontal {
  background: #e1e5ea;
  border: 0;
  margin: 0;
}
QScrollBar:vertical {
  width: 10px;
}
QScrollBar:horizontal {
  height: 10px;
}
QScrollBar::handle {
  background: #aeb7c2;
  min-height: 22px;
  min-width: 22px;
}
QScrollBar::handle:hover {
  background: #8995a3;
}
QScrollBar::add-line,
QScrollBar::sub-line {
  width: 0;
  height: 0;
}
QSplitter::handle {
  background-color: #b8c0ca;
}
CustomStatusBar {
  background-color: #ffffff;
  border: 1px solid #d8dee8;
  border-radius: 6px;
}
QWidget#statusSummary {
  background: #ffffff;
  border: 0;
}
QLabel#statusBadge {
  background: #eef2f7;
  color: #475569;
  border-radius: 9px;
  padding: 2px 8px;
  font-weight: 600;
}
QLabel#statusBadge[state="running"] {
  background: #dbeafe;
  color: #1d4ed8;
}
QLabel#statusBadge[state="success"] {
  background: #dcfce7;
  color: #166534;
}
QLabel#statusBadge[state="error"] {
  background: #fee2e2;
  color: #b91c1c;
}
QLabel#latestStatusMessage {
  background: transparent;
  color: #475569;
}
QToolButton#statusDetailsButton {
  background: transparent;
  border: 0;
  color: #2563eb;
  padding: 2px 6px;
  min-height: 20px;
}
QScrollArea#statusLog {
  background: #f8fafc;
  border-top: 1px solid #e2e8f0;
}
QLabel#statusMessage {
  background: transparent;
  border: 0;
  padding: 2px 7px;
  color: #2b3037;
  font-weight: 500;
}
QWidget#operationStatus {
  background-color: #f8fbff;
  border: 1px solid #bfdbfe;
  border-radius: 5px;
}
QProgressBar {
  border: 1px solid #9fa9b5;
  background-color: #eef1f4;
  height: 12px;
  text-align: center;
}
QProgressBar::chunk {
  background-color: #2563eb;
}
QDialog,
QMessageBox,
QFileDialog {
  background-color: #f2f4f6;
}
QLabel {
  color: #2f3742;
}
)IFCN");
}

QString readScreenshotInput(const QString &path)
{
    QFile input(path);
    if (!QFileInfo(path).isFile() || !input.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(QStringLiteral("Cannot read screenshot input: %1")
                                     .arg(path).toStdString());
    }
    const QString text = QString::fromUtf8(input.readAll());
    if (text.trimmed().isEmpty()) {
        throw std::runtime_error("Screenshot input is empty");
    }
    return text;
}

// The interactive waveform reader assumes complete, nonempty trace blocks.
// Validate batch inputs before handing them to that existing reader.
bool validScreenshotWaveform(const QString &text)
{
    int samples = 0;
    int traces = 0;
    int traceSamples = 0;
    bool inTrace = false;
    bool inData = false;
    bool sawData = false;
    const QRegularExpression whitespace(QStringLiteral("\\s+"));
    for (const QString &rawLine : text.split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("number_samples="))) {
            bool ok = false;
            samples = line.mid(15).toInt(&ok);
            if (!ok || samples <= 0) return false;
        } else if (line == QStringLiteral("[TRACE]")) {
            if (inTrace || samples <= 0) return false;
            inTrace = true;
            sawData = false;
            traceSamples = 0;
        } else if (line == QStringLiteral("[TRACE_DATA]")) {
            if (!inTrace || inData || sawData) return false;
            inData = true;
            sawData = true;
        } else if (line == QStringLiteral("[#TRACE_DATA]")) {
            if (!inData || traceSamples != samples) return false;
            inData = false;
        } else if (line == QStringLiteral("[#TRACE]")) {
            if (!inTrace || inData || !sawData || traceSamples != samples) return false;
            inTrace = false;
            ++traces;
        } else if (inData) {
            for (const QString &value : line.split(whitespace, Qt::SkipEmptyParts)) {
                bool ok = false;
                const double number = value.toDouble(&ok);
                if (!ok || !std::isfinite(number) || ++traceSamples > samples) return false;
            }
        }
    }
    return traces > 0 && !inTrace && !inData;
}

void captureRequestedView(QApplication &app, TabbedMainWindow &mainWindow)
{
    try {
        const QString path = qEnvironmentVariable("IFCN_UI_SCREENSHOT").trimmed();
        const QString inputPath = qEnvironmentVariable("IFCN_UI_SCREENSHOT_INPUT").trimmed();
        const QString viewName = qEnvironmentVariable("IFCN_UI_SCREENSHOT_VIEW").trimmed();
        const QString sourcePath = qEnvironmentVariable("IFCN_UI_SOURCE_FILE").trimmed();
        const QStringList supportedViews = {"source", "layout", "schematic", "structure", "waveform"};
        if (path.isEmpty() || inputPath.isEmpty() || !supportedViews.contains(viewName)
            || QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0) {
            throw std::runtime_error("Set IFCN_UI_SCREENSHOT=<output.png>, IFCN_UI_SCREENSHOT_INPUT=<input>, and IFCN_UI_SCREENSHOT_VIEW=source|layout|schematic|structure|waveform");
        }
        const QFileInfo inputInfo(inputPath);
        const QString suffix = inputInfo.suffix().toLower();
        const QString inputText = readScreenshotInput(inputPath);
        const QString sourceText = sourcePath.isEmpty() ? QString() : readScreenshotInput(sourcePath);
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            throw std::runtime_error("Cannot create screenshot output directory");
        }

        MainWindow *editor = nullptr;
        QWidget *captureWidget = &mainWindow;
        if (viewName == "waveform") {
            if (suffix != "rst" || !validScreenshotWaveform(inputText)) {
                throw std::runtime_error("Waveform capture requires a complete simulation .rst file");
            }
            auto *waveform = new WaveformWindow(nullptr, inputInfo.absoluteFilePath());
            if (waveform->findChildren<PlotWidget *>().isEmpty()) {
                delete waveform;
                throw std::runtime_error("Waveform input contains no visible signal traces");
            }
            for (PlotWidget *plot : waveform->findChildren<PlotWidget *>()) {
                int labelWidth = 180;
                for (const QString &line : plot->label->text().split('\n')) {
                    labelWidth = qMax(labelWidth, plot->label->fontMetrics().horizontalAdvance(line) + 12);
                }
                plot->label->setFixedWidth(labelWidth);
                plot->customPlot->axisRect()->setMargins(QMargins(5, 36, 5, 5));
                plot->customPlot->replot();
            }
            captureWidget = waveform;
            captureWidget->resize(1600, 1040);
        } else if (viewName == "source") {
            if (suffix != "v") throw std::runtime_error("Source capture requires a Verilog .v file");
            editor = mainWindow.openVerilogSourceInNewTab(inputText, inputInfo.absoluteFilePath());
            captureWidget = editor->verilogSourceDock;
            QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            font.setPointSize(14);
            editor->verilogSourceEditor->setFont(font);
        } else {
            if (suffix != "ifcn" && suffix != "qca") {
                throw std::runtime_error("Layout views require an .ifcn or .qca file");
            }
            editor = mainWindow.openFileInNewTab(inputInfo.absoluteFilePath());
            bool hasCells = false;
            for (const auto &layer : editor->layers) hasCells = hasCells || !layer.isEmpty();
            for (const auto &layer : editor->scene->fastCellsByLayer()) hasCells = hasCells || !layer.isEmpty();
            if (!hasCells) throw std::runtime_error("Screenshot input did not load any circuit cells");
            if (!sourcePath.isEmpty()) {
                editor->setVerilogSourceContent(sourceText, QFileInfo(sourcePath).absoluteFilePath());
            }
            if (viewName == "layout") {
                editor->phaseCodecDock->hide();
                if (editor->circuitSchematicView->scene()->items().isEmpty()) {
                    editor->circuitSchematicDock->hide();
                }
            }
            if (viewName == "schematic") {
                if (editor->circuitSchematicView->scene()->items().isEmpty()) {
                    throw std::runtime_error("Input has no gate-level schematic");
                }
                captureWidget = editor->circuitSchematicDock;
            } else if (viewName == "structure") {
                editor->phaseCodec3DButton->click();
                captureWidget = editor->structure3DDock;
            }
        }

        // Host the actual dock in its own window for a readable, unclipped view,
        // including its native filename/title bar. Only this batch session changes.
        if (captureWidget != &mainWindow && viewName != "waveform") {
            auto *captureWindow = new QMainWindow;
            auto *dock = qobject_cast<QDockWidget *>(captureWidget);
            captureWindow->setWindowTitle(dock->windowTitle());
            captureWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
            captureWidget = captureWindow;
            const int sourceHeight = editor == nullptr ? 360 : qBound(320,
                (editor->verilogSourceEditor->document()->blockCount() + 4)
                    * editor->verilogSourceEditor->fontMetrics().lineSpacing() + 100, 900);
            captureWidget->resize(viewName == "source" ? 1200 : 1400,
                                  viewName == "source" ? sourceHeight : 900);
        }
        captureWidget->show();
        QTimer::singleShot(250, captureWidget, [editor, captureWidget, viewName]() {
            if (editor != nullptr) {
                editor->centerViewOnItems();
                editor->circuitSchematicView->fitToCircuit();
                if (viewName == "structure") editor->structure3DView->fitToStructure();
            }
            captureWidget->update();
        });
        QTimer::singleShot(800, captureWidget, [&app, captureWidget, path]() {
            if (!captureWidget->grab().save(path, "PNG")) {
                qCritical() << "Cannot save screenshot:" << path;
                app.exit(3);
                return;
            }
            qInfo() << "Screenshot saved:" << QFileInfo(path).absoluteFilePath();
            app.exit(0);
        });
    } catch (const std::exception &error) {
        qCritical() << "Screenshot failed:" << error.what();
        app.exit(2);
    }
}
} // namespace

int main(int argc,char *argv[])
{
    if (!consoleLoggingEnabled()) {
        qInstallMessageHandler(silentQtMessageHandler);
        silenceProcessConsole();
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    const QStringList preferredFonts = {
        "Fira Sans",
        "Source Sans 3",
        "Noto Sans"
    };
    QString selectedFont;
    const auto families = QFontDatabase().families();
    for (const auto &family : preferredFonts) {
        if (families.contains(family)) {
            selectedFont = family;
            break;
        }
    }
    if (!selectedFont.isEmpty()) {
        QFont appFont(selectedFont, 10);
        app.setFont(appFont);
    }

    app.setStyleSheet(engineeringStyleSheet());

    const bool captureViewRequested = qEnvironmentVariableIsSet("IFCN_UI_SCREENSHOT_INPUT")
        || qEnvironmentVariableIsSet("IFCN_UI_SCREENSHOT_VIEW");
    if (captureViewRequested) qputenv("IFCN_NONINTERACTIVE", QByteArrayLiteral("1"));
    TabbedMainWindow mainWindow;
    if (captureViewRequested) {
        for (MainWindow *editor : mainWindow.findChildren<MainWindow *>()) editor->disableStartupRestore();
        mainWindow.resize(1600, 900);
        mainWindow.show();
        QTimer::singleShot(0, &mainWindow, [&app, &mainWindow]() {
            captureRequestedView(app, mainWindow);
        });
        return app.exec();
    }
    const QString screenshotPath = qEnvironmentVariable("IFCN_UI_SCREENSHOT").trimmed();
    const auto environmentPath = [](const char *name, const char *legacyName) {
        const QString preferred = qEnvironmentVariable(name).trimmed();
        return preferred.isEmpty()
            ? qEnvironmentVariable(legacyName).trimmed() : preferred;
    };
    QString automaticMappingPath =
        qEnvironmentVariable("IFCN_AUTO_MAP_FILE").trimmed();
    const QString automaticExportPath = environmentPath(
        "IFCN_AUTO_EXPORT_CELL_LAYOUT", "IFCN_EXPORT_CELL_LAYOUT");
    const QString automaticStructure3DExportPath = environmentPath(
        "IFCN_AUTO_EXPORT_3D_LAYOUT", "IFCN_EXPORT_3D_STRUCTURE");
    const QString automaticSchematicExportPath = environmentPath(
        "IFCN_AUTO_EXPORT_CIRCUIT_SCHEMATIC", "IFCN_EXPORT_CIRCUIT_SCHEMATIC");
    const QString automaticGraphRenderPath = environmentPath(
        "IFCN_AUTO_GRAPH_RENDER_FILE", "IFCN_COMPACT_GRAPH_INPUT");
    QStringList inputPaths;
    const QStringList commandLine = app.arguments();
    for (int index = 1; index < commandLine.size(); ++index) {
        const QFileInfo input(commandLine[index]);
        const QString suffix = input.suffix().toLower();
        if (input.isFile()
            && (suffix == QStringLiteral("ifcn") || suffix == QStringLiteral("qca"))) {
            inputPaths.append(input.absoluteFilePath());
        }
    }
    if (automaticMappingPath.isEmpty() && !inputPaths.isEmpty()
        && (!automaticExportPath.isEmpty()
            || !automaticStructure3DExportPath.isEmpty()
            || !automaticSchematicExportPath.isEmpty())) {
        automaticMappingPath = inputPaths.last();
    }
    const bool automaticIoContraction =
        qEnvironmentVariableIntValue("IFCN_AUTO_CONTRACT_IO") != 0;
    const bool automaticIoRestore =
        qEnvironmentVariableIntValue("IFCN_AUTO_RESTORE_IO") != 0;
    const bool automaticExportRequested =
        !automaticMappingPath.isEmpty()
        || !automaticExportPath.isEmpty()
        || !automaticStructure3DExportPath.isEmpty()
        || !automaticSchematicExportPath.isEmpty();
    const bool automaticGraphRenderRequested =
        !automaticGraphRenderPath.isEmpty();
    if (automaticExportRequested || automaticGraphRenderRequested) {
        qputenv("IFCN_NONINTERACTIVE", QByteArrayLiteral("1"));
    }
    if (!screenshotPath.isEmpty() || automaticExportRequested
        || automaticGraphRenderRequested) {
        mainWindow.resize(1600, 900);
    }
    mainWindow.show();
    if (!automaticExportRequested && !automaticGraphRenderRequested) {
        for (const QString &inputPath : inputPaths) {
            mainWindow.openFileInNewTab(inputPath);
        }
    }

    // Non-interactive production Compact Graph run for CI and reproducible
    // paper assets.  The handler itself saves and maps the generated IFCN,
    // LaTeX, and cell-level SVG artifacts.
    if (automaticGraphRenderRequested) {
        qputenv("IFCN_COMPACT_GRAPH_BATCH", QByteArrayLiteral("1"));
        qputenv("IFCN_NONINTERACTIVE", QByteArrayLiteral("1"));
        QTimer::singleShot(0, &mainWindow,
            [&app, &mainWindow, automaticGraphRenderPath]() {
                const QFileInfo sourceInfo(automaticGraphRenderPath);
                if (!sourceInfo.isFile()
                    || sourceInfo.suffix().compare(
                           QStringLiteral("v"), Qt::CaseInsensitive) != 0) {
                    qCritical() << "Cannot load Verilog source file:"
                                << automaticGraphRenderPath;
                    app.exit(2);
                    return;
                }

                QFile sourceFile(sourceInfo.absoluteFilePath());
                if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    qCritical() << "Failed to read Verilog source:"
                                << sourceInfo.absoluteFilePath();
                    app.exit(3);
                    return;
                }
                const QString sourceText =
                    QString::fromUtf8(sourceFile.readAll());
                MainWindow *editor = mainWindow.openVerilogSourceInNewTab(
                    sourceText, sourceInfo.absoluteFilePath());
                if (editor == nullptr) {
                    qCritical() << "Failed to open Verilog source:"
                                << sourceInfo.absoluteFilePath();
                    app.exit(3);
                    return;
                }

                VerilogHandler *handler = editor->graphLayoutHandler();
                QObject::connect(
                    handler,
                    &VerilogHandler::operationFinished,
                    &app,
                    [&app](const QString &message) {
                        qInfo() << message;
                        app.exit(0);
                    });
                QObject::connect(
                    handler,
                    &VerilogHandler::operationFailed,
                    &app,
                    [&app](const QString &message) {
                        qCritical() << message;
                        app.exit(3);
                    });
                handler->runGraphRenderForFile(sourceInfo.absoluteFilePath());
            });
    }

    // Non-interactive cell-layout export for offscreen CI and reproducible figures.
    // Reuse the normal load/export paths; command-line input and legacy
    // export variable names remain supported for existing automation.
    if (automaticExportRequested && !automaticGraphRenderRequested) {
        QTimer::singleShot(0, &mainWindow,
            [&app, &mainWindow, automaticMappingPath, automaticExportPath,
             automaticStructure3DExportPath, automaticSchematicExportPath,
             automaticIoContraction, automaticIoRestore, screenshotPath]() {
                if (automaticMappingPath.isEmpty()
                    || (automaticExportPath.isEmpty()
                        && automaticStructure3DExportPath.isEmpty()
                        && automaticSchematicExportPath.isEmpty())) {
                    qCritical() << "IFCN_AUTO_MAP_FILE and at least one automatic export path must be set together.";
                    app.exit(2);
                    return;
                }

                const QFileInfo inputInfo(automaticMappingPath);
                const QString inputSuffix = inputInfo.suffix().toLower();
                if (!inputInfo.isFile() ||
                    (inputSuffix != QStringLiteral("ifcn") &&
                     inputSuffix != QStringLiteral("qca"))) {
                    qCritical() << "Cannot load cell-level source file:" << automaticMappingPath;
                    app.exit(2);
                    return;
                }

                const auto validateExportPath = [&app](const QString &path,
                                                       const QString &label) {
                    if (path.isEmpty()) {
                        return true;
                    }
                    const QFileInfo outputInfo(path);
                    const QString outputSuffix = outputInfo.suffix().toLower();
                    if (outputSuffix != QStringLiteral("pdf")
                        && outputSuffix != QStringLiteral("svg")) {
                        qCritical() << label << "export path must end in .pdf or .svg:"
                                    << path;
                        app.exit(2);
                        return false;
                    }
                    if (!QDir().mkpath(outputInfo.absolutePath())) {
                        qCritical() << "Cannot create" << label << "export directory:"
                                    << outputInfo.absolutePath();
                        app.exit(2);
                        return false;
                    }
                    return true;
                };
                if (!validateExportPath(automaticExportPath,
                                        QStringLiteral("Cell-level"))
                    || !validateExportPath(automaticStructure3DExportPath,
                                           QStringLiteral("3D structure"))
                    || !validateExportPath(automaticSchematicExportPath,
                                           QStringLiteral("Circuit schematic"))) {
                    return;
                }

                MainWindow *editor = mainWindow.openFileInNewTab(
                    inputInfo.absoluteFilePath(), false);
                if (editor == nullptr) {
                    qCritical() << "Failed to load cell-level layout:" << automaticMappingPath;
                    app.exit(3);
                    return;
                }
                if (automaticIoContraction) {
                    // Exercise the same control path as a toolbar click so CI
                    // also covers action/checkbox state rollback on no-change
                    // layouts.
                    editor->ioContractionCheckBox->setChecked(true);
                    if (!editor->ioContractionCheckBox->isChecked()) {
                        qCritical() << "IO contraction made no change for:" << automaticMappingPath;
                        app.exit(3);
                        return;
                    }
                }
                if (automaticIoContraction && automaticIoRestore) {
                    editor->ioContractionCheckBox->setChecked(false);
                }
                if (!automaticExportPath.isEmpty()) {
                    const QFileInfo outputInfo(automaticExportPath);
                    if (!editor->exportCellLevelLayout(outputInfo.absoluteFilePath())) {
                        qCritical() << "Failed to export mapped cell-level layout to:"
                                    << outputInfo.absoluteFilePath();
                        app.exit(3);
                        return;
                    }
                }
                if (!automaticStructure3DExportPath.isEmpty()) {
                    const QFileInfo structureInfo(automaticStructure3DExportPath);
                    if (!editor->exportStructure3DLayout(
                            structureInfo.absoluteFilePath())) {
                        qCritical() << "Failed to export 3D encoded structure to:"
                                    << structureInfo.absoluteFilePath();
                        app.exit(3);
                        return;
                    }
                }
                if (!automaticSchematicExportPath.isEmpty()) {
                    if (!editor->saveCircuitSchematicGraphic(automaticSchematicExportPath)) {
                        qCritical() << "Failed to export circuit schematic to:"
                                    << automaticSchematicExportPath;
                        app.exit(3);
                        return;
                    }
                }
                if (!screenshotPath.isEmpty()) {
                    const QFileInfo screenshotInfo(screenshotPath);
                    QDir().mkpath(screenshotInfo.absolutePath());
                    QCoreApplication::processEvents();
                    mainWindow.grab().save(screenshotInfo.absoluteFilePath());
                }

                if (!automaticExportPath.isEmpty()) {
                    qInfo() << "Cell-level layout exported to:"
                            << QFileInfo(automaticExportPath).absoluteFilePath();
                }
                if (!automaticStructure3DExportPath.isEmpty()) {
                    qInfo() << "3D encoded structure exported to:"
                            << QFileInfo(automaticStructure3DExportPath).absoluteFilePath();
                }
                app.exit(0);
            });
    }

    // Opt-in visual smoke hook for offscreen CI and local theme review.
    if (!screenshotPath.isEmpty() && !automaticExportRequested
        && !automaticGraphRenderRequested) {
        QTimer::singleShot(800, &mainWindow, [&app, &mainWindow, screenshotPath]() {
            const QFileInfo target(screenshotPath);
            QDir().mkpath(target.absolutePath());
            mainWindow.grab().save(screenshotPath);
            app.quit();
        });
    }

    return app.exec();
}
