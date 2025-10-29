#include "GateLevelMapping.h"



// ====================================================
// 构造函数
// ====================================================
GateLevelMapping::GateLevelMapping(QObject *parent)
    : QObject(parent)
{
}

// ====================================================
// 主函数：选择并解析 .ifcn 文件
// ====================================================
void GateLevelMapping::parseGateLevelMappingFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        "Select Gate Level Mapping File",
        QDir::currentPath(),
        "iFCN Mapping Files (*.ifcn)"
    );

    if (filePath.isEmpty()) {
        qInfo() << "[GateLevelMapping] No file selected.";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, "File Error",
                             QString("Failed to open file:\n%1").arg(filePath));
        return;
    }

    QTextStream in(&file);
    section = NONE;
    inPhaseMapSection = false;
    nodes.clear();
    routes.clear();
    coordPhaseMap.clear();

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("#circuit name:")) {
            circuitName = line.section(':', 1).trimmed();
        }
        else if (line.startsWith("#node") || line.startsWith("#nodes")) {
            section = (section == NODE_INFO) ? NONE : NODE_INFO;
        }
        else if (line.startsWith("#path") || line.startsWith("#paths")) {
            section = (section == PATH_INFO) ? NONE : PATH_INFO;
        }
        else if (line.startsWith("#phase map")) {
            inPhaseMapSection = !inPhaseMapSection;
        }
        else if (section == NODE_INFO && line.contains(',')) {
            parseNodeLine(line);
        }
        else if (section == PATH_INFO && line.contains(':')) {
            parsePathLine(line);
        }
        else if (inPhaseMapSection && line.contains(':')) {
            parsePhaseMapLine(line);
        }
    }

    file.close();

    qInfo() << "[GateLevelMapping] Circuit:" << circuitName
            << "Nodes:" << nodes.size()
            << "Routes:" << routes.size()
            << "Phase entries:" << coordPhaseMap.size();

    QMessageBox::information(nullptr, "Parsing Complete",
                             QString("Parsed circuit: %1\nNodes: %2\nRoutes: %3\nPhase Cells: %4")
                             .arg(circuitName)
                             .arg(nodes.size())
                             .arg(routes.size())
                             .arg(coordPhaseMap.size()));
}

// ====================================================
// 节点解析
// ====================================================
void GateLevelMapping::parseNodeLine(const QString &line)
{
    // 例：0, pi00, Input, (0,0);
    QString clean = line;
    clean.remove(';');
    QStringList parts = clean.split(',', Qt::SkipEmptyParts);
    if (parts.size() < 4) return;

    NodeInfo node;
    node.index = parts[0].trimmed().toInt();
    node.name  = parts[1].trimmed();
    node.type  = parts[2].trimmed();

    QString posStr = parts[3].trimmed();
    posStr.remove('(').remove(')');
    QStringList xy = posStr.split(',');
    if (xy.size() == 2)
        node.pos = QPoint(xy[0].toInt(), xy[1].toInt());
    else
        node.pos = QPoint(0, 0);

    nodes[node.index] = node;
}

// ====================================================
// 路径解析
// ====================================================
void GateLevelMapping::parsePathLine(const QString &line)
{
    // 例：(1,2): (10,10),(11,10),(12,10);
    QString clean = line;
    clean.remove(';');
    int left1 = clean.indexOf('(');
    int comma1 = clean.indexOf(',');
    int right1 = clean.indexOf(')');
    int colon = clean.indexOf(':');
    if (left1 < 0 || comma1 < 0 || right1 < 0 || colon < 0)
        return;

    int u = clean.mid(left1 + 1, comma1 - left1 - 1).toInt();
    int v = clean.mid(comma1 + 1, right1 - comma1 - 1).toInt();

    QString pathPart = clean.mid(colon + 1);
    QStringList coordStrs = pathPart.split(')', Qt::SkipEmptyParts);

    QVector<QPoint> path;
    for (const QString &coordStr : coordStrs) {
        int l = coordStr.indexOf('(');
        int c = coordStr.indexOf(',');
        if (l < 0 || c < 0) continue;
        int x = coordStr.mid(l + 1, c - l - 1).toInt();
        int y = coordStr.mid(c + 1).trimmed().toInt();
        path.append(QPoint(x, y));
    }

    routes[{u, v}] = path;
}

// ====================================================
// 相位映射解析
// ====================================================
void GateLevelMapping::parsePhaseMapLine(const QString &line)
{
    // 例：(0,0):0; (1,0):1; (2,0):2;
    QString clean = line;
    clean.remove(';');
    QStringList entries = clean.split(' ', Qt::SkipEmptyParts);

    for (const QString &entry : entries) {
        int l = entry.indexOf('(');
        int c = entry.indexOf(',');
        int r = entry.indexOf(')');
        int colon = entry.indexOf(':');
        if (l < 0 || c < 0 || r < 0 || colon < 0)
            continue;

        int x = entry.mid(l + 1, c - l - 1).toInt();
        int y = entry.mid(c + 1, r - c - 1).toInt();
        int phase = entry.mid(colon + 1).trimmed().toInt();

        coordPhaseMap.insert(QPoint(x, y), phase);
    }
}

// ====================================================
// 根据节点索引查询相位
// ====================================================
int GateLevelMapping::getPhaseAtNode(int nodeIndex) const
{
    if (!nodes.contains(nodeIndex))
        return -1;
    const QPoint &pos = nodes[nodeIndex].pos;
    return coordPhaseMap.value(pos, -1);
}

// ====================================================
// 根据坐标查询相位
// ====================================================
int GateLevelMapping::getPhaseAtCoord(const QPoint &pt) const
{
    return coordPhaseMap.value(pt, -1);
}


