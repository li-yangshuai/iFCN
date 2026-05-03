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
    enum ClockPhaseType { Phase_None = -1, Phase_0 = 0, Phase_1, Phase_2, Phase_3};

    ClockPhaseType clockPhaseType() const{
        return myClockPhaseType;
    }

    int phase() const;
    void setPhase(int _phase);
    void setClockPhaseType(ClockPhaseType _clockPhaseType);

    QCADClockScheme(ClockPhaseType _clockPhaseType);
    QCADClockScheme(int _phase);

private:
    static ClockPhaseType phaseTypeFromInt(int _phase);

    ClockPhaseType myClockPhaseType;

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QRectF boundingRect() const override;
    // QPainterPath shape()const override;
    //记住，对item进行type赋值后，一定要重载该函数，否则还是父类的type值！！
    int type() const override {return Type;}


};


#endif
