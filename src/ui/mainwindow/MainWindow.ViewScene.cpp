#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/TabbedMainWindow.h"
#include "ui/widgets/CircuitSchematicView.h"
#include "ui/widgets/LayeredStructure3DView.h"
#include <autopr/algorithms/phase_codec.h>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDir>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSvgGenerator>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>

namespace {
QString dockChromeStyle(const QString &objectName)
{
    return QStringLiteral(
        "QDockWidget#%1 {"
        "  background: #e1e5ea;"
        "  color: #1f252d;"
        "  font-weight: 600;"
        "}"
        "QDockWidget#%1::title {"
        "  background: #e6eaef;"
        "  border-bottom: 1px solid #b9c1cc;"
        "  padding: 5px 8px;"
        "  text-align: left;"
        "}"
    ).arg(objectName);
}

QToolButton *createDockToolButton(QWidget *parent,
                                  const QString &text,
                                  const QString &toolTip,
                                  int minWidth = 48)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setMinimumSize(minWidth, 28);
    return button;
}

void polishToolBoxTitleButtons(QToolBox *toolBox,
                               const QString &standardLibraryTitle,
                               const QString &clockSchemeTitle)
{
    if (toolBox == nullptr) {
        return;
    }

    QFont titleFont = toolBox->font();
    titleFont.setPointSize(10);
    titleFont.setWeight(QFont::DemiBold);

    const QString titleStyle = QStringLiteral(
        "QAbstractButton {"
        "  background: #eef1f4;"
        "  border: 1px solid #b8c0ca;"
        "  color: #1f252d;"
        "  padding: 8px 10px;"
        "  text-align: left;"
        "}"
        "QAbstractButton:hover {"
        "  background: #e5ebf1;"
        "}"
        "QAbstractButton:checked {"
        "  background: #dce6f2;"
        "  border-color: #7f9fbd;"
        "}");

    const QList<QAbstractButton *> buttons = toolBox->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (button == nullptr) {
            continue;
        }
        const QString title = button->text();
        if (title != standardLibraryTitle && title != clockSchemeTitle) {
            continue;
        }

        button->setFont(titleFont);
        button->setMinimumHeight(42);
        button->setMaximumHeight(42);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(titleStyle);
    }
}

class VerilogSourceEditor : public QPlainTextEdit
{
public:
    explicit VerilogSourceEditor(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if ((event->modifiers() & Qt::ControlModifier) != 0) {
            if (event->angleDelta().y() > 0) {
                zoomIn(1);
            } else if (event->angleDelta().y() < 0) {
                zoomOut(1);
            }
            event->accept();
            return;
        }
        QPlainTextEdit::wheelEvent(event);
    }
};
} // namespace

void MainWindow::createViewAndScene()
{
    // layout
    centralWidget = new QWidget(this);  
    verticalLayout = new QVBoxLayout(centralWidget);
    splitter = new QSplitter(centralWidget);
    splitter->setOrientation(Qt::Horizontal);
    toolBox = new QToolBox(splitter);
    toolBox->setMinimumWidth(176);
    splitter->setHandleWidth(5);

    scene = new QCADScene(this); 
    scene->setSceneRect(QRectF(0,0,SCENE_WIDTH, SCENE_HEIGHT));
    // scene->setBackgroundBrush(Qt::black);
    view = new QCADView(this);
    view->setScene(scene);
    view->resize(800,600);

    splitter->addWidget(toolBox);
    splitter->addWidget(view);
    verticalLayout->setContentsMargins(6, 6, 6, 6);
    verticalLayout->setSpacing(6);
    verticalLayout->addWidget(splitter, 1);
    this->setCentralWidget(centralWidget);
    createVerilogSourceDock();
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes(QList<int>() << 184 << 320 << 1120);
    createCircuitSchematicDock();
    createPhaseCodecDock();
    createLayoutInfoDock();
    createStructure3DDock();
    view->centerOn(scene->sceneRect().center());

    customStatusBar = new CustomStatusBar(this);
    verticalLayout->addWidget(customStatusBar, 0);

}

QWidget* MainWindow::createPhaseCodecPanel()
{
    QWidget *panel = new QWidget;
    panel->setMinimumWidth(300);
    panel->setObjectName(QStringLiteral("phaseCodecPanel"));

    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    auto *toolbarLayout = new QVBoxLayout;
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(5);

    auto *closeButton = createDockToolButton(panel,
                                             QStringLiteral("×"),
                                             tr("Close Phase Codec"),
                                             32);

    auto *dockButton = createDockToolButton(panel,
                                            QStringLiteral("↙"),
                                            tr("Dock Phase Codec back into the main window"),
                                            32);

    auto *floatButton = createDockToolButton(panel,
                                             QStringLiteral("↗"),
                                             tr("Float Phase Codec as a standalone window"),
                                             32);

    phaseCodecEncodeButton = createDockToolButton(panel,
                                                  tr("Encode"),
                                                  tr("Encode clock regions"),
                                                  58);
    phaseCodecEncodeButton->setObjectName(QStringLiteral("phaseCodecEncodeButton"));

    phaseCodecCancelButton = createDockToolButton(panel,
                                                  tr("Clear"),
                                                  tr("Clear phase encoding preview"),
                                                  50);
    phaseCodecCancelButton->setObjectName(QStringLiteral("phaseCodecCancelButton"));

    phaseCodecShowAllButton = createDockToolButton(panel,
                                                   tr("All"),
                                                   tr("Show all encoded tiles on the cell-level layout"),
                                                   42);
    phaseCodecShowAllButton->setObjectName(QStringLiteral("phaseCodecShowAllButton"));
    phaseCodecShowAllButton->setCheckable(true);

    phaseCodec3DButton = createDockToolButton(panel,
                                              tr("3D"),
                                              tr("Show layered 3D clock and cell structure"),
                                              42);
    phaseCodec3DButton->setObjectName(QStringLiteral("phaseCodec3DButton"));

    phaseCodecModeComboBox = new QComboBox(panel);
    phaseCodecModeComboBox->setMinimumHeight(30);
    phaseCodecModeComboBox->setMinimumWidth(120);
    phaseCodecModeComboBox->addItem(tr("Auto phase"), 0);
    phaseCodecModeComboBox->addItem(tr("4-phase / 4x4"), 4);
    phaseCodecModeComboBox->addItem(tr("3-phase / 3x3"), 3);

    auto *phaseNavigationLayout = new QHBoxLayout;
    phaseNavigationLayout->setContentsMargins(0, 0, 0, 0);
    phaseNavigationLayout->setSpacing(6);
    phaseNavigationLayout->addWidget(closeButton);
    phaseNavigationLayout->addWidget(dockButton);
    phaseNavigationLayout->addWidget(floatButton);
    phaseNavigationLayout->addStretch(1);
    phaseNavigationLayout->addWidget(phaseCodecModeComboBox);

    auto *phaseActionsLayout = new QHBoxLayout;
    phaseActionsLayout->setContentsMargins(0, 0, 0, 0);
    phaseActionsLayout->setSpacing(6);
    phaseActionsLayout->addWidget(phaseCodecEncodeButton);
    phaseActionsLayout->addWidget(phaseCodecCancelButton);
    phaseActionsLayout->addWidget(phaseCodecShowAllButton);
    phaseActionsLayout->addWidget(phaseCodec3DButton);
    phaseActionsLayout->addStretch(1);
    toolbarLayout->addLayout(phaseNavigationLayout);
    toolbarLayout->addLayout(phaseActionsLayout);

    phaseCodecTable = new QTableWidget(panel);
    phaseCodecTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    phaseCodecTable->setSelectionMode(QAbstractItemView::SingleSelection);
    phaseCodecTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    phaseCodecTable->setFocusPolicy(Qt::NoFocus);
    phaseCodecTable->setShowGrid(false);
    phaseCodecTable->setWordWrap(true);
    phaseCodecTable->setAlternatingRowColors(true);
    phaseCodecTable->setCornerButtonEnabled(false);
    phaseCodecTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    phaseCodecTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    phaseCodecTable->horizontalHeader()->setDefaultSectionSize(112);
    phaseCodecTable->verticalHeader()->setDefaultSectionSize(96);
    phaseCodecTable->horizontalHeader()->setMinimumSectionSize(88);
    phaseCodecTable->verticalHeader()->setMinimumSectionSize(72);

    phaseCodecStatusLabel = new QLabel(tr("Not encoded"), panel);
    phaseCodecStatusLabel->setObjectName(QStringLiteral("phaseCodecStatusLabel"));
    phaseCodecStatusLabel->setWordWrap(true);

    layout->addLayout(toolbarLayout);
    layout->addWidget(phaseCodecTable, 1);
    layout->addWidget(phaseCodecStatusLabel);

    connect(closeButton, &QToolButton::clicked, this, [this]() {
        closeDockContent(phaseCodecDock, &phaseCodecFloatWindow);
    });
    connect(dockButton, &QToolButton::clicked, this, [this]() {
        restoreDockContent(phaseCodecDock, &phaseCodecFloatWindow);
    });
    connect(floatButton, &QToolButton::clicked, this, [this]() {
        floatDockContent(phaseCodecDock,
                         &phaseCodecFloatWindow,
                         tr("Phase Codec"),
                         QSize(720, 560));
    });
    connect(phaseCodecEncodeButton, &QToolButton::clicked,
            this, &MainWindow::slotEncodeClockRegions);
    connect(phaseCodecCancelButton, &QToolButton::clicked,
            this, &MainWindow::slotCancelPhaseCodecEncoding);
    connect(phaseCodecModeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::slotPhaseCodecModeChanged);
    connect(phaseCodecShowAllButton, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            if (!phaseCodecPreviewActive) {
                phaseCodecPreviewActive = true;
                updatePhaseCodecPreview();
                return;
            }
            highlightAllPhaseCodecTiles();
        } else {
            clearPhaseCodecHighlight();
        }
    });
    connect(phaseCodec3DButton, &QToolButton::clicked,
            this, &MainWindow::showStructure3DView);
    connect(phaseCodecTable, &QTableWidget::cellClicked,
            this, &MainWindow::slotPhaseCodecTileActivated);

    panel->setStyleSheet(QStringLiteral(
        "QWidget#phaseCodecPanel {"
        "  background: #f3f5f7;"
        "  border: 0;"
        "}"
        "QLabel#phaseCodecStatusLabel {"
        "  color: #606b78;"
        "}"
        "QComboBox {"
        "  background: #ffffff;"
        "  border: 1px solid #b9c1cc;"
        "  border-radius: 3px;"
        "  padding: 4px 8px;"
        "  color: #1f252d;"
        "}"
        "QToolButton {"
        "  background: #ffffff;"
        "  border: 1px solid #b9c1cc;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: #e8edf3;"
        "}"
        "QToolButton:checked {"
        "  background: #dbe8f6;"
        "  border-color: #4f7da5;"
        "}"
        "QToolButton#phaseCodecEncodeButton {"
        "  background: #2f5f8f;"
        "  border: 0;"
        "  border-radius: 3px;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "}"
        "QToolButton#phaseCodecEncodeButton:hover {"
        "  background: #2a547f;"
        "}"
        "QToolButton#phaseCodecEncodeButton:pressed {"
        "  background: #234866;"
        "}"
        "QToolButton#phaseCodec3DButton {"
        "  background: #2f3742;"
        "  border: 0;"
        "  border-radius: 3px;"
        "  color: #ffffff;"
        "  font-weight: 700;"
        "}"
        "QToolButton#phaseCodec3DButton:hover {"
        "  background: #3d4653;"
        "}"
        "QTableWidget {"
        "  background: #ffffff;"
        "  alternate-background-color: #f8fafc;"
        "  border: 1px solid #c6ccd4;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "}"
        "QTableWidget::item {"
        "  border-bottom: 1px solid #eef2f6;"
        "  border-right: 1px solid #eef2f6;"
        "  padding: 6px;"
        "}"
        "QTableWidget::item:selected {"
        "  background: #dbe8f6;"
        "  color: #0b3558;"
        "}"
        "QHeaderView::section {"
        "  background: #edf2f7;"
        "  border: 0;"
        "  color: #4d5865;"
        "  padding: 5px;"
        "}"
    ));

    return panel;
}

