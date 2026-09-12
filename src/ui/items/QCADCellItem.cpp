#include "QCADCellItem.h"
#include <QPainter>
#include <QRectF>
#include <QPen>
#include <QString>
#include <QDebug>
#include "ui/view/QCADScene.h"
#include "ui/mainwindow/MainWindow.h"

namespace {
constexpr qreal kCellLabelOffsetX = 12.0;
constexpr qreal kCellLabelOffsetY = -5.0;
constexpr int kCellLabelPointSize = 7;
constexpr qreal kCellLabelMinLod = 0.45;

void configureCellNameLabel(QGraphicsSimpleTextItem *label, const QString &text)
{
    if (label == nullptr) {
        return;
    }
    label->setText(text);
    label->setZValue(10);
    QFont font = label->font();
    font.setPointSize(kCellLabelPointSize);
    label->setFont(font);
    label->setBrush(QBrush(Qt::black));
    label->setFlag(QGraphicsItem::ItemIsMovable, true);
}
}

QDataStream &operator<<(QDataStream &out, const QCADCellItem &cellItem)
{
    out << QString("name:") << &simon::name(cellItem)
        << QString("x:") << simon::x(cellItem) 
        << QString("y:") << simon::y(cellItem) 
        << QString("width:") << simon::width(cellItem) 
        << QString("height:") << simon::height(cellItem) 
        << QString("polarization:") << simon::polarization(cellItem) 
        << QString("function:") << static_cast<qint32>(simon::function(cellItem)) 
        << QString("timezone:") << static_cast<qint32>(simon::timezone(cellItem)) 
        << QString("layer_index:") << static_cast<qint32>(simon::layer_index(cellItem))
        << QString("cellMode:") << &simon::cellMode(cellItem);

    foreach(auto &dot, simon::dots(cellItem))
    {
        out << QString("x:") << simon::x(dot) 
            << QString("y:") << simon::y(dot) 
            << QString("diameter:") << simon::diameter(dot) 
            << QString("charge:") << simon::charge(dot) 
            << QString("spin:") << simon::spin(dot) 
            << QString("potential:") << simon::potential(dot); 
    }
    return out;
}


QCADCellItem::QCADCellItem(CellType _qcaCellType)
{
    nameLabel = nullptr;
    // 初始化 dots
    simon::x(dots[0]) =  4.5; simon::y(dots[0]) = -4.5;
    simon::x(dots[1]) =  4.5; simon::y(dots[1]) =  4.5;
    simon::x(dots[2]) = -4.5; simon::y(dots[2]) =  4.5;
    simon::x(dots[3]) = -4.5; simon::y(dots[3]) = -4.5;

    // 设置逻辑类型
    switch (_qcaCellType) {
        case CellType::InputCell:
            simon::function(*this) = FCNCellFunction::INPUT;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            simon::name(*this) = ""; // 可后续更新
            break;
        case CellType::OutputCell:
            simon::function(*this) = FCNCellFunction::OUTPUT;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            simon::name(*this) = "";
            break;
        case CellType::FixedCell_0:
            simon::function(*this) = FCNCellFunction::FIXED;
            simon::name(*this) = "-1.00";
            break;
        case CellType::FixedCell_1:
            simon::function(*this) = FCNCellFunction::FIXED;
            simon::name(*this) = "1.00";
            break;
        case CellType::VerticalCell:
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::VERTICAL;
            break;
        case CellType::CrossoverCell:
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::CROSSOVER;
            break;
        default:
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
    }

    setFlags(ItemIsSelectable | ItemIsFocusable);
    setFlag(ItemSendsGeometryChanges);
    setFlag(ItemUsesExtendedStyleOption);
    setCacheMode(DeviceCoordinateCache);
    setAcceptHoverEvents(true);
}



QPixmap QCADCellItem::image(int _clockIdx) {
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QPen(Qt::black,1,Qt::SolidLine,Qt::SquareCap,Qt::MiterJoin));
    painter.translate(10, 10);
    QColor cellColor;
    switch(_clockIdx) {
        case 0 : cellColor = QColor(PARSE_0); break;
        case 1 : cellColor = QColor(PARSE_1); break;
        case 2 : cellColor = QColor(PARSE_2); break;
        case 3 : cellColor = QColor(PARSE_3); break;
        default: cellColor = QColor(0, 0, 0, 255); break;
    }
    switch (getCellType()) {
        case CellType::NormalCell : drawNormalCell(&painter, cellColor); break;
        case CellType::InputCell : drawNormalCell(&painter, INPUT_COLOR); break;
        case CellType::OutputCell : drawNormalCell(&painter, OUTPUT_COLOR); break;
        case CellType::FixedCell_0 : drawFixedCell(&painter, "-1.00"); break;
        case CellType::FixedCell_1 : drawFixedCell(&painter, "1.00"); break;
        case CellType::VerticalCell : drawHoleCell(&painter, cellColor); break;
        case CellType::CrossoverCell : drawCrossoverCell(&painter, cellColor); break;
        default: break;
    }
    return pixmap;
}


