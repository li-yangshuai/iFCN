#include "VerilogHandler.h"
#include "MainWindow.h"
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include <QImage>
#include <QPainter>
#include "widgets/GaChessboardInputDialog.h"
#include <QElapsedTimer>



VerilogHandler::VerilogHandler(MainWindow *parent)
    : QObject(parent), mainWindow(parent)
{

}

void VerilogHandler::handleParseVerilogFile()
{
    //选择加载文件
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), "/home/zjxiao/Projects/fcnx", 
                                                          tr("Verilog files (*.v);;All file (*)"));

    if(filePath.isEmpty()) {
        QString message = "FilePath is empty!";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }else{
        QString message = "open file: " + filePath;
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
    }                                                 

    std::string file = filePath.toStdString();

    //率先解析文件
    fcngraph::Parse parse;
    parse.parseVerilog(file);

    //打印信息使用
    QString Name = filePath;
    int Input = parse.get_input_num();
    int Gates = parse.getm_numVertices() - Input;
    int Output = parse.get_output_num();
    int wires = parse.getm_numEdges() - Output;
    //打印信息使用


    parse.optimizeAIOG_DRC(2,2,2,2,2,2);
    parse.optimizeBufferNode();
    parse.optimizeNOTNode();
    parse.caculateSameLayerNodeRoutePair();

    //打印信息使用
    int RNG = parse.hideNotNodeIndex.size();
    int REG = wires - parse.getEffectiveEdges().size();

    
    //创建全局变量
    QString clockSchemeStr;
    CLOCK_SCHEME scheme;
    int width;
    int height;
    int generationSize;
    int populationsSize;

    // 弹出参数选择框
    GaChessboardInputDialog inputDialog(mainWindow);
    if (inputDialog.exec() == QDialog::Accepted) {
        clockSchemeStr = inputDialog.getClockScheme();
        width = inputDialog.getWidth();
        height = inputDialog.getHeight();
        generationSize = inputDialog.getGeneration();
        populationsSize = inputDialog.getPopulation();

        if (clockSchemeStr == "TDD") {
            scheme = CLOCK_SCHEME::TDD;
        } else if (clockSchemeStr == "USE") {
            scheme = CLOCK_SCHEME::USE;
        } else if (clockSchemeStr == "RES") {
            scheme = CLOCK_SCHEME::RES;
        }
    }else{
        QString message = "GA was cancelled or closed.";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }


    //构建布局空间
    MortonChessboard grid(scheme, {0, 0}, {width, height});
    
    //打印棋盘格信息
    QString message = "Clock Scheme: " + clockSchemeStr + ", Chessboard size: [" + QString::number(width) + " , " + QString::number(height) + "];";
    mainWindow-> printToStatusBar(message);

    //构建A*
    Astar astar(grid);
    
    //构建Ga算法
    GeneticAlgorithm ga(parse, grid, astar, generationSize, populationsSize, 0.9, 0.5);
    
    // //打印每一代的最优解的fitness
    // ga.setFitnessCallback([this](double fitness){
    //     QString message = "Fitness: " + QString::number(fitness);
    //     mainWindow->customStatusBar->addMessage(message);
    //     QCoreApplication::processEvents();
    // });


    // //打印论文表格信息
    // QString info1 = "&" + Name +"     &" +QString::number(Gates) +" &" +QString::number(Input) +" / "+ QString::number(Output) +
    //                 " &" + QString::number(wires);

    // mainWindow-> printToStatusBar(info1);
    // QString info2 = " &"+ QString::number(RNG) + " &"+ QString::number(REG);
    // mainWindow-> printToStatusBar(info2);


    //测试时间
    QElapsedTimer timer;
    timer.start();  // 开始计时

    bool isSuccess = ga.gaRun();

    double elapsedSeconds = timer.elapsed() / 1000.0;
    // QString elapsedStr = "running time : " + QString::number(elapsedSeconds, 'f', 2);
    //测试时间


    


    QString elapsedStr = filePath +
                        " \& " + QString::number(Gates) + 
                        " \& " + QString::number(Input) + " / " + QString::number(Output) + 
                        " \& " + QString::number(wires) + "&  $ \\times$  = &"
                        " \& " + QString::number(RNG) + 
                        " \& " + QString::number(REG) +
                        " \& " + QString::number(width)+ " $\\times$ " + QString::number(height) +
                        " \& " + QString::number(elapsedSeconds, 'f', 1) +"& & & & &   $ \\times$  = &  \\\\";
    //测试时间

    // if(isSuccess){
    //     graph.printLaTex();
    //     QString message =  "Graph layout success! " + elapsedStr + " print LaTeX success!";
    //     mainWindow->customStatusBar->addMessage(message);

    // }else{
    //     QString message = "phase assign fail! But show the graph!";
    //     mainWindow->customStatusBar->addMessage(message);
    //     return;
    // }



    if(!isSuccess) {
        QString message = "gaRun fail;";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }else{
        QString message = "gaRun success;" + elapsedStr;
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
    }

    //映射算法
    std::map<unsigned int ,int> morton_phase;
    for(auto &[morton, cell]: grid.gridMap){
        auto pos = MortonChessboard::decodeMortonCode(morton);
        morton_phase[morton] = grid.getCoorPos_Phase(pos.first, pos.second)-1;
    }

    //创建多层结构，预防交叉线
    mainWindow->slotAddLayer("second layer");
    mainWindow->slotAddLayer("third layer");
    
    while(!isOptimizeNOTNode)
    {
        if(ga.best_individuals.empty())
        {
            QString message = "ga success, but cannot mapping not gate;";
            mainWindow->customStatusBar->addMessage(message);
            QCoreApplication::processEvents();
            break;
        }
        
        QString message = "The " +  QString::number(optimizeNOTNode_time)  + "  times optimizeNOTNode results :";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();

        if (optimizeNOTNode_time !=1)
        {
            ga.best_individuals.pop_back();
        }
        optimizeNOTNode_time++;

        auto node_morton = ga.getNodePos();
        auto nodepair_route = ga.getRoutes();
        ga.printLaTex(CLOCK_SCHEME::USE, {0,0}, {width,height}, node_morton, nodepair_route);

        mappingCellItem(node_morton, nodepair_route, parse, morton_phase);
        putClock(morton_phase);            
    }
    



}

