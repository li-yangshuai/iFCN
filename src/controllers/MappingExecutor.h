#ifndef MAPPINGEXECUTOR_H
#define MAPPINGEXECUTOR_H

#include <map>
#include <vector>
#include <QString>
#include "autopr/algorithms/mapping.h"
#include "ui/items/QCADCellItem.h"
#include "ui/mainwindow/MainWindow.h"
#include "controllers/GateLevelMapping.h"

using namespace fcngraph;

class MappingExecutor
{
public:
    explicit MappingExecutor(GateLevelMapping* gatelevelmapping, MainWindow* window);

    void executeMapping();
    void putClock();
    void putCellItem(position _cellpos, int _celllayer, CellType _cellType,  
                     std::map<position ,int>& _pos_phase, QString _name = "");

private:
    GateLevelMapping* gatelevelmapping; 
    MainWindow* mainWindow;
    std::map<position, int> toPositionPhaseMap(const QHash<QPoint, int>& coordPhaseMap);
};

#endif // MAPPINGEXECUTOR_H
