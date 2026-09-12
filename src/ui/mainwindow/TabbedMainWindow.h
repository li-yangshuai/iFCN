#ifndef TABBEDMAINWINDOW_H
#define TABBEDMAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QString>

class QEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class MainWindow;
class QWidget;

class TabbedMainWindow : public QMainWindow
{
public:
    explicit TabbedMainWindow(QWidget *parent = nullptr);
    ~TabbedMainWindow() override;

    void openNewTab();
    MainWindow *openFileInNewTab(const QString &fileName, bool forceGateLevelMapping = false);
    MainWindow *openVerilogSourceInNewTab(const QString &sourceText,
                                          const QString &sourcePath);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool shouldCenterPopupWidget(QWidget *popup) const;
    void scheduleCenterPopupWidget(QWidget *popup) const;
    void centerPopupWidget(QWidget *popup) const;
    MainWindow *addEditorTab(const QString &title);
    void updateTabTitle(MainWindow *editor, const QString &fileName);

    QTabWidget *tabWidget;
};

#endif
