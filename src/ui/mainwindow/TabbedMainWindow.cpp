#include "ui/mainwindow/TabbedMainWindow.h"
#include "ui/mainwindow/MainWindow.h"
#include <QApplication>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPointer>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QWindow>

namespace {
const QScreen *screenForWidget(const QWidget *widget)
{
    if (widget != nullptr) {
        const QWidget *topLevel = widget->window();
        if (topLevel != nullptr && topLevel->windowHandle() != nullptr
            && topLevel->windowHandle()->screen() != nullptr) {
            return topLevel->windowHandle()->screen();
        }
    }
    return qApp != nullptr ? qApp->primaryScreen() : nullptr;
}

QSize initialMainWindowSize(const QScreen *screen)
{
    constexpr double kPreferredWidthRatio = 0.92;
    constexpr double kPreferredHeightRatio = 0.90;
    const QSize minimumComfortSize(1100, 720);
    if (screen == nullptr) {
        return QSize(1680, 980);
    }

    const QRect available = screen->availableGeometry();
    const int availableWidth = qMax(1, available.width());
    const int availableHeight = qMax(1, available.height());
    const int width = qMax(qMin(minimumComfortSize.width(), availableWidth),
                           static_cast<int>(availableWidth * kPreferredWidthRatio));
    const int height = qMax(qMin(minimumComfortSize.height(), availableHeight),
                            static_cast<int>(availableHeight * kPreferredHeightRatio));
    return QSize(width, height);
}

void centerWindowOnScreen(QWidget *window, const QScreen *screen)
{
    if (window == nullptr || screen == nullptr) {
        return;
    }

    const QRect available = screen->availableGeometry();
    QRect target(QPoint(0, 0), window->size());
    target.moveCenter(available.center());
    window->move(target.topLeft());
}

bool isSupportedDroppedLayoutFile(const QString &fileName)
{
    const QFileInfo info(fileName);
    if (!info.isFile()) {
        return false;
    }

    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("ifcn") || suffix == QStringLiteral("qca");
}

QString firstSupportedDroppedLayoutFile(const QMimeData *mimeData)
{
    if (mimeData == nullptr || !mimeData->hasUrls()) {
        return QString();
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString fileName = url.toLocalFile();
        if (isSupportedDroppedLayoutFile(fileName)) {
            return fileName;
        }
    }
    return QString();
}
} // namespace

TabbedMainWindow::TabbedMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("iFCN — FCN/QCA Layout Designer"));
    tabWidget = new QTabWidget(this);
    tabWidget->setMovable(true);
    tabWidget->setTabsClosable(true);
    tabWidget->setDocumentMode(true);
    tabWidget->setAcceptDrops(true);
    tabWidget->installEventFilter(this);
    setCentralWidget(tabWidget);
    setAcceptDrops(true);
    qApp->installEventFilter(this);

    connect(tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *w = tabWidget->widget(index);
        auto *editor = qobject_cast<MainWindow *>(w);
        if (editor != nullptr && !editor->close()) {
            return;
        }
        tabWidget->removeTab(index);
        w->deleteLater();
        if (tabWidget->count() == 0) {
            openNewTab();
        }
    });

    openNewTab();

    const QScreen *targetScreen = screenForWidget(this);
    const QSize initialSize = initialMainWindowSize(targetScreen);
    resize(initialSize);
    setMinimumSize(qMin(1080, initialSize.width()), qMin(680, initialSize.height()));
    centerWindowOnScreen(this, targetScreen);
}

TabbedMainWindow::~TabbedMainWindow()
{
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
}

