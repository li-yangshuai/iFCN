#include "GateLevelMapping.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>
#include <QMessageBox>
#include <QTimer>
#include <QApplication>
#include <QScreen>
#include <QShowEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QWindow>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <exception>
#include <autopr/algorithms/phase_codec.h>
#include "ui/mainwindow/MainWindow.h"

namespace {
QScreen *screenForWidget(QWidget *widget)
{
    if (widget != nullptr) {
        QWidget *topLevel = widget->window();
        if (topLevel != nullptr && topLevel->windowHandle() != nullptr
            && topLevel->windowHandle()->screen() != nullptr) {
            return topLevel->windowHandle()->screen();
        }
    }
    return QApplication::primaryScreen();
}

struct ShiftedPosition {
    position pos{0, 0};
    bool valid = false;
};

ShiftedPosition shiftedPosition(const position &base, int dx, int dy)
{
    const auto x = static_cast<long long>(base.first) + dx;
    const auto y = static_cast<long long>(base.second) + dy;
    const auto maxCoord = static_cast<long long>(std::numeric_limits<unsigned int>::max());
    if (x < 0 || y < 0 || x > maxCoord || y > maxCoord) {
        return {};
    }
    return {{static_cast<unsigned int>(x), static_cast<unsigned int>(y)}, true};
}

bool containsPosition(const std::unordered_set<position, MappingPositionHash> &positions,
                      const ShiftedPosition &candidate)
{
    return candidate.valid && positions.find(candidate.pos) != positions.end();
}

bool sceneCoordinates(const position &cellPos, int &xCoord, int &yCoord)
{
    constexpr unsigned int kPitch = 20;
    constexpr unsigned int kOrigin = 200;
    constexpr unsigned int kMaxCellCoord =
        (static_cast<unsigned int>(std::numeric_limits<int>::max()) - kOrigin) / kPitch;

    if (cellPos.first > kMaxCellCoord || cellPos.second > kMaxCellCoord) {
        return false;
    }

    xCoord = static_cast<int>(cellPos.first * kPitch + kOrigin);
    yCoord = static_cast<int>(cellPos.second * kPitch + kOrigin);
    return true;
}

qulonglong environmentLimit(const char *name, qulonglong defaultValue)
{
    const QByteArray rawValue = qgetenv(name);
    if (rawValue.isEmpty()) {
        return defaultValue;
    }

    bool ok = false;
    const qulonglong value = QString::fromLatin1(rawValue).trimmed().toULongLong(&ok);
    return ok ? value : defaultValue;
}

qulonglong firstUnsignedNumber(const QString &text)
{
    const QRegularExpression numberPattern(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch match = numberPattern.match(text);
    if (!match.hasMatch()) {
        return 0;
    }

    bool ok = false;
    const qulonglong value = match.captured(1).toULongLong(&ok);
    return ok ? value : 0;
}

qulonglong lastUnsignedNumber(const QString &text)
{
    const QRegularExpression numberPattern(QStringLiteral("(\\d+)"));
    QRegularExpressionMatchIterator matches = numberPattern.globalMatch(text);
    qulonglong value = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        bool ok = false;
        const qulonglong nextValue = match.captured(1).toULongLong(&ok);
        if (ok) {
            value = nextValue;
        }
    }
    return value;
}

bool exceedsInteractiveLimit(qulonglong value, qulonglong limit)
{
    return limit > 0 && value > limit;
}

class CenteredMessageBox : public QMessageBox
{
public:
    CenteredMessageBox(QWidget *parent,
                       QMessageBox::Icon icon,
                       const QString &title,
                       const QString &text)
        : QMessageBox(icon,
                      title,
                      text,
                      QMessageBox::Ok,
                      parent ? parent->window() : nullptr),
          anchor(parent ? parent->window() : nullptr)
    {
        setWindowModality(anchor ? Qt::WindowModal : Qt::ApplicationModal);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QMessageBox::showEvent(event);
        centerOnAnchor();
        QTimer::singleShot(0, this, [this]() { centerOnAnchor(); });
        QTimer::singleShot(50, this, [this]() { centerOnAnchor(); });
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QMessageBox::resizeEvent(event);
        if (isVisible()) {
            centerOnAnchor();
        }
    }

private:
    void centerOnAnchor()
    {
        QWidget *target = anchor;
        if (target == nullptr || !target->isVisible()) {
            target = QApplication::activeWindow();
        }

        QPoint centerPoint;
        QScreen *screen = nullptr;
        if (target != nullptr) {
            centerPoint = target->mapToGlobal(target->rect().center());
            screen = screenForWidget(target);
        } else {
            screen = QApplication::primaryScreen();
            if (screen != nullptr) {
                centerPoint = screen->availableGeometry().center();
            }
        }

        QSize dialogSize = frameGeometry().size();
        if (dialogSize.isEmpty() || dialogSize.width() <= 0 || dialogSize.height() <= 0) {
            dialogSize = sizeHint();
        }
        QPoint topLeft(centerPoint.x() - dialogSize.width() / 2,
                       centerPoint.y() - dialogSize.height() / 2);

        if (screen != nullptr) {
            const QRect available = screen->availableGeometry();
            topLeft.setX(qBound(available.left(),
                                topLeft.x(),
                                available.right() - dialogSize.width() + 1));
            topLeft.setY(qBound(available.top(),
                                topLeft.y(),
                                available.bottom() - dialogSize.height() + 1));
        }

        move(topLeft);
    }

