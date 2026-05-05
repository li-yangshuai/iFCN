#include "QCADScene.h"
#include <QAction>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include<QStandardItem>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <cmath>
#include "ui/mainwindow/MainWindow.h"

namespace {
constexpr int kFastRenderTileSize = 800;
constexpr int kCellHalfSize = 9;
constexpr int kClockHalfSize = CLOCK_SCHEME_SIZE_5 / 2;

QRectF cellRectForPosition(int x, int y)
{
    return QRectF(x - 10, y - 10, 20, 20);
}

QRectF clockRectForPosition(int x, int y)
{
    return QRectF(x - kClockHalfSize, y - kClockHalfSize, CLOCK_SCHEME_SIZE_5, CLOCK_SCHEME_SIZE_5);
}

quint64 packSceneKey(int x, int y)
{
    const quint32 ux = static_cast<quint32>(x);
    const quint32 uy = static_cast<quint32>(y);
    return (static_cast<quint64>(ux) << 32) | uy;
}

void rebuildFastLayerIndex(const QVector<QCADScene::FastCellRecord> &cells,
                           QHash<quint64, QVector<int>> &tileMap,
                           QSet<quint64> &occupancy)
{
    tileMap.clear();
    occupancy.clear();

    for (int index = 0; index < cells.size(); ++index) {
        const auto &cell = cells[index];
        const quint64 key = packSceneKey(cell.x, cell.y);
        occupancy.insert(key);
        const int tileX = static_cast<int>(std::floor(static_cast<qreal>(cell.x) / kFastRenderTileSize));
        const int tileY = static_cast<int>(std::floor(static_cast<qreal>(cell.y) / kFastRenderTileSize));
        const quint32 ux = static_cast<quint32>(tileX);
        const quint32 uy = static_cast<quint32>(tileY);
        const quint64 tileKey = (static_cast<quint64>(ux) << 32) | uy;
        tileMap[tileKey].push_back(index);
    }
}
}

QCADScene::~QCADScene()
{
    clearFastClockOverlay();
    clearInteractiveFastLayer();
}

void QCADScene::notifyClockRegionsChanged()
{
    emit clockRegionsChanged();
}

void QCADScene::setEditMode(EditMode mode)
{
    if (currentMode == mode) {
        return;
    }
    currentMode = mode;
    rebuildInteractiveFastLayer();
}

void QCADScene::setItemType(CellType type){
    myCellType = type;
}

void QCADScene::setCurrentClockIndex(int _clockPhase){
    currentClockIndex = _clockPhase < 0 ? -1 : qBound(0, _clockPhase, 3);
}

void QCADScene::setCurrentLayerIndex(int _layer){
    currentLayerIndex = _layer;
    rebuildInteractiveFastLayer();
}

QPointF QCADScene::clockCenterForPosition(const QPointF &pos) const
{
    const int x = static_cast<int>(std::floor((pos.x() + 10.0) / CLOCK_SCHEME_SIZE_5)) * CLOCK_SCHEME_SIZE_5 + 40;
    const int y = static_cast<int>(std::floor((pos.y() + 10.0) / CLOCK_SCHEME_SIZE_5)) * CLOCK_SCHEME_SIZE_5 + 40;
    return QPointF(x, y);
}

QCADClockScheme* QCADScene::clockSchemeAt(const QPointF &pos) const
{
    const auto allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (item == nullptr || item->type() != QCADClockScheme::Type) {
            continue;
        }
        if (item->contains(item->mapFromScene(pos))) {
            return static_cast<QCADClockScheme *>(item);
        }
    }
    return nullptr;
}

int QCADScene::phaseAtPosition(const QPointF &pos, int fallbackPhase) const
{
    if (QCADClockScheme *clockItem = clockSchemeAt(pos)) {
        if (clockItem->phase() >= 0) {
            return clockItem->phase();
        }
    }
    return normalizedCellPhase(fallbackPhase);
}

void QCADScene::insertOrUpdateClockScheme(const QPointF &pos)
{
    const QPointF center = clockCenterForPosition(pos);
    if (QCADClockScheme *clockItem = clockSchemeAt(center)) {
        applyClockPhase(clockItem, currentClockIndex);
        return;
    }

    auto *item = new QCADClockScheme(currentClockIndex);
    item->setPos(center);
    item->setZValue(-1);
    item->setVisible(clockGridVisible);
    addItem(item);
    syncCellsWithClockScheme(item);
    update(item->sceneBoundingRect());
    if (!views().isEmpty()) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(views().first()->window())) {
            mw->setDirty(true);
            mw->pushUndoSnapshot();
        }
    }
    notifyClockRegionsChanged();
}