void MainWindow::floatDockContent(QDockWidget *dock,
                                  QDialog **floatWindowPtr,
                                  const QString &title,
                                  const QSize &size)
{
    if (dock == nullptr || floatWindowPtr == nullptr) {
        return;
    }

    if (*floatWindowPtr != nullptr) {
        (*floatWindowPtr)->show();
        (*floatWindowPtr)->raise();
        (*floatWindowPtr)->activateWindow();
        return;
    }

    QWidget *content = dock->widget();
    if (content == nullptr) {
        return;
    }

    content->setParent(nullptr);
    dock->setWidget(nullptr);
    dock->hide();

    auto *dialog = new QDialog(this);
    *floatWindowPtr = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    dialog->setWindowTitle(title);
    dialog->setWindowFlags(Qt::Window |
                           Qt::WindowTitleHint |
                           Qt::WindowSystemMenuHint |
                           Qt::WindowMinMaxButtonsHint |
                           Qt::WindowCloseButtonHint);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content);

    connect(dialog, &QDialog::finished, this, [this, dock, floatWindowPtr](int) {
        closeDockContent(dock, floatWindowPtr);
    });

    dialog->resize(size);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::restoreDockContent(QDockWidget *dock, QDialog **floatWindowPtr)
{
    if (dock == nullptr || floatWindowPtr == nullptr) {
        return;
    }

    QDialog *dialog = *floatWindowPtr;
    if (dialog != nullptr) {
        QWidget *content = nullptr;
        if (dialog->layout() != nullptr && dialog->layout()->count() > 0) {
            content = dialog->layout()->itemAt(0)->widget();
        }

        if (content != nullptr) {
            dialog->layout()->removeWidget(content);
            content->setParent(nullptr);
            dock->setWidget(content);
        }

        *floatWindowPtr = nullptr;
        dialog->hide();
        dialog->deleteLater();
    }

    dock->show();
    dock->raise();
}

void MainWindow::closeDockContent(QDockWidget *dock, QDialog **floatWindowPtr)
{
    if (dock == nullptr || floatWindowPtr == nullptr) {
        return;
    }

    QDialog *dialog = *floatWindowPtr;
    if (dialog != nullptr) {
        QWidget *content = nullptr;
        if (dialog->layout() != nullptr && dialog->layout()->count() > 0) {
            content = dialog->layout()->itemAt(0)->widget();
        }

        if (content != nullptr) {
            dialog->layout()->removeWidget(content);
            content->setParent(nullptr);
            dock->setWidget(content);
        }

        *floatWindowPtr = nullptr;
        dialog->hide();
        dialog->deleteLater();
    }

    dock->hide();
}

QWidget* MainWindow::createLayoutInfoPanel()
{
    QWidget *panel = new QWidget;
    panel->setMinimumWidth(300);
    panel->setObjectName(QStringLiteral("layoutInfoPanel"));

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto *closeButton = createDockToolButton(panel,
                                             QStringLiteral("×"),
                                             tr("Close Layout Info"),
                                             32);
    auto *dockButton = createDockToolButton(panel,
                                            QStringLiteral("↙"),
                                            tr("Dock Layout Info back into the main window"),
                                            32);
    auto *floatButton = createDockToolButton(panel,
                                             QStringLiteral("↗"),
                                             tr("Float Layout Info as a standalone window"),
                                             32);

    toolbarLayout->addWidget(closeButton);
    toolbarLayout->addWidget(dockButton);
    toolbarLayout->addWidget(floatButton);
    toolbarLayout->addStretch(1);

    layoutInfoTable = new QTableWidget(panel);
    layoutInfoTable->setColumnCount(2);
    layoutInfoTable->setHorizontalHeaderLabels(QStringList() << tr("Metric") << tr("Value"));
    layoutInfoTable->verticalHeader()->hide();
    layoutInfoTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layoutInfoTable->setSelectionMode(QAbstractItemView::NoSelection);
    layoutInfoTable->setFocusPolicy(Qt::NoFocus);
    layoutInfoTable->setShowGrid(false);
    layoutInfoTable->setWordWrap(true);
    layoutInfoTable->setAlternatingRowColors(true);
    layoutInfoTable->setCornerButtonEnabled(false);
    layoutInfoTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layoutInfoTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layoutInfoTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layoutInfoTable->setMinimumHeight(180);

    layout->addLayout(toolbarLayout);
    layout->addWidget(layoutInfoTable, 1);

    connect(closeButton, &QToolButton::clicked, this, [this]() {
        closeDockContent(layoutInfoDock, &layoutInfoFloatWindow);
    });
    connect(dockButton, &QToolButton::clicked, this, [this]() {
        restoreDockContent(layoutInfoDock, &layoutInfoFloatWindow);
    });
    connect(floatButton, &QToolButton::clicked, this, [this]() {
        floatDockContent(layoutInfoDock,
                         &layoutInfoFloatWindow,
                         tr("Layout Info"),
                         QSize(560, 460));
    });

    panel->setStyleSheet(QStringLiteral(
        "QWidget#layoutInfoPanel {"
        "  background: #f3f5f7;"
        "  border: 0;"
        "}"
        "QToolButton {"
        "  background: #ffffff;"
        "  border: 1px solid #b9c1cc;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: #e8edf3;"
        "}"
        "QTableWidget {"
        "  background: #ffffff;"
        "  alternate-background-color: #f8fafc;"
        "  border: 1px solid #c6ccd4;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "}"
        "QTableWidget::item {"
        "  border-bottom: 1px solid #eef2f6;"
        "  border-right: 1px solid #eef2f6;"
        "  padding: 6px;"
        "}"
        "QHeaderView::section {"
        "  background: #edf2f7;"
        "  border: 0;"
        "  color: #4d5865;"
        "  padding: 5px;"
        "}"
    ));

    return panel;
}

void MainWindow::createVerilogSourceDock()
{
    verilogSourceDock = new QDockWidget(tr("Verilog Source"), splitter);
    verilogSourceDock->setObjectName(QStringLiteral("verilogSourceDock"));
    verilogSourceDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                       Qt::RightDockWidgetArea |
                                       Qt::BottomDockWidgetArea);
    verilogSourceDock->setFeatures(QDockWidget::DockWidgetClosable);
    verilogSourceDock->setMinimumWidth(280);

    auto *content = new QWidget(verilogSourceDock);
    content->setObjectName(QStringLiteral("verilogSourceDockContent"));
    content->setMinimumSize(280, 280);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto *closeButton = createDockToolButton(content,
                                             QStringLiteral("×"),
                                             tr("Close the Verilog source dock"),
                                             32);
    auto *dockButton = createDockToolButton(content,
                                            QStringLiteral("↙"),
                                            tr("Dock the Verilog source view back into the main window"),
                                            32);
    auto *floatButton = createDockToolButton(content,
                                             QStringLiteral("↗"),
                                             tr("Float the Verilog source view as a standalone window"),
                                             32);
    auto *generateButton = createDockToolButton(content,
                                                tr("Generate"),
                                                tr("Generate a layout from the current Verilog source"),
                                                90);
    generateButton->setObjectName(QStringLiteral("verilogGenerateButton"));

    toolbarLayout->addWidget(closeButton);
    toolbarLayout->addWidget(dockButton);
    toolbarLayout->addWidget(floatButton);
    toolbarLayout->addSpacing(8);
    toolbarLayout->addWidget(generateButton);
    toolbarLayout->addStretch(1);

    verilogSourceEditor = new VerilogSourceEditor(content);
    verilogSourceEditor->setObjectName(QStringLiteral("verilogSourceEditor"));
    verilogSourceEditor->setReadOnly(false);
    verilogSourceEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    verilogSourceEditor->setUndoRedoEnabled(true);
    verilogSourceEditor->setAcceptDrops(true);
    verilogSourceEditor->setToolTip(tr("Edit Verilog source here. Use Ctrl + mouse wheel to zoom text."));
    verilogSourceEditor->setPlaceholderText(
        tr("Open a Verilog .v file, paste source here, then choose Generate."));
    QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codeFont.setPointSize(9);
    verilogSourceEditor->setFont(codeFont);

    layout->addLayout(toolbarLayout);
    layout->addWidget(verilogSourceEditor, 1);
    verilogSourceDock->setWidget(content);

    connect(closeButton, &QToolButton::clicked, this, [this]() {
        closeDockContent(verilogSourceDock, &verilogSourceFloatWindow);
    });
    connect(dockButton, &QToolButton::clicked, this, [this]() {
        restoreDockContent(verilogSourceDock, &verilogSourceFloatWindow);
    });
    connect(floatButton, &QToolButton::clicked, this, [this]() {
        const QString title = verilogSourceDock != nullptr
            ? verilogSourceDock->windowTitle()
            : tr("Verilog Source");
        floatDockContent(verilogSourceDock,
                         &verilogSourceFloatWindow,
                         title,
                         QSize(640, 620));
    });
    connect(generateButton, &QToolButton::clicked,
            this, &MainWindow::slotGenerateFromVerilogSource);

    verilogSourceDock->setStyleSheet(dockChromeStyle(QStringLiteral("verilogSourceDock")) +
        QStringLiteral(
        "QWidget#verilogSourceDockContent {"
        "  background: #f3f5f7;"
        "}"
        "QPlainTextEdit#verilogSourceEditor {"
        "  background: #ffffff;"
        "  border: 1px solid #c6ccd4;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "  padding: 8px;"
        "  selection-background-color: #cddff2;"
        "}"
        "QToolButton {"
        "  background: #ffffff;"
        "  border: 1px solid #b9c1cc;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: #e8edf3;"
        "}"
        "QToolButton#verilogGenerateButton {"
        "  background: #2f5f8f;"
        "  border: 0;"
        "  color: #ffffff;"
        "}"
        "QToolButton#verilogGenerateButton:hover {"
        "  background: #386c9f;"
        "}"
    ));

    if (splitter != nullptr) {
        splitter->insertWidget(1, verilogSourceDock);
    }
}

void MainWindow::updateVerilogSourceFile(const QString &fileName)
{
    if (verilogSourceEditor == nullptr) {
        return;
    }

    verilogSourceFilePath = fileName;
    QFile file(fileName);
    const QFileInfo info(fileName);
    const QString title = info.fileName().isEmpty()
        ? tr("Verilog Source")
        : tr("Verilog Source - %1").arg(info.fileName());

    if (verilogSourceDock != nullptr) {
        verilogSourceDock->setWindowTitle(title);
    }
    if (verilogSourceFloatWindow != nullptr) {
        verilogSourceFloatWindow->setWindowTitle(title);
        verilogSourceFloatWindow->raise();
    } else if (verilogSourceDock != nullptr) {
        verilogSourceDock->show();
        verilogSourceDock->raise();
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        verilogSourceEditor->setPlainText(tr("Cannot open %1\n\n%2")
                                              .arg(QDir::toNativeSeparators(fileName),
                                                   file.errorString()));
        return;
    }

    QTextStream in(&file);
    verilogSourceEditor->setPlainText(in.readAll());
    verilogSourceEditor->document()->setModified(false);
    verilogSourceEditor->moveCursor(QTextCursor::Start);
}

void MainWindow::setVerilogSourceContent(const QString &sourceText, const QString &filePath)
{
    if (verilogSourceEditor == nullptr) {
        return;
    }

    verilogSourceFilePath = filePath;
    const QFileInfo info(filePath);
    const QString title = info.fileName().isEmpty()
        ? tr("Verilog Source")
        : tr("Verilog Source - %1").arg(info.fileName());

    if (verilogSourceDock != nullptr) {
        verilogSourceDock->setWindowTitle(title);
        verilogSourceDock->show();
        verilogSourceDock->raise();
    }
    if (verilogSourceFloatWindow != nullptr) {
        verilogSourceFloatWindow->setWindowTitle(title);
        verilogSourceFloatWindow->raise();
    }

    verilogSourceEditor->setPlainText(sourceText);
    verilogSourceEditor->document()->setModified(false);
    verilogSourceEditor->moveCursor(QTextCursor::Start);
}

QString MainWindow::verilogSourceContent() const
{
    return verilogSourceEditor != nullptr ? verilogSourceEditor->toPlainText() : QString();
}

QString MainWindow::verilogSourcePath() const
{
    return verilogSourceFilePath;
}

