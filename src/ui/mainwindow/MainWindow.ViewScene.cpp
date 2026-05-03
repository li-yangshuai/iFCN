#include "ui/mainwindow/MainWindow.h"
#include <QGridLayout>
#include <QSignalBlocker>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

void MainWindow::createViewAndScene()
{
    // layout
    centralWidget = new QWidget(this);  
    verticalLayout = new QVBoxLayout(centralWidget);
    splitter = new QSplitter(centralWidget);
    splitter->setOrientation(Qt::Horizontal);
    toolBox = new QToolBox(splitter);
    toolBox->setMinimumWidth(260);
    splitter->setHandleWidth(6);

    scene = new QCADScene(this); 
    scene->setSceneRect(QRectF(0,0,SCENE_WIDTH, SCENE_HEIGHT));
    // scene->setBackgroundBrush(Qt::black);
    view = new QCADView(this);
    view->setScene(scene);
    view->resize(800,600);

    splitter->addWidget(toolBox);
    splitter->addWidget(view);   
    verticalLayout->setContentsMargins(12, 12, 12, 8);
    verticalLayout->setSpacing(10);
    verticalLayout->addWidget(splitter);
    this->setCentralWidget(centralWidget);
    view->centerOn(scene->sceneRect().center());

    customStatusBar = new CustomStatusBar(this);
    verticalLayout->addWidget(customStatusBar, 1);

}

void MainWindow::centerViewOnItems(bool fitToView)
{
    if (scene == nullptr || view == nullptr) {
        return;
    }

    QRectF contentRect = scene->itemsBoundingRect();
    if (scene->hasFastRender()) {
        const QRectF fastRect = scene->fastRenderBounds();
        contentRect = contentRect.isValid() ? contentRect.united(fastRect) : fastRect;
    }
    if (!contentRect.isValid() || contentRect.isEmpty()) {
        view->centerOn(scene->sceneRect().center());
        return;
    }

    constexpr qreal kMargin = 120.0;
    contentRect = contentRect.adjusted(-kMargin, -kMargin, kMargin, kMargin);

    // Expand scene boundaries to always include the full mapped layout.
    scene->setSceneRect(scene->sceneRect().united(contentRect));

    if (fitToView) {
        view->fitInView(contentRect, Qt::KeepAspectRatio);
    } else {
        view->centerOn(contentRect.center());
    }
}

quint64 MainWindow::packSceneCoord(int x, int y)
{
    const quint32 ux = static_cast<quint32>(x);
    const quint32 uy = static_cast<quint32>(y);
    return (static_cast<quint64>(ux) << 32) | uy;
}

void MainWindow::beginSceneBatchUpdate()
{
    if (isBatchUpdating || scene == nullptr || view == nullptr) {
        return;
    }

    isBatchUpdating = true;
    batchDirtyPending = false;

    batchOccupiedByLayer.clear();
    batchOccupiedByLayer.resize(layers.size());
    for (int layerIdx = 0; layerIdx < layers.size(); ++layerIdx) {
        for (QGraphicsItem* item : layers[layerIdx]) {
            if (item == nullptr) {
                continue;
            }
            batchOccupiedByLayer[layerIdx].insert(packSceneCoord(static_cast<int>(item->x()), static_cast<int>(item->y())));
        }
    }

    view->setUpdatesEnabled(false);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
}

void MainWindow::endSceneBatchUpdate(bool recenter)
{
    if (!isBatchUpdating || scene == nullptr || view == nullptr) {
        return;
    }

    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    view->setUpdatesEnabled(true);
    scene->update();

    isBatchUpdating = false;
    batchOccupiedByLayer.clear();

    if (batchDirtyPending) {
        setWindowModified(true);
        updateUi();
    }
    batchDirtyPending = false;

    if (recenter) {
        centerViewOnItems(true);
    }
}