    QWidget *anchor = nullptr;
};

void showCenteredMessageBox(QWidget *parent,
                            QMessageBox::Icon icon,
                            const QString &title,
                            const QString &text)
{
    if (qEnvironmentVariableIntValue("IFCN_NONINTERACTIVE") != 0) {
        qInfo().noquote() << "[GateLevelMapping]" << title << "-" << text;
        return;
    }
    CenteredMessageBox box(parent, icon, title, text);
    box.exec();
}

void resetMappedSceneLayers(MainWindow *mainWindow)
{
    if (mainWindow == nullptr || mainWindow->scene == nullptr || mainWindow->layerComboBox == nullptr) {
        return;
    }

    mainWindow->scene->clearSelection();
    mainWindow->scene->clearFastRender();
    mainWindow->scene->clearPhaseRecord();

    for (auto &layer : mainWindow->layers) {
        for (QGraphicsItem *item : layer) {
            if (item != nullptr) {
                mainWindow->scene->removeItem(item);
                delete item;
            }
        }
    }
    mainWindow->layers.clear();
    mainWindow->setInputNames({});

    QSignalBlocker blocker(mainWindow->layerComboBox);
    while (mainWindow->layerComboBox->GetNumRows() > 0) {
        mainWindow->layerComboBox->RemoveItem(mainWindow->layerComboBox->GetNumRows() - 1);
    }

    const QStringList layerNames = {
        QStringLiteral("Main Cell Layer"),
        QStringLiteral("second layer"),
        QStringLiteral("third layer")
    };
    for (const QString &layerName : layerNames) {
        mainWindow->layerComboBox->AddItem(layerName, true);
        mainWindow->layers.push_back(QVector<QGraphicsItem*>());
    }
    mainWindow->layerComboBox->setCurrentIndex(0);
    mainWindow->scene->setCurrentLayerIndex(0);
}
} // namespace

GateLevelMapping::GateLevelMapping(MainWindow *parent)
    : QObject(parent), mainWindow(parent)
{
    // qDebug() << "[GateLevelMapping] initialized (Qt containers)";
}