bool MainWindow::currentCanvasHasItemsOrData() const
{
    if (currentSceneCellCount() > 0) {
        return true;
    }
    if (scene != nullptr && !scene->clockRegions().isEmpty()) {
        return true;
    }
    if (gateLevelMapping != nullptr &&
        (!gateLevelMapping->nodes.isEmpty() ||
         !gateLevelMapping->routes.isEmpty() ||
         !gateLevelMapping->coordPhaseMap.isEmpty() ||
         !gateLevelMapping->mappedRouteCells.isEmpty() ||
         !gateLevelMapping->metadata.isEmpty())) {
        return true;
    }
    return false;
}

void MainWindow::clearCanvasAndMappingData()
{
    DesignSnapshot blank;
    blank.layerNames.push_back(tr("Main Cell Layer"));
    blank.cellsByLayer.resize(1);
    restoreDesignSnapshot(blank, true);

    if (scene != nullptr) {
        scene->clearFastRender();
        scene->clearPhaseRecord();
        scene->clearSelection();
    }
    clearCircuitNodeHighlight();
    if (circuitSchematicView != nullptr) {
        circuitSchematicView->clearCircuit();
    }
    if (structure3DView != nullptr) {
        structure3DView->setStructure(4,
                                      QVector<LayeredStructure3DView::ClockRegionRecord>(),
                                      QVector<QVector<LayeredStructure3DView::CellRecord>>(),
                                      QVector<LayeredStructure3DView::EncodedTileRecord>());
    }
    if (gateLevelMapping != nullptr) {
        gateLevelMapping->circuitName.clear();
        gateLevelMapping->nodes.clear();
        gateLevelMapping->routes.clear();
        gateLevelMapping->mappedRouteCells.clear();
        gateLevelMapping->coordPhaseMap.clear();
        gateLevelMapping->metadata.clear();
        gateLevelMapping->currentMappingFilePath.clear();
    }

    slotCancelPhaseCodecEncoding();
    setInputNames({});
    setCurrentFile(QString());
    resetUndoHistory();
    refreshLayoutInfoPanel();
}

QString MainWindow::writeVerilogSourceRunFile(QString *errorMessage) const
{
    if (verilogSourceEditor == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Verilog source editor is not available.");
        }
        return QString();
    }

    const QString sourceText = verilogSourceEditor->toPlainText();
    if (sourceText.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Verilog source is empty.");
        }
        return QString();
    }

    const QFileInfo sourceInfo(verilogSourceFilePath);
    QString sourceStem = sourceInfo.completeBaseName().isEmpty()
        ? QStringLiteral("verilog_source")
        : sourceInfo.completeBaseName();
    QDir baseDir = sourceInfo.absoluteDir().exists()
        ? sourceInfo.absoluteDir()
        : QDir::current();
    if (sourceStem.endsWith(QStringLiteral("_source")) &&
        baseDir.dirName().endsWith(QStringLiteral("_source_generate"))) {
        sourceStem.chop(QStringLiteral("_source").size());
        baseDir.cdUp();
    }
    const QString outputDirPath = baseDir.filePath(sourceStem + QStringLiteral("_source_generate"));
    if (!QDir().mkpath(outputDirPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Cannot create source run directory: %1")
                .arg(QDir::toNativeSeparators(outputDirPath));
        }
        return QString();
    }

    const QString runPath = QDir(outputDirPath).filePath(sourceStem + QStringLiteral("_source.v"));
    QFile file(runPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("Cannot write %1\n\n%2")
                .arg(QDir::toNativeSeparators(runPath), file.errorString());
        }
        return QString();
    }

    QTextStream out(&file);
    out << sourceText;
    file.close();
    return runPath;
}

void MainWindow::slotGenerateFromVerilogSource()
{
    if (verilogSourceEditor == nullptr) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Generate Layout"));
    auto *form = new QFormLayout(&dialog);
    auto *algorithmCombo = new QComboBox(&dialog);
    algorithmCombo->addItem(tr("Heuristic P&R"), QStringLiteral("heuristic"));
    algorithmCombo->addItem(tr("Graph P&R"), QStringLiteral("graph"));
    algorithmCombo->addItem(tr("Normal Graph P&R"), QStringLiteral("normal_graph"));
    algorithmCombo->addItem(tr("GCN+RL P&R"), QStringLiteral("gcn_rl"));
    form->addRow(tr("Algorithm:"), algorithmCombo);

    auto *normalGraphVisualCheck = new QCheckBox(tr("Generate circuit layer/SVG figures"), &dialog);
    auto *normalGraphStageCheck = new QCheckBox(tr("Generate stage debug TeX snapshots"), &dialog);
    form->addRow(normalGraphVisualCheck);
    form->addRow(normalGraphStageCheck);

    auto updateNormalGraphOptions = [&]() {
        const bool normalGraphSelected =
            algorithmCombo->currentData().toString() == QStringLiteral("normal_graph");
        normalGraphVisualCheck->setEnabled(normalGraphSelected);
        normalGraphStageCheck->setEnabled(normalGraphSelected);
    };
    connect(algorithmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            &dialog, updateNormalGraphOptions);
    updateNormalGraphOptions();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Generate"));
    form->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    MainWindow *targetWindow = this;
    if (currentCanvasHasItemsOrData()) {
        QMessageBox prompt(this);
        prompt.setIcon(QMessageBox::Question);
        prompt.setWindowTitle(tr("Existing Layout"));
        prompt.setText(tr("The current UI already contains layout items or mapping data."));
        prompt.setInformativeText(tr("Clear the current tab before generating, or generate in a new tab with the current Verilog source?"));
        auto *clearButton = prompt.addButton(tr("Clear and Generate"), QMessageBox::DestructiveRole);
        QPushButton *newTabButton = nullptr;
        if (tabHost != nullptr) {
            newTabButton = prompt.addButton(tr("New Tab and Generate"), QMessageBox::AcceptRole);
        }
        prompt.addButton(QMessageBox::Cancel);
        prompt.exec();

        if (prompt.clickedButton() == clearButton) {
            clearCanvasAndMappingData();
        } else if (newTabButton != nullptr && prompt.clickedButton() == newTabButton) {
            targetWindow = tabHost->openVerilogSourceInNewTab(verilogSourceContent(), verilogSourcePath());
            if (targetWindow == nullptr) {
                return;
            }
        } else {
            return;
        }
    }

    QString errorMessage;
    const QString runFilePath = targetWindow->writeVerilogSourceRunFile(&errorMessage);
    if (runFilePath.isEmpty()) {
        QMessageBox::warning(this, tr("Generate Layout"), errorMessage);
        return;
    }

    const QString algorithm = algorithmCombo->currentData().toString();
    if (algorithm == QStringLiteral("heuristic")) {
        targetWindow->verilogHandler->runHeuristicLayoutForFile(runFilePath);
    } else if (algorithm == QStringLiteral("graph")) {
        targetWindow->verilogHandler->runGraphRenderForFile(runFilePath);
    } else if (algorithm == QStringLiteral("normal_graph")) {
        targetWindow->verilogHandler->runNormalGraphDrawLayoutForFile(
            runFilePath,
            true,
            normalGraphVisualCheck->isChecked(),
            normalGraphStageCheck->isChecked());
    } else {
        targetWindow->verilogHandler->runGcnRlLayoutForFile(runFilePath, true, false);
    }
}

void MainWindow::createCircuitSchematicDock()
{
    circuitSchematicDock = new QDockWidget(tr("Circuit Structure"), this);
    circuitSchematicDock->setObjectName(QStringLiteral("circuitSchematicDock"));
    circuitSchematicDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                          Qt::RightDockWidgetArea |
                                          Qt::BottomDockWidgetArea);
    circuitSchematicDock->setFeatures(QDockWidget::DockWidgetClosable);
    circuitSchematicDock->setMinimumWidth(320);

    auto *content = new QWidget(circuitSchematicDock);
    content->setObjectName(QStringLiteral("circuitSchematicDockContent"));
    content->setMinimumSize(320, 280);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    auto *toolbarLayout = new QVBoxLayout;
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(5);

    auto *closeButton = createDockToolButton(content,
                                             QStringLiteral("×"),
                                             tr("Close the circuit structure dock"),
                                             32);

    auto *dockButton = createDockToolButton(content,
                                            QStringLiteral("↙"),
                                            tr("Dock the circuit view back into the main window"),
                                            32);

    auto *floatButton = createDockToolButton(content,
                                             QStringLiteral("↗"),
                                             tr("Float the circuit view as a standalone window"),
                                             32);

    auto *exportButton = createDockToolButton(content,
                                              tr("Save"),
                                              tr("Save circuit structure as SVG or cropped PDF"),
                                              54);

    auto *labelButton = createDockToolButton(content,
                                             tr("Labels"),
                                             tr("Show or hide node labels"),
                                             58);
    labelButton->setCheckable(true);
    labelButton->setChecked(true);

    auto *cancelButton = createDockToolButton(content,
                                              tr("Clear"),
                                              tr("Clear selected circuit item"),
                                              52);

    auto *zoomOutButton = createDockToolButton(content, QStringLiteral("-"), tr("Zoom out"), 28);

    auto *zoomInButton = createDockToolButton(content, QStringLiteral("+"), tr("Zoom in"), 28);

    auto *fitButton = createDockToolButton(content, tr("Fit"), tr("Fit circuit"), 42);

    auto *circuitNavigationLayout = new QHBoxLayout;
    circuitNavigationLayout->setContentsMargins(0, 0, 0, 0);
    circuitNavigationLayout->setSpacing(6);
    circuitNavigationLayout->addWidget(closeButton);
    circuitNavigationLayout->addWidget(dockButton);
    circuitNavigationLayout->addWidget(floatButton);
    circuitNavigationLayout->addWidget(exportButton);
    circuitNavigationLayout->addStretch(1);

    auto *circuitActionsLayout = new QHBoxLayout;
    circuitActionsLayout->setContentsMargins(0, 0, 0, 0);
    circuitActionsLayout->setSpacing(6);
    circuitActionsLayout->addWidget(labelButton);
    circuitActionsLayout->addWidget(cancelButton);
    circuitActionsLayout->addStretch(1);
    circuitActionsLayout->addWidget(zoomOutButton);
    circuitActionsLayout->addWidget(zoomInButton);
    circuitActionsLayout->addWidget(fitButton);
    toolbarLayout->addLayout(circuitNavigationLayout);
    toolbarLayout->addLayout(circuitActionsLayout);

    circuitSchematicView = new CircuitSchematicView(content);
    circuitSchematicView->setMinimumSize(0, 200);

    layout->addLayout(toolbarLayout);
    layout->addWidget(circuitSchematicView, 1);
    circuitSchematicDock->setWidget(content);

    connect(closeButton, &QToolButton::clicked, this, [this]() {
        closeDockContent(circuitSchematicDock, &circuitSchematicFloatWindow);
    });
    connect(dockButton, &QToolButton::clicked, this, [this]() {
        restoreDockContent(circuitSchematicDock, &circuitSchematicFloatWindow);
    });
    connect(floatButton, &QToolButton::clicked, this, [this]() {
        const QString title = circuitSchematicDock != nullptr
            ? circuitSchematicDock->windowTitle()
            : tr("Circuit Structure");
        floatDockContent(circuitSchematicDock,
                         &circuitSchematicFloatWindow,
                         title,
                         QSize(860, 620));
    });
    connect(exportButton, &QToolButton::clicked,
            this, &MainWindow::exportCircuitSchematicSvg);
    connect(labelButton, &QToolButton::toggled,
            circuitSchematicView, &CircuitSchematicView::setNodeLabelsVisible);
    connect(cancelButton, &QToolButton::clicked,
            this, &MainWindow::slotClearCircuitSelection);
    connect(zoomOutButton, &QToolButton::clicked,
            circuitSchematicView, &CircuitSchematicView::zoomOut);
    connect(zoomInButton, &QToolButton::clicked,
            circuitSchematicView, &CircuitSchematicView::zoomIn);
    connect(fitButton, &QToolButton::clicked,
            circuitSchematicView, &CircuitSchematicView::fitToCircuit);
    connect(circuitSchematicView, &CircuitSchematicView::nodeActivated,
            this, &MainWindow::slotCircuitNodeActivated);
    connect(circuitSchematicView, &CircuitSchematicView::edgeActivated,
            this, &MainWindow::slotCircuitEdgeActivated);

    circuitSchematicDock->setStyleSheet(dockChromeStyle(QStringLiteral("circuitSchematicDock")) +
        QStringLiteral(
        "QWidget#circuitSchematicDockContent {"
        "  background: #f3f5f7;"
        "}"
        "QGraphicsView#circuitSchematicView {"
        "  background: #fbfcfd;"
        "  border: 1px solid #c6ccd4;"
        "  border-radius: 3px;"
        "}"
        "QToolButton {"
        "  background: #ffffff;"
        "  border: 1px solid #b9c1cc;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: #e8edf3;"
        "}"
        "QToolButton:checked {"
        "  background: #dbe8f6;"
        "  border-color: #4f7da5;"
        "}"
        "QToolButton#verilogGenerateButton {"
        "  background: #2f5f8f;"
        "  border: 0;"
        "  color: #ffffff;"
        "}"
        "QToolButton#verilogGenerateButton:hover {"
        "  background: #386c9f;"
        "}"
    ));

    addDockWidget(Qt::RightDockWidgetArea, circuitSchematicDock);
    resizeDocks(QList<QDockWidget*>() << circuitSchematicDock,
                QList<int>() << 380,
                Qt::Horizontal);
}

