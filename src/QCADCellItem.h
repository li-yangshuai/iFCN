#ifndef HFUT_GUI_QCADCELLITEM_H
#define HFUT_GUI_QCADCELLITEM_H

#include <QGraphicsItem>
#include <QDataStream>
#include <simon/simon.hpp>
#include"config.h"
#include <QtWidgets>



using namespace simon;

class QCADCellItem : public QGraphicsItem, public QCACell 
{
    public:
        enum {Type = UserType + 1};

        CellType qcaCellType() const {
            return myCellType;
        }

        int type() const override {return Type;}


        QCADCellItem(CellType _qcaCellType);

        QCADCellItem();
        QCADCellItem( int mousePointX, int mousePointY, int layerIdx = 0, int clockIdx = 0, CellType _qcaCellType = CellType::NormalCell, QString _name = "default");
        QCADCellItem(const QCACell &cell);
        //QCADCellItem(const QCADCellItem &cellItem);

        QPixmap image(int _clockIdx);
        void drawNormalCell(QPainter* painter, QColor _color);
        void drawFixedCell(QPainter* painter, std::string _fixed);
        void drawHoleCell(QPainter* painter, QColor _color);
        void drawCrossoverCell(QPainter* painter, QColor _color);

        QRectF boundingRect() const override;
       
    protected:
        
        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

        void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
        
    public:
        CellType myCellType;
        // QGraphicsProxyWidget *proxyWidget;
        // QLabel *nameLabel;
        QString IOName;
        QGraphicsSimpleTextItem* nameLabel;
};

QDataStream &operator<<(QDataStream &out, const QCADCellItem &cellItem);
//QDataStream &operator>>(QDataStream &in, QCADCellItem &cellItem);


#endif //HFUT_GUI_QCADCELLITEM_H
