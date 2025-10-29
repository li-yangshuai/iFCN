#ifndef GATELEVELMAPPING_H
#define GATELEVELMAPPING_H

#include <QObject>
#include <QString>
#include <QPoint>
#include <QHash>
#include <QMap>
#include <QVector>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include "QCADCellItem.h"
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <autopr/mapping.h>
// #include "MainWindow.h"
// #include "config.h"
using namespace fcngraph;
class MainWindow;  // 前向声明

// ✅ 保守定义 QPoint 的哈希函数，兼容 Qt5/Qt6 所有版本
inline uint qHash(const QPoint &key, uint seed = 0) noexcept
{
    return qHash((static_cast<uint>(key.x()) << 16) ^ static_cast<uint>(key.y()), seed);
}

class GateLevelMapping : public QObject
{
    Q_OBJECT

public:
    explicit GateLevelMapping(QObject *parent = nullptr);

    // 打开文件选择对话框并解析 .ifcn 文件
    void parseGateLevelMappingFile();

    // 节点信息结构
    struct NodeInfo {
        int index;
        QString name;
        QString type;
        QPoint pos;
    };


    // 数据容器
    QString circuitName;                               // 电路名
    QMap<int, NodeInfo> nodes;                         // 节点信息
    QMap<QPair<int,int>, QVector<QPoint>> routes;      // 节点对路径
    QHash<QPoint, int> coordPhaseMap;                  // 坐标 -> 相位映射

    // 查询接口
    int getPhaseAtNode(int nodeIndex) const;           // 根据节点获取相位
    int getPhaseAtCoord(const QPoint &pt) const;       // 根据坐标获取相位

    void mappingCellItem();
    void putClock();
    void putCellItem(QPoint _cellpos, int _celllayer, CellType _cellType,  std::map<unsigned int ,int>& _pos_phase, QString _name = "");

private:
    // 辅助函数
    void parseNodeLine(const QString &line);
    void parsePathLine(const QString &line);
    void parsePhaseMapLine(const QString &line);

    // 状态枚举
    enum Section { NONE, NODE_INFO, PATH_INFO };
    Section section = NONE;
    bool inPhaseMapSection = false;
    MainWindow *mainWindow;
};

#endif // GATELEVELMAPPING_H


