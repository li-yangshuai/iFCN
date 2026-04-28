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

private:
    void ensureOperationWidget();
    void refreshOperationElapsed();

    QWidget *statusContent;
    QScrollArea *scrollArea;
    QVBoxLayout *statusLayout;
    QWidget *operationWidget = nullptr;
    QLabel *operationTitleLabel = nullptr;
    QLabel *operationDetailLabel = nullptr;
    QProgressBar *operationProgressBar = nullptr;
    QTimer *operationTimer = nullptr;
    QElapsedTimer operationElapsed;
    QString operationDetail;
};

#endif // CUSTOMSTATUSBAR_H