bool QCADScene::showClockPhaseMenu(const QPointF &pos, const QPoint &screenPos)
{
    QCADClockScheme *clockItem = clockSchemeAt(pos);
    if (clockItem == nullptr) {
        return false;
    }

    QMenu menu;
    const QVector<int> phases = {-1, 0, 1, 2, 3};
    for (int phase : phases) {
        QAction *phaseAction = menu.addAction(phase < 0 ? tr("No Phase (-1)")
                                                        : tr("Phase %1").arg(phase));
        phaseAction->setCheckable(true);
        phaseAction->setChecked(clockItem->phase() == phase);
        phaseAction->setData(phase);
    }
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(tr("Delete"));

    QAction *selectedAction = menu.exec(screenPos);
    if (selectedAction == nullptr) {
        return true;
    }

    if (selectedAction == deleteAction) {
        deleteClockScheme(clockItem);
        return true;
    }

    applyClockPhase(clockItem, selectedAction->data().toInt());
    return true;
}

void QCADScene::applyClockPhase(QCADClockScheme *clockItem, int phase)
{
    if (clockItem == nullptr) {
        return;
    }

    const int boundedPhase = qBound(0, phase, 3);
    const int nextPhase = phase < 0 ? -1 : boundedPhase;
    const bool phaseChanged = clockItem->phase() != nextPhase;
    clockItem->setPhase(nextPhase);

    const QVariant fastClock = clockItem->data(FastClockRole);
    const QVariant fastIndex = clockItem->data(FastIndexRole);
    if (fastClock.toBool() && fastIndex.isValid()) {
        const int index = fastIndex.toInt();
        if (index >= 0 && index < fastClocks.size()) {
            fastClocks[index].phase = nextPhase;
        }
    }

    const bool cellsChanged = syncCellsWithClockScheme(clockItem);
    if ((phaseChanged || cellsChanged) && !views().isEmpty()) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(views().first()->window())) {
            mw->setDirty(true);
            mw->pushUndoSnapshot();
        }
    }
    if (phaseChanged) {
        notifyClockRegionsChanged();
    }
    update(clockItem->sceneBoundingRect());
}

void QCADScene::deleteClockScheme(QCADClockScheme *clockItem)
{
    if (clockItem == nullptr) {
        return;
    }

    const QRectF dirtyRect = clockItem->sceneBoundingRect();
    const QVariant fastClock = clockItem->data(FastClockRole);
    const QVariant fastIndex = clockItem->data(FastIndexRole);
    bool removed = false;

    if (fastClock.toBool() && fastIndex.isValid()) {
        const int index = fastIndex.toInt();
        if (index >= 0 && index < fastClocks.size()) {
            fastClocks.remove(index);
            fastClockTiles.clear();
            fastClockOccupancy.clear();
            for (int clockIndex = 0; clockIndex < fastClocks.size(); ++clockIndex) {
                const auto &clock = fastClocks[clockIndex];
                fastClockOccupancy.insert(packSceneKey(clock.x, clock.y));
                fastClockTiles[packTileKey(clock.x / kFastRenderTileSize,
                                           clock.y / kFastRenderTileSize)].push_back(clockIndex);
            }

            fastBounds = QRectF();
            for (const auto &clock : fastClocks) {
                fastBounds = fastBounds.isValid() ? fastBounds.united(clockRectForPosition(clock.x, clock.y))
                                                  : clockRectForPosition(clock.x, clock.y);
            }
            for (const auto &layerCells : fastCellsPerLayer) {
                for (const auto &cell : layerCells) {
                    fastBounds = fastBounds.isValid() ? fastBounds.united(cellRectForPosition(cell.x, cell.y))
                                                      : cellRectForPosition(cell.x, cell.y);
                }
            }

            rebuildFastClockOverlay();
            removed = true;
        }
    }

    if (!removed) {
        removeItem(clockItem);
        delete clockItem;
        removed = true;
    }

    if (!views().isEmpty()) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(views().first()->window())) {
            mw->setDirty(true);
            mw->pushUndoSnapshot();
        }
    }
    if (removed) {
        notifyClockRegionsChanged();
    }
    update(dirtyRect);
}

