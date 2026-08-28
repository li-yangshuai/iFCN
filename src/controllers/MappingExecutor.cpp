#include "MappingExecutor.h"
#include <QDebug>
#include <algorithm>
#include <limits>
#include <QMessageBox>

namespace {
struct ShiftedPosition {
    position pos{0, 0};
    bool valid = false;
};

ShiftedPosition shiftedPosition(const position &base, int dx, int dy)
{
    const auto x = static_cast<long long>(base.first) + dx;
    const auto y = static_cast<long long>(base.second) + dy;
    const auto maxCoord = static_cast<long long>(std::numeric_limits<unsigned int>::max());
    if (x < 0 || y < 0 || x > maxCoord || y > maxCoord) {
        return {};
    }
    return {{static_cast<unsigned int>(x), static_cast<unsigned int>(y)}, true};
}

bool sceneCoordinates(const position &cellPos, int &xCoord, int &yCoord)
{
    constexpr unsigned int kPitch = 20;
    constexpr unsigned int kOrigin = 200;
    constexpr unsigned int kMaxCellCoord =
        (static_cast<unsigned int>(std::numeric_limits<int>::max()) - kOrigin) / kPitch;

    if (cellPos.first > kMaxCellCoord || cellPos.second > kMaxCellCoord) {
        return false;
    }

    xCoord = static_cast<int>(cellPos.first * kPitch + kOrigin);
    yCoord = static_cast<int>(cellPos.second * kPitch + kOrigin);
    return true;
}
} // namespace

MappingExecutor::MappingExecutor(GateLevelMapping* m, MainWindow* w)
    : gatelevelmapping(m), mainWindow(w) {}

