#include "CustomStatusBar.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QStyle>
#include <QTime>
#include <QToolButton>

CustomStatusBar::CustomStatusBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("CustomStatusBar");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(3);

    summaryWidget = new QWidget(this);
    summaryWidget->setObjectName(QStringLiteral("statusSummary"));
    summaryWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    summaryBadgeLabel = new QLabel(tr("Ready"), summaryWidget);
    summaryBadgeLabel->setObjectName(QStringLiteral("statusBadge"));
    summaryBadgeLabel->setProperty("state", QStringLiteral("ready"));
    summaryBadgeLabel->setAlignment(Qt::AlignCenter);
    summaryBadgeLabel->setMinimumWidth(72);

    latestMessageLabel = new QLabel(tr("Ready"), summaryWidget);
    latestMessageLabel->setObjectName(QStringLiteral("latestStatusMessage"));
    latestMessageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    latestMessageLabel->setMinimumWidth(0);
    latestMessageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    detailsButton = new QToolButton(summaryWidget);
    detailsButton->setObjectName(QStringLiteral("statusDetailsButton"));
    detailsButton->setText(tr("Details"));
    detailsButton->setToolTip(tr("Show or hide the operation log"));
    detailsButton->setCheckable(true);
    detailsButton->setAutoRaise(true);

    auto *summaryLayout = new QHBoxLayout(summaryWidget);
    summaryLayout->setContentsMargins(7, 4, 7, 4);
    summaryLayout->setSpacing(8);
    summaryLayout->addWidget(summaryBadgeLabel);
    summaryLayout->addWidget(latestMessageLabel, 1);
    summaryLayout->addWidget(detailsButton);
    mainLayout->addWidget(summaryWidget);

    statusContent = new QWidget(this);
    statusLayout = new QVBoxLayout(statusContent);
    statusLayout->setContentsMargins(4, 4, 4, 6);
    statusLayout->setSpacing(2);
    statusLayout->setAlignment(Qt::AlignTop);

    scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("statusLog"));
    scrollArea->setWidget(statusContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setMaximumHeight(112);
    scrollArea->hide();
    mainLayout->addWidget(scrollArea);

    connect(detailsButton, &QToolButton::toggled, this, [this](bool visible) {
        scrollArea->setVisible(visible);
        detailsButton->setText(visible ? tr("Hide details") : tr("Details"));
        updateGeometry();
    });
}

void CustomStatusBar::addMessage(const QString &message)
{
    if (statusMessagesMuted) {
        return;
    }

    latestMessageLabel->setText(message);
    latestMessageLabel->setToolTip(message);

    const QString timestamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QLabel *newMessageText = new QLabel(QStringLiteral("%1  %2").arg(timestamp, message), statusContent);
    newMessageText->setObjectName("statusMessage");
    newMessageText->setWordWrap(true);
    newMessageText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    newMessageText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    newMessageText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    newMessageText->setMinimumHeight(newMessageText->fontMetrics().lineSpacing() + 12);

    statusLayout->addWidget(newMessageText);
    messageLabels.push_back(newMessageText);
    while (messageLabels.size() > 100) {
        QLabel *oldest = messageLabels.takeFirst();
        statusLayout->removeWidget(oldest);
        oldest->deleteLater();
    }

    QTimer::singleShot(0, this, [this]() {
        scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
    });
}

void CustomStatusBar::setMessagesMuted(bool muted)
{
    statusMessagesMuted = muted;
}

bool CustomStatusBar::messagesMuted() const
{
    return statusMessagesMuted;
}

void CustomStatusBar::startOperation(const QString &title, const QString &detail)
{
    ensureOperationWidget();
    operationTitleLabel->setText(title);
    operationDetail = detail;
    operationProgressBar->setRange(0, 0);
    operationProgressBar->setValue(0);
    setSummaryState(tr("Running"), QStringLiteral("running"));
    latestMessageLabel->setText(detail);
    operationHideTimer->stop();
    showOperationWidget();
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
    setSummaryState(tr("Running"), QStringLiteral("running"));
    latestMessageLabel->setText(detail);
    showOperationWidget();
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
    setSummaryState(tr("Done"), QStringLiteral("success"));
    addMessage(message);
    showOperationWidget();
    refreshOperationElapsed();
    scheduleOperationHide(6000);
}

void CustomStatusBar::failOperation(const QString &message)
{
    ensureOperationWidget();
    operationTimer->stop();
    operationProgressBar->setRange(0, 100);
    operationProgressBar->setValue(0);
    operationTitleLabel->setText(tr("Failed"));
    operationDetail = message;
    setSummaryState(tr("Failed"), QStringLiteral("error"));
    addMessage(message);
    showOperationWidget();
    refreshOperationElapsed();
    scheduleOperationHide(12000);
}

void CustomStatusBar::ensureOperationWidget()
{
    if (operationWidget != nullptr) {
        return;
    }

    operationWidget = new QWidget(this);
    operationWidget->setObjectName("operationStatus");
    operationWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

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

    operationHideTimer = new QTimer(this);
    operationHideTimer->setSingleShot(true);
    connect(operationHideTimer, &QTimer::timeout, operationWidget, &QWidget::hide);

    mainLayout->insertWidget(1, operationWidget);
    operationWidget->hide();
}

void CustomStatusBar::showOperationWidget()
{
    if (operationWidget == nullptr) {
        return;
    }
    operationWidget->show();
    operationWidget->raise();
}

void CustomStatusBar::refreshOperationElapsed()
{
    if (operationDetailLabel == nullptr) {
        return;
    }

    const qint64 elapsedSeconds = operationElapsed.isValid() ? operationElapsed.elapsed() / 1000 : 0;
    operationDetailLabel->setText(tr("%1 | elapsed %2s").arg(operationDetail).arg(elapsedSeconds));
}

void CustomStatusBar::setSummaryState(const QString &label, const QString &state)
{
    if (summaryBadgeLabel == nullptr) {
        return;
    }
    summaryBadgeLabel->setText(label);
    summaryBadgeLabel->setProperty("state", state);
    summaryBadgeLabel->style()->unpolish(summaryBadgeLabel);
    summaryBadgeLabel->style()->polish(summaryBadgeLabel);
    summaryBadgeLabel->update();
}

void CustomStatusBar::scheduleOperationHide(int milliseconds)
{
    if (operationHideTimer != nullptr) {
        operationHideTimer->start(qMax(0, milliseconds));
    }
}