bool QCADScene::syncCellsWithClockScheme(QCADClockScheme *clockItem)
{
    if (clockItem == nullptr) {
        return false;
    }
    if (clockItem->phase() < 0) {
        return false;
    }

    bool changed = false;
    const QRectF region = clockItem->sceneBoundingRect();
    const auto overlappingItems = items(region, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform());
    for (QGraphicsItem *item : overlappingItems) {
        if (item == nullptr || item->type() != QCADCellItem::Type) {
            continue;
        }
        auto *cellItem = static_cast<QCADCellItem *>(item);
        if (!clockItem->contains(clockItem->mapFromScene(cellItem->pos()))) {
            continue;
        }
        changed = setCellPhase(cellItem, clockItem->phase()) || changed;
    }
    return changed;
}

bool QCADScene::setCellPhase(QCADCellItem *cellItem, int phase)
{
    if (cellItem == nullptr) {
        return false;
    }

    const int boundedPhase = normalizedCellPhase(phase);
    if (simon::timezone(*cellItem) == boundedPhase) {
        return false;
    }

    simon::timezone(*cellItem) = boundedPhase;
    cellItem->update();

    const QVariant fastLayer = cellItem->data(FastLayerRole);
    const QVariant fastIndex = cellItem->data(FastIndexRole);
    if (fastLayer.isValid() && fastIndex.isValid()) {
        updateFastCellPhase(fastLayer.toInt(), fastIndex.toInt(), boundedPhase);
    }
    return true;
}

int QCADScene::normalizedCellPhase(int phase) const
{
    return phase < 0 ? 0 : qBound(0, phase, 3);
}


void QCADScene::placeClockScheme( const int _clock_scheme [4][4] ) {

    //坐标起点(40,40)是校准过的，更换会无法正常识别相位
    QPoint startPoint(40,40);
    uint8_t i = 0,j =0;
    for(int xs = startPoint.x(); xs <= SCENE_WIDTH/2; xs+= CLOCK_SCHEME_SIZE_5){
        if(i==4)
            i = 0;
        for(int ys = startPoint.y(); ys <= SCENE_HEIGHT/2; ys+= CLOCK_SCHEME_SIZE_5){
            if(j == 4)
                j=0;
            int _phase = _clock_scheme[j][i];
            QCADClockScheme *item = new QCADClockScheme( _phase);
            item->setPos(xs, ys);
            item->setZValue(-1);
            item->setVisible(clockGridVisible);
            addItem(item);
            j++;
        }
        i++;
    }
    notifyClockRegionsChanged();
}


void QCADScene::clearPhaseRecord(){
    clearFastClockOverlay();
    const bool hadFastClocks = !fastClocks.isEmpty();
    fastClocks.clear();
    fastClockTiles.clear();
    fastClockOccupancy.clear();

    if (hadFastClocks) {
        fastBounds = QRectF();
        for (const auto &layerCells : fastCellsPerLayer) {
            for (const auto &cell : layerCells) {
                fastBounds = fastBounds.isValid() ? fastBounds.united(cellRectForPosition(cell.x, cell.y))
                                                  : cellRectForPosition(cell.x, cell.y);
            }
        }
    }

    bool removedClockItem = hadFastClocks;
    foreach(QGraphicsItem *item, items()){
        if(item->type() == QCADClockScheme::Type){
            removeItem(item);
            delete item;
            removedClockItem = true;
        }
    }
    if (removedClockItem) {
        notifyClockRegionsChanged();
    }
}

void QCADScene::beginFastRenderBuild(int layerCount)
{
    clearFastRender();
    fastRenderEnabled = true;
    const int count = qMax(0, layerCount);
    fastCellsPerLayer.resize(count);
    fastLayerVisible.fill(true, count);
    fastCellTilesPerLayer.resize(count);
    fastCellOccupancyPerLayer.resize(count);
}

void QCADScene::addFastCell(int x, int y, int layer, int phase, CellType type, const QString &name)
{
    if (!fastRenderEnabled || layer < 0) {
        return;
    }

    if (layer >= fastCellsPerLayer.size()) {
        fastCellsPerLayer.resize(layer + 1);
        while (fastLayerVisible.size() < layer + 1) {
            fastLayerVisible.push_back(true);
        }
        fastCellTilesPerLayer.resize(layer + 1);
        fastCellOccupancyPerLayer.resize(layer + 1);
    }

    const quint64 cellKey = packSceneKey(x, y);
    if (fastCellOccupancyPerLayer[layer].contains(cellKey)) {
        return;
    }
    fastCellOccupancyPerLayer[layer].insert(cellKey);

    FastCellRecord record;
    record.x = x;
    record.y = y;
    record.layer = layer;
    record.phase = phase;
    record.type = type;
    record.name = name;

    const int index = fastCellsPerLayer[layer].size();
    fastCellsPerLayer[layer].push_back(record);
    fastCellTilesPerLayer[layer][packTileKey(x / kFastRenderTileSize, y / kFastRenderTileSize)].push_back(index);

    fastBounds = fastBounds.isValid() ? fastBounds.united(cellRectForPosition(x, y)) : cellRectForPosition(x, y);
}

