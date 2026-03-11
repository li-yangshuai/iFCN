#include "ui/mainwindow/MainWindow.h"
#include <QGridLayout>
#include <QtGlobal>

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
            button->setChecked(false);
    }
    // reset
    QString text = button->text();
    if(text == tr("Clean")){
        scene->clearPhaseRecord();
    }else if(text == tr("Custom")){
        //待设计
    }else if(text == tr("ONE-D")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(ONEDIMEN_CLOKC_SCHEME);
    }else if(text == tr("2DDwave")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(TDDWAVE_CLOCK_SCHEME);
    }else if(text == tr("USE")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(USE_CLOKC_SCHEME);
    }else if(text == tr("RES")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(RES_CLOKC_SCHEME);
    }
    update();
    // setDirty(true);
}


QWidget* MainWindow::createCellWidget(const QString &text, CellType type){
    int clockIdx = this->clockComboBox->currentIndex();
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
    scene->setCurrentClockIndex(idx);
    QList<QGraphicsItem *> items = scene->selectedItems();

    if(items.isEmpty()) 
        return;

    for(QGraphicsItem *item: items)
    {
        simon::timezone(*static_cast<QCADCellItem *>(item)) = idx; //时钟域，由控制面板传递
        //添加更新操作
    }
}

void MainWindow::slotLayerActiveChanged(int idx){
    scene->setCurrentLayerIndex(idx);
}

void MainWindow::slotViewShowGrid(bool on)
{
    checkBox->setChecked(on);
    scene->setGridVisible(on);
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
        case EditMode::DragScene:
            view->setDragMode(QGraphicsView::ScrollHandDrag);
            view->setInteractive(false);  // 允许拖动场景，但不允许选择或移动item
            break;
    }
}
void MainWindow::slotCellItemInserted(QCADCellItem *cellItem){
    if(cellItem->myCellType == CellType::InputCell)
    {
        this->inputname.append(cellItem->IOName);
    }
    emit savedinputname(inputname);//for 输入lable可选择仿真
    int idx = layerComboBox->currentIndex();
    layers[idx].push_back(cellItem);
    cellItem->setZValue(idx);
    cellItem->setVisible(true);
    scene->addItem(cellItem);
    setDirty(true); 

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
    if(cellItem->myCellType == CellType::InputCell)
    {
        this->inputname.append(cellItem->IOName);
    }
    emit savedinputname(inputname);//for 输入lable可选择仿真
    cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加
    layers[layerIndex].push_back(cellItem);
    cellItem->setZValue(layerIndex);
    cellItem->setVisible(true);
    scene->addItem(cellItem);
    setDirty(true); //这行代码很重要，否则操作的界面无法保存

}

void MainWindow::slotDeleteItem()
{
    QList<QGraphicsItem *> selectedItems = scene->selectedItems();

    for (QGraphicsItem *item : selectedItems)
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
    setDirty(true);
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
