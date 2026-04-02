#include "GateLevelMapping.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <unordered_map>
#include <unordered_set>
#include "ui/mainwindow/MainWindow.h"

GateLevelMapping::GateLevelMapping(MainWindow *parent)
    : QObject(parent), mainWindow(parent)
{
    // qDebug() << "[GateLevelMapping] initialized (Qt containers)";
}

void GateLevelMapping::parseGateLevelMappingFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        "Open .ifcn File",
        QDir::currentPath(),
        "iFCN Mapping Files (*.ifcn *.iFCN);;All file (*)"
    );
    if (filePath.isEmpty()) {
        qWarning() << "[GateLevelMapping] No file selected.";
        return;
    }
    parseGateLevelMappingFile(filePath);
}

void GateLevelMapping::parseGateLevelMappingFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        qWarning() << "[GateLevelMapping] Empty file path.";
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(nullptr, "File Error", "Cannot open file:\n" + filePath);
        return;
    }

    QTextStream in(&file);
    nodes.clear();
    routes.clear();
    coordPhaseMap.clear();

    bool nodeSection = false;
    bool pathSection = false;
    bool phaseSection = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("#circuit name:")) {
            circuitName = line.section(':', 1).trimmed();
        }
        else if (line.startsWith("#nodes info")) {
            nodeSection = true; pathSection = phaseSection = false;
            continue;
        }
        else if (line.startsWith("#paths info")) {
            pathSection = true; nodeSection = phaseSection = false;
            continue;
        }
        else if (line.startsWith("#phase map")) {
            phaseSection = true; nodeSection = pathSection = false;
            continue;
        }
        else if (line.startsWith("#")) {
            continue;
        }

        if (nodeSection && line.contains(',')) {
            parseNodeLine(line);
        } 
        else if (pathSection && line.contains(':')) {
            parsePathLine(line);
        }
        else if (phaseSection && line.contains(':')) {
            parsePhaseLine(line);
        }
    }

    file.close();

    QMessageBox::information(nullptr, "Parsing Complete",
        QString("Circuit: %1\nNodes: %2\nRoutes: %3\nPhases: %4")
        .arg(circuitName)
        .arg(nodes.size())
        .arg(routes.size())
        .arg(coordPhaseMap.size()));

    QString message = "Mapping loaded: " + circuitName +
                      ", Nodes: " + QString::number(nodes.size()) +
                      ", Routes: " + QString::number(routes.size()) +
                      ", Phases: " + QString::number(coordPhaseMap.size());
    mainWindow->customStatusBar->addMessage(message);
    QCoreApplication::processEvents();

    mainWindow->beginSceneBatchUpdate();
    mainWindow->scene->clearFastRender();

    //映射
    mainWindow->slotAddLayer("second layer");
    mainWindow->slotAddLayer("third layer");
    mainWindow->scene->beginFastRenderBuild(mainWindow->layers.size());


    //遍历routes。打印key和value

    for (auto it = routes.begin(); it != routes.end(); ++it)
    {
        const QPair<int,int>& key = it.key();
        const QVector<QPoint>& path = it.value();
        // qDebug() << "============================";
        // qDebug() << "Route (" << key.first << "->" << key.second << "), length =" << path.size();

        // for (const QPoint& p : path)
        //     qDebug() << "   (" << p.x() << "," << p.y() << ")";
    }

    mappingCellItem();
    putClock();
    mainWindow->scene->finalizeFastRenderBuild();
    QVector<QString> inputNames;
    for (const auto &layerCells : mainWindow->scene->fastCellsByLayer()) {
        for (const auto &cell : layerCells) {
            if (cell.type == CellType::InputCell && !cell.name.isEmpty()) {
                inputNames.push_back(cell.name);
            }
        }
    }
    mainWindow->setInputNames(inputNames);
    mainWindow->endSceneBatchUpdate(true);
    // printCrossline();

    emit mappingLoaded();
}