void QCADScene::addFastClock(int x, int y, int phase)
{
    if (!fastRenderEnabled) {
        return;
    }

    const quint64 key = packSceneKey(x, y);
    if (fastClockOccupancy.contains(key)) {
        return;
    }
    fastClockOccupancy.insert(key);

    FastClockRecord record;
    record.x = x;
    record.y = y;
    record.phase = phase;

    const int index = fastClocks.size();
    fastClocks.push_back(record);
    fastClockTiles[packTileKey(x / kFastRenderTileSize, y / kFastRenderTileSize)].push_back(index);

    fastBounds = fastBounds.isValid() ? fastBounds.united(clockRectForPosition(x, y)) : clockRectForPosition(x, y);
}

void QCADScene::finalizeFastRenderBuild()
{
    rebuildFastClockOverlay();
    rebuildInteractiveFastLayer();
    update(fastBounds);
    notifyClockRegionsChanged();
}

void QCADScene::clearFastRender()
{
    clearFastClockOverlay();
    clearInteractiveFastLayer();
    fastRenderEnabled = false;
    fastBounds = QRectF();
    fastCellsPerLayer.clear();
    fastLayerVisible.clear();
    fastCellTilesPerLayer.clear();
    fastCellOccupancyPerLayer.clear();
    fastClocks.clear();
    fastClockTiles.clear();
    fastClockOccupancy.clear();
    interactiveFastLayerIndex = -1;
    update();
}

bool QCADScene::hasFastRender() const
{
    return fastRenderEnabled && fastBounds.isValid();
}

QRectF QCADScene::fastRenderBounds() const
{
    return fastBounds;
}

const QVector<QVector<QCADScene::FastCellRecord>>& QCADScene::fastCellsByLayer() const
{
    return fastCellsPerLayer;
}

void QCADScene::setFastLayerVisible(int layer, bool visible)
{
    if (layer < 0) {
        return;
    }
    while (fastLayerVisible.size() <= layer) {
        fastLayerVisible.push_back(true);
    }
    if (fastLayerVisible[layer] == visible) {
        return;
    }
    fastLayerVisible[layer] = visible;
    rebuildInteractiveFastLayer();
    update();
}

bool QCADScene::isFastLayerVisible(int layer) const
{
    return layer >= 0 && layer < fastLayerVisible.size() ? fastLayerVisible[layer] : true;
}

void QCADScene::updateFastCellPosition(int layer, int index, int x, int y)
{
    if (layer < 0 || layer >= fastCellsPerLayer.size()) {
        return;
    }
    auto &cells = fastCellsPerLayer[layer];
    if (index < 0 || index >= cells.size()) {
        return;
    }

    const quint64 currentKey = packSceneKey(cells[index].x, cells[index].y);
    const quint64 nextKey = packSceneKey(x, y);
    if (currentKey != nextKey && fastCellOccupancyPerLayer[layer].contains(nextKey)) {
        return;
    }

    cells[index].x = x;
    cells[index].y = y;
    rebuildFastLayerIndex(cells, fastCellTilesPerLayer[layer], fastCellOccupancyPerLayer[layer]);

    fastBounds = QRectF();
    for (const auto &clock : fastClocks) {
        fastBounds = fastBounds.isValid() ? fastBounds.united(clockRectForPosition(clock.x, clock.y))
                                          : clockRectForPosition(clock.x, clock.y);
    }
    for (const auto &layerCells : fastCellsPerLayer) {
        for (const auto &cell : layerCells) {
            fastBounds = fastBounds.isValid() ? fastBounds.united(cellRectForPosition(cell.x, cell.y))
                                              : cellRectForPosition(cell.x, cell.y);
        }
    }

    update();
}

void QCADScene::updateFastCellPhase(int layer, int index, int phase)
{
    if (layer < 0 || layer >= fastCellsPerLayer.size()) {
        return;
    }
    auto &cells = fastCellsPerLayer[layer];
    if (index < 0 || index >= cells.size()) {
        return;
    }

    cells[index].phase = qBound(0, phase, 3);
    update(cellRectForPosition(cells[index].x, cells[index].y));
}

