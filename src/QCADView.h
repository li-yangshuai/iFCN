#ifndef HFUT_GUI_QCADVIEW_H
#define HFUT_GUI_QCADVIEW_H

#include<QGraphicsView>
#include<QWidget>
#include<QMouseEvent>
#include<QWheelEvent>
#include<QScrollBar>

class QCADView : public QGraphicsView
{
    Q_OBJECT

    public:
        explicit QCADView(QWidget *parent = 0);

    public slots:
        //void slotZoom(int);

    protected:
        void mousePressEvent(QMouseEvent *event) override;
        // void mouseClickedEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void wheelEvent(QWheelEvent *event) override;

    private:
        QWidget *parentWindow;
        //qreal zoom;

};

#endif 