void GateLevelMapping::parseGateLevelMappingFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        qWarning() << "[GateLevelMapping] Empty file path.";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showCenteredMessageBox(mainWindow,
                               QMessageBox::Warning,
                               "File Error",
                               "Cannot open file:\n" + filePath);
        return;
    }

    QTextStream in(&file);
    nodes.clear();
    routes.clear();
    mappedRouteCells.clear();
    coordPhaseMap.clear();
    metadata.clear();
    currentMappingFilePath = filePath;
    phaseCodecPhaseCount = 4;
    phaseCodecBlockSize = 4;

    bool nodeSection = false;
    bool pathSection = false;
    bool phaseSection = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("#")) {
            parseMetadataLine(line);
        }

        if (line.startsWith("#circuit name:")) {
            circuitName = line.section(':', 1).trimmed();
        }
        else if (line.startsWith("#nodes info")) {
            nodeSection = true; pathSection = phaseSection = false;
            continue;
        }
        else if (line.startsWith("#paths info")) {
            pathSection = true; nodeSection = phaseSection = false;
            continue;
        }
        else if (line.startsWith("#phase map")) {
            phaseSection = true; nodeSection = pathSection = false;
            continue;
        }
        else if (line.startsWith("#phase codec")) {
            parsePhaseCodecLine(line);
            continue;
        }
        else if (line.startsWith("#")) {
            continue;
        }

        if (nodeSection && line.contains(',')) {
            parseNodeLine(line);
        } 
        else if (pathSection && line.contains(':')) {
            parsePathLine(line);
        }
        else if (phaseSection && line.contains(':')) {
            parsePhaseLine(line);
        }
    }

    file.close();

    applyClockSchemePhaseTemplate();
    mainWindow->updateLayoutInfoFromMapping(*this);
    showCenteredMessageBox(mainWindow,
        QMessageBox::Information,
        "Parsing Complete",
        QString("Circuit: %1\nNodes: %2\nRoutes: %3\nPhase entries: %4")
        .arg(circuitName)
        .arg(nodes.size())
        .arg(routes.size())
        .arg(coordPhaseMap.size()));

    const QString message = QStringLiteral("Parsed .ifcn: %1").arg(circuitName);
    mainWindow->customStatusBar->addMessage(message);
    QCoreApplication::processEvents();

    const qulonglong cellCount = firstUnsignedNumber(metadataValue({QStringLiteral("cell count")}));
    const qulonglong layoutArea = lastUnsignedNumber(metadataValue({QStringLiteral("layout area")}));
    const qulonglong cellLimit = environmentLimit("IFCN_MAX_INTERACTIVE_MAPPING_CELLS", 500000);
    const qulonglong areaLimit = environmentLimit("IFCN_MAX_INTERACTIVE_LAYOUT_AREA", 300000);
    const bool skipByCellCount = cellCount > 0 && exceedsInteractiveLimit(cellCount, cellLimit);
    const bool skipByArea = cellCount == 0 && layoutArea > 0 && exceedsInteractiveLimit(layoutArea, areaLimit);
    if (skipByCellCount || skipByArea) {
        resetMappedSceneLayers(mainWindow);
        const QString reason = skipByCellCount
            ? QStringLiteral("cell count %1 exceeds interactive limit %2").arg(cellCount).arg(cellLimit)
            : QStringLiteral("layout area %1 exceeds interactive limit %2").arg(layoutArea).arg(areaLimit);
        const QString skipMessage = QStringLiteral(
            "Gate-level .ifcn loaded, but cell-level interactive rendering was skipped because %1. "
            "Set IFCN_MAX_INTERACTIVE_MAPPING_CELLS=0 or IFCN_MAX_INTERACTIVE_LAYOUT_AREA=0 to disable this guard."
        ).arg(reason);
        qWarning() << "[GateLevelMapping]" << skipMessage;
        mainWindow->printToStatusBar(skipMessage);
        showCenteredMessageBox(mainWindow,
                               QMessageBox::Information,
                               QStringLiteral("Large Mapping Loaded"),
                               skipMessage);
        emit mappingLoaded();
        return;
    }

    resetMappedSceneLayers(mainWindow);

    bool batchStarted = false;
    try {
        mainWindow->beginSceneBatchUpdate();
        batchStarted = true;
        mainWindow->scene->beginFastRenderBuild(mainWindow->layers.size());

        //遍历routes。打印key和value

        for (auto it = routes.begin(); it != routes.end(); ++it)
        {
            const QPair<int,int>& key = it.key();
            const QVector<QPoint>& path = it.value();
            // qDebug() << "============================";
            // qDebug() << "Route (" << key.first << "->" << key.second << "), length =" << path.size();

            // for (const QPoint& p : path)
            //     qDebug() << "   (" << p.x() << "," << p.y() << ")";
        }

        mappingCellItem();
        putClock();
        mainWindow->scene->finalizeFastRenderBuild();
        QVector<QString> inputNames;
        for (const auto &layerCells : mainWindow->scene->fastCellsByLayer()) {
            for (const auto &cell : layerCells) {
                if (cell.type == CellType::InputCell && !cell.name.isEmpty()) {
                    inputNames.push_back(cell.name);
                }
            }
        }
        mainWindow->setInputNames(inputNames);
        mainWindow->endSceneBatchUpdate(true);
        batchStarted = false;
        mainWindow->scene->notifyClockRegionsChanged();
    } catch (const std::exception &ex) {
        mainWindow->scene->clearFastRender();
        if (batchStarted) {
            mainWindow->endSceneBatchUpdate(false);
        }
        const QString message = QStringLiteral("Cell-level mapping failed: %1").arg(QString::fromLocal8Bit(ex.what()));
        qWarning() << "[GateLevelMapping]" << message;
        mainWindow->printToStatusBar(message);
        showCenteredMessageBox(mainWindow,
                               QMessageBox::Warning,
                               QStringLiteral("Mapping Failed"),
                               message);
        return;
    } catch (...) {
        mainWindow->scene->clearFastRender();
        if (batchStarted) {
            mainWindow->endSceneBatchUpdate(false);
        }
        const QString message = QStringLiteral("Cell-level mapping failed with an unknown error.");
        qWarning() << "[GateLevelMapping]" << message;
        mainWindow->printToStatusBar(message);
        showCenteredMessageBox(mainWindow,
                               QMessageBox::Warning,
                               QStringLiteral("Mapping Failed"),
                               message);
        return;
    }
    // printCrossline();

    emit mappingLoaded();
}

void GateLevelMapping::parseMetadataLine(const QString &line)
{
    if (line.startsWith("###")) {
        return;
    }

    QString body = line;
    while (body.startsWith("#")) {
        body.remove(0, 1);
    }
    body = body.trimmed();
    if (body.isEmpty()) {
        return;
    }

    if (body.startsWith("designed by", Qt::CaseInsensitive)) {
        QString value = body.mid(QStringLiteral("designed by").size()).trimmed();
        if (value.endsWith('.')) {
            value.chop(1);
        }
        metadata.insert(QStringLiteral("designed by"), value);
        return;
    }

    const int colonIndex = body.indexOf(':');
    if (colonIndex <= 0) {
        return;
    }

    const QString key = body.left(colonIndex).trimmed().toLower();
    const QString value = body.mid(colonIndex + 1).trimmed();
    if (!key.isEmpty()) {
        metadata.insert(key, value);
    }
}

QString GateLevelMapping::metadataValue(const QStringList &keys) const
{
    for (const QString &key : keys) {
        const auto it = metadata.constFind(key.toLower());
        if (it != metadata.constEnd() && !it.value().isEmpty()) {
            return it.value();
        }
    }
    return QString();
}