void QCADScene::updateFastCellName(int layer, int index, const QString &name)
{
    if (layer < 0 || layer >= fastCellsPerLayer.size()) {
        return;
    }
    auto &cells = fastCellsPerLayer[layer];
    if (index < 0 || index >= cells.size()) {
        return;
    }
    cells[index].name = name;
    update(cellRectForPosition(cells[index].x, cells[index].y).adjusted(-4, -4, 48, 20));
}

void QCADScene::removeFastCell(int layer, int index)
{
    if (layer < 0 || layer >= fastCellsPerLayer.size()) {
        return;
    }
    auto &cells = fastCellsPerLayer[layer];
    if (index < 0 || index >= cells.size()) {
        return;
    }

    cells.remove(index);
    rebuildFastLayerIndex(cells, fastCellTilesPerLayer[layer], fastCellOccupancyPerLayer[layer]);

    fastBounds = QRectF();
    for (const auto &clock : fastClocks) {
        fastBounds = fastBounds.isValid() ? fastBounds.united(clockRectForPosition(clock.x, clock.y))
                                          : clockRectForPosition(clock.x, clock.y);
    }
    for (const auto &layerCells : fastCellsPerLayer) {
        for (const auto &cell : layerCells) {
            fastBounds = fastBounds.isValid() ? fastBounds.united(cellRectForPosition(cell.x, cell.y))
                                              : cellRectForPosition(cell.x, cell.y);
        }
    }

    rebuildInteractiveFastLayer();
    update();
}

void QCADScene::setHighQualityMode(bool enabled)
{
    if (highQualityMode == enabled) {
        return;
    }
    highQualityMode = enabled;
    update();
}

bool QCADScene::isHighQualityMode() const
{
    return highQualityMode;
}

void QCADScene::setClockGridVisible(bool visible)
{
    if (clockGridVisible == visible) {
        return;
    }

    clockGridVisible = visible;
    const auto allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (item != nullptr && item->type() == QCADClockScheme::Type &&
            !item->data(FastClockRole).toBool()) {
            item->setVisible(visible);
        }
    }
    if (visible) {
        rebuildFastClockOverlay();
    } else {
        clearFastClockOverlay();
    }
    update();
}

bool QCADScene::isClockGridVisible() const
{
    return clockGridVisible;
}

QVector<QCADScene::ClockRegionRecord> QCADScene::clockRegions() const
{
    QVector<ClockRegionRecord> regions;
    QSet<quint64> seen;

    auto appendRegion = [&regions, &seen](int x, int y, int phase) {
        const quint64 key = packSceneKey(x, y);
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
        ClockRegionRecord record;
        record.x = x;
        record.y = y;
        record.phase = phase < 0 ? -1 : qBound(0, phase, 3);
        regions.push_back(record);
    };

    for (const auto &clock : fastClocks) {
        appendRegion(clock.x, clock.y, clock.phase);
    }

    const auto allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (item == nullptr || item->type() != QCADClockScheme::Type ||
            item->data(FastClockRole).toBool()) {
            continue;
        }
        auto *clockItem = static_cast<QCADClockScheme *>(item);
        appendRegion(static_cast<int>(std::round(clockItem->pos().x())),
                     static_cast<int>(std::round(clockItem->pos().y())),
                     clockItem->phase());
    }

    std::sort(regions.begin(), regions.end(), [](const ClockRegionRecord &lhs,
                                                 const ClockRegionRecord &rhs) {
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        return lhs.y < rhs.y;
    });
    return regions;
}

void QCADScene::restoreClockRegions(const QVector<ClockRegionRecord> &regions)
{
    clearPhaseRecord();
    for (const ClockRegionRecord &region : regions) {
        auto *item = new QCADClockScheme(region.phase);
        item->setPos(region.x, region.y);
        item->setZValue(-1);
        item->setVisible(clockGridVisible);
        addItem(item);
    }
    update();
    notifyClockRegionsChanged();
}

void QCADScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QColor("#FFFFFF"));
}

void QCADScene::drawForeground(QPainter *painter, const QRectF &rect)
{
    if (hasFastRender()) {
        drawFastCellRecords(painter, rect);
    }
}

