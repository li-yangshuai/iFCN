#ifndef CUSTOMSTATUSBAR_H
#define CUSTOMSTATUSBAR_H

#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>  
#include <QScrollBar>  
#include <QApplication>  // 用于强制处理事件队列
#include<QTextEdit>
#include <QElapsedTimer>
#include <QProgressBar>
#include <QList>

class QToolButton;
class CustomStatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit CustomStatusBar(QWidget *parent = nullptr);
    void addMessage(const QString &message);
    void startOperation(const QString &title, const QString &detail);
    void updateOperation(const QString &detail, int value = -1, int maximum = 0);
    void finishOperation(const QString &message);
    void failOperation(const QString &message);
    void setMessagesMuted(bool muted);
    bool messagesMuted() const;

private:
    void ensureOperationWidget();
    void showOperationWidget();
    void refreshOperationElapsed();
    void setSummaryState(const QString &label, const QString &state);
    void scheduleOperationHide(int milliseconds);

    QVBoxLayout *mainLayout = nullptr;
    QWidget *summaryWidget = nullptr;
    QLabel *summaryBadgeLabel = nullptr;
    QLabel *latestMessageLabel = nullptr;
    QToolButton *detailsButton = nullptr;
    QWidget *statusContent = nullptr;
    QScrollArea *scrollArea = nullptr;
    QVBoxLayout *statusLayout = nullptr;
    QList<QLabel *> messageLabels;
    QWidget *operationWidget = nullptr;
    QLabel *operationTitleLabel = nullptr;
    QLabel *operationDetailLabel = nullptr;
    QProgressBar *operationProgressBar = nullptr;
    QTimer *operationTimer = nullptr;
    QTimer *operationHideTimer = nullptr;
    QElapsedTimer operationElapsed;
    QString operationDetail;
    bool statusMessagesMuted = false;
    bool operationHasReportedProgress = false;
};

#endif // CUSTOMSTATUSBAR_H
