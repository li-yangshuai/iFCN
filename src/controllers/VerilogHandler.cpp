#include "VerilogHandler.h"
#include "ui/mainwindow/MainWindow.h"
#include <QFileDialog>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QImage>
#include <QPainter>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include "ui/widgets/GaChessboardInputDialog.h"
#include <QElapsedTimer>
#include <cmath>
#include <limits>
#include <optional>
#include <tuple>

namespace {
struct GraphRenderSettings {
    int phaseCount = 4;
    int maxAttempts = 220;
};

struct LayoutAttempt {
    unsigned int xSpacing = 4;
    unsigned int ySpacing = 4;
    double searchCost = 90.0;
};

struct LayoutBounds {
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int width = 0;
    int height = 0;
    int area = 0;
};

struct PhaseRepeatStats {
    int repeatedAdjacent = 0;
    int maxRun = 1;
};

struct LayoutSearchResult {
    LayoutBounds bounds;
    int routeLength = 0;
    PhaseRepeatStats phaseRepeats;
    unsigned int xSpacing = 0;
    unsigned int ySpacing = 0;
    double searchCost = 0.0;
    std::map<int, fcngraph::position> nodePositions;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> routes;
    std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> gridCells;
};

bool readGraphRenderSettings(QWidget *parent, GraphRenderSettings &settings)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Graph Render Options"));

    auto *phaseCombo = new QComboBox(&dialog);
    phaseCombo->addItem(QObject::tr("4-phase"), 4);
    phaseCombo->addItem(QObject::tr("3-phase"), 3);
    phaseCombo->setCurrentIndex(0);

    auto *attemptSpin = new QSpinBox(&dialog);
    attemptSpin->setRange(8, 300);
    attemptSpin->setValue(settings.maxAttempts);
    attemptSpin->setSingleStep(8);