// 鼠标点击布局空间，能够获取离鼠标最近的网格点的位置，来放置cell
QPointF QCADScene::caculateRealPostion(int _posx, int _posy){
    
    int temp_x = qAbs(_posx % GRID_SIZE);
    int temp_y = qAbs(_posy % GRID_SIZE);

    int index_x = _posx / GRID_SIZE;
    int index_y = _posy / GRID_SIZE;

    int pos_x, pos_y;

    if(temp_x <= GRID_SIZE/2){
        pos_x = index_x * GRID_SIZE;
    }else{
        if(index_x >=0){
            pos_x = (index_x + 1) * GRID_SIZE;
        }else{
            pos_x = (index_x -1) * GRID_SIZE;
        }
    }

    if(temp_y <= GRID_SIZE/2){
        pos_y = index_y * GRID_SIZE;
    }else{
        if(index_y >=0){
            pos_y = (index_y + 1) * GRID_SIZE;
        }else{
            pos_y = (index_y -1) * GRID_SIZE;
        }
    }
    return QPointF(pos_x, pos_y);

}

/* 冲突检测*/
void QCADScene::mousePressEvent(QGraphicsSceneMouseEvent *event){

    if (event->button() == Qt::RightButton) {
        if (showClockPhaseMenu(event->scenePos(), event->screenPos())) {
            event->accept();
            return;
        }
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    if (currentMode == EditMode::ClockScheme) {
        if (event->button() == Qt::LeftButton) {
            insertOrUpdateClockScheme(event->scenePos());
            event->accept();
            return;
        }
    } else if(currentMode == EditMode::Insert && event->button() == Qt::LeftButton){

        QPointF scenePoint = event->scenePos();
        QPointF realPos = caculateRealPostion(scenePoint.x(),scenePoint.y());
        QList<QGraphicsItem*> itemsAtPos = items(realPos, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform());
        
        for(QGraphicsItem* item : itemsAtPos){
            if(item->type() == QCADCellItem::Type){
                if(currentLayerIndex == item->zValue())
                    return;
            }
        }

        const int phase = phaseAtPosition(realPos, currentClockIndex);
        QCADCellItem * cellItem = new QCADCellItem( realPos.x(), realPos.y(), currentLayerIndex, phase, myCellType);
        cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加

        emit cellItemInserted(cellItem);
        event->accept();
        return;

    }else if (currentMode == EditMode::Select) {
        // check if the item is cell， is not clockzone
        foreach (QGraphicsItem *item, selectedItems()){
            if(item->type() == QCADCellItem::Type){
                item->setFlag(QGraphicsItem::ItemIsMovable, true);
                // originalItemPositions[item] = item->pos();
            }
        }
    } 
    QGraphicsScene::mousePressEvent(event);
}

void QCADScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event){

    QGraphicsScene::mouseMoveEvent(event);
}

void QCADScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (currentMode == EditMode::Select) {
        bool changed = false;
        foreach (QGraphicsItem *item, selectedItems()) {
            if (item->type() == QCADCellItem::Type) {
                QCADCellItem* cellItem = static_cast<QCADCellItem*>(item);

                // 鼠标释放后的位置，吸附对齐
                QPointF nextpos = caculateRealPostion(item->pos().x(), item->pos().y());

                // 检查该位置是否已有其他 Cell（排除自己）
                bool conflict = false;
                QList<QGraphicsItem*> itemsAtPos = items(nextpos);
                for (QGraphicsItem* other : itemsAtPos) {
                    if (other == item) continue;
                    if (other->type() == QCADCellItem::Type &&
                        int(other->zValue()) == currentLayerIndex) {
                        conflict = true;
                        break;
                    }
                }

                if (conflict) {
                    // 冲突，回退到原逻辑位置
                    cellItem->setPos(QPointF(simon::x(*cellItem), simon::y(*cellItem)));
                    // qDebug() << "位置冲突，移动已取消";
                } else {
                    // 吸附 + 更新数据
                    cellItem->setPos(nextpos);
                    simon::x(*cellItem) = nextpos.x();
                    simon::y(*cellItem) = nextpos.y();
                    const int phase = phaseAtPosition(nextpos, simon::timezone(*cellItem));
                    const QVariant fastLayer = item->data(FastLayerRole);
                    const QVariant fastIndex = item->data(FastIndexRole);
                    if (fastLayer.isValid() && fastIndex.isValid()) {
                        updateFastCellPosition(fastLayer.toInt(), fastIndex.toInt(),
                                               static_cast<int>(nextpos.x()), static_cast<int>(nextpos.y()));
                    }
                    setCellPhase(cellItem, phase);
                    changed = true;
                }
            }
        }
        if (changed && !views().isEmpty()) {
            if (MainWindow* mw = qobject_cast<MainWindow*>(views().first()->window())) {
                mw->setDirty(true);
                mw->pushUndoSnapshot();
            }
        }
    }

    QGraphicsScene::mouseReleaseEvent(event);
}

