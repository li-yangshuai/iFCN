#include "CustomStatusBar.h"
#include <QHBoxLayout>

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

void CustomStatusBar::addMessage(const QString &message)
{
    QTextEdit *newMessageText = new QTextEdit(this);
    newMessageText->setObjectName("statusMessage");
    newMessageText->setReadOnly(true);  // 设置为只读模式
    newMessageText->setPlainText(message);  // 添加文本消息
    newMessageText->setWordWrapMode(QTextOption::WordWrap);  // 设置自动换行

    const int lineCount = message.count('\n') + 1;
    const int textHeight = newMessageText->fontMetrics().lineSpacing() * lineCount + 18;
    newMessageText->setFixedHeight(qBound(32, textHeight, 180));  // 字号更大，留出可读高度
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

void CustomStatusBar::startOperation(const QString &title, const QString &detail)
{
    ensureOperationWidget();
    operationTitleLabel->setText(title);
    operationDetail = detail;
    operationProgressBar->setRange(0, 0);
    operationProgressBar->setValue(0);
    operationWidget->show();
    operationElapsed.restart();
    operationTimer->start(1000);
    refreshOperationElapsed();
}

void CustomStatusBar::updateOperation(const QString &detail, int value, int maximum)
{
    ensureOperationWidget();
    operationDetail = detail;
    if (maximum > 0) {
        operationProgressBar->setRange(0, maximum);
        operationProgressBar->setValue(qBound(0, value, maximum));
    } else {
        operationProgressBar->setRange(0, 0);
    }
    operationWidget->show();
    refreshOperationElapsed();
}

void CustomStatusBar::finishOperation(const QString &message)
{
    ensureOperationWidget();
    operationTimer->stop();
    operationProgressBar->setRange(0, 100);
    operationProgressBar->setValue(100);
    operationTitleLabel->setText(tr("Completed"));
    operationDetail = message;
    refreshOperationElapsed();
}

void CustomStatusBar::failOperation(const QString &message)
{
    ensureOperationWidget();
    operationTimer->stop();
    operationProgressBar->setRange(0, 100);
    operationProgressBar->setValue(0);
    operationTitleLabel->setText(tr("Failed"));
    operationDetail = message;
    refreshOperationElapsed();
}

void CustomStatusBar::ensureOperationWidget()
{
    if (operationWidget != nullptr) {
        return;
    }

    operationWidget = new QWidget(this);
    operationWidget->setObjectName("operationStatus");

    operationTitleLabel = new QLabel(operationWidget);
    operationTitleLabel->setMinimumWidth(160);
    operationTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    operationDetailLabel = new QLabel(operationWidget);
    operationDetailLabel->setWordWrap(true);
    operationDetailLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    operationProgressBar = new QProgressBar(operationWidget);
    operationProgressBar->setTextVisible(false);
    operationProgressBar->setMinimumWidth(180);
    operationProgressBar->setMaximumWidth(280);
    operationProgressBar->setFixedHeight(14);

    QHBoxLayout *operationLayout = new QHBoxLayout(operationWidget);
    operationLayout->setContentsMargins(6, 4, 6, 4);
    operationLayout->setSpacing(8);
    operationLayout->addWidget(operationTitleLabel);
    operationLayout->addWidget(operationProgressBar);
    operationLayout->addWidget(operationDetailLabel, 1);

    operationTimer = new QTimer(this);
    connect(operationTimer, &QTimer::timeout, this, &CustomStatusBar::refreshOperationElapsed);

    statusLayout->insertWidget(0, operationWidget);
}

void CustomStatusBar::refreshOperationElapsed()
{
    if (operationDetailLabel == nullptr) {
        return;
    }

    const qint64 elapsedSeconds = operationElapsed.isValid() ? operationElapsed.elapsed() / 1000 : 0;
    operationDetailLabel->setText(tr("%1 | elapsed %2s").arg(operationDetail).arg(elapsedSeconds));
}