bool TabbedMainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == tabWidget && event != nullptr) {
        switch (event->type()) {
            case QEvent::DragEnter: {
                auto *dragEvent = static_cast<QDragEnterEvent *>(event);
                if (!firstSupportedDroppedLayoutFile(dragEvent->mimeData()).isEmpty()) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
                break;
            }
            case QEvent::DragMove: {
                auto *dragEvent = static_cast<QDragMoveEvent *>(event);
                if (!firstSupportedDroppedLayoutFile(dragEvent->mimeData()).isEmpty()) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
                break;
            }
            case QEvent::Drop: {
                auto *dropEvent = static_cast<QDropEvent *>(event);
                const QString fileName = firstSupportedDroppedLayoutFile(dropEvent->mimeData());
                if (!fileName.isEmpty()) {
                    openFileInNewTab(fileName);
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    if (event != nullptr && event->type() == QEvent::Show) {
        auto *popup = qobject_cast<QWidget *>(watched);
        if (shouldCenterPopupWidget(popup)) {
            scheduleCenterPopupWidget(popup);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void TabbedMainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event != nullptr && !firstSupportedDroppedLayoutFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void TabbedMainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event != nullptr && !firstSupportedDroppedLayoutFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragMoveEvent(event);
}

void TabbedMainWindow::dropEvent(QDropEvent *event)
{
    const QString fileName = event != nullptr
        ? firstSupportedDroppedLayoutFile(event->mimeData())
        : QString();
    if (!fileName.isEmpty()) {
        openFileInNewTab(fileName);
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dropEvent(event);
}

bool TabbedMainWindow::shouldCenterPopupWidget(QWidget *popup) const
{
    if (popup == nullptr || !popup->isWindow() || popup == this) {
        return false;
    }

    if (qobject_cast<QDialog *>(popup) != nullptr) {
        return true;
    }

    return popup->inherits("WaveformWindow") || popup->inherits("Typewindow");
}

void TabbedMainWindow::scheduleCenterPopupWidget(QWidget *popup) const
{
    QPointer<QWidget> guardedPopup(popup);
    const auto centerIfVisible = [this, guardedPopup]() {
        if (guardedPopup != nullptr && guardedPopup->isVisible()) {
            centerPopupWidget(guardedPopup);
        }
    };

    QTimer::singleShot(0, this, centerIfVisible);
    QTimer::singleShot(50, this, centerIfVisible);
    QTimer::singleShot(150, this, centerIfVisible);
}

void TabbedMainWindow::centerPopupWidget(QWidget *popup) const
{
    if (popup == nullptr) {
        return;
    }

    const QRect hostRect = isVisible() ? frameGeometry() : geometry();
    const QSize popupSize = popup->frameGeometry().isValid()
        ? popup->frameGeometry().size()
        : popup->sizeHint();
    QPoint topLeft(hostRect.center().x() - popupSize.width() / 2,
                   hostRect.center().y() - popupSize.height() / 2);

    const QScreen *screen = screenForWidget(this);
    if (screen != nullptr) {
        const QRect available = screen->availableGeometry();
        topLeft.setX(qBound(available.left(), topLeft.x(), available.right() - popupSize.width() + 1));
        topLeft.setY(qBound(available.top(), topLeft.y(), available.bottom() - popupSize.height() + 1));
    }
    popup->move(topLeft);
}

MainWindow *TabbedMainWindow::addEditorTab(const QString &title)
{
    MainWindow *editor = new MainWindow(tabWidget);
    editor->setTabHost(this);

    int idx = tabWidget->addTab(editor, title);
    tabWidget->setCurrentIndex(idx);

    connect(editor, &MainWindow::savedname, this, [this, editor](const QString &fileName) {
        updateTabTitle(editor, fileName);
    });

    return editor;
}

void TabbedMainWindow::openNewTab()
{
    addEditorTab(tr("Untitled"));
}

void TabbedMainWindow::openFileInNewTab(const QString &fileName)
{
    MainWindow *editor = addEditorTab(tr("Untitled"));
    editor->disableStartupRestore();
    editor->loadFile(fileName);
    updateTabTitle(editor, fileName);
}

MainWindow *TabbedMainWindow::openVerilogSourceInNewTab(const QString &sourceText,
                                                        const QString &sourcePath)
{
    const QString sourceName = QFileInfo(sourcePath).fileName();
    MainWindow *editor = addEditorTab(sourceName.isEmpty()
        ? tr("Verilog Source")
        : sourceName);
    editor->disableStartupRestore();
    editor->setVerilogSourceContent(sourceText, sourcePath);
    return editor;
}

void TabbedMainWindow::updateTabTitle(MainWindow *editor, const QString &fileName)
{
    int idx = tabWidget->indexOf(editor);
    if (idx < 0) {
        return;
    }
    QString title = fileName.isEmpty() ? tr("Untitled") : QFileInfo(fileName).fileName();
    tabWidget->setTabText(idx, title);
}
