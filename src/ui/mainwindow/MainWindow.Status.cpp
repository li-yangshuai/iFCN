#include "ui/mainwindow/MainWindow.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QTableWidgetItem>

namespace {
QString mappingMetadataValue(const QMap<QString, QString> &metadata, const QStringList &keys)
{
    for (const QString &key : keys) {
        const auto it = metadata.constFind(key);
        if (it != metadata.constEnd() && !it.value().isEmpty()) {
            return it.value();
        }
    }
    return QString();
}

void appendIfPresent(QVector<QPair<QString, QString>> &rows,
                     const QString &label,
                     const QString &value)
{
    if (!value.isEmpty()) {
        rows.push_back({label, value});
    }
}
} // namespace

void MainWindow::printToStatusBar(const QString &message)
{
    customStatusBar->addMessage(message);
    QCoreApplication::processEvents();
}

qulonglong MainWindow::currentSceneCellCount() const
{
    if (scene != nullptr && scene->hasFastRender()) {
        qulonglong count = 0;
        for (const auto &layerCells : scene->fastCellsByLayer()) {
            count += static_cast<qulonglong>(layerCells.size());
        }
        return count;
    }

    qulonglong count = 0;
    for (const auto &layerItems : layers) {
        for (QGraphicsItem *item : layerItems) {
            if (item != nullptr && item->type() == QCADCellItem::Type) {
                ++count;
            }
        }
    }
    return count;
}

void MainWindow::setLayoutInfoRows(const QVector<QPair<QString, QString>> &rows)
{
    if (layoutInfoTable == nullptr) {
        return;
    }

    layoutInfoTable->setUpdatesEnabled(false);
    layoutInfoTable->clearContents();
    layoutInfoTable->setRowCount(rows.size());

    for (int row = 0; row < rows.size(); ++row) {
        auto *metricItem = new QTableWidgetItem(rows[row].first);
        auto *valueItem = new QTableWidgetItem(rows[row].second);
        metricItem->setFlags(metricItem->flags() & ~Qt::ItemIsEditable);
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        metricItem->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        valueItem->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        layoutInfoTable->setItem(row, 0, metricItem);
        layoutInfoTable->setItem(row, 1, valueItem);
    }

    layoutInfoTable->resizeRowsToContents();
    layoutInfoTable->setUpdatesEnabled(true);
}

void MainWindow::refreshLayoutInfoPanel()
{
    QVector<QPair<QString, QString>> rows;
    rows.push_back({tr("Mode"), tr("Manual design")});
    if (!curFile.isEmpty() && curFile != tr("Unnamed")) {
        rows.push_back({tr("File"), QFileInfo(curFile).fileName()});
    }
    rows.push_back({tr("Cell count"), QString::number(currentSceneCellCount())});
    rows.push_back({tr("Layers"), QString::number(layers.size())});
    if (scene != nullptr) {
        rows.push_back({tr("Clock regions"), QString::number(scene->clockRegions().size())});
    }
    setLayoutInfoRows(rows);
}

void MainWindow::updateLayoutInfoFromMapping(const GateLevelMapping &mapping)
{
    QVector<QPair<QString, QString>> rows;
    rows.push_back({tr("Mode"), tr("Mapped .ifcn")});
    appendIfPresent(rows, tr("Circuit"), mapping.circuitName);
    appendIfPresent(rows, tr("Gates"), mappingMetadataValue(mapping.metadata, {QStringLiteral("gates number")}));
    appendIfPresent(rows, tr("I/O"), mappingMetadataValue(mapping.metadata, {QStringLiteral("input/output")}));
    appendIfPresent(rows, tr("Edges"), mappingMetadataValue(mapping.metadata, {QStringLiteral("edges number")}));
    appendIfPresent(rows, tr("Layers"), mappingMetadataValue(mapping.metadata, {QStringLiteral("total layers")}));
    appendIfPresent(rows, tr("Area"), mappingMetadataValue(mapping.metadata, {QStringLiteral("layout area")}));
    appendIfPresent(rows, tr("Mapped cells"), mappingMetadataValue(mapping.metadata, {QStringLiteral("cell count")}));
    appendIfPresent(rows, tr("Cross"), mappingMetadataValue(mapping.metadata, {QStringLiteral("cross count")}));
    rows.push_back({tr("Current cells"), QString::number(currentSceneCellCount())});
    appendIfPresent(rows, tr("Critical path"), mappingMetadataValue(mapping.metadata, {QStringLiteral("critical path")}));
    appendIfPresent(rows, tr("Clocks"), mappingMetadataValue(mapping.metadata, {QStringLiteral("clocks")}));
    appendIfPresent(rows, tr("Phase count"), mappingMetadataValue(mapping.metadata, {QStringLiteral("phase count")}));
    appendIfPresent(rows, tr("Clock scheme"), mappingMetadataValue(mapping.metadata, {QStringLiteral("clock scheme")}));

    const QString consistency = mappingMetadataValue(mapping.metadata, {
        QStringLiteral("random phase scheme consistency"),
        QStringLiteral("2ddwave template consistency"),
    });
    const QString conflicts = mappingMetadataValue(mapping.metadata, {
        QStringLiteral("random phase scheme conflicts"),
        QStringLiteral("2ddwave template conflicts"),
    });
    if (!consistency.isEmpty()) {
        QString value = consistency;
        if (!conflicts.isEmpty()) {
            value += tr(" (%1 conflicts)").arg(conflicts);
        }
        rows.push_back({tr("Clock check"), value});
    }

    appendIfPresent(rows, tr("Run time"), mappingMetadataValue(mapping.metadata, {
        QStringLiteral("run time"),
        QStringLiteral("runtime"),
    }));
    rows.push_back({tr("Parsed nodes"), QString::number(mapping.nodes.size())});
    rows.push_back({tr("Routes"), QString::number(mapping.routes.size())});
    rows.push_back({tr("Phase entries"), QString::number(mapping.coordPhaseMap.size())});

    setLayoutInfoRows(rows);
}

void MainWindow::setInputNames(const QVector<QString> &names)
{
    inputname = names;
    emit savedinputname(inputname);
}

QString MainWindow::currentFilePath() const
{
    return curFile;
}

void MainWindow::setDirty(bool on)
{
    if (isBatchUpdating) {
        if (on) {
            batchDirtyPending = true;
        }
        return;
    }
    //禁止其他页面响应
    setWindowModified(on);
    updateUi();
    if (on) {
        const bool mappedIfcn = QFileInfo(curFile).suffix().compare(QStringLiteral("ifcn"), Qt::CaseInsensitive) == 0 &&
                                gateLevelMapping != nullptr &&
                                !gateLevelMapping->metadata.isEmpty();
        if (mappedIfcn) {
            updateLayoutInfoFromMapping(*gateLevelMapping);
        } else {
            refreshLayoutInfoPanel();
        }
    }
}

void MainWindow::updateUi()
{
    saveAction->setEnabled(isWindowModified());
    //更新Action状态
}
void MainWindow::toggleStatusBar(bool checked)
{
    if (checked) {
        customStatusBar->show();
    } else {
        customStatusBar->hide();
    }
}