void VerilogHandler::handleGraphRender()
{

    // 选择加载文件
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), "/home/zjxiao/Projects/fcnx", 
                                                          tr("Verilog files (*.v);;All file (*)"));
    if(filePath.isEmpty()) {
        QString message = "FilePath is empty!";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }else{
        QString message = "open file: " + filePath;
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
    }

    std::string file = filePath.toStdString();

    fcngraph::Parse parse;
    parse.parseVerilog(file);

    parse.optimizeAIOG_DRC(2,2,2,2,2,2);

    
    auto gateNum = parse.getm_numVertices();
    auto inputNum = parse.get_input_num();
    auto outputNum = parse.get_output_num();
    auto wireNum = parse.getm_numEdges();

    parse.optimizeBufferNode();
    parse.addLayerRedundancyNode();
    parse.caculateSameLayerNodeRoutePair();
    MortonChessboard chessboard;
    bool isRegularClockScheme = false;
    Astar astar(chessboard, isRegularClockScheme);
    
    // 创建 CircuitGraph 对象并生成图形
    CircuitGraph graph(parse, file, chessboard, astar);

    graph.setFitnessCallback([this](std::string origin_message){
        QString message  = QString::fromStdString(origin_message);
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
    });

    graph.processAndGenerateGraph(true, true, true, true);
    graph.sortNodesByYThenXCoordinate();

    if(graph.placeAndRoute()){
        QString message = "Place and route success!";
        mainWindow->customStatusBar->addMessage(message);

    } else {
        QString message = "Place and route fail!";
        mainWindow->customStatusBar->addMessage(message);
    }

    //测试时间
    QElapsedTimer timer; 
    timer.start();  // 开始计时
    bool isSuccess = graph.assignPhases();
    double elapsedSeconds = timer.elapsed() / 1000.0;
    auto layer = parse.getmax_layer();

    // 计算布局面积你
    int minX = std::numeric_limits<int>::max();
    int maxX = 0;
    int minY = std::numeric_limits<int>::max();
    int maxY = 0;

    for (auto it = chessboard.getGridMap().begin(); it != chessboard.getGridMap().end(); ++it) {
        auto pos = it->first;
        auto x_y = MortonChessboard::decodeMortonCode(pos);
        auto x = x_y.first;
        auto y = x_y.second;

        // 更新最小和最大坐标值
        if (x <= minX) minX = x;
        if (x >= maxX) maxX = x;
        if (y <= minY) minY = y;
        if (y >= maxY) maxY = y;
    }
    int width = maxX - minX - 1; 
    int height = maxY - minY ;

    // auto gateNum = parse.getm_numVertices();
    // auto inputNum = parse.get_input_num();
    // auto outputNum = parse.get_output_num();
    // auto wireNum = parse.getm_numEdges();
    // auto hideNodeNum = parse.hideNotNodeIndex.size();


    QString elapsedStr = filePath + " \& " + QString::number(gateNum) +
                                    " \& " + QString::number(inputNum) + " / " + QString::number(outputNum) +
                                    " \& " + QString::number(wireNum) + 
                                    " \& " + QString::number(width)+ " $\\times$ " + QString::number(height) + " = " + QString::number(width*height) +
                                    " \& " + QString::number(elapsedSeconds, 'f', 1) ;
    //测试时间

    if(isSuccess){
        graph.printLaTex();
        QString message =  "Graph layout success! " + elapsedStr + " print LaTeX success!";
        mainWindow->customStatusBar->addMessage(message);

    }else{
        QString message = "phase assign fail! But show the graph!";
        mainWindow->customStatusBar->addMessage(message);
        return;
    }

    //映射
    mainWindow->slotAddLayer("second layer");
    mainWindow->slotAddLayer("third layer");

    std::map<unsigned int, unsigned int> node_morton;
    for (auto& pair : graph.nodeIndex_morton) {  
        node_morton[static_cast<unsigned int>(pair.first)] = pair.second;  
    } 
    
    std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>> routes = graph.routes;
    std::unordered_map<unsigned int, GridCell> gridCells = chessboard.gridMap;

    unsigned int scale;
    if(!node_morton.empty()) {
        unsigned int max = node_morton.begin()->second;
        for(auto &v : node_morton) {
            unsigned int y = (MortonChessboard::decodeMortonCode(v.second)).second;
            unsigned int ymax = (MortonChessboard::decodeMortonCode(max)).second;
            if(y > ymax) {
                max = v.second;
            }
        }
        auto pos = MortonChessboard::decodeMortonCode(max);
        scale = pos.second + 1;
    }

    std::map<unsigned int, unsigned int> node_morton_trans;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>> routes_trans;
    std::vector<std::pair<unsigned int, unsigned int>> morton_trans; // 坐标转换前后对应
    
    for(auto &v : node_morton) {
        node_morton_trans[v.first] = coordtrans(v.second, scale);
    }
    
    for(auto &v : routes) {
        std::vector<unsigned int> temp = v.second;
        std::vector<unsigned int> temp_trans;
        for(auto & route : temp) {
            temp_trans.push_back(coordtrans(route, scale));
            morton_trans.push_back(std::make_pair(route, coordtrans(route, scale)));
        }
        routes_trans[v.first] = temp_trans;
    }

    std::map<unsigned int ,int> morton_phase;
    for(auto &v : gridCells) {
        unsigned int morton_trans_pos;
        for(auto &pair : morton_trans) {
            if(v.first == pair.first) {
                morton_trans_pos = pair.second;
                morton_phase[morton_trans_pos] = v.second.getPhase()-1;
                break;
            }
        }
    }

    mappingCellItem(node_morton_trans, routes_trans, parse, morton_phase);
    putClock(morton_phase);

}

