#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include "qcustomplot.h"

class PlotWidget : public QWidget {
     Q_OBJECT
     
public:
    PlotWidget(const QString &labelText, const QColor &lineColor, QVector<double> vecX, QVector<double> vecY, double yMin, double yMax);
    
    QLabel *label;
    QCustomPlot *customPlot;
    QCPItemRect *selectionRect;
    QCPItemTracer *tracer;
protected:
    int calculateY(int x);

signals:
    void customPlot_changed(double lower, double upper);
    void tracer_changed(double x);
private slots:
    void onMousePress(QMouseEvent *event);
    void onMouseMove(QMouseEvent *event);
    void onMouseRelease(QMouseEvent *event);
    void axischanged();

private:
    bool m_isSelecting = false;
    QPoint m_selectionStart;
    QPoint m_selectionEnd;
    double yMinp;
    double yMaxp;


};
