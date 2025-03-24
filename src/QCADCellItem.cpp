
#include "QCADCellItem.h"
#include <QPainter>
#include <QRectF>
#include <QPen>
#include <QString>
#include <QDebug>
#include"QCADScene.h"

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
    simon::x(dots[0]) =  4.5;
    simon::y(dots[0]) = -4.5;
    simon::x(dots[1]) =  4.5;
    simon::y(dots[1]) =  4.5;
    simon::x(dots[2]) = -4.5;
    simon::y(dots[2]) =  4.5;
    simon::x(dots[3]) = -4.5;
    simon::y(dots[3]) = -4.5;
    myCellType = _qcaCellType;
    setFlags(ItemIsSelectable | ItemIsFocusable); 
    setAcceptHoverEvents(true);

}


QPixmap QCADCellItem::image(int _clockIdx) {
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter *painter =  new QPainter(&pixmap);
    painter->setPen(QPen(Qt::black,1,Qt::SolidLine,Qt::SquareCap,Qt::MiterJoin));
    painter->translate(10, 10);
    QColor cellColor;
    switch(_clockIdx) {
        case 0 :
            cellColor = QColor(PARSE_0);
            break;
        case 1 :
            cellColor = QColor(PARSE_1);
            break;
        case 2 :
            cellColor = QColor(PARSE_2);
            break;
        case 3 :
            cellColor = QColor(PARSE_3);
            break;
        default:
            cellColor = QColor(0, 0, 0, 255);
            break;
    }

    switch (myCellType)
    {
        case CellType::NormalCell :
            drawNormalCell(painter, cellColor);
            break;
        case CellType::InputCell :
            drawNormalCell(painter, INPUT_COLOR);
            break;
        case CellType::OutputCell :
            drawNormalCell(painter, OUTPUT_COLOR);
            break;
        case CellType::FixedCell_0 :
            drawFixedCell(painter, "-1.00");
            break;
        case CellType::FixedCell_1:
            drawFixedCell(painter, "1.00");
            break;
        case CellType::VerticalCell :
            drawHoleCell(painter, cellColor);
            break;
        case CellType::CrossoverCell :
            drawCrossoverCell(painter, cellColor);
            break;
        default:
            break;
    }


    return pixmap;
}


QCADCellItem::QCADCellItem()
{
    simon::x(dots[0]) =  4.5;
    simon::y(dots[0]) = -4.5;
    simon::x(dots[1]) =  4.5;
    simon::y(dots[1]) =  4.5;
    simon::x(dots[2]) = -4.5;
    simon::y(dots[2]) =  4.5;
    simon::x(dots[3]) = -4.5;
    simon::y(dots[3]) = -4.5;


    setFlags(ItemIsSelectable | ItemIsFocusable); 
    setAcceptHoverEvents(true);
}

QCADCellItem::QCADCellItem(int mousePointX, int mousePointY, int layerIdx /*= 0*/, int clockIdx /*=0*/,CellType _qcaCellType, QString _name)
{
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
            nameLabel = new QGraphicsSimpleTextItem(_name, this);
            
            nameLabel->setPos(-7.5, 3);
            nameLabel->setZValue(10);  // 设置 Z 值，使其位于其他项之上
            QFont font = nameLabel->font();
            font.setPointSize(15);  // 设置字体大小为10，可以根据需要调整
            nameLabel->setFont(font);
            nameLabel->setFlag(QGraphicsItem::ItemIsMovable);

            // 如果需要改变颜色
            // nameLabel->setBrush(QBrush(Qt::red));  // 设置文本颜色为红色

            simon::name(*this) = _name.toStdString();
            simon::function(*this) = FCNCellFunction::INPUT;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
        }
        case CellType::OutputCell:{
            IOName = _name;
            nameLabel = new QGraphicsSimpleTextItem(_name, this);
            nameLabel->setPos(-7.5, 3);
            nameLabel->setZValue(10);  // 设置 Z 值，使其位于其他项之上
            QFont font = nameLabel->font();
            font.setPointSize(15);  // 设置字体大小为10，可以根据需要调整
            nameLabel->setFont(font);
            nameLabel->setFlag(QGraphicsItem::ItemIsMovable);
            // 如果需要改变颜色
            // nameLabel->setBrush(QBrush(Qt::red));  // 设置文本颜色为红色
            simon::name(*this) = _name.toStdString();
            simon::function(*this) = FCNCellFunction::OUTPUT;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
        }
        case CellType::FixedCell_0:
            simon::name(*this) = "-1.00";
            simon::function(*this) = FCNCellFunction::FIXED;
            simon::cellMode(*this) = QCACellMode::NORMAL;
            break;
        case CellType::FixedCell_1:
            simon::name(*this) = "1.00";
            simon::function(*this) = FCNCellFunction::FIXED;
            simon::cellMode(*this) = QCACellMode::NORMAL;
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
    setFlags(ItemIsSelectable | ItemIsFocusable ); 
    setAcceptHoverEvents(true);
}

QCADCellItem::QCADCellItem(const QCACell &cell) 
{
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

    setFlags(ItemIsSelectable | ItemIsFocusable ); 
    setAcceptHoverEvents(true);
}


QRectF QCADCellItem::boundingRect() const 
{
    // return QRectF(-width/2, -height/2, width, height);
    return QRectF(-10, -10, 20, 20);
}

void QCADCellItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);
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
            drawFixedCell(painter, simon::QCACell(*this).name);
            return;
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

            switch (simon::cellMode(*this)){
                case QCACellMode::CROSSOVER:
                    drawCrossoverCell(painter, color);
                    return;
                    break;
                case QCACellMode::VERTICAL:
                    drawHoleCell(painter, color);
                    return;
                    break;
                default:

                    break;
                }
            break;
    }
    if (option->state & QStyle::State_Selected){
        color = Qt::red;
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
    // 弹出输入框获取新文本

    if (myCellType == CellType::InputCell || myCellType == CellType::OutputCell) {
        QString newName = QInputDialog::getText(nullptr, "Edit Cell Name", 
                                                "Enter new name:", QLineEdit::Normal);
        if(!newName.isEmpty() && nameLabel != nullptr){
            nameLabel->setText(newName);
        }
    }

    QGraphicsItem::mouseDoubleClickEvent(event);
}
