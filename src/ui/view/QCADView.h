#ifndef HFUT_GUI_QCADVIEW_H
#define HFUT_GUI_QCADVIEW_H

#include<QGraphicsView>
#include<QWidget>
#include<QMouseEvent>
#include<QWheelEvent>
#include<QScrollBar>

class QPaintEvent;

class QCADView : public QGraphicsView
{
    Q_OBJECT

    public:
        explicit QCADView(QWidget *parent = 0);
        void setHighQualityMode(bool enabled);
        bool isHighQualityMode() const;
        void setEmptyStateVisible(bool visible);
        bool isEmptyStateVisible() const;

    public slots:
        //void slotZoom(int);

    protected:
        void mousePressEvent(QMouseEvent *event) override;
        // void mouseClickedEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void wheelEvent(QWheelEvent *event) override;
        void paintEvent(QPaintEvent *event) override;

    private:
        void ensurePanTargetVisible(const QPointF &targetCenter);

        QWidget *parentWindow;
        bool highQualityMode = false;
        bool emptyStateVisible = true;
        bool handPanning = false;
        QPoint lastPanPoint;
        //qreal zoom;

};

#endif 
