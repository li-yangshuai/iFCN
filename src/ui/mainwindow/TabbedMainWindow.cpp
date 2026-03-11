#include "ui/mainwindow/TabbedMainWindow.h"
#include "ui/mainwindow/MainWindow.h"
#include <QFileInfo>

TabbedMainWindow::TabbedMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    tabWidget = new QTabWidget(this);
    tabWidget->setMovable(true);
    tabWidget->setTabsClosable(true);
    tabWidget->setDocumentMode(true);
    setCentralWidget(tabWidget);

    connect(tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *w = tabWidget->widget(index);
        tabWidget->removeTab(index);
        delete w;
        if (tabWidget->count() == 0) {
            openNewTab();
        }
    });

    openNewTab();
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
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == "ifcn") {
        editor->mapIfcnFile(fileName);
    } else {
        editor->loadFile(fileName);
    }
    updateTabTitle(editor, fileName);
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
