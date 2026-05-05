#ifndef QCADSCENE_H
#define QCADSCENE_H

#include <QWidget>
#include <QGraphicsScene>
#include <QPainter>
#include "config.h"
#include <QSet>
#include <QHash>
#include "ui/items/QCADCellItem.h"
#include "ui/items/QCADClockScheme.h"
#include<QDebug>


class QCADScene : public QGraphicsScene
{

   Q_OBJECT
public:
    static constexpr int FastLayerRole = 0x1001;
    static constexpr int FastIndexRole = 0x1002;
    static constexpr int FastClockRole = 0x1003;

    struct FastCellRecord {
        int x = 0;
        int y = 0;
        int layer = 0;
        int phase = 0;
        CellType type = CellType::NormalCell;
        QString name;
    };

    struct FastClockRecord {
        int x = 0;
        int y = 0;
        int phase = 0;
    };

    struct ClockRegionRecord {
        int x = 0;
        int y = 0;
        int phase = 0;
    };

    QCADScene(QObject *parent = Q_NULLPTR): QGraphicsScene(parent),
                                            currentMode(EditMode::Select),
                                            myCellType(CellType::NormalCell),
                                            currentLayerIndex(0),
                                            currentClockIndex(0)
    {

    }

    void setEditMode(EditMode mode);

    void setItemType(CellType type);
    void setCurrentClockIndex(int _clockPhase);
    void setCurrentLayerIndex(int _layer);

    ~QCADScene();

    static QPointF caculateRealPostion(int _posx, int _posy);

    void placeClockScheme(const int _clock_scheme [4][4] );
    // int caculateCellatCsPhase(QPointF _cellPos);

    void clearPhaseRecord();
    void notifyClockRegionsChanged();
    void beginFastRenderBuild(int layerCount);
    void addFastCell(int x, int y, int layer, int phase, CellType type, const QString &name = QString());
    void addFastClock(int x, int y, int phase);
    void finalizeFastRenderBuild();
    void clearFastRender();
    bool hasFastRender() const;
    QRectF fastRenderBounds() const;
    const QVector<QVector<FastCellRecord>>& fastCellsByLayer() const;
    void setFastLayerVisible(int layer, bool visible);
    bool isFastLayerVisible(int layer) const;
    void updateFastCellPosition(int layer, int index, int x, int y);
    void updateFastCellPhase(int layer, int index, int phase);
    void updateFastCellName(int layer, int index, const QString &name);
    void removeFastCell(int layer, int index);
    void setHighQualityMode(bool enabled);
    bool isHighQualityMode() const;
    void setClockGridVisible(bool visible);
    bool isClockGridVisible() const;
    QVector<ClockRegionRecord> clockRegions() const;
    void restoreClockRegions(const QVector<ClockRegionRecord> &regions);

protected:
    void drawBackground(QPainter* painter, const QRectF &rect) override;
    void drawForeground(QPainter* painter, const QRectF &rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

private:
    static quint64 packTileKey(int x, int y);
    void rebuildInteractiveFastLayer();
    void clearInteractiveFastLayer();
    bool shouldUseInteractiveFastLayer() const;
    void rebuildFastClockOverlay();
    void clearFastClockOverlay();
    void drawFastClockRecords(QPainter *painter, const QRectF &rect);
    void drawFastCellRecords(QPainter *painter, const QRectF &rect);
    void drawFastCell(QPainter *painter, const FastCellRecord &cell, qreal lod);
    QColor colorForPhase(int phase) const;
    QPointF clockCenterForPosition(const QPointF &pos) const;
    QCADClockScheme* clockSchemeAt(const QPointF &pos) const;
    int phaseAtPosition(const QPointF &pos, int fallbackPhase) const;
    void insertOrUpdateClockScheme(const QPointF &pos);
    bool showClockPhaseMenu(const QPointF &pos, const QPoint &screenPos);
    void applyClockPhase(QCADClockScheme *clockItem, int phase);
    void deleteClockScheme(QCADClockScheme *clockItem);
    bool syncCellsWithClockScheme(QCADClockScheme *clockItem);
    bool setCellPhase(QCADCellItem *cellItem, int phase);
    int normalizedCellPhase(int phase) const;

    EditMode currentMode;
    CellType myCellType;

    int currentLayerIndex;
    int currentClockIndex;
    bool highQualityMode = false;
    bool clockGridVisible = true;
    bool fastRenderEnabled = false;
    QRectF fastBounds;
    QVector<QVector<FastCellRecord>> fastCellsPerLayer;
    QVector<bool> fastLayerVisible;
    QVector<QHash<quint64, QVector<int>>> fastCellTilesPerLayer;
    QVector<QSet<quint64>> fastCellOccupancyPerLayer;
    QVector<FastClockRecord> fastClocks;
    QHash<quint64, QVector<int>> fastClockTiles;
    QSet<quint64> fastClockOccupancy;
    QVector<QGraphicsItem*> fastClockOverlayItems;
    int interactiveFastLayerIndex = -1;
    QVector<QGraphicsItem*> interactiveFastLayerItems;


private:

signals:
    void cellItemInserted(QCADCellItem * item);
    void clockPhaseInserted(QCADClockScheme * item);
    void clockRegionsChanged();


};

#endif