quint64 QCADScene::packTileKey(int x, int y)
{
    const quint32 ux = static_cast<quint32>(x);
    const quint32 uy = static_cast<quint32>(y);
    return (static_cast<quint64>(ux) << 32) | uy;
}

QColor QCADScene::colorForPhase(int phase) const
{
    switch (phase) {
        case 0: return QColor(PARSE_0);
        case 1: return QColor(PARSE_1);
        case 2: return QColor(PARSE_2);
        case 3: return QColor(PARSE_3);
        default: return QColor(PARSE_0);
    }
}

void QCADScene::drawFastClockRecords(QPainter *painter, const QRectF &rect)
{
    const QPen hdClockPen(QColor(120, 120, 120, highQualityMode ? 96 : 48),
                          highQualityMode ? 0.8 : 0.4);
    painter->setPen(highQualityMode ? hdClockPen : Qt::NoPen);

    const int minTileX = static_cast<int>(std::floor(rect.left() / kFastRenderTileSize));
    const int maxTileX = static_cast<int>(std::floor(rect.right() / kFastRenderTileSize));
    const int minTileY = static_cast<int>(std::floor(rect.top() / kFastRenderTileSize));
    const int maxTileY = static_cast<int>(std::floor(rect.bottom() / kFastRenderTileSize));

    for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
        for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
            const auto it = fastClockTiles.constFind(packTileKey(tileX, tileY));
            if (it == fastClockTiles.constEnd()) {
                continue;
            }
            for (int index : it.value()) {
                const FastClockRecord &clock = fastClocks[index];
                const QRectF clockRect = clockRectForPosition(clock.x, clock.y);
                if (!rect.intersects(clockRect)) {
                    continue;
                }

                QColor zoneColor;
                switch (clock.phase) {
                    case 0: zoneColor = QColor(CLOCK_ZONE_0); break;
                    case 1: zoneColor = QColor(CLOCK_ZONE_1); break;
                    case 2: zoneColor = QColor(CLOCK_ZONE_2); break;
                    case 3: zoneColor = QColor(CLOCK_ZONE_3); break;
                    default: zoneColor = QColor(CLOCK_ZONE_0); break;
                }
                painter->setBrush(zoneColor);
                painter->drawRect(clockRect);
            }
        }
    }
}

void QCADScene::drawFastCellRecords(QPainter *painter, const QRectF &rect)
{
    const qreal lod = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
    const int minTileX = static_cast<int>(std::floor(rect.left() / kFastRenderTileSize));
    const int maxTileX = static_cast<int>(std::floor(rect.right() / kFastRenderTileSize));
    const int minTileY = static_cast<int>(std::floor(rect.top() / kFastRenderTileSize));
    const int maxTileY = static_cast<int>(std::floor(rect.bottom() / kFastRenderTileSize));

    QPen pen(Qt::black);
    pen.setWidthF(highQualityMode ? 1.0 : 0.8);
    painter->setPen(pen);

    for (int layer = 0; layer < fastCellsPerLayer.size(); ++layer) {
        if (!isFastLayerVisible(layer)) {
            continue;
        }
        if (layer == interactiveFastLayerIndex && !interactiveFastLayerItems.isEmpty()) {
            continue;
        }
        const auto &tileMap = fastCellTilesPerLayer[layer];
        const auto &cells = fastCellsPerLayer[layer];
        for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
            for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
                const auto it = tileMap.constFind(packTileKey(tileX, tileY));
                if (it == tileMap.constEnd()) {
                    continue;
                }
                for (int index : it.value()) {
                    const FastCellRecord &cell = cells[index];
                    if (!rect.intersects(cellRectForPosition(cell.x, cell.y))) {
                        continue;
                    }
                    drawFastCell(painter, cell, lod);
                }
            }
        }
    }
}