QString GateLevelMapping::buildMappingStatusMessage() const
{
    QStringList parts;
    parts << QStringLiteral("Circuit: %1").arg(circuitName);

    const QString gates = metadataValue({QStringLiteral("gates number")});
    if (!gates.isEmpty()) {
        parts << QStringLiteral("Gates: %1").arg(gates);
    }

    const QString io = metadataValue({QStringLiteral("input/output")});
    if (!io.isEmpty()) {
        parts << QStringLiteral("I/O: %1").arg(io);
    }

    const QString edges = metadataValue({QStringLiteral("edges number")});
    if (!edges.isEmpty()) {
        parts << QStringLiteral("Edges: %1").arg(edges);
    }

    const QString layers = metadataValue({QStringLiteral("total layers")});
    if (!layers.isEmpty()) {
        parts << QStringLiteral("Layers: %1").arg(layers);
    }

    const QString area = metadataValue({QStringLiteral("layout area")});
    if (!area.isEmpty()) {
        parts << QStringLiteral("Area: %1").arg(area);
    }

    const QString cellCount = metadataValue({QStringLiteral("cell count")});
    if (!cellCount.isEmpty()) {
        parts << QStringLiteral("Cell count: %1").arg(cellCount);
    }

    const QString crossCount = metadataValue({QStringLiteral("cross count")});
    if (!crossCount.isEmpty()) {
        parts << QStringLiteral("Cross: %1").arg(crossCount);
    }

    const QString criticalPath = metadataValue({QStringLiteral("critical path")});
    if (!criticalPath.isEmpty()) {
        parts << QStringLiteral("Critical path: %1").arg(criticalPath);
    }

    const QString clocks = metadataValue({QStringLiteral("clocks")});
    if (!clocks.isEmpty()) {
        parts << QStringLiteral("Clocks: %1").arg(clocks);
    }

    const QString phaseCount = metadataValue({QStringLiteral("phase count")});
    if (!phaseCount.isEmpty()) {
        parts << QStringLiteral("Phase count: %1").arg(phaseCount);
    }

    const QString runtime = metadataValue({QStringLiteral("run time"),
                                           QStringLiteral("runtime")});
    if (!runtime.isEmpty()) {
        parts << QStringLiteral("Time: %1").arg(runtime);
    }

    const QString scheme = metadataValue({QStringLiteral("clock scheme")});
    if (!scheme.isEmpty()) {
        parts << QStringLiteral("Scheme: %1").arg(scheme);
    }

    const QString consistency = metadataValue({
        QStringLiteral("random phase scheme consistency"),
        QStringLiteral("2ddwave template consistency")
    });
    const QString conflicts = metadataValue({
        QStringLiteral("random phase scheme conflicts"),
        QStringLiteral("2ddwave template conflicts")
    });
    if (!consistency.isEmpty()) {
        QString clockInfo = QStringLiteral("Clock check: %1").arg(consistency);
        if (!conflicts.isEmpty()) {
            clockInfo += QStringLiteral(" (%1 conflicts)").arg(conflicts);
        }
        parts << clockInfo;
    }

    return parts.join(QStringLiteral(", "));
}

void GateLevelMapping::writeMappingMetricsToFile(qulonglong cellCount, qulonglong crossCount)
{
    metadata.insert(QStringLiteral("cell count"), QString::number(cellCount));
    metadata.insert(QStringLiteral("cross count"), QString::number(crossCount));

    if (currentMappingFilePath.isEmpty()) {
        return;
    }

    QFile file(currentMappingFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[GateLevelMapping] Cannot reopen mapping file for metrics:"
                   << currentMappingFilePath;
        return;
    }

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        lines.push_back(in.readLine());
    }
    file.close();

    const QString cellLine = QStringLiteral("#cell count: %1").arg(cellCount);
    const QString crossLine = QStringLiteral("#cross count: %1").arg(crossCount);
    bool hasCellLine = false;
    bool hasCrossLine = false;
    int insertAfter = -1;

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed().toLower();
        if (trimmed.startsWith(QStringLiteral("#cell count:"))) {
            lines[i] = cellLine;
            hasCellLine = true;
        } else if (trimmed.startsWith(QStringLiteral("#cross count:"))) {
            lines[i] = crossLine;
            hasCrossLine = true;
        }

        if (trimmed.startsWith(QStringLiteral("#layout area:"))) {
            insertAfter = i;
        } else if (insertAfter < 0 && trimmed.startsWith(QStringLiteral("#edges number:"))) {
            insertAfter = i;
        }
    }

    if (!hasCrossLine) {
        lines.insert(insertAfter + 1, crossLine);
    }
    if (!hasCellLine) {
        lines.insert(insertAfter + 1, cellLine);
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "[GateLevelMapping] Cannot write mapping metrics to file:"
                   << currentMappingFilePath;
        return;
    }

    QTextStream out(&file);
    for (const QString &line : lines) {
        out << line << '\n';
    }
}

void GateLevelMapping::parseNodeLine(const QString &line)
{
    // 格式: 0, pi00, Input, (0,0);
    QString clean = line;
    clean.remove(';');
    QStringList parts = clean.split(',', QString::SkipEmptyParts);
    if (parts.size() < 4) return;

    NodeInfo node;
    node.index = parts[0].trimmed().toInt();
    node.name  = parts[1].trimmed();
    node.type  = parts[2].trimmed();

    QRegularExpression posPattern("\\((\\d+),(\\d+)\\)");
    QRegularExpressionMatch match = posPattern.match(line);
    if (match.hasMatch()) {
        node.pos = QPoint(match.captured(1).toInt(), match.captured(2).toInt());
    }

    nodes.insert(node.index, node);
}

void GateLevelMapping::parsePathLine(const QString &line)
{
    // 格式: (1,2): (10,10),(11,10),(12,10);
    QRegularExpression header("\\((\\d+),(\\d+)\\):");
    QRegularExpressionMatch headMatch = header.match(line);
    if (!headMatch.hasMatch()) return;

    int u = headMatch.captured(1).toInt();
    int v = headMatch.captured(2).toInt();

    QVector<QPoint> path;
    QRegularExpression coordPattern("\\((\\d+),(\\d+)\\)");
    QRegularExpressionMatchIterator it = coordPattern.globalMatch(line);

    bool first = true;  // ✅ 用于跳过第一个坐标
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        if (first) { first = false; continue; } // 跳过 (u,v)
        path.append(QPoint(m.captured(1).toInt(), m.captured(2).toInt()));
    }

    routes.insert({u, v}, path);
}


