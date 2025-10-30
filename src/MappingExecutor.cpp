#include "MappingExecutor.h"
#include <QDebug>
#include <algorithm>
#include <QMessageBox>

MappingExecutor::MappingExecutor(GateLevelMapping* m, MainWindow* w)
    : gatelevelmapping(m), mainWindow(w) {}

void MappingExecutor::executeMapping()
{
    // ✅ 直接访问 GateLevelMapping 的 public 成员
    QString circuitName = gatelevelmapping->circuitName;
    auto& nodes = gatelevelmapping->nodes;
    auto& routes = gatelevelmapping->routes;
    auto& coordPhaseMap = gatelevelmapping->coordPhaseMap;

    Mapping mapping;

    //QMap transform to std::map (coordPhaseMap -> positionPhaseMap)
    std::map<position, int> positionPhaseMap;
    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint &p = it.key();
        int phase = it.value();

        positionPhaseMap[{static_cast<unsigned int>(p.x()), 
                        static_cast<unsigned int>(p.y())}] = phase;
    }

    //get circle_line from routes(containing all paths)
    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    for (const QVector<QPoint>& qPoints : routes)  // 直接拿 value
    {
        std::vector<position> convertedRoute;
        convertedRoute.reserve(qPoints.size());

        for (const QPoint& point : qPoints)
            convertedRoute.emplace_back(point.x(), point.y());

        circle_line.push_back(std::move(convertedRoute));
    }


    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node_position,type), (fan_in_position，fan_out_position)>
    Nodelink.clear();

    //初始化Nodelink映射,确保每个节点位置和类型都有一个对应的输入输出位置列表
    for (auto it = routes.begin(); it != routes.end(); ++it)
    {

        const QPair<int,int>& key = it.key();
        const QVector<QPoint>& templine = it.value();
        if (!nodes.contains(key.first) || !nodes.contains(key.second)) continue;
        if (templine.isEmpty()) continue;

        position startpos{templine.front().x(), templine.front().y()};
        position endpos{templine.back().x(), templine.back().y()};

        QString startnodetype = nodes[key.first].type;
        QString endnodetype   = nodes[key.second].type;

        Nodelink[{startpos, startnodetype.toStdString()}] = {{}, {}};
        Nodelink[{endpos, endnodetype.toStdString()}]   = {{}, {}};
    }