    auto *form = new QFormLayout(&dialog);
    form->addRow(QObject::tr("Phase assignment:"), phaseCombo);
    form->addRow(QObject::tr("Search attempts:"), attemptSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    settings.phaseCount = phaseCombo->currentData().toInt();
    settings.maxAttempts = attemptSpin->value();
    return true;
}

std::vector<LayoutAttempt> buildLayoutAttempts()
{
    std::vector<LayoutAttempt> attempts;
    for (unsigned int ySpacing = 2; ySpacing <= 12; ++ySpacing) {
        for (unsigned int xSpacing = 2; xSpacing <= 12; ++xSpacing) {
            attempts.push_back({xSpacing, ySpacing, 90.0});
            if (xSpacing <= 8 && ySpacing <= 8) {
                attempts.push_back({xSpacing, ySpacing, 150.0});
            }
            if (xSpacing >= 7 || ySpacing >= 7) {
                attempts.push_back({xSpacing, ySpacing, 240.0});
            }
        }
    }

    std::sort(attempts.begin(), attempts.end(), [](const LayoutAttempt &lhs, const LayoutAttempt &rhs) {
        const auto lhsArea = lhs.xSpacing * lhs.ySpacing;
        const auto rhsArea = rhs.xSpacing * rhs.ySpacing;
        if (lhsArea != rhsArea) {
            return lhsArea < rhsArea;
        }
        if (lhs.searchCost != rhs.searchCost) {
            return lhs.searchCost < rhs.searchCost;
        }
        if (lhs.ySpacing != rhs.ySpacing) {
            return lhs.ySpacing < rhs.ySpacing;
        }
        return lhs.xSpacing < rhs.xSpacing;
    });
    return attempts;
}

std::optional<LayoutBounds> calculateGridBounds(
    const std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> &gridCells)
{
    bool hasCell = false;
    unsigned int minX = std::numeric_limits<unsigned int>::max();
    unsigned int minY = std::numeric_limits<unsigned int>::max();
    unsigned int maxX = 0;
    unsigned int maxY = 0;

    for (const auto &entry : gridCells) {
        const auto &cell = entry.second;
        if (cell.get_current_weight() == 0 && cell.getPhase() == -1) {
            continue;
        }
        hasCell = true;
        minX = std::min(minX, entry.first.first);
        minY = std::min(minY, entry.first.second);
        maxX = std::max(maxX, entry.first.first);
        maxY = std::max(maxY, entry.first.second);
    }

    if (!hasCell) {
        return std::nullopt;
    }

    LayoutBounds bounds;
    bounds.minX = static_cast<int>(minX);
    bounds.maxX = static_cast<int>(maxX);
    bounds.minY = static_cast<int>(minY);
    bounds.maxY = static_cast<int>(maxY);
    bounds.width = bounds.maxX - bounds.minX + 1;
    bounds.height = bounds.maxY - bounds.minY + 1;
    bounds.area = bounds.width * bounds.height;
    return bounds;
}

int totalRouteLength(const std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> &routes)
{
    int length = 0;
    for (const auto &route : routes) {
        length += static_cast<int>(route.second.size());
    }
    return length;
}

PhaseRepeatStats calculatePhaseRepeatStats(
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> &routes,
    const std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> &gridCells)
{
    PhaseRepeatStats stats;
    for (const auto &route : routes) {
        int previousPhase = -1;
        int currentRun = 1;
        for (const auto &pos : route.second) {
            auto cell = gridCells.find(pos);
            const int phase = (cell != gridCells.end()) ? cell->second.getPhase() : -1;
            if (phase >= 1 && phase == previousPhase) {
                ++stats.repeatedAdjacent;
                ++currentRun;
                stats.maxRun = std::max(stats.maxRun, currentRun);
            } else {
                currentRun = 1;
            }
            previousPhase = phase;
        }
    }
    return stats;
}

bool isBetterLayout(const LayoutSearchResult &candidate,
                    const LayoutSearchResult &currentBest,
                    int phaseCount)
{
    const int allowedRun = std::max(4, phaseCount * 2);
    const bool candidateAllowed = candidate.phaseRepeats.maxRun <= allowedRun;
    const bool currentAllowed = currentBest.phaseRepeats.maxRun <= allowedRun;

    if (candidateAllowed != currentAllowed) {
        return candidateAllowed;
    }

    if (candidateAllowed) {
        return std::tie(candidate.bounds.area,
                        candidate.routeLength,
                        candidate.phaseRepeats.maxRun,
                        candidate.phaseRepeats.repeatedAdjacent,
                        candidate.bounds.width,
                        candidate.bounds.height)
             < std::tie(currentBest.bounds.area,
                        currentBest.routeLength,
                        currentBest.phaseRepeats.maxRun,
                        currentBest.phaseRepeats.repeatedAdjacent,
                        currentBest.bounds.width,
                        currentBest.bounds.height);
    }

    const auto score = [allowedRun](const LayoutSearchResult &layout) {
        const int excessiveRun = std::max(0, layout.phaseRepeats.maxRun - allowedRun);
        return static_cast<long long>(layout.bounds.area)
             + static_cast<long long>(excessiveRun) * 800
             + static_cast<long long>(layout.phaseRepeats.repeatedAdjacent) * 2
             + static_cast<long long>(layout.routeLength) / 4;
    };

    const auto candidateScore = score(candidate);
    const auto currentScore = score(currentBest);
    if (candidateScore != currentScore) {
        return candidateScore < currentScore;
    }

    return std::tie(candidate.bounds.area,
                    candidate.phaseRepeats.maxRun,
                    candidate.phaseRepeats.repeatedAdjacent,
                    candidate.routeLength,
                    candidate.bounds.width,
                    candidate.bounds.height)
         < std::tie(currentBest.bounds.area,
                    currentBest.phaseRepeats.maxRun,
                    currentBest.phaseRepeats.repeatedAdjacent,
                    currentBest.routeLength,
                    currentBest.bounds.width,
                    currentBest.bounds.height);
}
}



