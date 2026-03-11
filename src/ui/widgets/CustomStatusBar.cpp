#include "CustomStatusBar.h"

CustomStatusBar::CustomStatusBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("CustomStatusBar");

    // 创建内容区域
    statusContent = new QWidget(this);
    statusLayout = new QVBoxLayout(statusContent);
    statusLayout->setContentsMargins(4, 4, 4, 6);
    statusLayout->setSpacing(2);

    // 创建 QScrollArea 并将内容区域添加进去
    scrollArea = new QScrollArea(this);
    scrollArea->setWidget(statusContent);
    scrollArea->setWidgetResizable(true);

    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    scrollArea->setMinimumHeight(140);  // 设置最小高度

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(scrollArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(mainLayout);

    // 留一个弹簧避免最后一行被遮挡
    statusLayout->addStretch(1);
}

void CustomStatusBar::addMessage(QString &message)
{
    QTextEdit *newMessageText = new QTextEdit(this);
    newMessageText->setObjectName("statusMessage");
    newMessageText->setReadOnly(true);  // 设置为只读模式
    newMessageText->setPlainText(message);  // 添加文本消息
    newMessageText->setWordWrapMode(QTextOption::WordWrap);  // 设置自动换行

    newMessageText->setFixedHeight(32);  // 字号更大，留出可读高度
    newMessageText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // 将文本消息添加到布局中
    statusLayout->insertWidget(statusLayout->count() - 1, newMessageText);

    // 强制刷新布局和滚动到最新的消息
    QTimer::singleShot(0, this, [this]() {
        scrollArea->updateGeometry();  // 更新滚动区域的布局
        QApplication::processEvents();  // 处理事件队列，确保布局更新
        scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
    });
}