void VerilogHandler::mappingCellItem(std::map<unsigned int, unsigned int>& _node_morton, 
                                    std::map<std::pair<unsigned int, unsigned int>, 
                                    std::vector<unsigned int>>& _nodepair_route, 
                                    Parse _parse, std::map<unsigned int ,int>& _morton_phase)
{
    Mapping mapping;

    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    for(auto &v: _nodepair_route)
    {
        std::vector<position> unitcell;
        for(auto &morton : v.second)
        {
            position pos = MortonChessboard::decodeMortonCode(morton);
            unitcell.push_back(pos);
        }
        circle_line.push_back(unitcell);
    }
    

    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node,type), (扇入，扇出)>
    Nodelink.clear();


    for(auto &v : _nodepair_route)
    {
        std::vector<unsigned int> templine = v.second;
        std::string startnodeName = _parse.getNodeType(v.first.first);
        position startpos = MortonChessboard::decodeMortonCode(templine.front());
        std::string endnodeName = _parse.getNodeType(v.first.second);
        position endpos = MortonChessboard::decodeMortonCode(templine.back());
        Nodelink[std::make_pair(startpos, startnodeName)];
        Nodelink[std::make_pair(endpos, endnodeName)];
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

    std::vector<position> notcell = {};
    if (!_parse.hide_not_place_pair.empty())
    {
        for (auto &v: _parse.hide_not_place_pair){
            QString message = QString("node gate insert position: (%1 , %2)")
                            .arg(v.second.first)  
                            .arg(v.second.second); 
            mainWindow->customStatusBar->addMessage(message);
        }

        mapping.not_check(circle_line);
        auto noputplace1 = mapping.temppos_list_examp;
        auto noputplace2 = mapping.oneroutepos_list_examp;
        std::vector<position> crosspos = {};//将所有线路中格子容量已满的保存
        for (auto &line : noputplace1)
        {
            crosspos.push_back(line.second);
        }
        for (auto &line : noputplace2)
        {
            crosspos.push_back(line.second);
        }

        std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>> not_routes;
        for(auto &line : _parse.hide_not_place_pair)
        {
            not_routes[line.second] = _nodepair_route[line.second];
        }
        
        std::vector<std::vector<position>> not_line;//放置not的线路（结构同circle_line）
        //std::map<position, int> startpos_num;
        std::map<position, std::vector<std::vector<position>>> startpos_line;
        for(auto &v: not_routes)
        {
            std::vector<position> unitcell;
            for(auto &morton : v.second)
            {
                position pos = MortonChessboard::decodeMortonCode(morton);
                unitcell.push_back(pos);
            }
            not_line.push_back(unitcell);

            if (startpos_line.find(unitcell.front()) == startpos_line.end()) 
            {  
                startpos_line[unitcell.front()] = {}; 
                startpos_line[unitcell.front()].push_back(unitcell);
            } else 
            {  
                startpos_line[unitcell.front()].push_back(unitcell);
            }
        }
        
        //std::vector<std::vector<position>> not_line_used;
        for(auto &line : startpos_line)
        {
            if(line.second.size() == 1)
            {
                //避免非门插入到复用线路上
                std::vector<std::vector<position>> samestartpos_routes = {};
                std::vector<position> reusepos = {};
                for (auto &v : circle_line)
                {
                    if (line.first == v.front())
                    {
                        samestartpos_routes.push_back(v);
                    }
                }
                if (samestartpos_routes.size() == 2)
                {
                    int i = 0;
                    while(samestartpos_routes.front()[i] == samestartpos_routes.back()[i])
                    {
                        reusepos.push_back(samestartpos_routes.front()[i]);
                        i++;
                    }
                }
                
                auto single_route = line.second.front();
                for (auto it = single_route.begin(); it != single_route.end(); it++)
                {
                    if (*it == single_route.front())
                    {
                        continue;
                    }
                    else if (*it == single_route.back())
                    {
                        QString message = "NOT gate put fail!";
                        mainWindow->customStatusBar->addMessage(message);
                        return;
                    }
                    else
                    {
                        if ((std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                        ||(std::find(reusepos.begin(), reusepos.end(), *it) != reusepos.end()))
                        {
                            continue;
                        }
                        else
                        {
                            position prevpos = *(std::prev(it));
                            position nextpos = *(std::next(it));
                            Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                            Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                            Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                            notcell.push_back(*it);

                            std::vector<position> front_line;
                            std::vector<position> back_line;
                            front_line.insert(front_line.end(), single_route.begin(), std::next(it));
                            back_line.insert(back_line.end(), it, single_route.end());
                            
                            auto it1 = std::find(circle_line.begin(), circle_line.end(), single_route);
                            circle_line.insert(it1, front_line);
                            auto it2 = std::find(circle_line.begin(), circle_line.end(), single_route);
                            circle_line.insert(it2, back_line);
                            auto delete_line = std::find(circle_line.begin(), circle_line.end(), single_route);
                            circle_line.erase(delete_line);
                            
                            // circle_line.push_back(front_line);
                            // circle_line.push_back(back_line);

                            break;
                        }
                    }
                }
                
            }
            else//line.second.size() == 2
            {
                auto route1 = line.second.front();
                auto route2 = line.second.back();
                int i = 0;
                while ((i < route1.size()) && (i < route2.size()) && (route1[i] == route2[i]))
                {
                    ++i;
                }
                int fanout = i-1;
                
                if (fanout == 0)//无复用线路
                {
                    for (auto it = route1.begin(); it != route1.end(); it++)
                    {
                        if (*it == route1.front())
                        {
                            continue;
                        }
                        else if (*it == route1.back())
                        {
                            QString message = "NOT gate put fail!";
                            mainWindow->customStatusBar->addMessage(message);
                            return;
                        }
                        else
                        {
                            if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                            {
                                continue;
                            }
                            else
                            {
                                position prevpos = *(std::prev(it));
                                position nextpos = *(std::next(it));
                                Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                notcell.push_back(*it);

                                std::vector<position> front_line;
                                std::vector<position> back_line;
                                front_line.insert(front_line.end(), route1.begin(), std::next(it));
                                back_line.insert(back_line.end(), it, route1.end());
                                
                                auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                circle_line.insert(it1, front_line);
                                auto it2 = std::find(circle_line.begin(), circle_line.end(), route1);
                                circle_line.insert(it2, back_line);
                                auto delete_line = std::find(circle_line.begin(), circle_line.end(), route1);
                                circle_line.erase(delete_line);

                                break;
                            }
                        }
                    }

                    for (auto it = route2.begin(); it != route2.end(); it++)
                    {
                        if (*it == route2.front())
                        {
                            continue;
                        }
                        else if (*it == route2.back())
                        {
                            QString message = "NOT gate put fail!";
                            mainWindow->customStatusBar->addMessage(message);
                            return;
                        }
                        else
                        {
                            if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                            {
                                continue;
                            }
                            else
                            {
                                position prevpos = *(std::prev(it));
                                position nextpos = *(std::next(it));
                                Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                notcell.push_back(*it);

                                std::vector<position> front_line;
                                std::vector<position> back_line;
                                front_line.insert(front_line.end(), route2.begin(), std::next(it));
                                back_line.insert(back_line.end(), it, route2.end());
                                
                                auto it1 = std::find(circle_line.begin(), circle_line.end(), route2);
                                circle_line.insert(it1, front_line);
                                auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                circle_line.insert(it2, back_line);
                                auto delete_line = std::find(circle_line.begin(), circle_line.end(), route2);
                                circle_line.erase(delete_line);

                                break;
                            }
                        }
                    }
                }
                else//有复用线路，非门优先放置于扇出点
                {
                    auto fanout1 = std::find(route1.begin(), route1.end(), route1[fanout]);
                    auto fanout2 = std::find(route2.begin(), route2.end(), route2[fanout]);
                    position prevpos1 = *(std::prev(fanout1));
                    position prevpos2 = *(std::prev(fanout2));
                    position nextpos1 = *(std::next(fanout1));
                    position nextpos2 = *(std::next(fanout2));
                    if ((prevpos1 == prevpos2)&&(nextpos1 != nextpos2))
                    {
                        if (std::find(crosspos.begin(), crosspos.end(), *fanout1) == crosspos.end())
                        {
                            Nodelink[std::make_pair(*fanout1, "not")] = {{}, {}};
                            Nodelink[std::make_pair(*fanout1, "not")].first.push_back(prevpos1);
                            Nodelink[std::make_pair(*fanout1, "not")].second.push_back(nextpos1);
                            Nodelink[std::make_pair(*fanout1, "not")].second.push_back(nextpos2);
                            notcell.push_back(*fanout1);

                            std::vector<position> reuse_route = {};
                            std::vector<position> route1_back = {};
                            std::vector<position> route2_back = {};
                            
                            reuse_route.insert(reuse_route.end(), route1.begin(), std::next(fanout1));
                            route1_back.insert(route1_back.end(), fanout1, route1.end());
                            route2_back.insert(route2_back.end(), fanout2, route2.end());

                            auto it0 = std::find(circle_line.begin(), circle_line.end(), route1);
                            circle_line.insert(it0, reuse_route);
                            auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                            circle_line.insert(it1, route1_back);
                            auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                            circle_line.insert(it2, route2_back);
                            auto delete_line1 = std::find(circle_line.begin(), circle_line.end(), route1);
                            circle_line.erase(delete_line1);
                            auto delete_line2 = std::find(circle_line.begin(), circle_line.end(), route2);
                            circle_line.erase(delete_line2);
                        }
                        else//扇出点有交叉线不可插入not
                        {
                            bool reusenot = false;
                            for (int i = 1; i < fanout; i++)
                            {
                                if (std::find(crosspos.begin(), crosspos.end(), route1[i]) != crosspos.end())
                                {
                                    continue;
                                }
                                else
                                {
                                    Nodelink[std::make_pair(route1[i], "not")] = {{}, {}};
                                    Nodelink[std::make_pair(route1[i], "not")].first.push_back(route1[i-1]);
                                    Nodelink[std::make_pair(route1[i], "not")].second.push_back(route1[i+1]);
                                    notcell.push_back(route1[i]);
                                    
                                    std::vector<position> reuse_route = {};
                                    std::vector<position> route1_back = {};
                                    std::vector<position> route2_back = {};
                                    auto notpos1 = std::find(route1.begin(), route1.end(), route1[i]);
                                    auto notpos2 = std::find(route2.begin(), route2.end(), route2[i]);
                                    reuse_route.insert(reuse_route.end(), route1.begin(), std::next(notpos1));
                                    route1_back.insert(route1_back.end(), notpos1, route1.end());
                                    route2_back.insert(route2_back.end(), notpos2, route2.end());

                                    auto it0 = std::find(circle_line.begin(), circle_line.end(), route1);
                                    circle_line.insert(it0, reuse_route);
                                    auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                    circle_line.insert(it1, route1_back);
                                    auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                    circle_line.insert(it2, route2_back);
                                    auto delete_line1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                    circle_line.erase(delete_line1);
                                    auto delete_line2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                    circle_line.erase(delete_line2);

                                    reusenot = true;
                                    break;
                                }
                            }
                            if (!reusenot)//复用线路里无法插入not
                            {
                                std::vector<position> route1_back = {};
                                std::vector<position> route2_back = {};
                                route1_back.insert(route1_back.end(), fanout1, route1.end());
                                route2_back.insert(route2_back.end(), fanout2, route2.end());
                                
                                for (auto it = route1_back.begin(); it != route1_back.end(); it++)
                                {
                                    if (*it == route1_back.front())
                                    {
                                        continue;
                                    }
                                    else if (*it == route1_back.back())
                                    {
                                        QString message = "NOT gate put fail!";
                                        mainWindow->customStatusBar->addMessage(message);
                                        return;
                                    }
                                    else
                                    {
                                        if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                                        {
                                            continue;
                                        }
                                        else
                                        {
                                            position prevpos = *(std::prev(it));
                                            position nextpos = *(std::next(it));
                                            Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                            Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                            Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                            notcell.push_back(*it);

                                            std::vector<position> front_line;
                                            std::vector<position> back_line;
                                            auto itpos = std::find(route1.begin(), route1.end(), *it);
                                            front_line.insert(front_line.end(), route1.begin(), std::next(itpos));
                                            back_line.insert(back_line.end(), itpos, route1.end());
                                            
                                            auto it1 = std::find(circle_line.begin(), circle_line.end(), route1);
                                            circle_line.insert(it1, front_line);
                                            auto it2 = std::find(circle_line.begin(), circle_line.end(), route1);
                                            circle_line.insert(it2, back_line);
                                            auto delete_line = std::find(circle_line.begin(), circle_line.end(), route1);
                                            circle_line.erase(delete_line);

                                            break;
                                        }
                                    }
                                }

                                for (auto it = route2_back.begin(); it != route2_back.end(); it++)
                                {
                                    if (*it == route2_back.front())
                                    {
                                        continue;
                                    }
                                    else if (*it == route2_back.back())
                                    {
                                        QString message = "NOT gate put fail!";
                                        mainWindow->customStatusBar->addMessage(message);
                                        return;
                                    }
                                    else
                                    {
                                        if (std::find(crosspos.begin(), crosspos.end(), *it) != crosspos.end())
                                        {
                                            continue;
                                        }
                                        else
                                        {
                                            position prevpos = *(std::prev(it));
                                            position nextpos = *(std::next(it));
                                            Nodelink[std::make_pair(*it, "not")] = {{}, {}};
                                            Nodelink[std::make_pair(*it, "not")].first.push_back(prevpos);
                                            Nodelink[std::make_pair(*it, "not")].second.push_back(nextpos);
                                            notcell.push_back(*it);

                                            std::vector<position> front_line;
                                            std::vector<position> back_line;
                                            auto itpos = std::find(route2.begin(), route2.end(), *it);
                                            front_line.insert(front_line.end(), route2.begin(), std::next(itpos));
                                            back_line.insert(back_line.end(), itpos, route2.end());
                                            
                                            auto it1 = std::find(circle_line.begin(), circle_line.end(), route2);
                                            circle_line.insert(it1, front_line);
                                            auto it2 = std::find(circle_line.begin(), circle_line.end(), route2);
                                            circle_line.insert(it2, back_line);
                                            auto delete_line = std::find(circle_line.begin(), circle_line.end(), route2);
                                            circle_line.erase(delete_line);

                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        QString message = "Fanout_NOT gate put fail!";
                        mainWindow->customStatusBar->addMessage(message);
                        return;
                    }
                }
            }

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
                unsigned int mortoncode = MortonChessboard::calculateMortonCode(x_node, y_node);
                QString Iname = "default";
                for (auto &v : _node_morton)
                {
                    if (mortoncode == v.second)
                    {
                        std::string index = _parse.getVertexName(v.first);  
                        Iname = QString::fromStdString(index);
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::InputCell, _morton_phase, Iname);
                
            }
        }
        else if (cell.first == "output")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                unsigned int mortoncode = MortonChessboard::calculateMortonCode(x_node, y_node);
                QString Oname = "default";
                for (auto &v : _node_morton)
                {
                    if (mortoncode == v.second)
                    {
                        std::string index = _parse.getVertexName(v.first);  
                        Oname = QString::fromStdString(index);
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::OutputCell, _morton_phase, Oname);
                
            }
        }
        else if (cell.first == "normal")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::NormalCell, _morton_phase);
                
            }
        }
        else if (cell.first == "fix0")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_0, _morton_phase);
                
            }
        }
        else if (cell.first == "fix1")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_1, _morton_phase);
                
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
                            putCellItem(cellpos, 2, CellType::CrossoverCell, _morton_phase);
                            
                        } 

                        if(count < 2) 
                        {
                            //若端点无法直接放置柱点，则跨时钟延伸两个单位元胞
                            if((std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir3) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir4) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, _morton_phase);


                                position cellpos2 = dir1;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, _morton_phase);


                                position cellpos3 = {dir1.first, dir1.second + 1};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, _morton_phase);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, _morton_phase);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, _morton_phase);
                                verticalcell.push_back(cellpos3);


                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else if ((std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir1) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir2) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, _morton_phase);


                                position cellpos2 = dir4;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, _morton_phase);


                                position cellpos3 = {dir4.first + 1, dir4.second};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, _morton_phase);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, _morton_phase);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, _morton_phase);
                                verticalcell.push_back(cellpos3);

                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else//放置交叉线端点三层柱点
                            {
                                position cellpos = *unit;
                                putCellItem(cellpos, 0, CellType::VerticalCell, _morton_phase);
                                putCellItem(cellpos, 1, CellType::VerticalCell, _morton_phase);
                                putCellItem(cellpos, 2, CellType::VerticalCell, _morton_phase);
                                verticalcell.push_back(cellpos);
                            }
                        }
                    }
                    else
                    {
                        position cellpos = *unit;
                        putCellItem(cellpos, 2, CellType::CrossoverCell, _morton_phase);
                        
                        
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
                        putCellItem(pos, 0, CellType::NormalCell, _morton_phase);
                        
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
                                putCellItem(pos, 0, CellType::NormalCell, _morton_phase);
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

    isOptimizeNOTNode = true;
}