// QCADCellItem::QCADCellItem(){
//     simon::x(dots[0]) =  4.5;
//     simon::y(dots[0]) = -4.5;
//     simon::x(dots[1]) =  4.5;
//     simon::y(dots[1]) =  4.5;
//     simon::x(dots[2]) = -4.5;
//     simon::y(dots[2]) =  4.5;
//     simon::x(dots[3]) = -4.5;
//     simon::y(dots[3]) = -4.5;


//     setFlags(ItemIsSelectable | ItemIsFocusable);
//     setFlag(ItemSendsGeometryChanges);
//     setAcceptHoverEvents(true);
// }

CellType QCADCellItem::getCellType() const {
    auto func = simon::function(*this);
    auto mode = simon::cellMode(*this);

    if (func == FCNCellFunction::INPUT)
        return CellType::InputCell;
    else if (func == FCNCellFunction::OUTPUT)
        return CellType::OutputCell;
    else if (func == FCNCellFunction::FIXED) {
        if (simon::name(*this) == "-1.00")
            return CellType::FixedCell_0;
        else if (simon::name(*this) == "1.00")
            return CellType::FixedCell_1;
        else
            return CellType::FixedCell_0;
    } else if (mode == QCACellMode::CROSSOVER)
        return CellType::CrossoverCell;
    else if (mode == QCACellMode::VERTICAL)
        return CellType::VerticalCell;
    else
        return CellType::NormalCell;
}

QCADCellItem::QCADCellItem(int mousePointX, int mousePointY, int layerIdx /*= 0*/, 
    int clockIdx /*=0*/,CellType _qcaCellType, QString _name)
{
    nameLabel = nullptr;
    myCellType = _qcaCellType;
    simon::x(*this) = mousePointX;
    simon::y(*this) = mousePointY;
    simon::timezone(*this) = clockIdx; 
    simon::layer_index(*this) = layerIdx; 

    /***********初始化dots位置,相对于cell位置*************/
    simon::x(dots[0]) =  4.5;
    simon::y(dots[0]) = -4.5;
    simon::x(dots[1]) =  4.5;
    simon::y(dots[1]) =  4.5;
    simon::x(dots[2]) = -4.5;
    simon::y(dots[2]) =  4.5;
    simon::x(dots[3]) = -4.5;
    simon::y(dots[3]) = -4.5;

    switch (myCellType)
    {
        case CellType::NormalCell:
            simon::name(*this) = "";
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
        case CellType::InputCell:{
            IOName = _name;
            createNameLabel(_name);

            // 如果需要改变颜色
            // nameLabel->setBrush(QBrush(Qt::red));  // 设置文本颜色为红色

            simon::name(*this) = _name.toStdString();
            simon::function(*this) = FCNCellFunction::INPUT;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
        }
        case CellType::OutputCell:{
            IOName = _name;
            createNameLabel(_name);
            // 如果需要改变颜色
            // nameLabel->setBrush(QBrush(Qt::red));  // 设置文本颜色为红色
            simon::name(*this) = _name.toStdString();
            simon::function(*this) = FCNCellFunction::OUTPUT;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
        }
        case CellType::FixedCell_0:{
            simon::name(*this) = "-1.00";
            simon::function(*this) = FCNCellFunction::FIXED;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            createNameLabel(QStringLiteral("-1.00"));
            }
            break;
        case CellType::FixedCell_1:{
            simon::name(*this) = "1.00";
            simon::function(*this) = FCNCellFunction::FIXED;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            createNameLabel(QStringLiteral("1.00"));
        }
            break;
        case CellType::VerticalCell:
            simon::name(*this) = "";
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::VERTICAL;
            break;
        case CellType::CrossoverCell:
            simon::name(*this) = "";
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::CROSSOVER;
            break;
        default:
            simon::name(*this) = "";
            simon::function(*this) = FCNCellFunction::NORMAL;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
    }
    //setPos(simon::x(*this), simon::y(*this));     //在scene层添加
    setFlags(ItemIsSelectable | ItemIsFocusable);
    setFlag(ItemSendsGeometryChanges);
    setFlag(ItemUsesExtendedStyleOption);
    setCacheMode(DeviceCoordinateCache);

    setAcceptHoverEvents(true);
}