void GateLevelMapping::parsePhaseLine(const QString &line)
{
    int layoutWidth = 0;
    int layoutHeight = 0;
    const QString layoutArea = metadataValue({QStringLiteral("layout area")});
    const QRegularExpression widthPattern(QStringLiteral("width\\s*:\\s*(\\d+)"));
    const QRegularExpression heightPattern(QStringLiteral("height\\s*:\\s*(\\d+)"));
    const QRegularExpressionMatch widthMatch = widthPattern.match(layoutArea);
    const QRegularExpressionMatch heightMatch = heightPattern.match(layoutArea);
    if (widthMatch.hasMatch()) {
        layoutWidth = widthMatch.captured(1).toInt();
    }
    if (heightMatch.hasMatch()) {
        layoutHeight = heightMatch.captured(1).toInt();
    }
    auto inLayoutBounds = [layoutWidth, layoutHeight](const QPoint &pt) {
        if (layoutWidth <= 0 || layoutHeight <= 0) {
            return true;
        }
        return pt.x() >= 0 && pt.y() >= 0 && pt.x() < layoutWidth && pt.y() < layoutHeight;
    };

    // 新格式: tile(x,y):0xhhhh; 其中 tile 坐标映射到 block_size x block_size 的相位块。
    QRegularExpression tileEntry("tile\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)\\s*:\\s*(?:0x)?([0-9a-fA-F]+)");
    QRegularExpressionMatchIterator tileIt = tileEntry.globalMatch(line);
    bool decodedTile = false;
    while (tileIt.hasNext()) {
        decodedTile = true;
        QRegularExpressionMatch m = tileIt.next();
        const int tileX = m.captured(1).toInt();
        const int tileY = m.captured(2).toInt();
        const std::string hex = m.captured(3).toStdString();

        try {
            const auto matrix = fcngraph::phase_codec::decodePackedHexToMatrix(
                hex,
                phaseCodecPhaseCount,
                phaseCodecBlockSize
            );
            for (int row = 0; row < phaseCodecBlockSize; ++row) {
                for (int column = 0; column < phaseCodecBlockSize; ++column) {
                    const QPoint pt(tileX * phaseCodecBlockSize + column,
                                    tileY * phaseCodecBlockSize + row);
                    if (!inLayoutBounds(pt)) {
                        continue;
                    }
                    coordPhaseMap.insert(pt, matrix[static_cast<size_t>(row)][static_cast<size_t>(column)]);
                }
            }
        } catch (const std::exception &ex) {
            qWarning() << "[GateLevelMapping] Failed to decode phase tile:" << ex.what();
        }
    }
    if (decodedTile) {
        return;
    }

    // 格式: (x,y):phase
    QRegularExpression entry("\\((-?\\d+),(-?\\d+)\\)\\s*:\\s*(-?\\d+)");
    QRegularExpressionMatchIterator it = entry.globalMatch(line);

    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QPoint pt(m.captured(1).toInt(), m.captured(2).toInt());
        if (pt.x() < 0 || pt.y() < 0) {
            continue;
        }
        coordPhaseMap.insert(pt, m.captured(3).toInt());
    }
}

void GateLevelMapping::parsePhaseCodecLine(const QString &line)
{
    QRegularExpression phaseCountPattern("phase_count\\s*=\\s*(\\d+)");
    QRegularExpression blockSizePattern("block_size\\s*=\\s*(\\d+)");

    QRegularExpressionMatch phaseMatch = phaseCountPattern.match(line);
    if (phaseMatch.hasMatch()) {
        phaseCodecPhaseCount = phaseMatch.captured(1).toInt();
    }

    QRegularExpressionMatch blockMatch = blockSizePattern.match(line);
    if (blockMatch.hasMatch()) {
        phaseCodecBlockSize = blockMatch.captured(1).toInt();
    }

    if (phaseCodecPhaseCount != 3 && phaseCodecPhaseCount != 4) {
        phaseCodecPhaseCount = 4;
    }
    if (phaseCodecBlockSize != 3 && phaseCodecBlockSize != 4) {
        phaseCodecBlockSize = phaseCodecPhaseCount;
    }
}

void GateLevelMapping::applyClockSchemePhaseTemplate()
{
    QString scheme = metadataValue({QStringLiteral("clock scheme")}).toLower();
    scheme.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (scheme != QStringLiteral("2ddwave") && scheme != QStringLiteral("tddwave")) {
        return;
    }

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    auto includePoint = [&](const QPoint &point) {
        if (point.x() < 0 || point.y() < 0) {
            return;
        }
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    };

    for (auto it = coordPhaseMap.cbegin(); it != coordPhaseMap.cend(); ++it) {
        includePoint(it.key());
    }
    if (minX == std::numeric_limits<int>::max()) {
        for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
            includePoint(it.value().pos);
        }
        for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
            for (const QPoint &point : it.value()) {
                includePoint(point);
            }
        }
    }
    if (minX == std::numeric_limits<int>::max()) {
        return;
    }

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            coordPhaseMap.insert(QPoint(x, y), (x + y) & 0x3);
        }
    }
}