unsigned int VerilogHandler::coordtrans(unsigned int morton, unsigned int scale)
{
    auto pos = MortonChessboard::decodeMortonCode(morton);
    unsigned int x = pos.first;
    unsigned int y = pos.second;
    unsigned int ytrans = scale-y;
    unsigned int mortontrans = MortonChessboard::calculateMortonCode(x, ytrans);
    return mortontrans;
}

void VerilogHandler::putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<unsigned int ,int>& _morton_phase, QString _name)
{
    int x_node = _cellpos.first / 5;
    int y_node = _cellpos.second / 5;
    int x_coord = _cellpos.first*20 + 200;  // 坐标
    int y_coord = _cellpos.second*20 + 200;
    int cell_layer = _celllayer;
    unsigned int mortonpos = MortonChessboard::calculateMortonCode(x_node, y_node);
    int phase = _morton_phase[mortonpos];
    
    QCADCellItem *cellItem = new QCADCellItem(x_coord, y_coord, cell_layer, phase, _cellType, _name);
    mainWindow->checkCellInserted(mainWindow->layers, cellItem, cell_layer, x_coord, y_coord);
}

void VerilogHandler::putClock(std::map<unsigned int ,int>& _morton_phase)
{
    for(auto &v : _morton_phase)
    {
        auto pos = MortonChessboard::decodeMortonCode(v.first);
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

void VerilogHandler::generateSVG()
{
    // 获取所有Item的联合边界矩形
    QRectF itemsBoundingRect = mainWindow->scene->itemsBoundingRect();

    // 定义输出图像的大小，这里根据实际内容调整大小
    QSize imageSize = itemsBoundingRect.size().toSize();

    // 创建一个QImage对象用于保存PNG文件
    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent); // 透明背景

    // 使用QPainter将场景内容绘制到QImage上
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    // 渲染场景内容（不进行平移）
    mainWindow->scene->render(&painter, QRectF(), itemsBoundingRect);

    // 保存为PNG文件
    image.save("cell_level_layout.png");

    //打印信息
    QString message = "The cell-level layout file saved as \"cell_level_layout.png\";";
    mainWindow->printToStatusBar(message);

}