void MainWindow::createToolBox(){
    buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(false);
    connect(buttonGroup, SIGNAL(buttonClicked(int)), this, SLOT(buttonGroupClicked(int)));  
    QGridLayout *layout = new QGridLayout;
    layout->addWidget(createCellWidget(tr("Normal"), CellType::NormalCell), 0,0);
    layout->addWidget(createCellWidget(tr("Fixed_0"), CellType::FixedCell_0), 0,1);
    layout->addWidget(createCellWidget(tr("Fixed_1"), CellType::FixedCell_1), 1,0);
    layout->addWidget(createCellWidget(tr("Input"), CellType::InputCell), 1,1);
    layout->addWidget(createCellWidget(tr("Output"), CellType::OutputCell), 2,0);
    layout->addWidget(createCellWidget(tr("Vertical"), CellType::VerticalCell), 2,1);
    layout->addWidget(createCellWidget(tr("Crossover"), CellType::CrossoverCell), 3,0);
    layout->setRowStretch(4, 10);
    layout->setColumnStretch(4, 10);
    QWidget *itemWidget = new QWidget;
    itemWidget->setLayout(layout);
    toolBox->setMinimumWidth(qMax(200, itemWidget->sizeHint().width()));
    toolBox->addItem(itemWidget, tr("Standard Cell Library"));

    clockSchemeGroup = new QButtonGroup(this);
    connect(clockSchemeGroup, SIGNAL(buttonClicked(QAbstractButton*)),  this, SLOT(slotClockSchemeGroupClicked(QAbstractButton*)));    //还没写
    QGridLayout *clockSchemeLayout = new QGridLayout;
    // clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Select"),":/csSelect.svg")     ,0,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Clean"),":/cleanCS.svg")       ,0,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Custom"),":/custom.svg")       ,0,1);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("ONE-D"),":/2dd.svg")           ,1,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("2DDwave"),":/2dd.svg")         ,1,1);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("USE"),":/use.svg")             ,2,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("RES"),":/res.svg")             ,2,1);
    clockSchemeLayout->setRowStretch(3, 8);
    clockSchemeLayout->setColumnStretch(3, 8);
    QWidget *clockSchemeWidget = new QWidget;
    clockSchemeWidget->setLayout(clockSchemeLayout);
    toolBox->addItem(clockSchemeWidget, tr("Clock Schemes"));

}

void MainWindow::buttonGroupClicked(int id){
    QList<QAbstractButton *> buttons = buttonGroup->buttons();
    for (QAbstractButton *button : buttons) {
        if (buttonGroup->button(id) != button)
            button->setChecked(false);
    }
    scene->setItemType(CellType(id));
    // scene->setMode(QCADScene::InsertCell);
}

void MainWindow::slotClockSchemeGroupClicked(QAbstractButton* button){
    QList<QAbstractButton *> buttons = clockSchemeGroup->buttons();
    for (QAbstractButton *myButton: buttons) {
        if (myButton != button)
            myButton->setChecked(false);
    }
    // reset
    QString text = button->text();
    bool changedClockRegions = false;
    if(text == tr("Clean")){
        scene->clearPhaseRecord();
        changedClockRegions = true;
    }else if(text == tr("Custom")){
        viewModeButtonGroup->setExclusive(false);
        selectModeButton->setChecked(false);
        insertModeButton->setChecked(false);
        dragModeButton->setChecked(false);
        viewModeButtonGroup->setExclusive(true);
        setEditMode(EditMode::ClockScheme);
        scene->setEditMode(EditMode::ClockScheme);
        if (customStatusBar != nullptr) {
            customStatusBar->addMessage(tr("Custom clock scheme placement enabled"));
        }
    }else if(text == tr("ONE-D")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(ONEDIMEN_CLOKC_SCHEME);
        changedClockRegions = true;
    }else if(text == tr("2DDwave")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(TDDWAVE_CLOCK_SCHEME);
        changedClockRegions = true;
    }else if(text == tr("USE")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(USE_CLOKC_SCHEME);
        changedClockRegions = true;
    }else if(text == tr("RES")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(RES_CLOKC_SCHEME);
        changedClockRegions = true;
    }
    if (changedClockRegions) {
        setDirty(true);
        pushUndoSnapshot();
    }
    update();
    // setDirty(true);
}

