#ifndef TABBEDMAINWINDOW_H
#define TABBEDMAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QString>

class MainWindow;

class TabbedMainWindow : public QMainWindow
{
public:
    explicit TabbedMainWindow(QWidget *parent = nullptr);

    void openNewTab();
    void openFileInNewTab(const QString &fileName);

private:
    MainWindow *addEditorTab(const QString &title);
    void updateTabTitle(MainWindow *editor, const QString &fileName);

    QTabWidget *tabWidget;
};

#endif