void GateLevelMapping::mappingCellItem(){
    Mapping mapping;

    const auto toPosition = [](const QPoint &point) {
        return position{static_cast<unsigned int>(point.x()),
                        static_cast<unsigned int>(point.y())};
    };

    std::map<position, int> positionPhaseMap;
    std::unordered_map<position, QString, MappingPositionHash> nodeNameByPos;
    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint &p = it.key();
        int phase = it.value();

        positionPhaseMap[{static_cast<unsigned int>(p.x()), 
                        static_cast<unsigned int>(p.y())}] = phase;
    }

    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    circle_line.reserve(routes.size());
    for (auto it = routes.begin(); it != routes.end(); ++it) 
    {
        const QVector<QPoint>& qPoints = it.value();
        std::vector<position> convertedRoute;
        convertedRoute.reserve(qPoints.size());
        if (qPoints.size() < 2) {
            continue;
        }
        for (const QPoint& point : qPoints) {
            if (point.x() < 0 || point.y() < 0) {
                convertedRoute.clear();
                break;
            }
            convertedRoute.emplace_back(static_cast<unsigned int>(point.x()),
                                        static_cast<unsigned int>(point.y()));
        }
        if (convertedRoute.size() < 2) {
            continue;
        }
        circle_line.push_back(std::move(convertedRoute));
    }
    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node,type), (扇入，扇出)>
    Nodelink.clear();

    std::unordered_map<int, position> routeEndpointByNode;
    for (auto it = routes.begin(); it != routes.end(); ++it)
    {
        const QPair<int,int>& key = it.key();
        const QVector<QPoint>& path = it.value();
        if (path.isEmpty()) {
            continue;
        }

        if (nodes.contains(key.first)) {
            const position sourceEndpoint = toPosition(path.front());
            const auto inserted = routeEndpointByNode.emplace(key.first, sourceEndpoint);
            if (!inserted.second && inserted.first->second != sourceEndpoint) {
                qWarning() << "[GateLevelMapping] Route source endpoint mismatch for node"
                           << key.first;
            }
        }
        if (nodes.contains(key.second)) {
            const position sinkEndpoint = toPosition(path.back());
            const auto inserted = routeEndpointByNode.emplace(key.second, sinkEndpoint);
            if (!inserted.second && inserted.first->second != sinkEndpoint) {
                qWarning() << "[GateLevelMapping] Route sink endpoint mismatch for node"
                           << key.second;
            }
        }
    }

    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        const auto endpointIt = routeEndpointByNode.find(it.key());
        const position nodepos = (endpointIt != routeEndpointByNode.end())
                                     ? endpointIt->second
                                     : toPosition(it.value().pos);
        std::string type = it.value().type.toStdString();
        Nodelink.try_emplace({nodepos, type}, std::make_pair(std::vector<position>{}, std::vector<position>{}));
        nodeNameByPos[nodepos] = it.value().name;
    }


    // qDebug() << "start print all route:";
    // int idx = 0;
    // for (const auto &line : circle_line)
    // {
    //     QString lineStr = QString("Path %1: ").arg(idx++);
    //     for (const auto &p : line)
    //         lineStr += QString("(%1,%2) ").arg(p.first).arg(p.second);
    //     qDebug().noquote() << lineStr;
    // }
    // qDebug() << "start print Nodelink:";
    int node_idx = 0;
    for (const auto &entry : Nodelink)
    {
        const auto &pos = entry.first.first;
        const auto &type = entry.first.second;
        const auto &inputs = entry.second.first;
        const auto &outputs = entry.second.second;

        QString lineStr = QString("Node %1 (%2,%3) Type:%4 | Fan-in:")
                            .arg(node_idx++)
                            .arg(pos.first)
                            .arg(pos.second)
                            .arg(QString::fromStdString(type));

        for (const auto &in : inputs)
            lineStr += QString(" (%1,%2)").arg(in.first).arg(in.second);

        lineStr += " | Fan-out:";
        for (const auto &out : outputs)
            lineStr += QString(" (%1,%2)").arg(out.first).arg(out.second);

        // qDebug().noquote() << lineStr;
    }


    for (auto it = routes.begin(); it != routes.end(); ++it)
    {
        const QPair<int,int>& key = it.key();
        const QVector<QPoint>& path = it.value();
        const size_t len = static_cast<size_t>(path.size());
        if (len < 2 || !nodes.contains(key.first) || !nodes.contains(key.second)) {
            continue;
        }

        const NodeInfo& source = nodes[key.first];
        const NodeInfo& sink = nodes[key.second];
        const auto sourceKey = std::make_pair(toPosition(path.front()),
                                              source.type.toStdString());
        const auto sinkKey = std::make_pair(toPosition(path.back()),
                                            sink.type.toStdString());

        nodeNameByPos[sourceKey.first] = source.name;
        nodeNameByPos[sinkKey.first] = sink.name;
        Nodelink[sourceKey].second.push_back(toPosition(path[1]));
        Nodelink[sinkKey].first.push_back(toPosition(path[len - 2]));
    }

    for (auto &entry : Nodelink)
    {
        auto &inputs = entry.second.first;
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

        auto &outputs = entry.second.second;
        std::sort(outputs.begin(), outputs.end());
        outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    }


    if(Nodelink.empty())
    {
        QString message = "Nodelink empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }

    mapping.node_mapping(Nodelink);
    auto nodeexample = mapping.nodecell_list;
    if(nodeexample.empty())
    {
        QString message = "nodeexample empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }
    for(auto &cell : nodeexample)
    {
        auto cellpos_list = cell.second;
        if(cell.first == "input")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                const auto nameIt = nodeNameByPos.find({x_node, y_node});
                const QString Iname = (nameIt != nodeNameByPos.end()) ? nameIt->second : QStringLiteral("default");
                putCellItem(cellpos, 0, CellType::InputCell, positionPhaseMap, Iname);
                
            }
        }
        else if (cell.first == "output")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                const auto nameIt = nodeNameByPos.find({x_node, y_node});
                const QString Oname = (nameIt != nodeNameByPos.end()) ? nameIt->second : QStringLiteral("default");
                putCellItem(cellpos, 0, CellType::OutputCell, positionPhaseMap, Oname);

            }
        }
        else if (cell.first == "normal")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::NormalCell, positionPhaseMap);
                
            }
        }
        else if (cell.first == "fix0")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_0, positionPhaseMap);

            }
        }
        else if (cell.first == "fix1")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_1, positionPhaseMap);
                
            }
        }
    }

    auto routeexample = mapping.mapping_line(circle_line);
    auto crossexample = mapping.crossline_list;
    auto nodeexample2 = mapping.nodecell_list;

    mappedRouteCells.clear();
    for (auto it = routes.begin(); it != routes.end(); ++it)
    {
        const QVector<QPoint>& path = it.value();
        if (path.size() < 2) {
            continue;
        }

        const auto routeKey = std::make_pair(toPosition(path.front()),
                                             toPosition(path.back()));
        const auto routeIt = routeexample.find(routeKey);
        if (routeIt == routeexample.end()) {
            continue;
        }

        QVector<QPoint> mappedCells;
        std::unordered_set<position, MappingPositionHash> seenMappedCells;
        for (const auto &segment : routeIt->second) {
            for (const auto &cellPos : segment) {
                if (seenMappedCells.insert(cellPos).second) {
                    mappedCells.push_back(QPoint(static_cast<int>(cellPos.first),
                                                 static_cast<int>(cellPos.second)));
                }
            }
        }

        if (!mappedCells.isEmpty()) {
            mappedRouteCells.insert(it.key(), mappedCells);
        }
    }

    std::vector<position> allroutecells;
    for (auto &pair : routeexample)
    {
        for (auto &v : pair.second)
        {
            allroutecells.insert(allroutecells.end(), v.begin(), v.end());
        }
        
    }
    std::vector<position> allnodecells;
    for (auto &pair : nodeexample2)
    {
        allnodecells.insert(allnodecells.end(), pair.second.begin(), pair.second.end());
    }

    std::unordered_set<position, MappingPositionHash> allcrosscellsSet;
    if(!crossexample.empty())
    {
        for (auto &pair : crossexample)
        {
            for (auto &v : pair.second)
            {
                allcrosscellsSet.insert(v.begin(), v.end());
            }
            
        }
    }

    // 对于线路元胞，交叉点不去重，非交叉点线路复用时去重。
    std::vector<position> result;
    result.reserve(allroutecells.size());
    std::unordered_set<position, MappingPositionHash> seen;
    for (auto &p : allroutecells) {
        const bool isCross = allcrosscellsSet.find(p) != allcrosscellsSet.end();
        if (isCross || seen.insert(p).second) {
            result.push_back(p);
        }
    }
    allroutecells = result;
    std::unordered_set<position, MappingPositionHash> allroutecellsSet(allroutecells.begin(), allroutecells.end());

    std::size_t total_cross = 0;
    for (const auto &entry : crossexample) 
    {
        total_cross += entry.second.size();
    }

    const size_t total_count = allroutecells.size() + allnodecells.size();
    writeMappingMetricsToFile(static_cast<qulonglong>(total_count),
                              static_cast<qulonglong>(total_cross));
    mainWindow->updateLayoutInfoFromMapping(*this);

    QString message = QStringLiteral("Mapped .ifcn: %1 cells, %2 crossings")
                          .arg(static_cast<qulonglong>(total_count))
                          .arg(static_cast<qulonglong>(total_cross));
    mainWindow->printToStatusBar(message);

    // Cross线路元胞放置
    std::unordered_set<position, MappingPositionHash> crosscellSet;
    std::unordered_set<position, MappingPositionHash> verticalcellSet;
    if(!crossexample.empty())
    {
        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                crosscellSet.insert(cross.begin(), cross.end());
            }
        }

        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                for(auto unit = cross.begin(); unit != cross.end(); unit++)
                {
                    if((unit == cross.begin()) || (std::next(unit) == cross.end()))
                    {
                        int count = 0;
                        const position base = *unit;
                        const auto dir1 = shiftedPosition(base, 0, 1);
                        const auto dir2 = shiftedPosition(base, 0, -1);
                        const auto dir3 = shiftedPosition(base, -1, 0);
                        const auto dir4 = shiftedPosition(base, 1, 0);
                        count += containsPosition(crosscellSet, dir1) ? 1 : 0;
                        count += containsPosition(crosscellSet, dir2) ? 1 : 0;
                        count += containsPosition(crosscellSet, dir3) ? 1 : 0;
                        count += containsPosition(crosscellSet, dir4) ? 1 : 0;

                        if (count >= 2) 
                        {  
                            putCellItem(*unit, 2, CellType::CrossoverCell, positionPhaseMap);
                        } 
                        else
                        {
                            // 若端点无法直接放置柱点，则跨时钟延伸两个单位元胞。
                            if(containsPosition(crosscellSet, dir2)
                            && containsPosition(allroutecellsSet, dir3)
                            && containsPosition(allroutecellsSet, dir4)
                            && dir1.valid)
                            {
                                putCellItem(*unit, 2, CellType::CrossoverCell, positionPhaseMap);
                                putCellItem(dir1.pos, 2, CellType::CrossoverCell, positionPhaseMap);

                                auto cellpos3 = shiftedPosition(dir1.pos, 0, 1);
                                if (cellpos3.valid) {
                                    putCellItem(cellpos3.pos, 0, CellType::VerticalCell, positionPhaseMap);
                                    putCellItem(cellpos3.pos, 1, CellType::VerticalCell, positionPhaseMap);
                                    putCellItem(cellpos3.pos, 2, CellType::VerticalCell, positionPhaseMap);
                                    verticalcellSet.insert(cellpos3.pos);

                                    crosscellSet.insert(dir1.pos);
                                    crosscellSet.insert(cellpos3.pos);
                                }
                            }
                            else if (containsPosition(crosscellSet, dir3)
                            && containsPosition(allroutecellsSet, dir1)
                            && containsPosition(allroutecellsSet, dir2)
                            && dir4.valid)
                            {
                                putCellItem(*unit, 2, CellType::CrossoverCell, positionPhaseMap);
                                putCellItem(dir4.pos, 2, CellType::CrossoverCell, positionPhaseMap);

                                auto cellpos3 = shiftedPosition(dir4.pos, 1, 0);
                                if (cellpos3.valid) {
                                    putCellItem(cellpos3.pos, 0, CellType::VerticalCell, positionPhaseMap);
                                    putCellItem(cellpos3.pos, 1, CellType::VerticalCell, positionPhaseMap);
                                    putCellItem(cellpos3.pos, 2, CellType::VerticalCell, positionPhaseMap);
                                    verticalcellSet.insert(cellpos3.pos);

                                    crosscellSet.insert(dir4.pos);
                                    crosscellSet.insert(cellpos3.pos);
                                }
                            }
                            else
                            {
                                putCellItem(*unit, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(*unit, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(*unit, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcellSet.insert(*unit);
                            }
                        }
                    }
                    else
                    {
                        putCellItem(*unit, 2, CellType::CrossoverCell, positionPhaseMap);
                    }
                }
            }
        }
    }

    // Normal线路元胞放置
    if(!routeexample.empty())
    {
        for(auto &line : routeexample)
        {
            for(auto &unit : line.second)
            {
                for(auto &pos : unit)
                {
                    if(crosscellSet.find(pos) == crosscellSet.end())
                    {
                        putCellItem(pos, 0, CellType::NormalCell, positionPhaseMap);
                    }
                    else
                    {
                        std::vector<position> tempcross;
                        tempcross.reserve(unit.size());
                        for(const auto &v : unit)
                        {
                            if(crosscellSet.find(v) != crosscellSet.end())
                            {
                                tempcross.push_back(v);
                            }
                        }
                        bool isvertical = false;
                        for (auto &cell : tempcross)
                        {
                            if (verticalcellSet.find(cell) != verticalcellSet.end())
                            {
                                isvertical = true;
                                break;
                            }
                        }
                        if (!isvertical)
                        {
                            for(auto &crossPos : tempcross)
                            {
                                putCellItem(crossPos, 0, CellType::NormalCell, positionPhaseMap);
                            }
                        }
                    }
                }
            }
        }
    }

}

