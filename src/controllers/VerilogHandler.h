#ifndef VERILOGHANDLER_H
#define VERILOGHANDLER_H

#include <QObject>
#include <QString>
#include <map>
#include <autopr/graph/parse.h>
#include <autopr/algorithms/genetic.h>
#include <autopr/grid/grid.h>
#include <autopr/algorithms/astar.h>
#include <autopr/algorithms/mapping.h>
#include <autopr/graph/circuitGraph.h>
#include <QSvgGenerator>
#include <QPainter>
#include "ui/items/QCADCellItem.h"

using namespace fcngraph;
class MainWindow;  // 前向声明

class VerilogHandler : public QObject
{
    Q_OBJECT

public:
    explicit VerilogHandler(MainWindow *parent = nullptr);
    
    // 公开接口
    void handleParseVerilogFile();
    void handleGraphRender();  // 新增的函数
    void handleGcnRlLayout();
    void generateSVG();
    void slotForceOrientedAlgorithm();

signals:
    void operationStarted(const QString &title, const QString &detail);
    void operationProgress(const QString &detail, int value, int maximum);
    void operationFinished(const QString &message);
    void operationFailed(const QString &message);

private:
    MainWindow *mainWindow;

    bool isOptimizeNOTNode = false;
    int optimizeNOTNode_time = 1;

    void mappingCellItem(std::map<unsigned int, position>& _node_pos, 
                                    std::map<std::pair<unsigned int, unsigned int>, 
                                    std::vector<position>>& _nodepair_route, 
                                    Parse _parse, std::map<position, int>& _pos_phase);

    position coordtrans(const position& pos, unsigned int scale);

    void putClock(std::map<position, int>& pos_phase);

    void putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<position, int>& _pos_phase, QString _name = "");
    void saveGraphRenderIfcn(const QString &sourceFilePath,
                             Parse &parse,
                             const std::map<unsigned int, position> &nodePositions,
                             const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes,
                             const std::map<position, int> &posPhase,
                             int phaseCount,
                             int gateNum,
                             int inputNum,
                             int outputNum,
                             int wireNum,
                             int width,
                             int height,
                             double elapsedSeconds);
    void saveGraphRenderLatex(const QString &sourceFilePath,
                              Parse &parse,
                              const std::map<unsigned int, position> &nodePositions,
                              const std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> &routes,
                              const std::map<position, int> &posPhase,
                              int phaseCount,
                              int width,
                              int height);


};

#endif // VERILOGHANDLER_H
