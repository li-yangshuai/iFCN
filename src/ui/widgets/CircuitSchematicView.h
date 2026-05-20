#ifndef CIRCUITSCHEMATICVIEW_H
#define CIRCUITSCHEMATICVIEW_H

#include <QGraphicsView>
#include <QHash>
#include <QPainterPath>
#include <QPoint>
#include <QString>
#include <QVector>

class CircuitSchematicNodeItem;
class QGraphicsScene;

class CircuitSchematicView : public QGraphicsView
{
    Q_OBJECT

public:
    struct NodeRecord {
        int index = -1;
        QString name;
        QString type;
        QPoint gridPos;
    };

    struct EdgeRecord {
        int source = -1;
        int sink = -1;
        QVector<QPoint> routePath;
    };

    struct ClockRecord {
        QPoint gridPos;
        int phase = -1;
    };

    explicit CircuitSchematicView(QWidget *parent = nullptr);

    void setCircuit(const QString &circuitName,
                    const QVector<NodeRecord> &nodes,
                    const QVector<EdgeRecord> &edges,
                    const QVector<ClockRecord> &clockGrid = {});
    void clearCircuit();
    void selectNode(int nodeIndex);
    void zoomIn();
    void zoomOut();
    void fitToCircuit();
    bool exportToSvg(const QString &filePath) const;
    bool exportToPdf(const QString &filePath) const;
    void setNodeLabelsVisible(bool visible);
    bool nodeLabelsVisible() const;
    void clearSelectionState();

signals:
    void nodeActivated(int nodeIndex);
    void edgeActivated(int sourceNodeIndex, int sinkNodeIndex);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void handleSelectionChanged();

private:
    void fitCircuit();
    void zoomBy(qreal factor);
    QPainterPath addEdge(CircuitSchematicNodeItem *sourceItem,
                         CircuitSchematicNodeItem *sinkItem,
                         const EdgeRecord &edge,
                         int sourceOrder,
                         int sourceCount,
                         int sinkOrder,
                         int sinkCount);

    QGraphicsScene *schematicScene = nullptr;
    QHash<int, CircuitSchematicNodeItem*> nodeItems;
    bool suppressSelectionSignal = false;
    bool userAdjustedZoom = false;
    bool showNodeLabels = true;
    int pressedNodeIndex = -1;
    int pressedEdgeSource = -1;
    int pressedEdgeSink = -1;
    qreal currentZoom = 1.0;
};

#endif // CIRCUITSCHEMATICVIEW_H
