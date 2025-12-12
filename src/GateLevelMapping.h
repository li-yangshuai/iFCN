#ifndef GATELEVELMAPPING_H
#define GATELEVELMAPPING_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QPair>
#include <QPoint>
#include <QVector>
#include <QHash>
#include "QCADCellItem.h"
#include <map>
#include <vector>
#include <utility>
#include <autopr/mapping.h>
using namespace fcngraph;
class MainWindow;  // 前向声明


inline uint qHash(const QPoint &key, uint seed = 0) noexcept
{
    return qHash((static_cast<quint64>(key.x()) << 32) ^ quint64(key.y()), seed);
}

class GateLevelMapping : public QObject
{
    Q_OBJECT

public:
    explicit GateLevelMapping(MainWindow *parent = nullptr);

    struct NodeInfo {
        int index;
        QString name;
        QString type;
        QPoint pos;
    };

    // ======= 数据存储 =======
    QString circuitName;                              // 电路名
    QMap<int, NodeInfo> nodes;                        // 节点信息
    QMap<QPair<int,int>, QVector<QPoint>> routes;     // 节点对 → 路径
    QHash<QPoint, int> coordPhaseMap;                 // 坐标 → 相位

public slots:
    void parseGateLevelMappingFile();                 // 打开并解析文件

        void mappingCellItem();
        void putClock();
        void putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<position ,int>& _pos_phase, QString _name = "");
        // void printCrossline();

signals:
    void mappingLoaded();                             // 解析完成信号

private:
    void parseNodeLine(const QString &line);
    void parsePathLine(const QString &line);
    void parsePhaseLine(const QString &line);
    MainWindow *mainWindow;
};



#endif // GATELEVELMAPPING_H