int MainWindow::selectedClockPhase() const
{
    if (clockComboBox == nullptr) {
        return 0;
    }
    const QVariant phaseData = clockComboBox->itemData(clockComboBox->currentIndex());
    if (phaseData.isValid()) {
        return phaseData.toInt();
    }
    return clockComboBox->currentIndex();
}


QWidget* MainWindow::createCellWidget(const QString &text, CellType type){
    int clockIdx = qMax(0, selectedClockPhase());
    QCADCellItem item(type);
    QIcon icon(item.image(clockIdx));
    QToolButton *button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setCheckable(true);
    buttonGroup->addButton(button, int(type));

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(button, 0, 0, Qt::AlignHCenter);
    layout->addWidget(new QLabel(text), 1, 0, Qt::AlignCenter);

    QWidget *widget = new QWidget;
    widget->setLayout(layout);

    return widget;
}

QWidget* MainWindow::createClockSchemeWidget(const QString &text, const QString &image){
    QToolButton *button = new QToolButton;
    button->setText(text);
    button->setIcon(QIcon(image));
    button->setIconSize(QSize(50,50));
    button->setCheckable(true);
    clockSchemeGroup->addButton(button);

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(button, 0, 0, Qt::AlignHCenter);
    layout->addWidget(new QLabel(text), 1, 0, Qt::AlignCenter);
    QWidget *widget = new QWidget;
    widget->setLayout(layout);

    return widget;
}


void MainWindow::updateLayerAndCellZValue()
{
    int num = layers.size();
    for(int i = 0; i < num; ++i)
    {
        auto itemGroup = layers[i];
        for(QGraphicsItem* item :itemGroup){
            item->setZValue(i);
            simon::layer_index(*static_cast<QCADCellItem *>(item)) = i;
        }
    }
}
void MainWindow::slotClockIndexChanged(int idx)
{
    Q_UNUSED(idx);
    const int phase = selectedClockPhase();
    scene->setCurrentClockIndex(phase);
    if (phase < 0) {
        return;
    }
    QList<QGraphicsItem *> items = scene->selectedItems();

    if(items.isEmpty()) 
        return;

    for(QGraphicsItem *item: items)
    {
        if (item->type() != QCADCellItem::Type) {
            continue;
        }
        simon::timezone(*static_cast<QCADCellItem *>(item)) = phase; //时钟域，由控制面板传递
        //添加更新操作
    }
    setDirty(true);
    pushUndoSnapshot();
}

void MainWindow::slotLayerActiveChanged(int idx){
    scene->setCurrentLayerIndex(idx);
}

void MainWindow::slotToggleClockGrid(bool on)
{
    if (scene != nullptr) {
        scene->setClockGridVisible(on);
    }
    if (customStatusBar != nullptr) {
        QString message = on ? QStringLiteral("Clock grid enabled")
                             : QStringLiteral("Clock grid disabled");
        customStatusBar->addMessage(message);
    }
}

void MainWindow::setHighQualityMode(bool on)
{
    if (view != nullptr) {
        view->setHighQualityMode(on);
    }
    if (scene != nullptr) {
        scene->setHighQualityMode(on);
    }
}

void MainWindow::slotToggleHighQualityView(bool on)
{
    setHighQualityMode(on);
    QString message = on ? QStringLiteral("HD view enabled") : QStringLiteral("HD view disabled");
    customStatusBar->addMessage(message);
}

void MainWindow::viewModeChange() {
    if (selectModeButton->isChecked()) {
        setEditMode(EditMode::Select);
        scene->setEditMode(EditMode::Select);
    } else if (insertModeButton->isChecked()) {
        setEditMode(EditMode::Insert);
        scene->setEditMode(EditMode::Insert);
    } else if (dragModeButton->isChecked()) {
        setEditMode(EditMode::DragScene);
        scene->setEditMode(EditMode::DragScene);
    }
}

