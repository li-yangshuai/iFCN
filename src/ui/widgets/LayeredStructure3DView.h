#ifndef LAYEREDSTRUCTURE3DVIEW_H
#define LAYEREDSTRUCTURE3DVIEW_H

#include "config.h"

#include <QGraphicsView>
#include <QVector>

class QGraphicsScene;

class LayeredStructure3DView : public QGraphicsView
{
    Q_OBJECT

public:
    struct ClockRegionRecord {
        int x = 0;
        int y = 0;
        int phase = 0;
    };

    struct CellRecord {
        int x = 0;
        int y = 0;
        int layer = 0;
        int phase = 0;
        CellType type = CellType::NormalCell;
        QString name;
    };

    struct EncodedTileRecord {
        unsigned int tileX = 0;
        unsigned int tileY = 0;
        int startGridX = 0;
        int startGridY = 0;
        int blockSize = 4;
        QString hex;
        QString matrixText;
    };

    explicit LayeredStructure3DView(QWidget *parent = nullptr);

    void setStructure(int phaseCount,
                      const QVector<ClockRegionRecord> &clockRegions,
                      const QVector<QVector<CellRecord>> &cellsByLayer,
                      const QVector<EncodedTileRecord> &encodedTiles);
    bool exportToSvg(const QString &fileName);
    bool exportToPdf(const QString &fileName);

public slots:
    void fitToStructure();
    void zoomIn();
    void zoomOut();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QRectF sourceBounds() const;
    QPointF project(qreal x, qreal y, qreal z) const;
    QPolygonF isoRect(const QRectF &rect, qreal z) const;
    void addPlate(const QRectF &rect,
                  qreal z,
                  qreal thickness,
                  const QColor &fill,
                  const QColor &edge,
                  qreal opacity = 1.0);
    void addLabel(const QString &text,
                  const QPointF &pos,
                  const QColor &color,
                  int pointSize = 9,
                  bool bold = false);
    void addLeaderLabel(const QString &text,
                        const QPointF &anchor,
                        const QPointF &labelPos,
                        const QColor &color,
                        int pointSize = 11,
                        bool bold = true);
    void addClockGenerator(const QRectF &bounds);
    void addClockPattern(qreal z);
    void addEncodedPattern(qreal z);
    void addCellLayer(int layerIndex, qreal z);
    void addCellBridge(const CellRecord &cell, qreal z);
    void rebuildScene();
    void zoomBy(qreal factor);
    QRectF encodedTileRect(const EncodedTileRecord &tile) const;

    QColor clockPhaseColor(int phase) const;
    QColor phaseColor(int phase) const;
    QColor cellColor(const CellRecord &cell) const;
    QString cellTypeLabel(CellType type) const;

    QGraphicsScene *structureScene = nullptr;
    int activePhaseCount = 4;
    QVector<ClockRegionRecord> clockRegionRecords;
    QVector<QVector<CellRecord>> layerCells;
    QVector<EncodedTileRecord> encodedTileRecords;
    QRectF cachedBounds;
    qreal currentZoom = 1.0;
    bool userAdjustedZoom = false;
};

#endif // LAYEREDSTRUCTURE3DVIEW_H