void MainWindow::createPhaseCodecDock()
{
    phaseCodecDock = new QDockWidget(tr("Phase Codec"), this);
    phaseCodecDock->setObjectName(QStringLiteral("phaseCodecDock"));
    phaseCodecDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                    Qt::RightDockWidgetArea |
                                    Qt::BottomDockWidgetArea);
    phaseCodecDock->setFeatures(QDockWidget::DockWidgetClosable);
    phaseCodecDock->setMinimumWidth(300);
    phaseCodecPanel = createPhaseCodecPanel();
    phaseCodecDock->setWidget(phaseCodecPanel);
    phaseCodecDock->setStyleSheet(dockChromeStyle(QStringLiteral("phaseCodecDock")));

    addDockWidget(Qt::RightDockWidgetArea, phaseCodecDock);
    if (circuitSchematicDock != nullptr) {
        splitDockWidget(circuitSchematicDock, phaseCodecDock, Qt::Vertical);
    }
}

void MainWindow::createLayoutInfoDock()
{
    layoutInfoDock = new QDockWidget(tr("Layout Info"), this);
    layoutInfoDock->setObjectName(QStringLiteral("layoutInfoDock"));
    layoutInfoDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                    Qt::RightDockWidgetArea |
                                    Qt::BottomDockWidgetArea);
    layoutInfoDock->setFeatures(QDockWidget::DockWidgetClosable);
    layoutInfoDock->setMinimumWidth(300);
    layoutInfoPanel = createLayoutInfoPanel();
    layoutInfoDock->setWidget(layoutInfoPanel);
    layoutInfoDock->setStyleSheet(dockChromeStyle(QStringLiteral("layoutInfoDock")));

    addDockWidget(Qt::RightDockWidgetArea, layoutInfoDock);
    if (phaseCodecDock != nullptr) {
        splitDockWidget(phaseCodecDock, layoutInfoDock, Qt::Vertical);
    }
    resizeDocks(QList<QDockWidget*>() << circuitSchematicDock << phaseCodecDock << layoutInfoDock,
                QList<int>() << 520 << 280 << 220,
                Qt::Vertical);
}

void MainWindow::createStructure3DDock()
{
    structure3DDock = new QDockWidget(tr("3D Structure"), this);
    structure3DDock->setObjectName(QStringLiteral("structure3DDock"));
    structure3DDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                     Qt::RightDockWidgetArea |
                                     Qt::BottomDockWidgetArea);
    structure3DDock->setFeatures(QDockWidget::DockWidgetClosable);
    structure3DDock->setMinimumSize(720, 420);

    auto *content = new QWidget(structure3DDock);
    content->setObjectName(QStringLiteral("structure3DDockContent"));
    content->setMinimumSize(720, 420);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(8);

    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(6);

    auto *closeButton = createDockToolButton(content,
                                             QStringLiteral("×"),
                                             tr("Close the 3D structure dock"),
                                             32);
    auto *dockButton = createDockToolButton(content,
                                            QStringLiteral("↙"),
                                            tr("Dock the 3D structure view back into the main window"),
                                            32);
    auto *floatButton = createDockToolButton(content,
                                             QStringLiteral("↗"),
                                             tr("Float the 3D structure view as a standalone window"),
                                             32);
    auto *refreshButton = createDockToolButton(content,
                                               tr("Update"),
                                               tr("Refresh the 3D structure from the current layout"),
                                               62);
    auto *saveButton = createDockToolButton(content,
                                            tr("Save"),
                                            tr("Save the current 3D structure as SVG or cropped PDF"),
                                            54);
    auto *zoomOutButton = createDockToolButton(content, QStringLiteral("-"), tr("Zoom out"), 28);
    auto *zoomInButton = createDockToolButton(content, QStringLiteral("+"), tr("Zoom in"), 28);
    auto *fitButton = createDockToolButton(content, tr("Fit"), tr("Fit 3D structure"), 42);

    toolbarLayout->addWidget(closeButton);
    toolbarLayout->addWidget(dockButton);
    toolbarLayout->addWidget(floatButton);
    toolbarLayout->addWidget(refreshButton);
    toolbarLayout->addWidget(saveButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(zoomOutButton);
    toolbarLayout->addWidget(zoomInButton);
    toolbarLayout->addWidget(fitButton);

    structure3DView = new LayeredStructure3DView(content);
    structure3DView->setMinimumSize(700, 360);

    layout->addLayout(toolbarLayout);
    layout->addWidget(structure3DView, 1);
    structure3DDock->setWidget(content);

    connect(closeButton, &QToolButton::clicked, this, [this]() {
        closeDockContent(structure3DDock, &structure3DFloatWindow);
    });
    connect(dockButton, &QToolButton::clicked, this, [this]() {
        restoreDockContent(structure3DDock, &structure3DFloatWindow);
    });
    connect(floatButton, &QToolButton::clicked, this, [this]() {
        floatDockContent(structure3DDock,
                         &structure3DFloatWindow,
                         tr("3D Structure"),
                         QSize(980, 720));
    });
    connect(refreshButton, &QToolButton::clicked,
            this, &MainWindow::updateStructure3DView);
    connect(saveButton, &QToolButton::clicked,
            this, &MainWindow::exportStructure3DGraphic);
    connect(zoomOutButton, &QToolButton::clicked,
            structure3DView, &LayeredStructure3DView::zoomOut);
    connect(zoomInButton, &QToolButton::clicked,
            structure3DView, &LayeredStructure3DView::zoomIn);
    connect(fitButton, &QToolButton::clicked,
            structure3DView, &LayeredStructure3DView::fitToStructure);

    structure3DDock->setStyleSheet(dockChromeStyle(QStringLiteral("structure3DDock")) +
        QStringLiteral(
        "QWidget#structure3DDockContent {"
        "  background: #f3f5f7;"
        "}"
        "QGraphicsView#layeredStructure3DView {"
        "  background: #f7fafd;"
        "  border: 1px solid #c6ccd4;"
        "  border-radius: 3px;"
        "}"
        "QToolButton {"
        "  background: #ffffff;"
        "  border: 1px solid #b9c1cc;"
        "  border-radius: 3px;"
        "  color: #1f252d;"
        "  font-weight: 600;"
        "}"
        "QToolButton:hover {"
        "  background: #e8edf3;"
        "}"
    ));

    addDockWidget(Qt::BottomDockWidgetArea, structure3DDock);
    structure3DDock->hide();
}

void MainWindow::exportStructure3DGraphic()
{
    if (structure3DView == nullptr) {
        return;
    }

    updateStructure3DView();

    const QFileInfo currentInfo(curFile);
    const QString defaultDir = currentInfo.absoluteDir().exists()
        ? currentInfo.absoluteDir().absolutePath()
        : QDir::currentPath();
    const QString defaultName = QStringLiteral("ifcn_3d_structure.pdf");
    QString selectedFilter;
    QString filePath = QFileDialog::getSaveFileName(this,
                                                    tr("Save 3D Structure"),
                                                    QDir(defaultDir).absoluteFilePath(defaultName),
                                                    tr("PDF files (*.pdf);;SVG files (*.svg)"),
                                                    &selectedFilter);
    if (filePath.isEmpty()) {
        return;
    }

    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();
    if (suffix.isEmpty()) {
        suffix = selectedFilter.contains(QStringLiteral("SVG"), Qt::CaseInsensitive)
            ? QStringLiteral("svg")
            : QStringLiteral("pdf");
        filePath += QLatin1Char('.') + suffix;
    }

    if (suffix != QStringLiteral("svg") && suffix != QStringLiteral("pdf")) {
        suffix = selectedFilter.contains(QStringLiteral("SVG"), Qt::CaseInsensitive)
            ? QStringLiteral("svg")
            : QStringLiteral("pdf");
        filePath += QLatin1Char('.') + suffix;
    }

    const bool saved = saveStructure3DGraphic(filePath);

    if (!saved) {
        QMessageBox::warning(this,
                             tr("Save 3D Structure"),
                             tr("Failed to save the 3D structure graphic."));
        return;
    }

    printToStatusBar(tr("3D structure saved: %1")
                         .arg(QDir::toNativeSeparators(filePath)));
}

bool MainWindow::saveStructure3DGraphic(const QString &filePath)
{
    if (structure3DView == nullptr || filePath.trimmed().isEmpty()) {
        return false;
    }

    updateStructure3DView();
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        return structure3DView->exportToPdf(filePath);
    }
    if (suffix == QStringLiteral("svg")) {
        return structure3DView->exportToSvg(filePath);
    }
    return false;
}