void GateLevelMapping::mappingCellItem(){
    Mapping mapping;

    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    for(auto &v: routes)
    {
        circle_line.push_back(std::vector<position>(v.second.begin(), v.second.end()));
    }


    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node,type), (扇入，扇出)>
    Nodelink.clear();

    for(auto &v : routes)
    {
        std::vector<QPoint> templine = v.second;
        QString startnodetype = nodes[v.first.first].type;
        position startpos = templine.front();
        QString endnodetype = nodes[v.first.second].type;
        position endpos = templine.back();
        Nodelink[std::make_pair(startpos, startnodetype)] = std::make_pair(std::vector<position>(), std::vector<position>());
        Nodelink[std::make_pair(endpos, endnodetype)] = std::make_pair(std::vector<position>(), std::vector<position>());
    }

    for(auto &pair : Nodelink)
    {
        for(auto &line : circle_line)
        {
            if(pair.first.first == line.front())
            {
                std::vector<position> &output = pair.second.second;
                output.push_back(*std::next(line.begin()));
            }
            else if(pair.first.first == line.back())
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

    if(Nodelink.empty())
    {
        QString message = "Nodelink empty!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }

    mapping.node_mapping(Nodelink);
    auto nodeexample = mapping.nodecell_list;
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
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                QPoint pos = QPoint(x_node, y_node);
                QString Iname = "default";
                for (auto &v : nodes)
                {
                    if (pos == v.second.pos)
                    {
                        Iname = v.second.name;
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::InputCell, coordPhaseMap, Iname);
                
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
                    if (pos == v.second.pos)
                    {
                        Oname = v.second.name;
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::OutputCell, coordPhaseMap, Oname);

            }
        }
        else if (cell.first == "normal")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::NormalCell, coordPhaseMap);
                
            }
        }
        else if (cell.first == "fix0")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_0, coordPhaseMap);

            }
        }
        else if (cell.first == "fix1")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_1, coordPhaseMap);
                
            }
        }
    }

    auto routeexample = mapping.mapping_line(circle_line);
    auto crossexample = mapping.crossline_list;

    std::vector<position> allroutecells;
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
                            putCellItem(cellpos, 2, CellType::CrossoverCell, coordPhaseMap);
                            
                        } 

                        if(count < 2) 
                        {
                            //若端点无法直接放置柱点，则跨时钟延伸两个单位元胞
                            if((std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir3) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir4) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, coordPhaseMap);


                                position cellpos2 = dir1;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, coordPhaseMap);


                                position cellpos3 = {dir1.first, dir1.second + 1};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, coordPhaseMap);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, coordPhaseMap);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, coordPhaseMap);
                                verticalcell.push_back(cellpos3);


                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else if ((std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir1) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir2) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, coordPhaseMap);


                                position cellpos2 = dir4;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, coordPhaseMap);


                                position cellpos3 = {dir4.first + 1, dir4.second};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, coordPhaseMap);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, coordPhaseMap);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, coordPhaseMap);
                                verticalcell.push_back(cellpos3);

                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else//放置交叉线端点三层柱点
                            {
                                position cellpos = *unit;
                                putCellItem(cellpos, 0, CellType::VerticalCell, coordPhaseMap);
                                putCellItem(cellpos, 1, CellType::VerticalCell, coordPhaseMap);
                                putCellItem(cellpos, 2, CellType::VerticalCell, coordPhaseMap);
                                verticalcell.push_back(cellpos);
                            }
                        }
                    }
                    else
                    {
                        position cellpos = *unit;
                        putCellItem(cellpos, 2, CellType::CrossoverCell, coordPhaseMap);
                        
                        
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
                        putCellItem(pos, 0, CellType::NormalCell, coordPhaseMap);
                        
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
                                putCellItem(pos, 0, CellType::NormalCell, coordPhaseMap);
                            }
                        }

                    }

                }
            }
        }
    }

    for (auto &vpos : verticalcell) 
    {
        int pl = 0;
        int posx_node = vpos.first / 5;
        int posy_node = vpos.second / 5;
        position pos_node = {posx_node, posy_node};
        std::vector<position> vtemp = {{vpos.first, vpos.second + 1},  
                                    {vpos.first, vpos.second - 1},  
                                    {vpos.first - 1, vpos.second},  
                                    {vpos.first + 1, vpos.second} };
        for (auto &vcell : vtemp)
        {
            if (std::find(crosscell.begin(), crosscell.end(), vcell) != crosscell.end())
            {
                pl++;
            }
        }
        if ((pl >= 2) || (std::find(notcell.begin(), notcell.end(), pos_node) != notcell.end()))
        {
            QString message = "vertical problem position : ( "+ QString::number(posx_node) + " , "+ QString::number(posy_node) + " )";
            mainWindow->customStatusBar->addMessage(message);
        }
    }
}

void GateLevelMapping::putCellItem(QPoint _cellpos, int _celllayer, CellType _cellType,  std::map<unsigned int ,int>& _pos_phase, QString _name = ""){
    int x_node = _cellpos.x() / 5;
    int y_node = _cellpos.y() / 5;
    int x_coord = _cellpos.x()*20 + 200;  // 坐标
    int y_coord = _cellpos.y()*20 + 200;
    int cell_layer = _celllayer;
    QPoint pos = QPoint(x_node, y_node);
    int phase = _pos_phase[pos];

    QCADCellItem *cellItem = new QCADCellItem(x_coord, y_coord, cell_layer, phase, _cellType, _name);
    mainWindow->checkCellInserted(mainWindow->layers, cellItem, cell_layer, x_coord, y_coord);
}

void GateLevelMapping::putClock(){
    for(auto &v : coordPhaseMap)
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

