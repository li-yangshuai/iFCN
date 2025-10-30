#ifndef MAPPINGEXECUTOR_H
#define MAPPINGEXECUTOR_H

#include <map>
#include <vector>
#include <QString>
#include "autopr/mapping.h"
#include "QCADCellItem.h"
#include "MainWindow.h"
#include "GateLevelMapping.h"

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
};

#endif // MAPPINGEXECUTOR_H