void GateLevelMapping::putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<position ,int>& _pos_phase, QString _name ){
    int x_coord = 0;
    int y_coord = 0;
    if (!sceneCoordinates(_cellpos, x_coord, y_coord)) {
        qWarning() << "[GateLevelMapping] Skip mapped cell with invalid scene coordinate:"
                   << _cellpos.first << _cellpos.second;
        return;
    }

    const position cellpos = std::make_pair(_cellpos.first / 5, _cellpos.second / 5);
    const auto phaseIt = _pos_phase.find(cellpos);
    int phase = 0;
    if (phaseIt != _pos_phase.end()) {
        phase = phaseIt->second;
    } else {
        qWarning() << "[GateLevelMapping] Mapped cell outside phase map; using phase 0:"
                   << _cellpos.first << _cellpos.second;
    }

    int cell_layer = _celllayer;
    mainWindow->scene->addFastCell(x_coord, y_coord, cell_layer, phase, _cellType, _name);
}

void GateLevelMapping::putClock(){
    std::map<position, int> positionPhaseMap;
    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint &p = it.key();
        int phase = it.value();

        positionPhaseMap[{static_cast<unsigned int>(p.x()), 
                        static_cast<unsigned int>(p.y())}] = phase;
    }

    for(auto &v : positionPhaseMap)
    {
        auto pos = v.first;
        int x = ((pos.first*5) + 2) * 20 + 200; 
        int y = ((pos.second*5) + 2) * 20 + 200;
        if((v.second >= 0) && (v.second <= 3))
        {
            mainWindow->scene->addFastClock(x, y, v.second);
        }
    }
}