VerilogHandler::VerilogHandler(MainWindow *parent)
    : QObject(parent), mainWindow(parent)
{

}

void VerilogHandler::handleParseVerilogFile()
{
    //选择加载文件
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), "/home/lys/projects/github/iFCN", 
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
    GridChessboard grid(scheme, {0, 0}, {width, height});
    
    //打印棋盘格信息
    QString message = "Clock Scheme: " + clockSchemeStr + ", Chessboard size: [" + QString::number(width) + " , " + QString::number(height) + "];";
    mainWindow-> printToStatusBar(message);

    //构建A*
    Astar astar(grid);
    
    //构建Ga算法
    GeneticAlgorithm ga(parse, grid, astar, generationSize, populationsSize, 0.9, 0.5);
    

    //测试时间
    QElapsedTimer timer;
    timer.start();  // 开始计时

    bool isSuccess = ga.gaRun();

    double elapsedSeconds = timer.elapsed() / 1000.0;

    QString elapsedStr = filePath +
                        " \& " + QString::number(Gates) + 
                        " \& " + QString::number(Input) + " / " + QString::number(Output) + 
                        " \& " + QString::number(wires) + "&  $ \\times$  = &"
                        " \& " + QString::number(RNG) + 
                        " \& " + QString::number(REG) +
                        " \& " + QString::number(width)+ " $\\times$ " + QString::number(height) +
                        " \& " + QString::number(elapsedSeconds, 'f', 1) +"& & & & &   $ \\times$  = &  \\\\";

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
    std::map<position, int> pos_phase;
    for(auto &[pos, cell]: grid.gridMap){
        pos_phase[pos] = grid.getCoorPos_Phase(pos.first, pos.second)-1;
    }

    //创建多层结构，预防交叉线
    mainWindow->beginSceneBatchUpdate();
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

        auto node_pos = ga.getNodePos();
        auto nodepair_route = ga.getRoutes();
       
        
        mappingCellItem(node_pos, nodepair_route, parse, pos_phase);
        putClock(pos_phase);      
        auto crossPos = ga.getCrossPos();
        ga.printLaTex(CLOCK_SCHEME::USE, {0,0}, {width,height}, node_pos, nodepair_route, crossPos);      
    }
    mainWindow->endSceneBatchUpdate(true);

}