bool MainWindow::saveCellLevelLayoutGraphic(const QString &filePath)
{
    if (scene == nullptr || filePath.trimmed().isEmpty()) {
        return false;
    }

    QRectF sourceRect = scene->exportContentBounds();
    if (!sourceRect.isValid() || sourceRect.isEmpty()) {
        return false;
    }
    sourceRect = sourceRect.normalized();

    const QSize exportSize(qMax(1, static_cast<int>(std::ceil(sourceRect.width()))),
                           qMax(1, static_cast<int>(std::ceil(sourceRect.height()))));
    const QSizeF figureSize(exportSize);
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == QStringLiteral("pdf")) {
        QPdfWriter writer(filePath);
        writer.setResolution(72);
        writer.setTitle(QStringLiteral("iFCN cell-level layout"));
        writer.setCreator(QStringLiteral("iFCN"));
        const QPageSize pageSize(figureSize,
                                 QPageSize::Point,
                                 QStringLiteral("iFCN cell-level layout"));
        QPageLayout pageLayout(pageSize,
                               QPageLayout::Portrait,
                               QMarginsF(0.0, 0.0, 0.0, 0.0),
                               QPageLayout::Point);
        pageLayout.setMode(QPageLayout::FullPageMode);
        writer.setPageLayout(pageLayout);

        QPainter painter(&writer);
        if (!painter.isActive()) {
            return false;
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        scene->renderForExport(&painter,
                               QRectF(QPointF(0.0, 0.0), figureSize),
                               sourceRect,
                               Qt::IgnoreAspectRatio);
        return painter.end();
    }

    if (suffix == QStringLiteral("svg")) {
        QSvgGenerator generator;
        generator.setFileName(filePath);
        generator.setSize(exportSize);
        generator.setViewBox(QRect(QPoint(0, 0), exportSize));
        generator.setTitle(QStringLiteral("iFCN cell-level layout"));
        generator.setDescription(QStringLiteral("Vector cell-level layout exported by iFCN."));

        QPainter painter(&generator);
        if (!painter.isActive()) {
            return false;
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        scene->renderForExport(&painter,
                               QRectF(QPointF(0.0, 0.0), figureSize),
                               sourceRect,
                               Qt::IgnoreAspectRatio);
        return painter.end();
    }

    return false;
}

void MainWindow::centerViewOnItems(bool fitToView)
{
    if (scene == nullptr || view == nullptr) {
        return;
    }

    QRectF contentRect = scene->itemsBoundingRect();
    if (scene->hasFastRender()) {
        const QRectF fastRect = scene->fastRenderBounds();
        contentRect = contentRect.isValid() ? contentRect.united(fastRect) : fastRect;
    }
    if (!contentRect.isValid() || contentRect.isEmpty()) {
        view->centerOn(scene->sceneRect().center());
        return;
    }

    constexpr qreal kMargin = 120.0;
    contentRect = contentRect.adjusted(-kMargin, -kMargin, kMargin, kMargin);

    // Expand scene boundaries to always include the full mapped layout.
    scene->setSceneRect(scene->sceneRect().united(contentRect));

    if (fitToView) {
        view->fitInView(contentRect, Qt::KeepAspectRatio);
    } else {
        view->centerOn(contentRect.center());
    }
}

quint64 MainWindow::packSceneCoord(int x, int y)
{
    const quint32 ux = static_cast<quint32>(x);
    const quint32 uy = static_cast<quint32>(y);
    return (static_cast<quint64>(ux) << 32) | uy;
}

void MainWindow::beginSceneBatchUpdate()
{
    if (isBatchUpdating || scene == nullptr || view == nullptr) {
        return;
    }

    isBatchUpdating = true;
    batchDirtyPending = false;

    batchOccupiedByLayer.clear();
    batchOccupiedByLayer.resize(layers.size());
    for (int layerIdx = 0; layerIdx < layers.size(); ++layerIdx) {
        for (QGraphicsItem* item : layers[layerIdx]) {
            if (item == nullptr) {
                continue;
            }
            batchOccupiedByLayer[layerIdx].insert(packSceneCoord(static_cast<int>(item->x()), static_cast<int>(item->y())));
        }
    }

    view->setUpdatesEnabled(false);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
}

void MainWindow::endSceneBatchUpdate(bool recenter)
{
    if (!isBatchUpdating || scene == nullptr || view == nullptr) {
        return;
    }

    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    view->setUpdatesEnabled(true);
    scene->update();

    isBatchUpdating = false;
    batchOccupiedByLayer.clear();

    if (batchDirtyPending) {
        setWindowModified(true);
        updateUi();
    }
    batchDirtyPending = false;
    const bool mappedIfcn = QFileInfo(curFile).suffix().compare(QStringLiteral("ifcn"), Qt::CaseInsensitive) == 0 &&
                            gateLevelMapping != nullptr &&
                            !gateLevelMapping->metadata.isEmpty();
    if (mappedIfcn) {
        updateLayoutInfoFromMapping(*gateLevelMapping);
    } else {
        refreshLayoutInfoPanel();
    }

    if (recenter) {
        centerViewOnItems(true);
    }
}

void MainWindow::createToolBox(){
    buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(false);
    connect(buttonGroup, SIGNAL(buttonClicked(int)), this, SLOT(buttonGroupClicked(int)));

    QGridLayout *layout = new QGridLayout;
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(8);
    layout->addWidget(createCellWidget(tr("Normal"), CellType::NormalCell), 0, 0);
    layout->addWidget(createCellWidget(tr("Fixed_0"), CellType::FixedCell_0), 0, 1);
    layout->addWidget(createCellWidget(tr("Fixed_1"), CellType::FixedCell_1), 1, 0);
    layout->addWidget(createCellWidget(tr("Input"), CellType::InputCell), 1, 1);
    layout->addWidget(createCellWidget(tr("Output"), CellType::OutputCell), 2, 0);
    layout->addWidget(createCellWidget(tr("Vertical"), CellType::VerticalCell), 2, 1);
    layout->addWidget(createCellWidget(tr("Crossover"), CellType::CrossoverCell), 3, 0);
    layout->setRowStretch(4, 10);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    QWidget *itemWidget = new QWidget;
    itemWidget->setLayout(layout);
    const int standardLibraryIndex = toolBox->addItem(itemWidget, tr("Cell Library"));
    toolBox->setItemToolTip(standardLibraryIndex, tr("Standard Library"));

    clockSchemeGroup = new QButtonGroup(this);
    connect(clockSchemeGroup, SIGNAL(buttonClicked(QAbstractButton*)),  this, SLOT(slotClockSchemeGroupClicked(QAbstractButton*)));
    QGridLayout *clockSchemeLayout = new QGridLayout;
    clockSchemeLayout->setContentsMargins(8, 8, 8, 8);
    clockSchemeLayout->setHorizontalSpacing(6);
    clockSchemeLayout->setVerticalSpacing(8);
    // clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Select"),":/csSelect.svg")     ,0,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Clean"),":/cleanCS.svg")       ,0,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Custom"),":/custom.svg")       ,0,1);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("ONE-D"),":/2dd.svg")           ,1,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("2DDwave"),":/2dd.svg")         ,1,1);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("USE"),":/use.svg")             ,2,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("RES"),":/res.svg")             ,2,1);
    clockSchemeLayout->setRowStretch(3, 8);
    clockSchemeLayout->setColumnStretch(0, 1);
    clockSchemeLayout->setColumnStretch(1, 1);
    QWidget *clockSchemeWidget = new QWidget;
    clockSchemeWidget->setLayout(clockSchemeLayout);
    const int clockSchemeIndex = toolBox->addItem(clockSchemeWidget, tr("Clocking"));
    toolBox->setItemToolTip(clockSchemeIndex, tr("Clock Schemes"));
    toolBox->setMinimumWidth(176);
    polishToolBoxTitleButtons(toolBox, tr("Cell Library"), tr("Clocking"));

}

void MainWindow::buttonGroupClicked(int id){
    QList<QAbstractButton *> buttons = buttonGroup->buttons();
    for (QAbstractButton *button : buttons) {
        if (buttonGroup->button(id) != button)
            button->setChecked(false);
    }
    scene->setItemType(CellType(id));
    // Selecting a library cell is an intent to place it.  Reflect that intent
    // immediately in the mode selector instead of requiring a second click.
    insertModeButton->setChecked(true);
}

void MainWindow::slotClockSchemeGroupClicked(QAbstractButton* button){
    QList<QAbstractButton *> buttons = clockSchemeGroup->buttons();
    for (QAbstractButton *myButton: buttons) {
        if (myButton != button)
            myButton->setChecked(false);
    }
    // reset
    QString text = button->text();
    bool changedClockRegions = false;
    if(text == tr("Clean")){
        scene->clearPhaseRecord();
        changedClockRegions = true;
    }else if(text == tr("Custom")){
        viewModeButtonGroup->setExclusive(false);
        selectModeButton->setChecked(false);
        insertModeButton->setChecked(false);
        dragModeButton->setChecked(false);
        viewModeButtonGroup->setExclusive(true);
        setEditMode(EditMode::ClockScheme);
        scene->setEditMode(EditMode::ClockScheme);
        if (customStatusBar != nullptr) {
            customStatusBar->addMessage(tr("Custom clock scheme placement enabled"));
        }
    }else if(text == tr("ONE-D")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(ONEDIMEN_CLOKC_SCHEME);
        changedClockRegions = true;
    }else if(text == tr("2DDwave")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(TDDWAVE_CLOCK_SCHEME);
        changedClockRegions = true;
    }else if(text == tr("USE")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(USE_CLOKC_SCHEME);
        changedClockRegions = true;
    }else if(text == tr("RES")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(RES_CLOKC_SCHEME);
        changedClockRegions = true;
    }
    if (changedClockRegions) {
        setDirty(true);
        pushUndoSnapshot();
    }
    update();
    // setDirty(true);
}

int MainWindow::selectedClockPhase() const
{
    if (clockComboBox == nullptr) {
        return 0;
    }
    const QVariant phaseData = clockComboBox->itemData(clockComboBox->currentIndex());
    if (phaseData.isValid()) {
        return phaseData.toInt();
    }
    return clockComboBox->currentIndex();
}

int MainWindow::selectedPhaseCodecCount(const QVector<QCADScene::ClockRegionRecord> &regions) const
{
    const int mode = phaseCodecModeComboBox != nullptr
        ? phaseCodecModeComboBox->currentData().toInt()
        : 4;
    if (mode == 3 || mode == 4) {
        return mode;
    }

    for (const QCADScene::ClockRegionRecord &region : regions) {
        if (region.phase == 3) {
            return 4;
        }
    }
    return 3;
}

QPoint MainWindow::clockRegionGridCoord(const QCADScene::ClockRegionRecord &region) const
{
    const int gridX = static_cast<int>(std::round((region.x - 40.0) / CLOCK_SCHEME_SIZE_5));
    const int gridY = static_cast<int>(std::round((region.y - 40.0) / CLOCK_SCHEME_SIZE_5));
    return QPoint(gridX, gridY);
}

void MainWindow::clearPhaseCodecHighlight()
{
    for (QGraphicsItem *item : phaseCodecHighlightItems) {
        if (item == nullptr) {
            continue;
        }
        if (scene != nullptr && item->scene() == scene) {
            scene->removeItem(item);
        }
        delete item;
    }
    phaseCodecHighlightItems.clear();
}

QRectF MainWindow::phaseCodecTileSceneRect(const PhaseCodecTilePreview &tile) const
{
    const int startGridX = phaseCodecOriginGrid.x()
        + static_cast<int>(tile.tileX) * phaseCodecBlockSize;
    const int startGridY = phaseCodecOriginGrid.y()
        + static_cast<int>(tile.tileY) * phaseCodecBlockSize;
    const qreal left = 40.0 + startGridX * CLOCK_SCHEME_SIZE_5 - CLOCK_SCHEME_SIZE_5 / 2.0;
    const qreal top = 40.0 + startGridY * CLOCK_SCHEME_SIZE_5 - CLOCK_SCHEME_SIZE_5 / 2.0;
    return QRectF(left,
                  top,
                  phaseCodecBlockSize * CLOCK_SCHEME_SIZE_5,
                  phaseCodecBlockSize * CLOCK_SCHEME_SIZE_5);
}

void MainWindow::highlightPhaseCodecTile(const PhaseCodecTilePreview &tile)
{
    if (scene == nullptr || view == nullptr) {
        return;
    }

    clearPhaseCodecHighlight();
    const QRectF blockRect = phaseCodecTileSceneRect(tile);

    QPen outerPen(QColor(0, 103, 192, 235), 4.0);
    outerPen.setCosmetic(true);
    outerPen.setJoinStyle(Qt::MiterJoin);
    auto *overlay = scene->addRect(blockRect,
                                   outerPen,
                                   QBrush(QColor(0, 103, 192, 42)));
    overlay->setZValue(1000000);
    phaseCodecHighlightItems.push_back(overlay);

    QPen innerPen(QColor(255, 255, 255, 230), 1.6);
    innerPen.setCosmetic(true);
    auto *inner = scene->addRect(blockRect.adjusted(3, 3, -3, -3), innerPen, Qt::NoBrush);
    inner->setZValue(1000001);
    phaseCodecHighlightItems.push_back(inner);

    QPen markerPen(QColor(255, 255, 255, 240), 1.8);
    markerPen.setCosmetic(true);
    const QBrush markerBrush(QColor(0, 103, 192, 230));
    const qreal markerSize = 24.0;
    const QVector<QRectF> markerRects = {
        QRectF(blockRect.left(), blockRect.top(), markerSize, markerSize),
        QRectF(blockRect.right() - markerSize, blockRect.top(), markerSize, markerSize),
        QRectF(blockRect.left(), blockRect.bottom() - markerSize, markerSize, markerSize),
        QRectF(blockRect.right() - markerSize, blockRect.bottom() - markerSize, markerSize, markerSize)
    };
    for (const QRectF &markerRect : markerRects) {
        auto *marker = scene->addRect(markerRect, markerPen, markerBrush);
        marker->setZValue(1000002);
        phaseCodecHighlightItems.push_back(marker);
    }

    auto *label = scene->addSimpleText(tr("tile(%1,%2) 0x%3")
                                       .arg(tile.tileX)
                                       .arg(tile.tileY)
                                       .arg(tile.hex));
    QFont labelFont = label->font();
    labelFont.setBold(true);
    labelFont.setPointSize(10);
    label->setFont(labelFont);
    label->setBrush(QBrush(QColor(0, 63, 120)));
    label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    label->setPos(blockRect.left() + 8, blockRect.top() - 24);
    label->setZValue(1000003);
    phaseCodecHighlightItems.push_back(label);

    view->centerOn(blockRect.center());

    if (customStatusBar != nullptr) {
        customStatusBar->addMessage(tr("Located encoded tile (%1, %2)")
                                    .arg(tile.tileX)
                                    .arg(tile.tileY));
    }
}

void MainWindow::highlightAllPhaseCodecTiles()
{
    if (scene == nullptr || view == nullptr) {
        return;
    }

    clearPhaseCodecHighlight();
    QRectF combinedRect;
    for (const PhaseCodecTilePreview &tile : phaseCodecTiles) {
        const QRectF blockRect = phaseCodecTileSceneRect(tile);
        combinedRect = combinedRect.isValid() ? combinedRect.united(blockRect) : blockRect;

        QPen outerPen(QColor(0, 103, 192, 205), 2.8);
        outerPen.setCosmetic(true);
        outerPen.setJoinStyle(Qt::MiterJoin);
        auto *overlay = scene->addRect(blockRect,
                                       outerPen,
                                       QBrush(QColor(0, 103, 192, 24)));
        overlay->setZValue(999990);
        phaseCodecHighlightItems.push_back(overlay);

        auto *label = scene->addSimpleText(tr("tile(%1,%2)\n0x%3")
                                           .arg(tile.tileX)
                                           .arg(tile.tileY)
                                           .arg(tile.hex));
        QFont labelFont = label->font();
        labelFont.setBold(true);
        labelFont.setPointSize(8);
        label->setFont(labelFont);
        label->setBrush(QBrush(QColor(0, 63, 120)));
        label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        label->setPos(blockRect.left() + 6, blockRect.top() + 6);
        label->setZValue(999991);
        phaseCodecHighlightItems.push_back(label);
    }

    if (combinedRect.isValid()) {
        view->centerOn(combinedRect.center());
    }

    if (customStatusBar != nullptr) {
        customStatusBar->addMessage(tr("Showing all %1 encoded phase tile(s)")
                                    .arg(phaseCodecTiles.size()));
    }
}

void MainWindow::updateStructure3DView()
{
    if (structure3DView == nullptr || scene == nullptr) {
        return;
    }

    QVector<LayeredStructure3DView::ClockRegionRecord> clockRegions;
    const QVector<QCADScene::ClockRegionRecord> sceneClockRegions = scene->clockRegions();
    if (phaseCodecTiles.isEmpty() && !sceneClockRegions.isEmpty()) {
        phaseCodecPreviewActive = true;
        updatePhaseCodecPreview();
    }
    clockRegions.reserve(sceneClockRegions.size());
    for (const QCADScene::ClockRegionRecord &region : sceneClockRegions) {
        LayeredStructure3DView::ClockRegionRecord record;
        record.x = region.x;
        record.y = region.y;
        record.phase = region.phase;
        clockRegions.push_back(record);
    }

    if (clockRegions.isEmpty() && gateLevelMapping != nullptr) {
        clockRegions.reserve(gateLevelMapping->coordPhaseMap.size());
        for (auto it = gateLevelMapping->coordPhaseMap.cbegin();
             it != gateLevelMapping->coordPhaseMap.cend();
             ++it) {
            LayeredStructure3DView::ClockRegionRecord record;
            record.x = 40 + it.key().x() * CLOCK_SCHEME_SIZE_5;
            record.y = 40 + it.key().y() * CLOCK_SCHEME_SIZE_5;
            record.phase = it.value();
            clockRegions.push_back(record);
        }
    }

    QVector<QVector<LayeredStructure3DView::CellRecord>> cellsByLayer;
    cellsByLayer.resize(3);

    if (scene->hasFastRender()) {
        const auto &fastLayers = scene->fastCellsByLayer();
        const int layerLimit = qMin(3, fastLayers.size());
        for (int layer = 0; layer < layerLimit; ++layer) {
            cellsByLayer[layer].reserve(fastLayers[layer].size());
            for (const QCADScene::FastCellRecord &cell : fastLayers[layer]) {
                LayeredStructure3DView::CellRecord record;
                record.x = cell.x;
                record.y = cell.y;
                record.layer = layer;
                record.phase = cell.phase;
                record.type = cell.type;
                record.name = cell.name;
                cellsByLayer[layer].push_back(record);
            }
        }
    } else {
        const int layerLimit = qMin(3, layers.size());
        for (int layer = 0; layer < layerLimit; ++layer) {
            cellsByLayer[layer].reserve(layers[layer].size());
            for (QGraphicsItem *item : layers[layer]) {
                if (item == nullptr || item->type() != QCADCellItem::Type) {
                    continue;
                }
                const auto *cell = static_cast<const QCADCellItem *>(item);
                LayeredStructure3DView::CellRecord record;
                record.x = static_cast<int>(std::round(simon::x(*cell)));
                record.y = static_cast<int>(std::round(simon::y(*cell)));
                record.layer = layer;
                record.phase = simon::timezone(*cell);
                record.type = cell->getCellType();
                record.name = QString::fromStdString(simon::name(*cell));
                cellsByLayer[layer].push_back(record);
            }
        }
    }

    int phaseCount = 4;
    if (!sceneClockRegions.isEmpty()) {
        phaseCount = selectedPhaseCodecCount(sceneClockRegions);
    } else if (phaseCodecModeComboBox != nullptr) {
        const int requested = phaseCodecModeComboBox->currentData().toInt();
        if (requested == 3 || requested == 4) {
            phaseCount = requested;
        }
    }

    QVector<LayeredStructure3DView::EncodedTileRecord> encodedTiles;
    encodedTiles.reserve(phaseCodecTiles.size());
    for (const PhaseCodecTilePreview &tile : phaseCodecTiles) {
        LayeredStructure3DView::EncodedTileRecord record;
        record.tileX = tile.tileX;
        record.tileY = tile.tileY;
        record.startGridX = phaseCodecOriginGrid.x()
            + static_cast<int>(tile.tileX) * phaseCodecBlockSize;
        record.startGridY = phaseCodecOriginGrid.y()
            + static_cast<int>(tile.tileY) * phaseCodecBlockSize;
        record.blockSize = phaseCodecBlockSize;
        record.hex = tile.hex;

        try {
            const auto matrix = fcngraph::phase_codec::decodePackedHexToMatrix(
                tile.hex.toStdString(),
                phaseCount,
                phaseCodecBlockSize
            );
            for (int row = 0; row < phaseCodecBlockSize; ++row) {
                if (row > 0) {
                    record.matrixText += QLatin1Char('\n');
                }
                for (int column = 0; column < phaseCodecBlockSize; ++column) {
                    if (column > 0) {
                        record.matrixText += QLatin1Char(' ');
                    }
                    record.matrixText += QString::number(matrix[static_cast<size_t>(row)]
                                                               [static_cast<size_t>(column)]);
                }
            }
        } catch (const std::exception &) {
            record.matrixText = tr("decode failed");
        }

        encodedTiles.push_back(record);
    }

    structure3DView->setStructure(phaseCount, clockRegions, cellsByLayer, encodedTiles);

    if (customStatusBar != nullptr) {
        customStatusBar->addMessage(tr("3D structure refreshed: %1-phase clock generator, %2 clock regions, %3 encoded tiles")
                                    .arg(phaseCount)
                                    .arg(clockRegions.size())
                                    .arg(encodedTiles.size()));
    }
}

void MainWindow::showStructure3DView()
{
    if (structure3DDock == nullptr || structure3DView == nullptr) {
        return;
    }

    phaseCodecPreviewActive = true;
    updatePhaseCodecPreview();
    updateStructure3DView();
    structure3DDock->show();
    structure3DDock->raise();
    if (structure3DFloatWindow != nullptr) {
        structure3DFloatWindow->show();
        structure3DFloatWindow->raise();
    }
}

void MainWindow::updatePhaseCodecPreview()
{
    if (scene == nullptr || phaseCodecTable == nullptr || phaseCodecStatusLabel == nullptr) {
        return;
    }

    QVector<QCADScene::ClockRegionRecord> regions = scene->clockRegions();
    bool usingMappedPhaseMap = false;
    if (regions.isEmpty() && gateLevelMapping != nullptr && !gateLevelMapping->coordPhaseMap.isEmpty()) {
        regions.reserve(gateLevelMapping->coordPhaseMap.size());
        for (auto it = gateLevelMapping->coordPhaseMap.cbegin();
             it != gateLevelMapping->coordPhaseMap.cend();
             ++it) {
            QCADScene::ClockRegionRecord record;
            record.x = 40 + it.key().x() * CLOCK_SCHEME_SIZE_5;
            record.y = 40 + it.key().y() * CLOCK_SCHEME_SIZE_5;
            record.phase = it.value();
            regions.push_back(record);
        }
        usingMappedPhaseMap = !regions.isEmpty();
    }
    phaseCodecTiles.clear();
    phaseCodecTable->clear();
    phaseCodecTable->setRowCount(0);
    phaseCodecTable->setColumnCount(0);

    if (regions.isEmpty()) {
        clearPhaseCodecHighlight();
        phaseCodecStatusLabel->setText(tr("No clock regions"));
        return;
    }

    int phaseCount = selectedPhaseCodecCount(regions);
    if (usingMappedPhaseMap && gateLevelMapping != nullptr) {
        bool ok = false;
        const int mappedPhaseCount = gateLevelMapping->metadata
            .value(QStringLiteral("phase count"))
            .toInt(&ok);
        if (ok && (mappedPhaseCount == 3 || mappedPhaseCount == 4)) {
            phaseCount = mappedPhaseCount;
        }
    }
    const int blockSize = phaseCount;
    phaseCodecBlockSize = blockSize;
    phaseCodecTable->verticalHeader()->setDefaultSectionSize(blockSize == 4 ? 104 : 88);
    phaseCodecTable->horizontalHeader()->setDefaultSectionSize(blockSize == 4 ? 118 : 104);

    int minGridX = std::numeric_limits<int>::max();
    int minGridY = std::numeric_limits<int>::max();
    int maxGridX = std::numeric_limits<int>::min();
    int maxGridY = std::numeric_limits<int>::min();

    struct RegionGridRecord {
        QPoint grid;
        int phase = 0;
    };
    QVector<RegionGridRecord> validRegions;
    validRegions.reserve(regions.size());

    for (const QCADScene::ClockRegionRecord &region : regions) {
        if (region.phase < 0) {
            continue;
        }
        if (region.phase >= phaseCount) {
            phaseCodecStatusLabel->setText(
                tr("Phase %1 is invalid in %2-phase mode")
                    .arg(region.phase)
                    .arg(phaseCount));
            clearPhaseCodecHighlight();
            return;
        }

        const QPoint grid = clockRegionGridCoord(region);
        minGridX = qMin(minGridX, grid.x());
        minGridY = qMin(minGridY, grid.y());
        maxGridX = qMax(maxGridX, grid.x());
        maxGridY = qMax(maxGridY, grid.y());
        validRegions.push_back({grid, region.phase});
    }

    if (validRegions.isEmpty()) {
        clearPhaseCodecHighlight();
        phaseCodecStatusLabel->setText(tr("No phased clock regions"));
        return;
    }

    phaseCodecOriginGrid = QPoint(minGridX, minGridY);

    std::map<fcngraph::phase_codec::PhaseCoord, int> phaseMap;
    for (const RegionGridRecord &region : validRegions) {
        const unsigned int normalizedX = static_cast<unsigned int>(region.grid.x() - minGridX);
        const unsigned int normalizedY = static_cast<unsigned int>(region.grid.y() - minGridY);
        phaseMap[{normalizedX, normalizedY}] = region.phase;
    }

    const int gridWidth = maxGridX - minGridX + 1;
    const int gridHeight = maxGridY - minGridY + 1;

    try {
        const auto encodedTiles = fcngraph::phase_codec::encodePhaseMapToTiles(
            phaseMap,
            phaseCount,
            blockSize,
            gridWidth,
            gridHeight
        );

        const int tableColumns = (gridWidth + blockSize - 1) / blockSize;
        const int tableRows = (gridHeight + blockSize - 1) / blockSize;
        phaseCodecTable->setColumnCount(tableColumns);
        phaseCodecTable->setRowCount(tableRows);

        QStringList horizontalLabels;
        for (int column = 0; column < tableColumns; ++column) {
            horizontalLabels.push_back(tr("X%1").arg(column));
        }
        QStringList verticalLabels;
        for (int row = 0; row < tableRows; ++row) {
            verticalLabels.push_back(tr("Y%1").arg(row));
        }
        phaseCodecTable->setHorizontalHeaderLabels(horizontalLabels);
        phaseCodecTable->setVerticalHeaderLabels(verticalLabels);

        phaseCodecTiles.reserve(static_cast<int>(encodedTiles.size()));
        for (const auto &tile : encodedTiles) {
            PhaseCodecTilePreview preview;
            preview.tileX = tile.tileX;
            preview.tileY = tile.tileY;
            preview.hex = QString::fromStdString(tile.hex);
            const int previewIndex = phaseCodecTiles.size();
            phaseCodecTiles.push_back(preview);

            QString matrixText;
            const auto matrix = fcngraph::phase_codec::decodePackedHexToMatrix(
                tile.hex,
                phaseCount,
                blockSize
            );
            for (int row = 0; row < blockSize; ++row) {
                if (row > 0) {
                    matrixText += QLatin1Char('\n');
                }
                for (int column = 0; column < blockSize; ++column) {
                    if (column > 0) {
                        matrixText += QLatin1Char(' ');
                    }
                    matrixText += QString::number(matrix[static_cast<size_t>(row)]
                                                        [static_cast<size_t>(column)]);
                }
            }

            auto *item = new QTableWidgetItem(
                tr("0x%1\n%2").arg(preview.hex, matrixText)
            );
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole, previewIndex);
            QFont tileFont(QStringLiteral("monospace"));
            tileFont.setStyleHint(QFont::Monospace);
            tileFont.setPointSize(9);
            item->setFont(tileFont);
            item->setBackground(QColor("#fbfdff"));
            item->setForeground(QColor("#1f252d"));
            item->setToolTip(tr("Tile (%1, %2), %3x%3")
                             .arg(preview.tileX)
                             .arg(preview.tileY)
                             .arg(blockSize));
            phaseCodecTable->setItem(static_cast<int>(preview.tileY),
                                     static_cast<int>(preview.tileX),
                                     item);
        }

        phaseCodecStatusLabel->setText(
            tr("%1-phase, %2x%2 scan, %3 tile(s)")
                .arg(phaseCount)
                .arg(blockSize)
                .arg(phaseCodecTiles.size()));
        if (phaseCodecShowAllButton != nullptr && phaseCodecShowAllButton->isChecked()) {
            highlightAllPhaseCodecTiles();
        } else {
            clearPhaseCodecHighlight();
        }
    } catch (const std::exception &ex) {
        clearPhaseCodecHighlight();
        phaseCodecStatusLabel->setText(tr("Encoding failed: %1").arg(ex.what()));
    }
}