void MainWindow::setEditMode(EditMode mode) {
    currentMode = mode;
    switch (mode) {
        case EditMode::Select:
            view->setDragMode(QGraphicsView::RubberBandDrag);
            view->setInteractive(true);  // 允许选择和移动item
            break;
        case EditMode::Insert:
            view->setDragMode(QGraphicsView::NoDrag);
            view->setInteractive(true);  // 不允许交互，以便在鼠标点击时插入新item
            break;
        case EditMode::ClockScheme:
            view->setDragMode(QGraphicsView::NoDrag);
            view->setInteractive(true);
            break;
        case EditMode::DragScene:
            view->setDragMode(QGraphicsView::ScrollHandDrag);
            view->setInteractive(false);  // 允许拖动场景，但不允许选择或移动item
            break;
    }
}
void MainWindow::slotCellItemInserted(QCADCellItem *cellItem){
    int idx = layerComboBox->currentIndex();
    addCellToScene(cellItem, idx);
    setDirty(true); 
    pushUndoSnapshot();

    // 获取 cellItem 的坐标
    QPointF pos = cellItem->pos();
    QString message = QString("Inserted item at Layer %1, Position (%2, %3)")
                      .arg(idx)
                      .arg(pos.x())
                      .arg(pos.y());

    // 在状态栏打印消息
    customStatusBar->addMessage(message);
}


void MainWindow::slotCellItemInserted(QCADCellItem* cellItem, int layerIndex){
    addCellToScene(cellItem, layerIndex);
    setDirty(true); //这行代码很重要，否则操作的界面无法保存
    if (!isBatchUpdating) {
        pushUndoSnapshot();
    }

}

void MainWindow::slotDeleteItem()
{
    QList<QGraphicsItem *> selectedItems = scene->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }
    QHash<int, QVector<int>> fastSelectionsByLayer;
    QList<QGraphicsItem *> regularSelections;

    for (QGraphicsItem *item : selectedItems)
    {
        const QVariant fastLayer = item->data(QCADScene::FastLayerRole);
        const QVariant fastIndex = item->data(QCADScene::FastIndexRole);
        if (fastLayer.isValid() && fastIndex.isValid()) {
            fastSelectionsByLayer[fastLayer.toInt()].push_back(fastIndex.toInt());
            continue;
        }
        regularSelections.push_back(item);
    }

    for (QGraphicsItem *item : regularSelections)
    {
        // 从 scene 中移除
        scene->removeItem(item);

        // 从 layers 中查找并移除
        for (int i = 0; i < layers.size(); ++i)
        {
            int index = layers[i].indexOf(item);
            if (index != -1)
            {
                layers[i].remove(index);  // 从该层移除
                break; // 找到后即可退出循环
            }
        }

        // 删除对象
        delete item;
    }

    for (auto it = fastSelectionsByLayer.begin(); it != fastSelectionsByLayer.end(); ++it)
    {
        auto &indices = it.value();
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int index : indices) {
            scene->removeFastCell(it.key(), index);
        }
    }
    setDirty(true);
    pushUndoSnapshot();
}

QVector<MainWindow::ClipboardCell> MainWindow::selectedCellsForClipboard() const
{
    QVector<ClipboardCell> selectedCells;
    if (scene == nullptr) {
        return selectedCells;
    }

    const QList<QGraphicsItem *> selectedItems = scene->selectedItems();
    selectedCells.reserve(selectedItems.size());
    for (QGraphicsItem *item : selectedItems) {
        if (item == nullptr || item->type() != QCADCellItem::Type) {
            continue;
        }
        const auto *cellItem = static_cast<const QCADCellItem *>(item);
        ClipboardCell copied;
        copied.cell.x = static_cast<int>(std::round(simon::x(*cellItem)));
        copied.cell.y = static_cast<int>(std::round(simon::y(*cellItem)));
        copied.cell.layer = qBound(0, static_cast<int>(std::round(item->zValue())), qMax(0, layers.size() - 1));
        copied.cell.phase = qBound(0, simon::timezone(*cellItem), 3);
        copied.cell.type = cellItem->getCellType();
        copied.cell.name = QString::fromStdString(simon::name(*cellItem));
        selectedCells.push_back(copied);
    }

    std::sort(selectedCells.begin(), selectedCells.end(), [](const ClipboardCell &lhs,
                                                             const ClipboardCell &rhs) {
        if (lhs.cell.layer != rhs.cell.layer) {
            return lhs.cell.layer < rhs.cell.layer;
        }
        if (lhs.cell.y != rhs.cell.y) {
            return lhs.cell.y < rhs.cell.y;
        }
        return lhs.cell.x < rhs.cell.x;
    });
    return selectedCells;
}

