#ifndef VERILOGHANDLER_H
#define VERILOGHANDLER_H

#include <QObject>
#include <QString>
#include <map>
#include <autopr/parse.h>
#include <autopr/genetic.h>
#include <autopr/mortonGrid.h>
#include <autopr/astar.h>
#include <autopr/mapping.h>
#include <autopr/circuitGraph.h>
#include <QSvgGenerator>
#include <QPainter>
#include "QCADCellItem.h"

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
    void generateSVG();

private:
    MainWindow *mainWindow;

    bool isOptimizeNOTNode = false;
    int optimizeNOTNode_time = 1;

    void mappingCellItem(std::map<unsigned int, unsigned int>& _node_morton, 
                                    std::map<std::pair<unsigned int, unsigned int>, 
                                    std::vector<unsigned int>>& _nodepair_route, 
                                    Parse _parse, std::map<unsigned int ,int>& _morton_phase);

    unsigned int coordtrans(unsigned int morton, unsigned int scale);

    void putClock(std::map<unsigned int ,int>& morton_phase);

    void putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<unsigned int ,int>& _morton_phase, QString _name = "");


};

#endif // VERILOGHANDLER_H