void MainWindow::slotEncodeClockRegions()
{
    phaseCodecPreviewActive = true;
    updatePhaseCodecPreview();
}

void MainWindow::slotCancelPhaseCodecEncoding()
{
    phaseCodecPreviewActive = false;
    phaseCodecTiles.clear();
    if (phaseCodecShowAllButton != nullptr) {
        const QSignalBlocker blocker(phaseCodecShowAllButton);
        phaseCodecShowAllButton->setChecked(false);
    }
    clearPhaseCodecHighlight();
    if (phaseCodecTable != nullptr) {
        phaseCodecTable->clear();
        phaseCodecTable->setRowCount(0);
        phaseCodecTable->setColumnCount(0);
    }
    if (phaseCodecStatusLabel != nullptr) {
        phaseCodecStatusLabel->setText(tr("Not encoded"));
    }
    if (customStatusBar != nullptr) {
        customStatusBar->addMessage(tr("Phase encoding cancelled"));
    }
}

void MainWindow::slotPhaseCodecModeChanged(int idx)
{
    Q_UNUSED(idx);
    if (phaseCodecPreviewActive) {
        updatePhaseCodecPreview();
    }
    if (structure3DDock != nullptr && structure3DDock->isVisible()) {
        updateStructure3DView();
    }
}

