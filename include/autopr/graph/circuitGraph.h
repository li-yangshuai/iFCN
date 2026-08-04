#pragma once
#include <graphviz/gvc.h>
#include "parse.h"
#include "autopr/grid/grid.h"
#include "autopr/algorithms/astar.h"
#include "autopr/algorithms/astarwithphase.h"
#include "autopr/algorithms/phaseSolver.h"
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include "autopr/grid/gridCell.h"
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility> // For std::pair
#include <functional> // For std::hash

// // 定义一个自定义哈希函数
// namespace std {
//     template <>
//     struct hash<std::pair<int, int>> {
//         std::size_t operator()(const std::pair<int, int>& p) const noexcept {
//             return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
//         }
//     };
// }

namespace fcngraph {

struct Path;

struct AdaptiveExpansionStats
{
    int insertedRows = 0;
    int insertedColumns = 0;
    int removedRows = 0;
    int removedColumns = 0;
    int acceptedRounds = 0;
};

class CircuitGraph {
public:
    CircuitGraph( Parse& parse,std::string fileName, GridChessboard& chessboard, Astar& astar)
        : parse(parse), fileName(fileName), chessboard(chessboard), astar(astar) {
        gvc = nullptr;
    }

    //回调函数
    void setFitnessCallback(const std::function<void(std::string)> &callback);
    void setStageCallback(const std::function<void(const std::string&)> &callback);
    
    //生成图
    void processAndGenerateGraph(bool printSVG = false, bool showCircuitLabel = false, bool isBox = false, bool isOGD = false);

    //图->网格坐标处理
    void sortNodesByYThenXCoordinate(double grid_size = 400,
                                     double yGridSize = 0.0);
    void sortNodesByLayeredGrid(unsigned int xSpacing = 4,
                                unsigned int ySpacing = 4,
                                unsigned int xPadding = 4,
                                unsigned int yPadding = 4);
    void sortNodesByFixedLayerOrder(const std::vector<std::vector<int>>& orderedLayers,
                                    unsigned int xSpacing = 4,
                                    unsigned int ySpacing = 4,
                                    unsigned int xPadding = 4,
                                    unsigned int yPadding = 4);

    //布局布线
        bool placeAndRoute(int shuffledRouteOrderRetries = 24);
        // Route an already prepared compact placement.  Routing-order repair
        // is attempted first; persistent failed-edge/port pressure then
        // proposes one transactional row or column cut at a time.  Random
        // clock phases are assigned only after every net is routed.
        bool routeCompactRandomClockWithExpansion(
            int phaseCount = 4,
            int shuffledRouteOrderRetries = 12,
            int maxExpansionRounds = 8,
            double maxSearchCost = 240.0,
            int maxSamePhase = 4);
        const AdaptiveExpansionStats& getAdaptiveExpansionStats() const
        {
            return adaptiveExpansionStats;
        }
    // Restored June 2025 Graphviz placement + four-direction A* routing +
    // post-route irregular clock assignment flow. The geometry follows the
    // June pipeline; phase solving uses the current bounded/validated solver
    // instead of restoring its unbounded million-sample random loop.
        bool placeAndRouteJuneRandomClock(int phaseCount = 4,
                                          double graphvizGridSize = 40.0,
                                          int shuffledRouteOrderRetries = 24,
                                          int maxSamePhase = -1);
        bool placeAndRouteJuneRandomClockAnisotropic(
            int phaseCount = 4,
            double graphvizGridSizeX = 40.0,
            double graphvizGridSizeY = 40.0,
            int shuffledRouteOrderRetries = 24,
            int maxSamePhase = -1);
        bool placeAndRouteCompactLayeredClock(int phaseCount = 4,
                                              unsigned int xSpacing = 3,
                                              unsigned int ySpacing = 3,
                                              int shuffledRouteOrderRetries = 24);
    bool placeAndRoutePhaseAware(int phaseCount = 4,
                                 int maxSamePhase = 4,
                                 double maxSearchCost = 160.0,
                                 int maxRoutingAttempts = 24,
                                 bool enableFlexiblePhasePass = false);
    int compactPhaseAware(int phaseCount = 4,
                          int maxSamePhase = 4,
                          double maxSearchCost = 240.0,
                          int maxRounds = 6,
                          int maxElapsedMilliseconds = 2500,
                          int maxEvaluatedCuts = 8);
    int compactClockPhaseCycles(int phaseCount = 4,
                                int maxRounds = 6);

    std::map<unsigned int, std::vector<std::vector<position>>> reclassifyLayers(const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>>& classifiedRoutes, std::map<unsigned int, std::vector<unsigned int>>& groupMapping);
    void printGroupMapping(const std::map<unsigned int, std::vector<unsigned int>>& groupMapping);
    void printClassifiedRoutes(
        const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>>& classifiedRoutes);
    // 调用SA分配相位
    bool assignPhases(int phaseCount = 4);

    bool phaseOptimize(int current_layer, std::vector<fcngraph::Path>& paths, std::vector<int>& start_phases, int phaseCount = 4, int recursion_count = 0); 
    bool assignPhasesFallback(int phaseCount = 4);
    bool assignRouteConstraintPhases(int phaseCount = 4);
    bool validateAssignedRoutePhases(int phaseCount = 4) const;

    // 打印latex结果
    void printLaTex(const std::string &outputPath = std::string(),
                    bool use2ddStyle = false);
    
    std::map<int, position> nodeIndex_pos;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;
private:
    struct RoutingFailureInfo
    {
        std::vector<std::pair<int, int>> failedEdges;
        std::size_t routedEdges = 0;
        bool routedLayoutRejected = false;
    };

    void alignPrimaryIoToBoundaryRows(bool yAxisPointsUp);
    bool placeAndRouteInternal(int shuffledRouteOrderRetries,
                               RoutingFailureInfo *failureInfo,
                               bool emitConflictStages,
                               int deterministicPolicyLimit = 6,
                               const std::function<bool()> &acceptRoutedLayout = {});
    bool validateJuneRandomClockRoutedLayout(int phaseCount,
                                             int maxSamePhase = -1);
    bool routeAndValidateJuneRandomClock(int phaseCount,
                                         int shuffledRouteOrderRetries,
                                         int maxSamePhase = -1);
    Parse& parse;
    std::string fileName;
    GridChessboard& chessboard;
    Astar& astar;
    std::map<int, Agnode_t*> node_map;
    GVC_t* gvc;
    //由dot算法生成的node位置
    std::vector<std::pair<int, std::pair<double, double>>> node_positions;
    //由dot算法生成的node位置转换为网格坐标
    std::map<int, position> grid_positions;

    //按照y坐标从大到小，x坐标从小到大排序
    std::vector<std::pair<int, position>> sorted_grid_positions;
    //回调打印的信息用；
    std::function<void(std::string)> fitnessCallback;
    std::function<void(const std::string&)> stageCallback;
    AdaptiveExpansionStats adaptiveExpansionStats;

    bool hasAcceptableAssignedRoutePhases(int phaseCount = 4) const;

};


/* SA 实现相位分配 */


}
 
