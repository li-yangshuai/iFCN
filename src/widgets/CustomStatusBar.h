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
class CustomStatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit CustomStatusBar(QWidget *parent = nullptr);
    void addMessage(QString &message);

private:
    QWidget *statusContent;
    QScrollArea *scrollArea;
    QVBoxLayout *statusLayout;
};

#endif // CUSTOMSTATUSBAR_H