void MainWindow::slotPhaseCodecTileActivated(int row, int column)
{
    if (phaseCodecTable == nullptr) {
        return;
    }
    QTableWidgetItem *item = phaseCodecTable->item(row, column);
    if (item == nullptr) {
        return;
    }

    bool ok = false;
    const int previewIndex = item->data(Qt::UserRole).toInt(&ok);
    if (!ok || previewIndex < 0 || previewIndex >= phaseCodecTiles.size()) {
        return;
    }
    if (phaseCodecShowAllButton != nullptr && phaseCodecShowAllButton->isChecked()) {
        const QSignalBlocker blocker(phaseCodecShowAllButton);
        phaseCodecShowAllButton->setChecked(false);
    }
    highlightPhaseCodecTile(phaseCodecTiles[previewIndex]);
}

QWidget* MainWindow::createCellWidget(const QString &text, CellType type){
    int clockIdx = qMax(0, selectedClockPhase());
    QCADCellItem item(type);
    QIcon icon(item.image(clockIdx));
    QToolButton *button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(42, 30);
    button->setCheckable(true);
    buttonGroup->addButton(button, int(type));

    QGridLayout *layout = new QGridLayout;
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(button, 0, 0, Qt::AlignHCenter);
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumHeight(20);
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    layout->addWidget(label, 1, 0, Qt::AlignCenter);

    QWidget *widget = new QWidget;
    widget->setLayout(layout);

    return widget;
}

QWidget* MainWindow::createClockSchemeWidget(const QString &text, const QString &image){
    QToolButton *button = new QToolButton;
    button->setText(text);
    button->setIcon(QIcon(image));
    button->setIconSize(QSize(56, 56));
    button->setFixedSize(82, 66);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setCheckable(true);
    clockSchemeGroup->addButton(button);

    QGridLayout *layout = new QGridLayout;
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(button, 0, 0, Qt::AlignHCenter);
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumHeight(22);
    label->setStyleSheet(QStringLiteral("background: transparent;"));
    layout->addWidget(label, 1, 0, Qt::AlignCenter);
    QWidget *widget = new QWidget;
    widget->setLayout(layout);

    return widget;
}


void MainWindow::updateLayerAndCellZValue()
{
    int num = layers.size();
    for(int i = 0; i < num; ++i)
    {
        auto itemGroup = layers[i];
        for(QGraphicsItem* item :itemGroup){
            item->setZValue(i);
            simon::layer_index(*static_cast<QCADCellItem *>(item)) = i;
        }
    }
}
void MainWindow::slotClockIndexChanged(int idx)
{
    Q_UNUSED(idx);
    const int phase = selectedClockPhase();
    scene->setCurrentClockIndex(phase);
    if (phase < 0) {
        return;
    }
    QList<QGraphicsItem *> items = scene->selectedItems();

    if(items.isEmpty()) 
        return;

    for(QGraphicsItem *item: items)
    {
        if (item->type() != QCADCellItem::Type) {
            continue;
        }
        simon::timezone(*static_cast<QCADCellItem *>(item)) = phase; //时钟域，由控制面板传递
        //添加更新操作
    }
    setDirty(true);
    pushUndoSnapshot();
}

void MainWindow::slotLayerActiveChanged(int idx){
    scene->setCurrentLayerIndex(idx);
}

void MainWindow::slotToggleClockGrid(bool on)
{
    if (scene != nullptr) {
        scene->setClockGridVisible(on);
    }
    if (customStatusBar != nullptr) {
        QString message = on ? QStringLiteral("Clock grid enabled")
                             : QStringLiteral("Clock grid disabled");
        customStatusBar->addMessage(message);
    }
}

void MainWindow::setHighQualityMode(bool on)
{
    if (view != nullptr) {
        view->setHighQualityMode(on);
    }
    if (scene != nullptr) {
        scene->setHighQualityMode(on);
    }
}

void MainWindow::slotToggleHighQualityView(bool on)
{
    setHighQualityMode(on);
    QString message = on ? QStringLiteral("HD view enabled") : QStringLiteral("HD view disabled");
    customStatusBar->addMessage(message);
}

void MainWindow::viewModeChange() {
    if (selectModeButton->isChecked()) {
        setEditMode(EditMode::Select);
        scene->setEditMode(EditMode::Select);
    } else if (insertModeButton->isChecked()) {
        setEditMode(EditMode::Insert);
        scene->setEditMode(EditMode::Insert);
    } else if (dragModeButton->isChecked()) {
        setEditMode(EditMode::DragScene);
        scene->setEditMode(EditMode::DragScene);
    }
}

void MainWindow::setEditMode(EditMode mode) {
    currentMode = mode;
    switch (mode) {
        case EditMode::Select:
            view->setDragMode(QGraphicsView::RubberBandDrag);
            view->setInteractive(true);  // 允许选择和移动item
            view->unsetCursor();
            break;
        case EditMode::Insert:
            view->setDragMode(QGraphicsView::NoDrag);
            view->setInteractive(true);  // 不允许交互，以便在鼠标点击时插入新item
            view->unsetCursor();
            break;
        case EditMode::ClockScheme:
            view->setDragMode(QGraphicsView::NoDrag);
            view->setInteractive(true);
            view->unsetCursor();
            break;
        case EditMode::DragScene:
            view->setDragMode(QGraphicsView::ScrollHandDrag);
            view->setInteractive(false);  // 允许拖动场景，但不允许选择或移动item
            view->setCursor(Qt::OpenHandCursor);
            break;
    }
}
void MainWindow::slotCellItemInserted(QCADCellItem *cellItem){
    int idx = layerComboBox->currentIndex();
    addCellToScene(cellItem, idx);
    setDirty(true); 
    pushUndoSnapshot();

    // 获取 cellItem 的坐标
    QPointF pos = cellItem->pos();
    QString message = QString("Inserted item at Layer %1, Position (%2, %3)")
                      .arg(idx)
                      .arg(pos.x())
                      .arg(pos.y());

    // 在状态栏打印消息
    customStatusBar->addMessage(message);
}


void MainWindow::slotCellItemInserted(QCADCellItem* cellItem, int layerIndex){
    addCellToScene(cellItem, layerIndex);
    setDirty(true); //这行代码很重要，否则操作的界面无法保存
    if (!isBatchUpdating) {
        pushUndoSnapshot();
    }

}

void MainWindow::slotDeleteItem()
{
    QList<QGraphicsItem *> selectedItems = scene->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }
    QHash<int, QVector<int>> fastSelectionsByLayer;
    QList<QGraphicsItem *> regularSelections;

    for (QGraphicsItem *item : selectedItems)
    {
        const QVariant fastLayer = item->data(QCADScene::FastLayerRole);
        const QVariant fastIndex = item->data(QCADScene::FastIndexRole);
        if (fastLayer.isValid() && fastIndex.isValid()) {
            fastSelectionsByLayer[fastLayer.toInt()].push_back(fastIndex.toInt());
            continue;
        }
        regularSelections.push_back(item);
    }

    for (QGraphicsItem *item : regularSelections)
    {
        // 从 scene 中移除
        scene->removeItem(item);

        // 从 layers 中查找并移除
        for (int i = 0; i < layers.size(); ++i)
        {
            int index = layers[i].indexOf(item);
            if (index != -1)
            {
                layers[i].remove(index);  // 从该层移除
                break; // 找到后即可退出循环
            }
        }

        // 删除对象
        delete item;
    }

    for (auto it = fastSelectionsByLayer.begin(); it != fastSelectionsByLayer.end(); ++it)
    {
        auto &indices = it.value();
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int index : indices) {
            scene->removeFastCell(it.key(), index);
        }
    }
    setDirty(true);
    pushUndoSnapshot();
}

QVector<MainWindow::ClipboardCell> MainWindow::selectedCellsForClipboard() const
{
    QVector<ClipboardCell> selectedCells;
    if (scene == nullptr) {
        return selectedCells;
    }

    const QList<QGraphicsItem *> selectedItems = scene->selectedItems();
    selectedCells.reserve(selectedItems.size());
    for (QGraphicsItem *item : selectedItems) {
        if (item == nullptr || item->type() != QCADCellItem::Type) {
            continue;
        }
        const auto *cellItem = static_cast<const QCADCellItem *>(item);
        ClipboardCell copied;
        copied.cell.x = static_cast<int>(std::round(simon::x(*cellItem)));
        copied.cell.y = static_cast<int>(std::round(simon::y(*cellItem)));
        copied.cell.layer = qBound(0, static_cast<int>(std::round(item->zValue())), qMax(0, layers.size() - 1));
        copied.cell.phase = qBound(0, simon::timezone(*cellItem), 3);
        copied.cell.type = cellItem->getCellType();
        copied.cell.name = QString::fromStdString(simon::name(*cellItem));
        selectedCells.push_back(copied);
    }

    std::sort(selectedCells.begin(), selectedCells.end(), [](const ClipboardCell &lhs,
                                                             const ClipboardCell &rhs) {
        if (lhs.cell.layer != rhs.cell.layer) {
            return lhs.cell.layer < rhs.cell.layer;
        }
        if (lhs.cell.y != rhs.cell.y) {
            return lhs.cell.y < rhs.cell.y;
        }
        return lhs.cell.x < rhs.cell.x;
    });
    return selectedCells;
}