/*
    for (auto &pair : Nodelink)
    {
        
        for (auto &line : circle_line)
        {

            if (pair.first.first == line.front())
            {
                std::vector<position> &output = pair.second.second;
                output.push_back(*std::next(line.begin()));
            }
            else if (pair.first.first == line.back())
            {
                std::vector<position> &intput = pair.second.first;
                intput.push_back(*std::prev(std::prev(line.end())));
            }
        }
        //避免重复放置输入输出
        if(pair.second.first.size() > 1)
        {
            std::sort(pair.second.first.begin(), pair.second.first.end());
            auto unique_end = std::unique(pair.second.first.begin(), pair.second.first.end());
            pair.second.first.erase(unique_end, pair.second.first.end());
        }
        if(pair.second.second.size() > 1)
        {
            std::sort(pair.second.second.begin(), pair.second.second.end());
            auto unique_end = std::unique(pair.second.second.begin(), pair.second.second.end());
            pair.second.second.erase(unique_end, pair.second.second.end());
        }
    }
*/
    //确保所有节点（nodes容器中的每个节点）都在 Nodelink 这个映射中占一个位置，即使该节点没有任何连线（没有出线或入线的孤立节点）
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        position nodepos{it.value().pos.x(), it.value().pos.y()};
        Nodelink[{nodepos, it.value().type.toStdString()}] = {{}, {}};
    }

    //遍历所有路径，填充Nodelink的输入输出位置列表，可能多扇入或多扇出
    for (auto &entry : Nodelink)
    {
        const position &nodePos = entry.first.first;

        for (const auto &line : circle_line)
        {
            const size_t len = line.size();
            if (len < 2) continue;  //必须至少两个点，否则非法(起点和终点)

            const position &start = line.front();
            const position &end   = line.back();

            //如果是输出节点（路径起点）
            if (nodePos == start)
            {
                entry.second.second.push_back(line[1]);  //第二个点安全访问（fan-out position）
            }
            // 如果是输入节点（路径终点）
            else if (nodePos == end)
            {
                entry.second.first.push_back(line[len - 2]);  //倒数第二个点安全访问（fan-in position）
            }
        }

        //去重（输入端）
        auto &inputs = entry.second.first;
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

        //去重（输出端）
        auto &outputs = entry.second.second;
        std::sort(outputs.begin(), outputs.end());
        outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    }

    if(Nodelink.empty())
    {
        QString message = "Nodelink empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }

    mapping.node_mapping(Nodelink);
    auto nodeexample = mapping.nodecell_list;//按单位元胞类型分类的映射 std::map<std::string, std::vector<position>>
    if(nodeexample.empty())
    {
        QString message = "nodeexample empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }
    for(auto &cell : nodeexample)
    {
        auto cellpos_list = cell.second;
        if(cell.first == "input")
        {
            for(auto &cellpos : cellpos_list)
            {
                //元胞坐标 transform to 门级节点坐标
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                QPoint pos = QPoint(x_node, y_node);
                QString Iname = "default";
                for (auto &v : nodes)
                {
                    if (pos == v.pos)
                    {
                        Iname = v.name;
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::InputCell, positionPhaseMap, Iname);
                
            }
        }
        else if (cell.first == "output")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                QPoint pos = QPoint(x_node, y_node);
                QString Oname = "default";
                for (auto &v : nodes)
                {
                    if (pos == v.pos)
                    {
                        Oname = v.name;
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::OutputCell, positionPhaseMap, Oname);

            }
        }
        else if (cell.first == "normal")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::NormalCell, positionPhaseMap);
                
            }
        }
        else if (cell.first == "fix0")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_0, positionPhaseMap);

            }
        }
        else if (cell.first == "fix1")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_1, positionPhaseMap);
                
            }
        }
    }

    auto routeexample = mapping.mapping_line(circle_line);
    auto crossexample = mapping.crossline_list;

    std::vector<position> allroutecells;//存放所有路线元胞坐标，用于后续交叉线的检查
    allroutecells.clear();
    for (auto &pair : routeexample)
    {
        for (auto &v : pair.second)
        {
            allroutecells.insert(allroutecells.end(), v.begin(), v.end());
        }
        
    }

    //Cross线路元胞放置
    std::vector<position> crosscell;
    std::vector<position> verticalcell;
    if(!crossexample.empty())
    {
        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                crosscell.insert(crosscell.end(), cross.begin(), cross.end());
            }
        }
        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                for(auto unit = cross.begin(); unit != cross.end(); unit++)
                {
                    if((unit == cross.begin()) || (std::next(unit) == cross.end()))
                    {
                        int count = 0; 
                        position dir1 = {(*unit).first, (*unit).second + 1}; 
                        position dir2 = {(*unit).first, (*unit).second - 1}; 
                        position dir3 = {(*unit).first - 1, (*unit).second}; 
                        position dir4 = {(*unit).first + 1, (*unit).second}; 
                        if(std::find(crosscell.begin(), crosscell.end(), dir1) != crosscell.end())
                        {
                            ++count;  
                        }
                        if(std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                        {
                            ++count;  
                        }
                        if(std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                        {
                            ++count;  
                        }
                        if(std::find(crosscell.begin(), crosscell.end(), dir4) != crosscell.end())
                        {
                            ++count;  
                        }
                        if (count >= 2) 
                        {  
                            position cellpos = *unit;
                            putCellItem(cellpos, 2, CellType::CrossoverCell, positionPhaseMap);
                            
                        } 

                        if(count < 2) 
                        {
                            //若端点无法直接放置柱点，则跨时钟延伸两个单位元胞
                            if((std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir3) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir4) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos2 = dir1;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos3 = {dir1.first, dir1.second + 1};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcell.push_back(cellpos3);


                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else if ((std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir1) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir2) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos2 = dir4;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos3 = {dir4.first + 1, dir4.second};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcell.push_back(cellpos3);

                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else//放置交叉线端点三层柱点
                            {
                                position cellpos = *unit;
                                putCellItem(cellpos, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcell.push_back(cellpos);
                            }
                        }
                    }
                    else
                    {
                        position cellpos = *unit;
                        putCellItem(cellpos, 2, CellType::CrossoverCell, positionPhaseMap);
                        
                        
                    }
                }
            }

        }
    }

    //Normal线路元胞放置
    if(!routeexample.empty())
    {
        for(auto &line : routeexample)
        {
            for(auto &unit : line.second)
            {
                for(auto &pos : unit)
                {
                    if(std::find(crosscell.begin(), crosscell.end(), pos) == crosscell.end())
                    {
                        putCellItem(pos, 0, CellType::NormalCell, positionPhaseMap);
                        
                    }
                    else
                    {
                        std::vector<position> unitroute = unit;
                        std::vector<position> tempcross;
                        for(auto &v : unitroute)
                        {
                            if(std::find(crosscell.begin(), crosscell.end(), v) != crosscell.end())
                            {
                                tempcross.push_back(v);
                            }
                        }
                        bool isvertical = false;
                        for (auto &cell : tempcross)
                        {
                            if (std::find(verticalcell.begin(), verticalcell.end(), cell) != verticalcell.end())
                            {
                                isvertical = true;
                                break;
                            }
                        }
                        if (!isvertical)
                        {
                            for(auto &pos : tempcross)
                            {
                                putCellItem(pos, 0, CellType::NormalCell, positionPhaseMap);
                            }
                        }

                    }

                }
            }
        }
    }

}

void MappingExecutor::putClock(){

    auto& coordPhaseMap = gatelevelmapping->coordPhaseMap;
    //QMap transform to std::map (coordPhaseMap -> positionPhaseMap)
    std::map<position, int> positionPhaseMap;
    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint &p = it.key();
        int phase = it.value();

        positionPhaseMap[{static_cast<unsigned int>(p.x()), 
                        static_cast<unsigned int>(p.y())}] = phase;
    }

    for(auto &v : positionPhaseMap)
    {
        auto pos = v.first;
        int x = ((pos.first*5) + 2) * 20 + 200; 
        int y = ((pos.second*5) + 2) * 20 + 200;
        if((v.second >= 0) && (v.second <= 3))
        {
            QCADClockScheme *item = new QCADClockScheme(v.second);
            item->setPos(x, y);
            item->setZValue(-1);
            mainWindow->scene->addItem(item);
        }
    }
}

void MappingExecutor::putCellItem(position _cellpos, int _celllayer, CellType _cellType, std::map<position, int>& _pos_phase, QString _name)
{
    int x_node = _cellpos.first / 5;
    int y_node = _cellpos.second / 5;
    int x_coord = _cellpos.first*20 + 200;  // 坐标
    int y_coord = _cellpos.second*20 + 200;
    int cell_layer = _celllayer;
    position cellpos = std::make_pair(x_node, y_node);
    int phase = _pos_phase[cellpos];

    QCADCellItem *cellItem = new QCADCellItem(x_coord, y_coord, cell_layer, phase, _cellType, _name);
    mainWindow->checkCellInserted(mainWindow->layers, cellItem, cell_layer, x_coord, y_coord);
}