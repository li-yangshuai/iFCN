#include "CustomStatusBar.h"

CustomStatusBar::CustomStatusBar(QWidget *parent) : QWidget(parent)
{
    // 创建内容区域
    statusContent = new QWidget(this);
    statusLayout = new QVBoxLayout(statusContent);

    // 创建 QScrollArea 并将内容区域添加进去
    scrollArea = new QScrollArea(this);
    scrollArea->setWidget(statusContent);
    scrollArea->setWidgetResizable(true);

    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    scrollArea->setMinimumHeight(140);  // 设置最小高度

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(scrollArea);
    setLayout(mainLayout);
}

void CustomStatusBar::addMessage(QString &message)
{
    QTextEdit *newMessageText = new QTextEdit(this);
    newMessageText->setReadOnly(true);  // 设置为只读模式
    newMessageText->setPlainText(message);  // 添加文本消息
    newMessageText->setWordWrapMode(QTextOption::WordWrap);  // 设置自动换行

    newMessageText->setStyleSheet("QTextEdit { margin: 0px; padding: 0px; border: none; line-height: 1.2em; }");  // 减小行高，移除边距

    newMessageText->setFixedHeight(25);  // 设置合适的高度

    // 将文本消息添加到布局中
    statusLayout->addWidget(newMessageText);
    statusLayout->setSpacing(0);  


    QSpacerItem *spacer = new QSpacerItem(20, 20, QSizePolicy::Minimum, QSizePolicy::Expanding);
    statusLayout->addItem(spacer);  // 添加弹簧

    // 强制刷新布局和滚动到最新的消息
    QTimer::singleShot(0, this, [this]() {
        scrollArea->updateGeometry();  // 更新滚动区域的布局
        QApplication::processEvents();  // 处理事件队列，确保布局更新
        scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
    });
}



