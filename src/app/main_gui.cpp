#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QTimer>
#include <QtGlobal>
#include "controllers/VerilogHandler.h"
#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/TabbedMainWindow.h"

namespace {
bool consoleLoggingEnabled()
{
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

    TabbedMainWindow mainWindow;
    const QString screenshotPath = qEnvironmentVariable("IFCN_UI_SCREENSHOT").trimmed();
    const QString cellLayoutPath = qEnvironmentVariable("IFCN_EXPORT_CELL_LAYOUT").trimmed();
    const QString circuitPath = qEnvironmentVariable("IFCN_EXPORT_CIRCUIT_SCHEMATIC").trimmed();
    const QString structure3DPath = qEnvironmentVariable("IFCN_EXPORT_3D_STRUCTURE").trimmed();
    const QString compactGraphInput = qEnvironmentVariable("IFCN_COMPACT_GRAPH_INPUT").trimmed();
    const bool batchGraphicExport = !cellLayoutPath.isEmpty()
        || !circuitPath.isEmpty()
        || !structure3DPath.isEmpty();
    if (!screenshotPath.isEmpty() || batchGraphicExport) {
        mainWindow.resize(1600, 900);
    }
    mainWindow.show();

    // Opening an IFCN/QCA path from the command line makes the desktop binary
    // directly testable and is also convenient for generated cell layouts.
    const QStringList commandLine = app.arguments();
    MainWindow *openedEditor = nullptr;
    for (int index = 1; index < commandLine.size(); ++index) {
        const QFileInfo input(commandLine[index]);
        const QString suffix = input.suffix().toLower();
        if (input.isFile()
            && (suffix == QStringLiteral("ifcn") || suffix == QStringLiteral("qca"))) {
            openedEditor = mainWindow.openFileInNewTab(input.absoluteFilePath(), batchGraphicExport);
        }
    }

    // Opt-in visual smoke hook for offscreen CI and local theme review.
    if (!compactGraphInput.isEmpty()) {
        const QFileInfo input(compactGraphInput);
        if (!input.isFile()) {
            qCritical("Compact Graph P&R input does not exist: %s", qPrintable(compactGraphInput));
            return 2;
        }

        qputenv("IFCN_COMPACT_GRAPH_BATCH", QByteArrayLiteral("1"));
        qputenv("IFCN_NONINTERACTIVE", QByteArrayLiteral("1"));

        MainWindow *batchEditor = mainWindow.openVerilogSourceInNewTab(QString(), input.absoluteFilePath());
        QObject::connect(batchEditor->layoutHandler(), &VerilogHandler::operationFinished,
                         &app, [&app](const QString &message) {
            qInfo("%s", qPrintable(message));
            app.exit(0);
        });
        QObject::connect(batchEditor->layoutHandler(), &VerilogHandler::operationFailed,
                         &app, [&app](const QString &message) {
            qCritical("%s", qPrintable(message));
            app.exit(2);
        });
        QTimer::singleShot(0, batchEditor, [batchEditor, input]() {
            batchEditor->layoutHandler()->runGraphRenderForFile(input.absoluteFilePath());
        });
    } else if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(800, &mainWindow, [&app, &mainWindow, screenshotPath]() {
            const QFileInfo target(screenshotPath);
            QDir().mkpath(target.absolutePath());
            mainWindow.grab().save(screenshotPath);
            app.quit();
        });
    } else if (batchGraphicExport) {
        QTimer::singleShot(1000, &mainWindow,
                           [&app, openedEditor, cellLayoutPath, circuitPath, structure3DPath]() {
            if (openedEditor != nullptr) {
                if (!cellLayoutPath.isEmpty()) {
                    const QFileInfo target(cellLayoutPath);
                    QDir().mkpath(target.absolutePath());
                    openedEditor->saveCellLevelLayoutGraphic(cellLayoutPath);
                }
                if (!circuitPath.isEmpty()) {
                    const QFileInfo target(circuitPath);
                    QDir().mkpath(target.absolutePath());
                    openedEditor->saveCircuitSchematicGraphic(circuitPath);
                }
                if (!structure3DPath.isEmpty()) {
                    const QFileInfo target(structure3DPath);
                    QDir().mkpath(target.absolutePath());
                    openedEditor->saveStructure3DGraphic(structure3DPath);
                }
            }
            app.quit();
        });
    }

    return app.exec();
}