void MappingExecutor::executeMapping()
{
    // 直接访问 GateLevelMapping 的 public 成员
    QString circuitName = gatelevelmapping->circuitName;
    auto& nodes = gatelevelmapping->nodes;
    auto& routes = gatelevelmapping->routes;
    auto& coordPhaseMap = gatelevelmapping->coordPhaseMap;

    Mapping mapping;

    //QMap transform to std::map (coordPhaseMap -> positionPhaseMap)
    /*
    std::map<position, int> positionPhaseMap;
    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint &p = it.key();
        int phase = it.value();

        positionPhaseMap[{static_cast<unsigned int>(p.x()), 
                        static_cast<unsigned int>(p.y())}] = phase;
    }
    */
    auto positionPhaseMap = toPositionPhaseMap(coordPhaseMap);

    //get circle_line from routes(containing all paths)
    std::vector<std::vector<position>> circle_line;
    std::vector<unsigned int> circleIterationDistances;
    circle_line.clear();
    for (auto routeIt = routes.cbegin(); routeIt != routes.cend(); ++routeIt)
    {
        const QVector<QPoint>& qPoints = routeIt.value();
        std::vector<position> convertedRoute;
        convertedRoute.reserve(qPoints.size());

        for (const QPoint& point : qPoints)
            convertedRoute.emplace_back(point.x(), point.y());

        circle_line.push_back(std::move(convertedRoute));
        circleIterationDistances.push_back(
            gatelevelmapping->routeIterationDistances.value(routeIt.key(), 0));
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

    mapping.node_mapping(Nodelink, gatelevelmapping->resolvedMappingMode());
    auto routeexample = mapping.mapping_line(
        circle_line,
        gatelevelmapping->resolvedMappingMode(),
        circleIterationDistances);
    std::string crossoverError;
    if (!mapping.validate_crossovers(&crossoverError)) {
        const QString message = QStringLiteral("Cell mapping rejected: invalid crossover: %1")
                                    .arg(QString::fromStdString(crossoverError));
        qWarning().noquote() << message;
        mainWindow->customStatusBar->addMessage(message);
        return;
    }
    auto nodeexample = mapping.nodecell_list;//按单位元胞类型分类的映射 std::map<std::string, std::vector<position>>
    if(nodeexample.empty())
    {
        QString message = "nodeexample empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }
    /*
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
    */
    for (const auto& [type, cellList] : nodeexample)
    {
        // 统一映射类型到 CellType 枚举
        static const std::map<std::string, CellType> typeMap = {
            {"input", CellType::InputCell},
            {"output", CellType::OutputCell},
            {"normal", CellType::NormalCell},
            {"fix0",  CellType::FixedCell_0},
            {"fix1",  CellType::FixedCell_1}
        };

        auto itType = typeMap.find(type);
        if (itType == typeMap.end()) continue;  // 未知类型，跳过

        CellType cellType = itType->second;

        for (const auto& cellPos : cellList)
        {
            QString nodeName = "default";

            // 仅 input/output 需要节点名匹配
            if (cellType == CellType::InputCell || cellType == CellType::OutputCell)
            {
                position nodePosition{cellPos.first / 5, cellPos.second / 5};
                QPoint nodePoint(static_cast<int>(nodePosition.first),
                                 static_cast<int>(nodePosition.second));

                for (const auto& node : nodes)
                {
                    if (nodePoint == node.pos)
                    {
                        nodeName = node.name;
                        break;
                    }
                }
            }

            putCellItem(cellPos, 0, cellType, positionPhaseMap, nodeName);
        }
    }

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
    /*
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
    */
    // === Cross线路元胞放置 ===
    std::vector<position> crosscell;
    std::vector<position> verticalcell;

    auto contains = [](const std::vector<position>& vec, const position& p) {
        return std::find(vec.begin(), vec.end(), p) != vec.end();
    };
    auto containsShifted = [&](const std::vector<position>& vec, const ShiftedPosition& p) {
        return p.valid && contains(vec, p.pos);
    };

    if (!crossexample.empty())
    {
        // 收集所有交叉线坐标
        for (const auto& [_, lines] : crossexample)
            for (const auto& seg : lines)
                crosscell.insert(crosscell.end(), seg.begin(), seg.end());

        // 遍历交叉线路
        for (const auto& [_, lines] : crossexample)
        {
            for (const auto& seg : lines)
            {
                for (auto it = seg.begin(); it != seg.end(); ++it)
                {
                    const position p = *it;
                    const bool isEnd = (it == seg.begin()) || (std::next(it) == seg.end());

                    // --- 中间点：直接放置交叉线 ---
                    if (!isEnd)
                    {
                        putCellItem(p, 2, CellType::CrossoverCell, positionPhaseMap);
                        continue;
                    }

                    // --- 端点处理 ---
                    const ShiftedPosition dirs[4] = {
                        shiftedPosition(p, 0, 1),
                        shiftedPosition(p, 0, -1),
                        shiftedPosition(p, -1, 0),
                        shiftedPosition(p, 1, 0)
                    };

                    int neighborCount = 0;
                    for (auto& d : dirs)
                        if (containsShifted(crosscell, d)) ++neighborCount;

                    //情况1：足够连接 → 直接交叉元胞
                    if (neighborCount >= 2)
                    {
                        putCellItem(p, 2, CellType::CrossoverCell, positionPhaseMap);
                        continue;
                    }

                    //情况2：连接不足 → 需要柱状延伸
                    bool extended = false;

                    const auto extendWithPillar = [&](const position& base,
                                                       const ShiftedPosition& dirCross,
                                                       const ShiftedPosition& pillar) {
                        if (!dirCross.valid || !pillar.valid) {
                            return false;
                        }
                        putCellItem(base, 2, CellType::CrossoverCell, positionPhaseMap);
                        putCellItem(dirCross.pos, 2, CellType::CrossoverCell, positionPhaseMap);

	                        for (int l = 0; l < 3; ++l)
	                            putCellItem(pillar.pos, l, CellType::VerticalCell, positionPhaseMap);

                        verticalcell.push_back(pillar.pos);
                        crosscell.push_back(dirCross.pos);
                        crosscell.push_back(pillar.pos);
                        return true;
                    };

                    // 左右/上下方向延伸条件保持一致
                    if (containsShifted(crosscell, dirs[1]) &&
                        containsShifted(allroutecells, dirs[2]) &&
                        containsShifted(allroutecells, dirs[3]))
                    {
                        extended = extendWithPillar(
                            p,
                            dirs[0],
                            dirs[0].valid ? shiftedPosition(dirs[0].pos, 0, 1) : ShiftedPosition{}
                        );
                    }
                    else if (containsShifted(crosscell, dirs[2]) &&
                            containsShifted(allroutecells, dirs[0]) &&
                            containsShifted(allroutecells, dirs[1]))
                    {
                        extended = extendWithPillar(
                            p,
                            dirs[3],
                            dirs[3].valid ? shiftedPosition(dirs[3].pos, 1, 0) : ShiftedPosition{}
                        );
                    }

                    // 情况3：无法延伸 → 三层垂直柱点
                    if (!extended)
                    {
                        for (int l = 0; l < 3; ++l)
                            putCellItem(p, l, CellType::VerticalCell, positionPhaseMap);
                        verticalcell.push_back(p);
                    }
                }
            }
        }
    }

    // === Normal线路元胞放置 ===
    if (!routeexample.empty())
    {
        for (const auto& [_, lines] : routeexample)
        {
            for (const auto& seg : lines)
            {
                for (const auto& p : seg)
                {
                    const bool isCross = contains(crosscell, p);
                    if (!isCross)
                    {
                        putCellItem(p, 0, CellType::NormalCell, positionPhaseMap);
                        continue;
                    }

                    // 检查是否为垂直冲突点
                    bool isVertical = contains(verticalcell, p);
                    if (!isVertical)
                        putCellItem(p, 0, CellType::NormalCell, positionPhaseMap);
                }
            }
        }
    }

}

void MappingExecutor::putClock(){

    /*
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
    */
    auto positionPhaseMap = toPositionPhaseMap(gatelevelmapping->coordPhaseMap);


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
    int x_coord = 0;
    int y_coord = 0;
    if (!sceneCoordinates(_cellpos, x_coord, y_coord)) {
        qWarning() << "[MappingExecutor] Skip mapped cell with invalid scene coordinate:"
                   << _cellpos.first << _cellpos.second;
        return;
    }

    const position cellpos = std::make_pair(_cellpos.first / 5, _cellpos.second / 5);
    const auto phaseIt = _pos_phase.find(cellpos);
    int phase = 0;
    if (phaseIt != _pos_phase.end()) {
        phase = phaseIt->second;
    } else {
        qWarning() << "[MappingExecutor] Mapped cell outside phase map; using phase 0:"
                   << _cellpos.first << _cellpos.second;
    }

    int cell_layer = _celllayer;

    QCADCellItem *cellItem = new QCADCellItem(x_coord, y_coord, cell_layer, phase, _cellType, _name);
    mainWindow->checkCellInserted(mainWindow->layers, cellItem, cell_layer, x_coord, y_coord);
}

std::map<position, int> MappingExecutor::toPositionPhaseMap(const QHash<QPoint, int>& coordPhaseMap)
{
    std::map<position, int> result;

    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint& p = it.key();
        result[{static_cast<unsigned>(p.x()), static_cast<unsigned>(p.y())}] = it.value();
    }
    return result;
}
