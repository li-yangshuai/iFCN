#ifndef QCADCLOCKSCHEME_H
#define QCADCLOCKSCHEME_H

#include<QGraphicsRectItem>
#include"config.h"
#include<QPainter>
#include<QtWidgets>

class QCADClockScheme : public QGraphicsItem
{
public:
    enum {Type = UserType + 30};
    enum ClockPhaseType { Phase_0, Phase_1, Phase_2, Phase_3};

    ClockPhaseType clockPhaseType() const{
        return myClockPhaseType;
    }

    QCADClockScheme(ClockPhaseType _clockPhaseType);

    QCADClockScheme(int _phase){
        switch(_phase){
            case 0:
                myClockPhaseType = Phase_0;
                break;
            case 1:
                myClockPhaseType = Phase_1;
                break;
            case 2:
                myClockPhaseType = Phase_2;
                break;
            case 3:
                myClockPhaseType = Phase_3;
                break;
        }
        setFlags(ItemIsFocusable);
    }

private:
    ClockPhaseType myClockPhaseType;

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QRectF boundingRect() const override;
    // QPainterPath shape()const override;
    //记住，对item进行type赋值后，一定要重载该函数，否则还是父类的type值！！
    int type() const override {return Type;}


};




#endif 