QCADCellItem::QCADCellItem(const QCACell &cell) 
{
    nameLabel = nullptr;
    simon::name(*this) = simon::name(cell);
    simon::x(*this) = simon::x(cell);
    simon::y(*this) = simon::y(cell);
    simon::width(*this) = simon::width(cell);
    simon::height(*this) = simon::height(cell);
    simon::polarization(*this) = simon::polarization(cell);
    simon::function(*this) = simon::function(cell);
    simon::timezone(*this) = simon::timezone(cell);
    simon::layer_index(*this) = simon::layer_index(cell);
    //simon::extrinsics(*this) = simon::extrinsics(cell);
    simon::cellMode(*this) = simon::cellMode(cell);


    simon::x(dots[0]) =  cell.dots[0].x - cell.x;
    simon::y(dots[0]) =  cell.dots[0].y - cell.y;
    simon::x(dots[1]) =  cell.dots[1].x - cell.x;
    simon::y(dots[1]) =  cell.dots[1].y - cell.y;
    simon::x(dots[2]) =  cell.dots[2].x - cell.x;
    simon::y(dots[2]) =  cell.dots[2].y - cell.y;
    simon::x(dots[3]) =  cell.dots[3].x - cell.x;
    simon::y(dots[3]) =  cell.dots[3].y - cell.y;

    for(int i = 0; i < 4; ++i)
    {
        simon::diameter(dots[i]) = simon::diameter(simon::dots(cell)[i]);
        simon::charge(dots[i]) = simon::charge(simon::dots(cell)[i]);
        simon::spin(dots[i]) = simon::spin(simon::dots(cell)[i]);
        simon::potential(dots[i]) = simon::potential(simon::dots(cell)[i]);
    }

    setFlags(ItemIsSelectable | ItemIsFocusable);
    setFlag(ItemSendsGeometryChanges);
    setFlag(ItemUsesExtendedStyleOption);
    setCacheMode(DeviceCoordinateCache);
    setAcceptHoverEvents(true);

    const QString copiedName = QString::fromStdString(simon::name(*this));
    if (!copiedName.isEmpty() &&
        (simon::function(*this) == FCNCellFunction::INPUT ||
         simon::function(*this) == FCNCellFunction::OUTPUT ||
         simon::function(*this) == FCNCellFunction::FIXED)) {
        createNameLabel(copiedName);
    }
}


QRectF QCADCellItem::boundingRect() const 
{
    // return QRectF(-width/2, -height/2, width, height);
    return QRectF(-10, -10, 20, 20);
}

void QCADCellItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);

    const qreal lod = QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
    const bool isFixed = simon::function(*this) == FCNCellFunction::FIXED;
    const bool isCrossover = simon::cellMode(*this) == QCACellMode::CROSSOVER;
    const bool isVertical = simon::cellMode(*this) == QCACellMode::VERTICAL;
    QColor color;
    switch(simon::function(*this)) {
        case FCNCellFunction::INPUT : {
                color = QColor(INPUT_COLOR);
            }
            break;
        case FCNCellFunction::OUTPUT : 
            color = QColor(OUTPUT_COLOR);
            break;
        case FCNCellFunction::FIXED :
            color = QColor(Qt::black);
            break;
        default :

            switch(simon::timezone(*this)) {
                case 0 :
                    color = QColor(PARSE_0);
                    break;
                case 1 :
                    color = QColor(PARSE_1);
                    break;
                case 2 :
                    color = QColor(PARSE_2);
                    break;
                case 3 :
                    color = QColor(PARSE_3);
                    break;
                default:
                    color = QColor(PARSE_0);
                    break;
            }
            break;
    }
    if (option->state & QStyle::State_Selected){
        color = Qt::red;
    }

    if (nameLabel != nullptr) {
        configureCellNameLabel(nameLabel, nameLabel->text());
        const bool showLabel = lod >= kCellLabelMinLod;
        if (nameLabel->isVisible() != showLabel) {
            nameLabel->setVisible(showLabel);
        }
    }

    if (lod < 0.45) {
        painter->setBrush(color);
        painter->drawRect(-width / 2, -height / 2, width, height);
        return;
    }

    if (lod < 0.8) {
        painter->setBrush(color);
        painter->drawRect(-width / 2, -height / 2, width, height);
        if (isCrossover) {
            painter->drawLine(QPointF(-width / 2, -height / 2), QPointF(width / 2, height / 2));
            painter->drawLine(QPointF(-width / 2, height / 2), QPointF(width / 2, -height / 2));
        } else if (isVertical) {
            painter->drawEllipse(QPointF(0, 0), width / 2, height / 2);
        }
        return;
    }

    if (isFixed) {
        drawFixedCell(painter, simon::QCACell(*this).name);
        return;
    }
    if (isCrossover) {
        drawCrossoverCell(painter, color);
        return;
    }
    if (isVertical) {
        drawHoleCell(painter, color);
        return;
    }
    drawNormalCell(painter, color);
}