void MainWindow::slotCopyItems()
{
    clipboardCells = selectedCellsForClipboard();
    clipboardPasteCount = 0;
    if (clipboardCells.isEmpty()) {
        return;
    }

    clipboardAnchor = QPoint(clipboardCells.first().cell.x, clipboardCells.first().cell.y);
    for (const ClipboardCell &copied : clipboardCells) {
        clipboardAnchor.setX(qMin(clipboardAnchor.x(), copied.cell.x));
        clipboardAnchor.setY(qMin(clipboardAnchor.y(), copied.cell.y));
    }
    customStatusBar->addMessage(tr("Copied %1 cell(s)").arg(clipboardCells.size()));
}

void MainWindow::slotCutItems()
{
    slotCopyItems();
    if (!clipboardCells.isEmpty()) {
        slotDeleteItem();
    }
}

bool MainWindow::positionOccupied(int layer, int x, int y) const
{
    if (layer < 0 || layer >= layers.size()) {
        return false;
    }
    for (QGraphicsItem *item : layers[layer]) {
        if (item == nullptr) {
            continue;
        }
        if (static_cast<int>(std::round(item->x())) == x &&
            static_cast<int>(std::round(item->y())) == y) {
            return true;
        }
    }
    return false;
}

void MainWindow::ensureLayerExists(int layer)
{
    while (layer >= layers.size()) {
        const int nextLayer = layers.size();
        layerComboBox->AddItem(tr("New Layer %1").arg(nextLayer), true);
        layers.push_back(QVector<QGraphicsItem*>());
    }
}

void MainWindow::addCellToScene(QCADCellItem *cellItem, int layerIndex)
{
    if (cellItem == nullptr) {
        return;
    }
    ensureLayerExists(layerIndex);
    cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));
    cellItem->setZValue(layerIndex);
    cellItem->setVisible(true);
    layers[layerIndex].push_back(cellItem);
    scene->addItem(cellItem);
    if (cellItem->myCellType == CellType::InputCell) {
        inputname.append(cellItem->IOName);
        emit savedinputname(inputname);
    }
}

void MainWindow::slotPasteItems()
{
    if (clipboardCells.isEmpty()) {
        return;
    }

    ++clipboardPasteCount;
    const int baseOffset = GRID_SIZE * clipboardPasteCount;
    QVector<QGraphicsItem *> pastedItems;
    int extraOffset = 0;

    for (const ClipboardCell &copied : clipboardCells) {
        int targetLayer = copied.cell.layer;
        if (targetLayer < 0) {
            targetLayer = qMax(0, layerComboBox->currentIndex());
        }
        ensureLayerExists(targetLayer);

        int x = copied.cell.x + baseOffset + extraOffset;
        int y = copied.cell.y + baseOffset + extraOffset;
        while (positionOccupied(targetLayer, x, y)) {
            extraOffset += GRID_SIZE;
            x = copied.cell.x + baseOffset + extraOffset;
            y = copied.cell.y + baseOffset + extraOffset;
        }

        auto *cellItem = new QCADCellItem(x, y, targetLayer, copied.cell.phase,
                                          copied.cell.type, copied.cell.name);
        addCellToScene(cellItem, targetLayer);
        pastedItems.push_back(cellItem);
    }

    scene->clearSelection();
    for (QGraphicsItem *item : pastedItems) {
        item->setSelected(true);
    }
    setDirty(true);
    pushUndoSnapshot();
}

bool MainWindow::snapshotCellsEqual(const SnapshotCell &lhs, const SnapshotCell &rhs) const
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.layer == rhs.layer &&
           lhs.phase == rhs.phase && lhs.type == rhs.type && lhs.name == rhs.name;
}

bool MainWindow::clockRegionsEqual(const QCADScene::ClockRegionRecord &lhs,
                                   const QCADScene::ClockRegionRecord &rhs) const
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.phase == rhs.phase;
}

bool MainWindow::snapshotsEqual(const DesignSnapshot &lhs, const DesignSnapshot &rhs) const
{
    if (lhs.layerNames != rhs.layerNames ||
        lhs.cellsByLayer.size() != rhs.cellsByLayer.size() ||
        lhs.clockRegions.size() != rhs.clockRegions.size()) {
        return false;
    }
    for (int layer = 0; layer < lhs.cellsByLayer.size(); ++layer) {
        if (lhs.cellsByLayer[layer].size() != rhs.cellsByLayer[layer].size()) {
            return false;
        }
        for (int index = 0; index < lhs.cellsByLayer[layer].size(); ++index) {
            if (!snapshotCellsEqual(lhs.cellsByLayer[layer][index], rhs.cellsByLayer[layer][index])) {
                return false;
            }
        }
    }
    for (int index = 0; index < lhs.clockRegions.size(); ++index) {
        if (!clockRegionsEqual(lhs.clockRegions[index], rhs.clockRegions[index])) {
            return false;
        }
    }
    return true;
}

MainWindow::DesignSnapshot MainWindow::captureDesignSnapshot() const
{
    DesignSnapshot snapshot;
    snapshot.layerNames.reserve(layers.size());
    snapshot.cellsByLayer.resize(layers.size());

    for (int layer = 0; layer < layers.size(); ++layer) {
        const QStandardItem *layerItem = layerComboBox->GetItem(layer);
        snapshot.layerNames.push_back(layerItem != nullptr ? layerItem->text()
                                                           : tr("Layer %1").arg(layer));
        auto &snapshotLayer = snapshot.cellsByLayer[layer];
        snapshotLayer.reserve(layers[layer].size());
        for (QGraphicsItem *item : layers[layer]) {
            if (item == nullptr || item->type() != QCADCellItem::Type) {
                continue;
            }
            const auto *cellItem = static_cast<const QCADCellItem *>(item);
            SnapshotCell cell;
            cell.x = static_cast<int>(std::round(simon::x(*cellItem)));
            cell.y = static_cast<int>(std::round(simon::y(*cellItem)));
            cell.layer = layer;
            cell.phase = qBound(0, simon::timezone(*cellItem), 3);
            cell.type = cellItem->getCellType();
            cell.name = QString::fromStdString(simon::name(*cellItem));
            snapshotLayer.push_back(cell);
        }
        std::sort(snapshotLayer.begin(), snapshotLayer.end(), [](const SnapshotCell &lhs,
                                                                 const SnapshotCell &rhs) {
            if (lhs.y != rhs.y) {
                return lhs.y < rhs.y;
            }
            return lhs.x < rhs.x;
        });
    }

    snapshot.clockRegions = scene != nullptr ? scene->clockRegions()
                                             : QVector<QCADScene::ClockRegionRecord>();
    return snapshot;
}

void MainWindow::restoreDesignSnapshot(const DesignSnapshot &snapshot, bool markDirty)
{
    restoringSnapshot = true;
    scene->clearSelection();
    scene->clearFastRender();

    for (auto &layer : layers) {
        for (QGraphicsItem *item : layer) {
            if (item != nullptr) {
                scene->removeItem(item);
                delete item;
            }
        }
    }
    layers.clear();
    inputname.clear();

    {
        QSignalBlocker blocker(layerComboBox);
        while (layerComboBox->GetNumRows() > 0) {
            layerComboBox->RemoveItem(layerComboBox->GetNumRows() - 1);
        }
        for (int layer = 0; layer < snapshot.layerNames.size(); ++layer) {
            layerComboBox->AddItem(snapshot.layerNames[layer], true);
            layers.push_back(QVector<QGraphicsItem*>());
        }
    }

    if (layers.isEmpty()) {
        layerComboBox->AddItem(tr("Main Cell Layer"), true);
        layers.push_back(QVector<QGraphicsItem*>());
    }

    for (int layer = 0; layer < snapshot.cellsByLayer.size(); ++layer) {
        ensureLayerExists(layer);
        for (const SnapshotCell &cell : snapshot.cellsByLayer[layer]) {
            auto *cellItem = new QCADCellItem(cell.x, cell.y, layer, cell.phase, cell.type, cell.name);
            addCellToScene(cellItem, layer);
        }
    }

    scene->restoreClockRegions(snapshot.clockRegions);
    layerComboBox->setCurrentIndex(qBound(0, layerComboBox->currentIndex(), qMax(0, layerComboBox->GetNumRows() - 1)));
    scene->setCurrentLayerIndex(qMax(0, layerComboBox->currentIndex()));
    emit savedinputname(inputname);

    restoringSnapshot = false;
    if (markDirty) {
        setDirty(true);
    }
}

void MainWindow::updateUndoRedoActions()
{
    if (undoAction != nullptr) {
        undoAction->setEnabled(undoSnapshotIndex > 0);
    }
    if (redoAction != nullptr) {
        redoAction->setEnabled(undoSnapshotIndex >= 0 &&
                               undoSnapshotIndex < undoSnapshots.size() - 1);
    }
}

void MainWindow::resetUndoHistory()
{
    undoSnapshots.clear();
    undoSnapshotIndex = -1;
    pushUndoSnapshot();
}

void MainWindow::pushUndoSnapshot()
{
    if (restoringSnapshot || scene == nullptr || layerComboBox == nullptr) {
        return;
    }

    const DesignSnapshot snapshot = captureDesignSnapshot();
    if (undoSnapshotIndex >= 0 && undoSnapshotIndex < undoSnapshots.size() &&
        snapshotsEqual(undoSnapshots[undoSnapshotIndex], snapshot)) {
        updateUndoRedoActions();
        return;
    }

    while (undoSnapshots.size() > undoSnapshotIndex + 1) {
        undoSnapshots.removeLast();
    }
    undoSnapshots.push_back(snapshot);
    if (undoSnapshots.size() > 100) {
        undoSnapshots.removeFirst();
    }
    undoSnapshotIndex = undoSnapshots.size() - 1;
    updateUndoRedoActions();
}

void MainWindow::slotUndo()
{
    if (undoSnapshotIndex <= 0 || undoSnapshotIndex >= undoSnapshots.size()) {
        return;
    }
    --undoSnapshotIndex;
    restoreDesignSnapshot(undoSnapshots[undoSnapshotIndex], true);
    updateUndoRedoActions();
}

void MainWindow::slotRedo()
{
    if (undoSnapshotIndex < 0 || undoSnapshotIndex >= undoSnapshots.size() - 1) {
        return;
    }
    ++undoSnapshotIndex;
    restoreDesignSnapshot(undoSnapshots[undoSnapshotIndex], true);
    updateUndoRedoActions();
}

void MainWindow::checkCellInserted(QVector<QVector<QGraphicsItem*>> &_layers, QCADCellItem* cellItem, int cell_layer, int x_coord, int y_coord)
{
    if (cell_layer < 0 || cell_layer >= _layers.size()) {  
        delete cellItem;
        return;  
    } 

    if (isBatchUpdating) {
        if (cell_layer >= batchOccupiedByLayer.size()) {
            batchOccupiedByLayer.resize(cell_layer + 1);
        }
        const quint64 key = packSceneCoord(x_coord, y_coord);
        if (batchOccupiedByLayer[cell_layer].contains(key)) {
            delete cellItem;
            return;
        }
        batchOccupiedByLayer[cell_layer].insert(key);
        slotCellItemInserted(cellItem, cell_layer);
        return;
    }

    for (QGraphicsItem* item : layers[cell_layer]) {  
        // 检查坐标是否匹配  
        if (item->x() == x_coord && item->y() == y_coord) {  
            delete cellItem;
            return; // 找到存在的cell
        }  
    }
    slotCellItemInserted(cellItem, cell_layer);
}