void MainWindow::slotCopyItems()
{
    clipboardCells = selectedCellsForClipboard();
    clipboardPasteCount = 0;
    if (clipboardCells.isEmpty()) {
        return;
    }

    clipboardAnchor = QPoint(clipboardCells.first().cell.x, clipboardCells.first().cell.y);
    for (const ClipboardCell &copied : clipboardCells) {
        clipboardAnchor.setX(qMin(clipboardAnchor.x(), copied.cell.x));
        clipboardAnchor.setY(qMin(clipboardAnchor.y(), copied.cell.y));
    }
    customStatusBar->addMessage(tr("Copied %1 cell(s)").arg(clipboardCells.size()));
}

void MainWindow::slotCutItems()
{
    slotCopyItems();
    if (!clipboardCells.isEmpty()) {
        slotDeleteItem();
    }
}

bool MainWindow::positionOccupied(int layer, int x, int y) const
{
    if (layer < 0 || layer >= layers.size()) {
        return false;
    }
    for (QGraphicsItem *item : layers[layer]) {
        if (item == nullptr) {
            continue;
        }
        if (static_cast<int>(std::round(item->x())) == x &&
            static_cast<int>(std::round(item->y())) == y) {
            return true;
        }
    }
    return false;
}

void MainWindow::ensureLayerExists(int layer)
{
    while (layer >= layers.size()) {
        const int nextLayer = layers.size();
        layerComboBox->AddItem(tr("New Layer %1").arg(nextLayer), true);
        layers.push_back(QVector<QGraphicsItem*>());
    }
}

void MainWindow::addCellToScene(QCADCellItem *cellItem, int layerIndex)
{
    if (cellItem == nullptr) {
        return;
    }
    ensureLayerExists(layerIndex);
    cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));
    cellItem->setZValue(layerIndex);
    cellItem->setVisible(true);
    layers[layerIndex].push_back(cellItem);
    scene->addItem(cellItem);
    if (cellItem->myCellType == CellType::InputCell) {
        inputname.append(cellItem->IOName);
        emit savedinputname(inputname);
    }
}

void MainWindow::slotPasteItems()
{
    if (clipboardCells.isEmpty()) {
        return;
    }

    ++clipboardPasteCount;
    const int baseOffset = GRID_SIZE * clipboardPasteCount;
    QVector<QGraphicsItem *> pastedItems;
    int extraOffset = 0;

    for (const ClipboardCell &copied : clipboardCells) {
        int targetLayer = copied.cell.layer;
        if (targetLayer < 0) {
            targetLayer = qMax(0, layerComboBox->currentIndex());
        }
        ensureLayerExists(targetLayer);

        int x = copied.cell.x + baseOffset + extraOffset;
        int y = copied.cell.y + baseOffset + extraOffset;
        while (positionOccupied(targetLayer, x, y)) {
            extraOffset += GRID_SIZE;
            x = copied.cell.x + baseOffset + extraOffset;
            y = copied.cell.y + baseOffset + extraOffset;
        }

        auto *cellItem = new QCADCellItem(x, y, targetLayer, copied.cell.phase,
                                          copied.cell.type, copied.cell.name);
        addCellToScene(cellItem, targetLayer);
        pastedItems.push_back(cellItem);
    }

    scene->clearSelection();
    for (QGraphicsItem *item : pastedItems) {
        item->setSelected(true);
    }
    setDirty(true);
    pushUndoSnapshot();
}

bool MainWindow::snapshotCellsEqual(const SnapshotCell &lhs, const SnapshotCell &rhs) const
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.layer == rhs.layer &&
           lhs.phase == rhs.phase && lhs.type == rhs.type && lhs.name == rhs.name;
}