void GateLevelMapping::parseNodeLine(const QString &line)
{
    // 格式: 0, pi00, Input, (0,0);
    QString clean = line;
    clean.remove(';');
    QStringList parts = clean.split(',', QString::SkipEmptyParts);
    if (parts.size() < 4) return;

    NodeInfo node;
    node.index = parts[0].trimmed().toInt();
    node.name  = parts[1].trimmed();
    node.type  = parts[2].trimmed();

    QRegularExpression posPattern("\\((\\d+),(\\d+)\\)");
    QRegularExpressionMatch match = posPattern.match(line);
    if (match.hasMatch()) {
        node.pos = QPoint(match.captured(1).toInt(), match.captured(2).toInt());
    }

    nodes.insert(node.index, node);
}

void GateLevelMapping::parsePathLine(const QString &line)
{
    // 格式: (1,2): (10,10),(11,10),(12,10);
    QRegularExpression header("\\((\\d+),(\\d+)\\):");
    QRegularExpressionMatch headMatch = header.match(line);
    if (!headMatch.hasMatch()) return;

    int u = headMatch.captured(1).toInt();
    int v = headMatch.captured(2).toInt();

    QVector<QPoint> path;
    QRegularExpression coordPattern("\\((\\d+),(\\d+)\\)");
    QRegularExpressionMatchIterator it = coordPattern.globalMatch(line);

    bool first = true;  // ✅ 用于跳过第一个坐标
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        if (first) { first = false; continue; } // 跳过 (u,v)
        path.append(QPoint(m.captured(1).toInt(), m.captured(2).toInt()));
    }

    routes.insert({u, v}, path);
}


void GateLevelMapping::parsePhaseLine(const QString &line)
{
    // 格式: (x,y):phase
    QRegularExpression entry("\\((\\d+),(\\d+)\\)\\s*:\\s*(\\d+)");
    QRegularExpressionMatchIterator it = entry.globalMatch(line);

    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QPoint pt(m.captured(1).toInt(), m.captured(2).toInt());
        coordPhaseMap.insert(pt, m.captured(3).toInt());
    }
}