void QCADCellItem::drawNormalCell(QPainter* painter, QColor _color){
    double dot_radius = simon::diameter(dots[0])/2;
    painter->setBrush(QColor(_color));
    painter->drawRect(-width/2, -height/2, width, height);
    painter->drawEllipse(QPointF(simon::x(dots[0]), simon::y(dots[0])), dot_radius, dot_radius);
    painter->drawEllipse(QPointF(simon::x(dots[1]), simon::y(dots[1])), dot_radius, dot_radius);
    painter->drawEllipse(QPointF(simon::x(dots[2]), simon::y(dots[2])), dot_radius, dot_radius);
    painter->drawEllipse(QPointF(simon::x(dots[3]), simon::y(dots[3])), dot_radius, dot_radius);
}

void QCADCellItem::drawFixedCell(QPainter* painter, std::string _fixed){
    double dot_radius = simon::diameter(dots[0])/2;
    painter->setBrush(QBrush(QColor(Qt::black)));
    painter->drawRect(-width/2, -height/2, width, height);
    painter->setBrush(QBrush(QColor(Qt::white)));

    if(_fixed == "-1.00"){
        painter->drawEllipse(QPointF(simon::x(dots[1]), simon::y(dots[1])), dot_radius, dot_radius);
        painter->drawEllipse(QPointF(simon::x(dots[3]), simon::y(dots[3])), dot_radius, dot_radius);
    }else if(_fixed == "1.00"){
        painter->drawEllipse(QPointF(simon::x(dots[0]), simon::y(dots[0])), dot_radius, dot_radius);
        painter->drawEllipse(QPointF(simon::x(dots[2]), simon::y(dots[2])), dot_radius, dot_radius);
    }else{
        return;
    }
}

void QCADCellItem::drawHoleCell(QPainter* painter, QColor _color){
    painter->setBrush(QBrush(QColor(_color)));
    painter->drawRect(-width/2, -height/2, width, height);
    painter->drawEllipse(QPointF(0, 0) , width/2, height/2);
}

void QCADCellItem::drawCrossoverCell(QPainter* painter, QColor _color){
    painter->setBrush(QBrush(_color));
    painter->drawRect(-width/2, -height/2, width, height);
    painter->drawLine(QPointF(simon::x(dots[0]), simon::y(dots[0])), QPointF(simon::x(dots[2]), simon::y(dots[2])));
    painter->drawLine(QPointF(simon::x(dots[1]), simon::y(dots[1])), QPointF(simon::x(dots[3]), simon::y(dots[3])));
}

void QCADCellItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {

    QGraphicsItem::mousePressEvent(event);
}


void QCADCellItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)  {

    QGraphicsItem::mouseReleaseEvent(event);
}

void QCADCellItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    // qDebug() << "QCADCellItem::mouseDoubleClickEvent";

    if (getCellType() == CellType::InputCell || getCellType() == CellType::OutputCell) {
        QString oldName = QString::fromStdString(simon::name(*this));
        QWidget *parentWindow = nullptr;
        if (scene() != nullptr) {
            const QList<QGraphicsView*> views = scene()->views();
            if (!views.isEmpty()) {
                parentWindow = views.first()->window();
            }
        }
        QString newName = QInputDialog::getText(parentWindow, "Edit Cell Name", "Enter new name:", QLineEdit::Normal, oldName);

        if (!newName.isEmpty() && newName != oldName) {
            createNameLabel(newName);
            simon::name(*this) = newName.toStdString();
            if (QCADScene *qcadScene = qobject_cast<QCADScene *>(scene())) {
                const QVariant fastLayer = data(QCADScene::FastLayerRole);
                const QVariant fastIndex = data(QCADScene::FastIndexRole);
                if (fastLayer.isValid() && fastIndex.isValid()) {
                    qcadScene->updateFastCellName(fastLayer.toInt(), fastIndex.toInt(), newName);
                }
            }

            if (scene()) {
                const QList<QGraphicsView*> views = scene()->views();
                if (!views.isEmpty()) {
                    QWidget* viewWidget = views.first()->window();
                    MainWindow* mainWin = qobject_cast<MainWindow*>(viewWidget);
                    if (mainWin) {
                        mainWin->setDirty(true);
                    }
                }
            }
        }
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
}


QVariant QCADCellItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    return QGraphicsItem::itemChange(change, value);
}

void QCADCellItem::createNameLabel(const QString &name) {
    if (name.isEmpty()) {
        if (nameLabel != nullptr) {
            nameLabel->setVisible(false);
        }
        return;
    }
    if (nameLabel == nullptr) {
        nameLabel = new QGraphicsSimpleTextItem(name, this);
        nameLabel->setPos(kCellLabelOffsetX, kCellLabelOffsetY);
    }
    configureCellNameLabel(nameLabel, name);
    nameLabel->setVisible(true);
}