bool MainWindow::clockRegionsEqual(const QCADScene::ClockRegionRecord &lhs,
                                   const QCADScene::ClockRegionRecord &rhs) const
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.phase == rhs.phase;
}

bool MainWindow::snapshotsEqual(const DesignSnapshot &lhs, const DesignSnapshot &rhs) const
{
    if (lhs.layerNames != rhs.layerNames ||
        lhs.cellsByLayer.size() != rhs.cellsByLayer.size() ||
        lhs.clockRegions.size() != rhs.clockRegions.size()) {
        return false;
    }
    for (int layer = 0; layer < lhs.cellsByLayer.size(); ++layer) {
        if (lhs.cellsByLayer[layer].size() != rhs.cellsByLayer[layer].size()) {
            return false;
        }
        for (int index = 0; index < lhs.cellsByLayer[layer].size(); ++index) {
            if (!snapshotCellsEqual(lhs.cellsByLayer[layer][index], rhs.cellsByLayer[layer][index])) {
                return false;
            }
        }
    }
    for (int index = 0; index < lhs.clockRegions.size(); ++index) {
        if (!clockRegionsEqual(lhs.clockRegions[index], rhs.clockRegions[index])) {
            return false;
        }
    }
    return true;
}

MainWindow::DesignSnapshot MainWindow::captureDesignSnapshot() const
{
    DesignSnapshot snapshot;
    snapshot.layerNames.reserve(layers.size());
    snapshot.cellsByLayer.resize(layers.size());

    for (int layer = 0; layer < layers.size(); ++layer) {
        const QStandardItem *layerItem = layerComboBox->GetItem(layer);
        snapshot.layerNames.push_back(layerItem != nullptr ? layerItem->text()
                                                           : tr("Layer %1").arg(layer));
        auto &snapshotLayer = snapshot.cellsByLayer[layer];
        snapshotLayer.reserve(layers[layer].size());
        for (QGraphicsItem *item : layers[layer]) {
            if (item == nullptr || item->type() != QCADCellItem::Type) {
                continue;
            }
            const auto *cellItem = static_cast<const QCADCellItem *>(item);
            SnapshotCell cell;
            cell.x = static_cast<int>(std::round(simon::x(*cellItem)));
            cell.y = static_cast<int>(std::round(simon::y(*cellItem)));
            cell.layer = layer;
            cell.phase = qBound(0, simon::timezone(*cellItem), 3);
            cell.type = cellItem->getCellType();
            cell.name = QString::fromStdString(simon::name(*cellItem));
            snapshotLayer.push_back(cell);
        }
        std::sort(snapshotLayer.begin(), snapshotLayer.end(), [](const SnapshotCell &lhs,
                                                                 const SnapshotCell &rhs) {
            if (lhs.y != rhs.y) {
                return lhs.y < rhs.y;
            }
            return lhs.x < rhs.x;
        });
    }

    snapshot.clockRegions = scene != nullptr ? scene->clockRegions()
                                             : QVector<QCADScene::ClockRegionRecord>();
    return snapshot;
}

void MainWindow::restoreDesignSnapshot(const DesignSnapshot &snapshot, bool markDirty)
{
    restoringSnapshot = true;
    scene->clearSelection();
    scene->clearFastRender();

    for (auto &layer : layers) {
        for (QGraphicsItem *item : layer) {
            if (item != nullptr) {
                scene->removeItem(item);
                delete item;
            }
        }
    }
    layers.clear();
    inputname.clear();

    {
        QSignalBlocker blocker(layerComboBox);
        while (layerComboBox->GetNumRows() > 0) {
            layerComboBox->RemoveItem(layerComboBox->GetNumRows() - 1);
        }
        for (int layer = 0; layer < snapshot.layerNames.size(); ++layer) {
            layerComboBox->AddItem(snapshot.layerNames[layer], true);
            layers.push_back(QVector<QGraphicsItem*>());
        }
    }

    if (layers.isEmpty()) {
        layerComboBox->AddItem(tr("Main Cell Layer"), true);
        layers.push_back(QVector<QGraphicsItem*>());
    }

    for (int layer = 0; layer < snapshot.cellsByLayer.size(); ++layer) {
        ensureLayerExists(layer);
        for (const SnapshotCell &cell : snapshot.cellsByLayer[layer]) {
            auto *cellItem = new QCADCellItem(cell.x, cell.y, layer, cell.phase, cell.type, cell.name);
            addCellToScene(cellItem, layer);
        }
    }

    scene->restoreClockRegions(snapshot.clockRegions);
    layerComboBox->setCurrentIndex(qBound(0, layerComboBox->currentIndex(), qMax(0, layerComboBox->GetNumRows() - 1)));
    scene->setCurrentLayerIndex(qMax(0, layerComboBox->currentIndex()));
    emit savedinputname(inputname);

    restoringSnapshot = false;
    if (markDirty) {
        setDirty(true);
    }
}

