#include "CustomStatusBar.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSizePolicy>

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
    QLabel *newMessageText = new QLabel(message, statusContent);
    newMessageText->setObjectName("statusMessage");
    newMessageText->setWordWrap(true);
    newMessageText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    newMessageText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    newMessageText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    newMessageText->setMinimumHeight(newMessageText->fontMetrics().lineSpacing() + 12);

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
    operationTitleLabel->setWordWrap(true);
    operationTitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    operationTitleLabel->setMinimumWidth(120);
    operationTitleLabel->setMaximumWidth(220);
    operationTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    operationDetailLabel = new QLabel(operationWidget);
    operationDetailLabel->setWordWrap(true);
    operationDetailLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    operationDetailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    operationDetailLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    operationProgressBar = new QProgressBar(operationWidget);
    operationProgressBar->setTextVisible(false);
    operationProgressBar->setMinimumWidth(120);
    operationProgressBar->setMaximumWidth(260);
    operationProgressBar->setFixedHeight(14);

    QGridLayout *operationLayout = new QGridLayout(operationWidget);
    operationLayout->setContentsMargins(6, 4, 6, 4);
    operationLayout->setHorizontalSpacing(8);
    operationLayout->setVerticalSpacing(4);
    operationLayout->addWidget(operationTitleLabel, 0, 0, 2, 1);
    operationLayout->addWidget(operationProgressBar, 0, 1, Qt::AlignVCenter);
    operationLayout->addWidget(operationDetailLabel, 1, 1);
    operationLayout->setColumnStretch(0, 0);
    operationLayout->setColumnStretch(1, 1);

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