void GateLevelMapping::mappingCellItem(){
    Mapping mapping;

    std::map<position, int> positionPhaseMap;
    std::unordered_map<position, QString, MappingPositionHash> nodeNameByPos;
    for (auto it = coordPhaseMap.begin(); it != coordPhaseMap.end(); ++it)
    {
        const QPoint &p = it.key();
        int phase = it.value();

        positionPhaseMap[{static_cast<unsigned int>(p.x()), 
                        static_cast<unsigned int>(p.y())}] = phase;
    }
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        nodeNameByPos[{static_cast<unsigned int>(it.value().pos.x()),
                       static_cast<unsigned int>(it.value().pos.y())}] = it.value().name;
    }

    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    circle_line.reserve(routes.size());
    for (auto it = routes.begin(); it != routes.end(); ++it) 
    {
        const QVector<QPoint>& qPoints = it.value();
        std::vector<position> convertedRoute;
        convertedRoute.reserve(qPoints.size());
        for (const QPoint& point : qPoints)
            convertedRoute.emplace_back(static_cast<unsigned int>(point.x()),
                                        static_cast<unsigned int>(point.y()));
        circle_line.push_back(std::move(convertedRoute));
    }

    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node,type), (扇入，扇出)>
    Nodelink.clear();

    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        position nodepos{it.value().pos.x(), it.value().pos.y()};
        std::string type = it.value().type.toStdString();
        Nodelink.try_emplace({nodepos, type}, std::make_pair(std::vector<position>{}, std::vector<position>{}));
    }


    // qDebug() << "start print all route:";
    // int idx = 0;
    // for (const auto &line : circle_line)
    // {
    //     QString lineStr = QString("Path %1: ").arg(idx++);
    //     for (const auto &p : line)
    //         lineStr += QString("(%1,%2) ").arg(p.first).arg(p.second);
    //     qDebug().noquote() << lineStr;
    // }
    // qDebug() << "start print Nodelink:";
    int node_idx = 0;
    for (const auto &entry : Nodelink)
    {
        const auto &pos = entry.first.first;
        const auto &type = entry.first.second;
        const auto &inputs = entry.second.first;
        const auto &outputs = entry.second.second;

        QString lineStr = QString("Node %1 (%2,%3) Type:%4 | Fan-in:")
                            .arg(node_idx++)
                            .arg(pos.first)
                            .arg(pos.second)
                            .arg(QString::fromStdString(type));

        for (const auto &in : inputs)
            lineStr += QString(" (%1,%2)").arg(in.first).arg(in.second);

        lineStr += " | Fan-out:";
        for (const auto &out : outputs)
            lineStr += QString(" (%1,%2)").arg(out.first).arg(out.second);

        // qDebug().noquote() << lineStr;
    }


    for (auto it = routes.begin(); it != routes.end(); ++it)
    {
        const QPair<int,int>& key = it.key();
        const QVector<QPoint>& path = it.value();
        const size_t len = static_cast<size_t>(path.size());
        if (len < 2 || !nodes.contains(key.first) || !nodes.contains(key.second)) {
            continue;
        }

        const NodeInfo& source = nodes[key.first];
        const NodeInfo& sink = nodes[key.second];
        const auto sourceKey = std::make_pair(position{static_cast<unsigned int>(source.pos.x()),
                                                       static_cast<unsigned int>(source.pos.y())},
                                              source.type.toStdString());
        const auto sinkKey = std::make_pair(position{static_cast<unsigned int>(sink.pos.x()),
                                                     static_cast<unsigned int>(sink.pos.y())},
                                            sink.type.toStdString());

        Nodelink[sourceKey].second.push_back(position{static_cast<unsigned int>(path[1].x()),
                                                      static_cast<unsigned int>(path[1].y())});
        Nodelink[sinkKey].first.push_back(position{static_cast<unsigned int>(path[len - 2].x()),
                                                   static_cast<unsigned int>(path[len - 2].y())});
    }

    for (auto &entry : Nodelink)
    {
        auto &inputs = entry.second.first;
        std::sort(inputs.begin(), inputs.end());
        inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

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
                const auto nameIt = nodeNameByPos.find({x_node, y_node});
                const QString Iname = (nameIt != nodeNameByPos.end()) ? nameIt->second : QStringLiteral("default");
                putCellItem(cellpos, 0, CellType::InputCell, positionPhaseMap, Iname);
                
            }
        }
        else if (cell.first == "output")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                const auto nameIt = nodeNameByPos.find({x_node, y_node});
                const QString Oname = (nameIt != nodeNameByPos.end()) ? nameIt->second : QStringLiteral("default");
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
    auto nodeexample2 = mapping.nodecell_list;

    std::vector<position> allroutecells;
    for (auto &pair : routeexample)
    {
        for (auto &v : pair.second)
        {
            allroutecells.insert(allroutecells.end(), v.begin(), v.end());
        }
        
    }
    std::vector<position> allnodecells;
    for (auto &pair : nodeexample2)
    {
        allnodecells.insert(allnodecells.end(), pair.second.begin(), pair.second.end());
    }

    std::unordered_set<position, MappingPositionHash> allcrosscellsSet;
    if(!crossexample.empty())
    {
        for (auto &pair : crossexample)
        {
            for (auto &v : pair.second)
            {
                allcrosscellsSet.insert(v.begin(), v.end());
            }
            
        }
    }

    //对于线路元胞，交叉点不去重，非交叉点（线路复用）去重
    std::vector<position> result;
    result.reserve(allroutecells.size());
    std::unordered_set<position, MappingPositionHash> seen;  // 已出现过的“非交叉”点
    for (auto &p : allroutecells) {
        const bool is_cross = allcrosscellsSet.find(p) != allcrosscellsSet.end();

        if (is_cross) {
            result.push_back(p);
        } else {
            if (seen.insert(p).second) {
                result.push_back(p);
            }
        }
    }
    allroutecells = result;
    std::unordered_set<position, MappingPositionHash> allroutecellsSet(allroutecells.begin(), allroutecells.end());

    size_t total_count = allroutecells.size() + allnodecells.size();
    QString message = QStringLiteral("Total cells: %1").arg(static_cast<qulonglong>(total_count));
    mainWindow->printToStatusBar(message);


    //Cross线路元胞放置
    std::unordered_set<position, MappingPositionHash> crosscellSet;
    std::unordered_set<position, MappingPositionHash> verticalcellSet;
    if(!crossexample.empty())
    {
        size_t total_cross = 0;
        for (const auto &entry : crossexample) 
        {
            total_cross += entry.second.size();
        }
        QString message = QStringLiteral("Total crossline segments: %1").arg(static_cast<qulonglong>(total_cross));
        mainWindow->printToStatusBar(message);
        
        
        for(auto &crossline : crossexample)
        {
            for(auto &cross : crossline.second)
            {
                crosscellSet.insert(cross.begin(), cross.end());
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
                        if(crosscellSet.find(dir1) != crosscellSet.end())
                        {
                            ++count;  
                        }
                        if(crosscellSet.find(dir2) != crosscellSet.end())
                        {
                            ++count;  
                        }
                        if(crosscellSet.find(dir3) != crosscellSet.end())
                        {
                            ++count;  
                        }
                        if(crosscellSet.find(dir4) != crosscellSet.end())
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
                            if((crosscellSet.find(dir2) != crosscellSet.end())
                            &&(allroutecellsSet.find(dir3) != allroutecellsSet.end())
                            &&(allroutecellsSet.find(dir4) != allroutecellsSet.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos2 = dir1;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos3 = {dir1.first, dir1.second + 1};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcellSet.insert(cellpos3);


                                crosscellSet.insert(cellpos2);
                                crosscellSet.insert(cellpos3);
                            }
                            else if ((crosscellSet.find(dir3) != crosscellSet.end())
                            &&(allroutecellsSet.find(dir1) != allroutecellsSet.end())
                            &&(allroutecellsSet.find(dir2) != allroutecellsSet.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos2 = dir4;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, positionPhaseMap);


                                position cellpos3 = {dir4.first + 1, dir4.second};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcellSet.insert(cellpos3);

                                crosscellSet.insert(cellpos2);
                                crosscellSet.insert(cellpos3);
                            }
                            else//放置交叉线端点三层柱点
                            {
                                position cellpos = *unit;
                                putCellItem(cellpos, 0, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos, 1, CellType::VerticalCell, positionPhaseMap);
                                putCellItem(cellpos, 2, CellType::VerticalCell, positionPhaseMap);
                                verticalcellSet.insert(cellpos);
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
                    if(crosscellSet.find(pos) == crosscellSet.end())
                    {
                        putCellItem(pos, 0, CellType::NormalCell, positionPhaseMap);
                        
                    }
                    else
                    {
                        std::vector<position> tempcross;
                        tempcross.reserve(unit.size());
                        for(const auto &v : unit)
                        {
                            if(crosscellSet.find(v) != crosscellSet.end())
                            {
                                tempcross.push_back(v);
                            }
                        }
                        bool isvertical = false;
                        for (auto &cell : tempcross)
                        {
                            if (verticalcellSet.find(cell) != verticalcellSet.end())
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

void GateLevelMapping::putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<position ,int>& _pos_phase, QString _name ){
    int x_node = _cellpos.first / 5;
    int y_node = _cellpos.second / 5;
    int x_coord = _cellpos.first*20 + 200;  // 坐标
    int y_coord = _cellpos.second*20 + 200;
    int cell_layer = _celllayer;
    position cellpos = std::make_pair(x_node, y_node);
    int phase = _pos_phase[cellpos];
    mainWindow->scene->addFastCell(x_coord, y_coord, cell_layer, phase, _cellType, _name);
}

void GateLevelMapping::putClock(){
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
            mainWindow->scene->addFastClock(x, y, v.second);
        }
    }
}