void QCADScene::drawFastCell(QPainter *painter, const FastCellRecord &cell, qreal lod)
{
    const qreal detailLod = highQualityMode ? qMax(lod, 0.75) : lod;
    QColor color;
    switch (cell.type) {
        case CellType::InputCell:
            color = QColor(INPUT_COLOR);
            break;
        case CellType::OutputCell:
            color = QColor(OUTPUT_COLOR);
            break;
        case CellType::FixedCell_0:
        case CellType::FixedCell_1:
            color = QColor(Qt::black);
            break;
        default:
            color = colorForPhase(cell.phase);
            break;
    }

    const QRectF body(cell.x - kCellHalfSize, cell.y - kCellHalfSize, kCellHalfSize * 2, kCellHalfSize * 2);
    painter->setBrush(color);
    painter->drawRect(body);

    if (detailLod < 0.45) {
        return;
    }

    if (cell.type == CellType::VerticalCell) {
        painter->drawEllipse(QPointF(cell.x, cell.y), kCellHalfSize, kCellHalfSize);
    } else if (cell.type == CellType::CrossoverCell) {
        painter->drawLine(QPointF(cell.x - 4.5, cell.y - 4.5), QPointF(cell.x + 4.5, cell.y + 4.5));
        painter->drawLine(QPointF(cell.x - 4.5, cell.y + 4.5), QPointF(cell.x + 4.5, cell.y - 4.5));
    } else if ((cell.type == CellType::FixedCell_0 || cell.type == CellType::FixedCell_1) && detailLod >= 0.65) {
        painter->setBrush(QBrush(QColor(Qt::white)));
        if (cell.type == CellType::FixedCell_0) {
            painter->drawEllipse(QPointF(cell.x + 4.5, cell.y + 4.5), 2.5, 2.5);
            painter->drawEllipse(QPointF(cell.x - 4.5, cell.y - 4.5), 2.5, 2.5);
        } else {
            painter->drawEllipse(QPointF(cell.x + 4.5, cell.y - 4.5), 2.5, 2.5);
            painter->drawEllipse(QPointF(cell.x - 4.5, cell.y + 4.5), 2.5, 2.5);
        }
    } else if (detailLod >= 0.65) {
        painter->drawEllipse(QPointF(cell.x + 4.5, cell.y - 4.5), 2.5, 2.5);
        painter->drawEllipse(QPointF(cell.x + 4.5, cell.y + 4.5), 2.5, 2.5);
        painter->drawEllipse(QPointF(cell.x - 4.5, cell.y + 4.5), 2.5, 2.5);
        painter->drawEllipse(QPointF(cell.x - 4.5, cell.y - 4.5), 2.5, 2.5);
    }

    if (!cell.name.isEmpty() && detailLod >= (highQualityMode ? 0.8 : 0.95)) {
        painter->drawText(QPointF(cell.x + 12, cell.y - 5), cell.name);
    }
}

bool QCADScene::shouldUseInteractiveFastLayer() const
{
    return fastRenderEnabled
        && currentMode != EditMode::DragScene
        && currentLayerIndex >= 0
        && currentLayerIndex < fastCellsPerLayer.size()
        && isFastLayerVisible(currentLayerIndex);
}

void QCADScene::clearFastClockOverlay()
{
    for (QGraphicsItem *item : fastClockOverlayItems) {
        if (item != nullptr) {
            removeItem(item);
            delete item;
        }
    }
    fastClockOverlayItems.clear();
}

void QCADScene::rebuildFastClockOverlay()
{
    clearFastClockOverlay();
    if (!clockGridVisible || !fastRenderEnabled) {
        return;
    }

    fastClockOverlayItems.reserve(fastClocks.size());
    for (const auto &clock : fastClocks) {
        auto *item = new QCADClockScheme(clock.phase);
        item->setPos(clock.x, clock.y);
        item->setZValue(-1);
        item->setVisible(true);
        item->setData(FastClockRole, true);
        item->setData(FastIndexRole, fastClockOverlayItems.size());
        addItem(item);
        fastClockOverlayItems.push_back(item);
    }
}

void QCADScene::clearInteractiveFastLayer()
{
    for (QGraphicsItem *item : interactiveFastLayerItems) {
        if (item != nullptr) {
            removeItem(item);
            delete item;
        }
    }
    interactiveFastLayerItems.clear();
    interactiveFastLayerIndex = -1;
}

void QCADScene::rebuildInteractiveFastLayer()
{
    clearInteractiveFastLayer();
    if (!shouldUseInteractiveFastLayer()) {
        return;
    }

    interactiveFastLayerIndex = currentLayerIndex;
    const auto &cells = fastCellsPerLayer[interactiveFastLayerIndex];
    interactiveFastLayerItems.reserve(cells.size());

    for (int index = 0; index < cells.size(); ++index) {
        const auto &cell = cells[index];
        auto *item = new QCADCellItem(cell.x, cell.y, cell.layer, cell.phase, cell.type, cell.name);
        item->setPos(cell.x, cell.y);
        item->setZValue(cell.layer);
        item->setVisible(true);
        item->setData(FastLayerRole, cell.layer);
        item->setData(FastIndexRole, index);
        addItem(item);
        interactiveFastLayerItems.push_back(item);
    }
}