void VerilogHandler::handleGraphRender()
{

    // 选择加载文件
    QString filePath = QFileDialog::getOpenFileName(mainWindow, tr("打开文件"), "/home/lys/projects/github/iFCN", 
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

    GraphRenderSettings settings;
    if (!readGraphRenderSettings(mainWindow, settings)) {
        QString message = "Graph render was cancelled.";
        mainWindow->customStatusBar->addMessage(message);
        QCoreApplication::processEvents();
        return;
    }

    std::string file = filePath.toStdString();

    fcngraph::Parse parse;
    parse.parseVerilog(file);

    try {
        parse.optimizeAIOG_DRC(2,2,2,2,2,2);

        auto gateNum = parse.getm_numVertices();
        auto inputNum = parse.get_input_num();
        auto outputNum = parse.get_output_num();
        auto wireNum = parse.getm_numEdges();

        parse.optimizeBufferNode();
        // parse.addLayerRedundancyNode();
        parse.caculateSameLayerNodeRoutePair();

        emit operationStarted(tr("Graph placement and routing"),
                              tr("Preparing compact layout search for %1-phase assignment")
                                  .arg(settings.phaseCount));
        QCoreApplication::processEvents();

        //测试时间
        QElapsedTimer timer; 
        timer.start();  // 开始计时

        std::optional<LayoutSearchResult> bestLayout;
        std::string lastFailure;
        const auto attempts = buildLayoutAttempts();
        const int attemptLimit = std::min(settings.maxAttempts, static_cast<int>(attempts.size()));

        for (int attemptIndex = 0; attemptIndex < attemptLimit; ++attemptIndex) {
            const auto &attempt = attempts[attemptIndex];
            GridChessboard chessboard;
            Astar astar(chessboard, false, attempt.searchCost);
            CircuitGraph graph(parse, file, chessboard, astar);

            {
                QString progress = QString("Candidate %1/%2: spacing=(%3,%4), search cost=%5, phase=%6")
                    .arg(attemptIndex + 1)
                    .arg(attemptLimit)
                    .arg(attempt.xSpacing)
                    .arg(attempt.ySpacing)
                    .arg(attempt.searchCost)
                    .arg(settings.phaseCount);
                if (bestLayout.has_value()) {
                    progress += QString("; best=%1x%2 area=%3 route=%4 max repeat=%5")
                                    .arg(bestLayout->bounds.width)
                                    .arg(bestLayout->bounds.height)
                                    .arg(bestLayout->bounds.area)
                                    .arg(bestLayout->routeLength)
                                    .arg(bestLayout->phaseRepeats.maxRun);
                }
                emit operationProgress(progress, attemptIndex + 1, attemptLimit);
                QCoreApplication::processEvents();
            }

            try {
                graph.processAndGenerateGraph(attemptIndex == 0, true, true, true);
                graph.sortNodesByLayeredGrid(attempt.xSpacing, attempt.ySpacing);

                if (!graph.placeAndRoute()) {
                    lastFailure = "route failed";
                    continue;
                }

                if (!graph.assignPhases(settings.phaseCount)) {
                    lastFailure = "phase assignment failed";
                    continue;
                }

                const auto bounds = calculateGridBounds(chessboard.getGridMap());
                if (!bounds.has_value()) {
                    lastFailure = "empty layout";
                    continue;
                }

                LayoutSearchResult result;
                result.bounds = bounds.value();
                result.routeLength = totalRouteLength(graph.routes);
                result.phaseRepeats = calculatePhaseRepeatStats(graph.routes, chessboard.getGridMap());
                result.xSpacing = attempt.xSpacing;
                result.ySpacing = attempt.ySpacing;
                result.searchCost = attempt.searchCost;
                result.nodePositions = graph.nodeIndex_pos;
                result.routes = graph.routes;
                result.gridCells = chessboard.getGridMap();

                const bool isBetter = !bestLayout.has_value()
                    || isBetterLayout(result, bestLayout.value(), settings.phaseCount);
                if (isBetter) {
                    bestLayout = std::move(result);
                    QString message = QString("Candidate %1/%2 success: %3x%4=%5, phase=%6, spacing=(%7,%8), max repeat=%9")
                        .arg(attemptIndex + 1)
                        .arg(attemptLimit)
                        .arg(bestLayout->bounds.width)
                        .arg(bestLayout->bounds.height)
                        .arg(bestLayout->bounds.area)
                        .arg(settings.phaseCount)
                        .arg(bestLayout->xSpacing)
                        .arg(bestLayout->ySpacing)
                        .arg(bestLayout->phaseRepeats.maxRun);
                    emit operationProgress(message, attemptIndex + 1, attemptLimit);
                    QCoreApplication::processEvents();
                }
            } catch (const std::exception &ex) {
                lastFailure = ex.what();
                continue;
            }
        }

        if (!bestLayout.has_value()) {
            QString message = QString("Place, route, and phase assignment failed after %1 attempts. Last failure: %2")
                .arg(attemptLimit)
                .arg(QString::fromStdString(lastFailure));
            emit operationFailed(message);
            QCoreApplication::processEvents();
            return;
        }

        double elapsedSeconds = timer.elapsed() / 1000.0;
        int width = bestLayout->bounds.width;
        int height = bestLayout->bounds.height;

        QString elapsedStr = filePath + " \& " + QString::number(gateNum) +
                                        " \& " + QString::number(inputNum) + " / " + QString::number(outputNum) +
                                        " \& " + QString::number(wireNum) + 
                                        " \& " + QString::number(width)+ " $\\times$ " + QString::number(height) + " = " + QString::number(width*height) +
                                        " \& phase " + QString::number(settings.phaseCount) +
                                        " \& " + QString::number(elapsedSeconds, 'f', 1) ;
        //测试时间

        QString message =  "Graph layout success! " + elapsedStr +
                           QString(" ; best spacing=(%1,%2), route length=%3")
                               .arg(bestLayout->xSpacing)
                               .arg(bestLayout->ySpacing)
                               .arg(bestLayout->routeLength) +
                           QString(" ; max phase repeat=%1")
                               .arg(bestLayout->phaseRepeats.maxRun);
        emit operationFinished(message);
        QCoreApplication::processEvents();

        //映射
        mainWindow->beginSceneBatchUpdate();
        mainWindow->slotAddLayer("second layer");
        mainWindow->slotAddLayer("third layer");

        std::map<unsigned int, position> node_pos;
        for (auto& pair : bestLayout->nodePositions) {
            node_pos[static_cast<unsigned int>(pair.first)] = pair.second;  
        } 
        
        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes = bestLayout->routes;
        std::unordered_map<position, GridCell, PositionHash> gridCells = bestLayout->gridCells;

        unsigned int scale = 0;
        if(!node_pos.empty()) {
            position max = node_pos.begin()->second;
            for(auto &v : node_pos) {
                unsigned int y = v.second.second;
                unsigned int ymax = max.second;
                if(y > ymax) {
                    max = v.second;
                }
            }
            scale = max.second + 1;
        } else {
            throw std::runtime_error("No node positions generated");
        }

        std::map<unsigned int, position> node_pos_trans;
        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes_trans;
        std::vector<std::pair<position, position>> pos_trans; // 坐标转换前后对应
        
        for(auto &v : node_pos) {
            node_pos_trans[v.first] = coordtrans(v.second, scale);
            pos_trans.push_back(std::make_pair(v.second, node_pos_trans[v.first]));
        }
        
        for(auto &v : routes) {
            std::vector<position> temp = v.second;
            std::vector<position> temp_trans;
            for(auto & route : temp) {
                temp_trans.push_back(coordtrans(route, scale));
                pos_trans.push_back(std::make_pair(route, coordtrans(route, scale)));
            }
            routes_trans[v.first] = temp_trans;
        }

        std::map<position, int> pos_phase;
        for(auto &v : gridCells) {
            if (v.second.getPhase() < 1) {
                continue;
            }
            for(auto &pair : pos_trans) {
                if(v.first == pair.first) {
                    pos_phase[pair.second] = v.second.getPhase()-1;
                    break;
                }
            }
        }

        mappingCellItem(node_pos_trans, routes_trans, parse, pos_phase);
        putClock(pos_phase);
        mainWindow->endSceneBatchUpdate(true);
    } catch (const std::exception &ex) {
        mainWindow->endSceneBatchUpdate(false);
        QString message = QString("布局布线失败: %1").arg(ex.what());
        emit operationFailed(message);
        QCoreApplication::processEvents();
        return;
    }

}

void VerilogHandler::mappingCellItem(std::map<unsigned int, position>& _node_pos, 
                                    std::map<std::pair<unsigned int, unsigned int>, 
                                    std::vector<position>>& _nodepair_route, 
                                    Parse _parse, std::map<position, int>& _pos_phase)
{
    Mapping mapping;

    std::vector<std::vector<position>> circle_line;
    circle_line.clear();
    for(auto &v: _nodepair_route)
    {
        std::vector<position> unitcell;
        for(auto &pos : v.second)
        {
            unitcell.push_back(pos);
        }
        circle_line.push_back(unitcell);
    }
    

    std::map<std::pair<position, std::string>, std::pair<std::vector<position>, std::vector<position>>> Nodelink;//map<(node,type), (扇入，扇出)>
    Nodelink.clear();


    for(auto &v : _nodepair_route)
    {
        std::vector<position> templine = v.second;
        std::string startnodeName = _parse.getNodeType(v.first.first);
        position startpos = templine.front();
        std::string endnodeName = _parse.getNodeType(v.first.second);
        position endpos = templine.back();
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

        std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> not_routes;
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
            for(auto &pos : v.second)
            {
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
                position node_pos = {x_node, y_node};
                QString Iname = "default";
                for (auto &v : _node_pos)
                {
                    if (node_pos == v.second)
                    {
                        std::string index = _parse.getVertexName(v.first);  
                        Iname = QString::fromStdString(index);
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::InputCell, _pos_phase, Iname);          
            }
        }
        else if (cell.first == "output")
        {
            for(auto &cellpos : cellpos_list)
            {
                unsigned int x_node = cellpos.first / 5;
                unsigned int y_node = cellpos.second / 5;
                position node_pos = {x_node, y_node};
                QString Oname = "default";
                for (auto &v : _node_pos)
                {
                    if (node_pos == v.second)
                    {
                        std::string index = _parse.getVertexName(v.first);  
                        Oname = QString::fromStdString(index);
                        break;
                    }
                }
                putCellItem(cellpos, 0, CellType::OutputCell, _pos_phase, Oname);
                
            }
        }
        else if (cell.first == "normal")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::NormalCell, _pos_phase);
                
            }
        }
        else if (cell.first == "fix0")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_0, _pos_phase);
                
            }
        }
        else if (cell.first == "fix1")
        {
            for(auto &cellpos : cellpos_list)
            {
                putCellItem(cellpos, 0, CellType::FixedCell_1, _pos_phase);
                
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
                            putCellItem(cellpos, 2, CellType::CrossoverCell, _pos_phase);
                            
                        } 

                        if(count < 2) 
                        {
                            //若端点无法直接放置柱点，则跨时钟延伸两个单位元胞
                            if((std::find(crosscell.begin(), crosscell.end(), dir2) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir3) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir4) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos2 = dir1;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos3 = {dir1.first, dir1.second + 1};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, _pos_phase);
                                verticalcell.push_back(cellpos3);


                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else if ((std::find(crosscell.begin(), crosscell.end(), dir3) != crosscell.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir1) != allroutecells.end())
                            &&(std::find(allroutecells.begin(), allroutecells.end(), dir2) != allroutecells.end()))
                            {
                                position cellpos1 = *unit;
                                putCellItem(cellpos1, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos2 = dir4;
                                putCellItem(cellpos2, 2, CellType::CrossoverCell, _pos_phase);


                                position cellpos3 = {dir4.first + 1, dir4.second};
                                putCellItem(cellpos3, 0, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 1, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos3, 2, CellType::VerticalCell, _pos_phase);
                                verticalcell.push_back(cellpos3);

                                crosscell.push_back(cellpos2);
                                crosscell.push_back(cellpos3);
                            }
                            else//放置交叉线端点三层柱点
                            {
                                position cellpos = *unit;
                                putCellItem(cellpos, 0, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos, 1, CellType::VerticalCell, _pos_phase);
                                putCellItem(cellpos, 2, CellType::VerticalCell, _pos_phase);
                                verticalcell.push_back(cellpos);
                            }
                        }
                    }
                    else
                    {
                        position cellpos = *unit;
                        putCellItem(cellpos, 2, CellType::CrossoverCell, _pos_phase);
                        
                        
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
                        putCellItem(pos, 0, CellType::NormalCell, _pos_phase);
                        
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
                                putCellItem(pos, 0, CellType::NormalCell, _pos_phase);
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

position VerilogHandler::coordtrans(const position& pos, unsigned int scale)
{
    (void)scale;
    unsigned int x = pos.first;
    unsigned int y = pos.second;
    return {x, y};
}

void VerilogHandler::putCellItem(position _cellpos, int _celllayer, CellType _cellType,  std::map<position, int>& _pos_phase, QString _name)
{
    int x_node = _cellpos.first / 5;
    int y_node = _cellpos.second / 5;
    int x_coord = _cellpos.first*20 + 200;  // 坐标
    int y_coord = _cellpos.second*20 + 200;
    int cell_layer = _celllayer;
    position node_pos = {static_cast<unsigned int>(x_node), static_cast<unsigned int>(y_node)};
    auto phase_it = _pos_phase.find(node_pos);
    int phase = (phase_it != _pos_phase.end()) ? phase_it->second : -1;
    
    QCADCellItem *cellItem = new QCADCellItem(x_coord, y_coord, cell_layer, phase, _cellType, _name);
    mainWindow->checkCellInserted(mainWindow->layers, cellItem, cell_layer, x_coord, y_coord);
}

void VerilogHandler::putClock(std::map<position, int>& _pos_phase)
{
    for(auto &v : _pos_phase)
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

void VerilogHandler::generateSVG()
{
    constexpr qreal kPreferredExportScale = 4.0;
    constexpr int kMaxExportDimension = 20000;

    // 获取所有Item的联合边界矩形
    QRectF itemsBoundingRect = mainWindow->scene->itemsBoundingRect();
    if (mainWindow->scene->hasFastRender()) {
        const QRectF fastRect = mainWindow->scene->fastRenderBounds();
        itemsBoundingRect = itemsBoundingRect.isValid() ? itemsBoundingRect.united(fastRect) : fastRect;
    }

    if (!itemsBoundingRect.isValid() || itemsBoundingRect.isEmpty()) {
        QString message = "No cell-level layout to save.";
        mainWindow->printToStatusBar(message);
        return;
    }

    // 定义输出图像的大小，这里根据实际内容调整大小
    qreal exportScale = kPreferredExportScale;
    const qreal widthAtPreferredScale = std::ceil(itemsBoundingRect.width() * exportScale);
    const qreal heightAtPreferredScale = std::ceil(itemsBoundingRect.height() * exportScale);
    const qreal maxDimension = qMax(widthAtPreferredScale, heightAtPreferredScale);
    if (maxDimension > kMaxExportDimension) {
        exportScale *= (static_cast<qreal>(kMaxExportDimension) / maxDimension);
    }

    const QSize imageSize(
        qMax(1, static_cast<int>(std::ceil(itemsBoundingRect.width() * exportScale))),
        qMax(1, static_cast<int>(std::ceil(itemsBoundingRect.height() * exportScale)))
    );

    // 创建一个QImage对象用于保存PNG文件
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent); // 透明背景

    // 使用QPainter将场景内容绘制到QImage上
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // 渲染场景内容（不进行平移）
    mainWindow->scene->render(&painter, QRectF(QPointF(0, 0), QSizeF(imageSize)), itemsBoundingRect);

    const QFileInfo currentFileInfo(mainWindow->currentFilePath());
    const QString circuitName = currentFileInfo.completeBaseName().isEmpty()
        ? QStringLiteral("cell_level_layout")
        : currentFileInfo.completeBaseName();
    const QDir outputDir = currentFileInfo.absoluteDir().exists()
        ? currentFileInfo.absoluteDir()
        : QDir::current();
    const QString outputPath = outputDir.absoluteFilePath(circuitName + "_cell_level_layout.png");

    if (!image.save(outputPath)) {
        QString message = "Failed to save cell-level layout: " + QDir::toNativeSeparators(outputPath);
        mainWindow->printToStatusBar(message);
        return;
    }

    //打印信息
    QString message = QString("Cell-level layout saved: %1 (%2x, %3x%4)")
        .arg(QDir::toNativeSeparators(outputPath))
        .arg(QString::number(exportScale, 'f', 2))
        .arg(imageSize.width())
        .arg(imageSize.height());
    mainWindow->printToStatusBar(message);

}



void VerilogHandler::slotForceOrientedAlgorithm(){
    qDebug() << "Force Oriented Placement and Routing Algorithm triggered.";
}