void MainWindow::updateUndoRedoActions()
{
    if (undoAction != nullptr) {
        undoAction->setEnabled(undoSnapshotIndex > 0);
    }
    if (redoAction != nullptr) {
        redoAction->setEnabled(undoSnapshotIndex >= 0 &&
                               undoSnapshotIndex < undoSnapshots.size() - 1);
    }
}

void MainWindow::resetUndoHistory()
{
    undoSnapshots.clear();
    undoSnapshotIndex = -1;
    pushUndoSnapshot();
}

void MainWindow::pushUndoSnapshot()
{
    if (restoringSnapshot || scene == nullptr || layerComboBox == nullptr) {
        return;
    }

    const DesignSnapshot snapshot = captureDesignSnapshot();
    if (undoSnapshotIndex >= 0 && undoSnapshotIndex < undoSnapshots.size() &&
        snapshotsEqual(undoSnapshots[undoSnapshotIndex], snapshot)) {
        updateUndoRedoActions();
        return;
    }

    while (undoSnapshots.size() > undoSnapshotIndex + 1) {
        undoSnapshots.removeLast();
    }
    undoSnapshots.push_back(snapshot);
    if (undoSnapshots.size() > 100) {
        undoSnapshots.removeFirst();
    }
    undoSnapshotIndex = undoSnapshots.size() - 1;
    updateUndoRedoActions();
}

void MainWindow::slotUndo()
{
    if (undoSnapshotIndex <= 0 || undoSnapshotIndex >= undoSnapshots.size()) {
        return;
    }
    --undoSnapshotIndex;
    restoreDesignSnapshot(undoSnapshots[undoSnapshotIndex], true);
    updateUndoRedoActions();
}

void MainWindow::slotRedo()
{
    if (undoSnapshotIndex < 0 || undoSnapshotIndex >= undoSnapshots.size() - 1) {
        return;
    }
    ++undoSnapshotIndex;
    restoreDesignSnapshot(undoSnapshots[undoSnapshotIndex], true);
    updateUndoRedoActions();
}

void MainWindow::checkCellInserted(QVector<QVector<QGraphicsItem*>> &_layers, QCADCellItem* cellItem, int cell_layer, int x_coord, int y_coord)
{
    if (cell_layer < 0 || cell_layer >= _layers.size()) {  
        delete cellItem;
        return;  
    } 

    if (isBatchUpdating) {
        if (cell_layer >= batchOccupiedByLayer.size()) {
            batchOccupiedByLayer.resize(cell_layer + 1);
        }
        const quint64 key = packSceneCoord(x_coord, y_coord);
        if (batchOccupiedByLayer[cell_layer].contains(key)) {
            delete cellItem;
            return;
        }
        batchOccupiedByLayer[cell_layer].insert(key);
        slotCellItemInserted(cellItem, cell_layer);
        return;
    }

    for (QGraphicsItem* item : layers[cell_layer]) {  
        // 检查坐标是否匹配  
        if (item->x() == x_coord && item->y() == y_coord) {  
            delete cellItem;
            return; // 找到存在的cell
        }  
    }
    slotCellItemInserted(cellItem, cell_layer);
}
