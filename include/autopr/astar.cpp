#include"astar.h"

namespace fcngraph{

std::vector<unsigned int> Astar::findPath(unsigned int startMorton, unsigned int goalMorton, bool isOneFanout) {

    reusedPath.clear();
    startMortonPos = startMorton;
    goalMortonPos = goalMorton;
    is_pathReused = false;
    
    std::unordered_map<unsigned int, unsigned int> cameFrom;      // 记录父节点，value是父节点
    std::unordered_map<unsigned int, double> gScore;              //当前节点到起点的消耗，这里实际作用是当做关闭列表使用
    std::unordered_map<unsigned int, double> fScore;              //总寻路消耗
    std::set<std::pair<double, unsigned int>> openSet;            //开启列表记录总消耗，<F, morton_code>

    //如果路径复用，起点设置为复用路径的起点
    if(isOneFanout){
        if(outDirections.find(startMorton) != outDirections.end()){
            //复用路径，当前起点直接设置为出度方向，保存出度方向和真正起点的映射关系
            is_pathReused = true;

            reusedPath = finishRoutes[startMortonPos];
            reusedPath.pop_back();
            reusedPath.erase(reusedPath.begin());

            startMorton = outDirections[startMorton];
            cameFrom[startMorton] = startMortonPos;
            gScore[startMorton] = 1.0;  
        }else{
            gScore[startMorton] = 0.0;  
        }
    }else{
        gScore[startMorton] = 0.0;  
    }

    // f = 曼哈顿 + g
    fScore[startMorton] = heuristic(startMorton, goalMorton) + gScore[startMorton];
    //把起点放入开启列表
    openSet.insert({fScore[startMorton], startMorton}); 

    while (!openSet.empty()){

        //说明这个路径找了很多格子都没有找到，没有必要遍历整个格子
        double current_f = openSet.begin()->first;
        if(current_f > 40.0) break;

        //开启列表中f值最小的位置作为当前节点
        unsigned int current = openSet.begin()->second; 

        //如果当前节点为终点，直接返回
        if(current == goalMorton){
           return reconstructPath(cameFrom, current);
        }

        //开启列表中删除当前点
        openSet.erase(openSet.begin()); 

        //遍历所有可行邻居，这里是首先依据时钟方案选择可布线路径，然后是是否是边界的判断
        auto neighbors = getNeighbors(current);

        //对于获取到的所有可布线节点，进行计算
        for (auto neighbor : neighbors) {
            if(neighbor == goalMorton){
                if(cameFrom.find(neighbor) == cameFrom.end()){
                    //记录父节点，用于回溯路径
                    cameFrom[neighbor] = current;
                    current = neighbor;
                }
                return reconstructPath(cameFrom, current);
            }

            //因为这些邻居节点相对于当前节点都移动了一个单位，所以邻居的g值增加1
            double tentative_gScore = gScore[current] + 1.0; 

            if(is_pathReused){
                auto iter = std::find(reusedPath.begin(), reusedPath.end(), neighbor);
                if(iter != reusedPath.end()){
                    //复用路径 -1
                    tentative_gScore -= 1;
                }
            }
            
            // 如果存储父节点列表中存在节点，那么不需要更新父节点
            if(cameFrom.find(neighbor) == cameFrom.end()){
                //记录父节点，用于回溯路径
                cameFrom[neighbor] = current;   
            }

            /*
            如果这个邻居在开启列表和关闭列表中都不存在，这里的gScore容器可以看作是开启列表和关闭列表的集合，
            因为邻居节点都需要对开启和关闭列表中查询是否存在，如果任意一个存在都不会再被插入
            */
            if (!gScore.count(neighbor)) {    
                //存储邻居节点的g值        
                gScore[neighbor] = tentative_gScore;
                //计算总寻路消耗
                double emiateScore = tentative_gScore + heuristic(neighbor, goalMorton);
                fScore[neighbor] = emiateScore;
                //开启列表中放入邻居
                openSet.insert({emiateScore, neighbor});
            }
        }
    }

    return {};
}


double Astar::heuristic(const unsigned int a, const unsigned int b){
    auto aPos = MortonChessboard::decodeMortonCode(a);
    auto bPos = MortonChessboard::decodeMortonCode(b);
    unsigned int x1 = aPos.first, y1 = aPos.second;
    unsigned int x2 = bPos.first, y2 = bPos.second;
    return std::abs((int)x2 - (int)x1) + abs((int)y2 - (int)y1);
}

bool Astar::drcInDegreeCheck(unsigned int current_neighbor){

    //检查当前点是否和终点的入度方向冲突
    auto ranger = inDirections.equal_range(goalMortonPos);
    for(auto it = ranger.first; it != ranger.second; ++it){
        if(it->second == current_neighbor){
            return true;
        }
    }

    //检查当前点是否和终点的出度方向冲突
    if(outDirections.find(goalMortonPos) != outDirections.end()){
        if(outDirections[goalMortonPos] == current_neighbor)
            return true;
    }
    return false;
}

/* A* （布线）算法使用的*/
std::vector<unsigned int> Astar::getNeighbors(unsigned int mortonCode){
    //可以布线的邻居网格
    std::vector<unsigned int> neighbors;
    //规律时钟方案
    if(isRegularClockScheme){
        if(chessboard.directionMap.find(mortonCode) != chessboard.directionMap.end()){

            //时钟方案决定的所有可布线邻居
            auto can_wire_mortons = chessboard.directionMap.at(mortonCode);

            //再从时钟方案决定的可布线中再筛选是否被占用的网格
            for(auto &neighbor_morton : can_wire_mortons){

                //边界不要
                if(neighbor_morton == -1 || chessboard.gridMap.find(neighbor_morton) == chessboard.gridMap.end())
                    continue;

                //如果当前点的邻居是终点，且当前点作为终点的入度没有被占用
                if(neighbor_morton == goalMortonPos){
                    //检查当前点是否和终点的入度方向冲突
                    if(!drcInDegreeCheck(mortonCode)){
                        neighbors.clear();
                        neighbors.push_back(neighbor_morton);
                        break;
                    }
                }
                
                auto it = std::find(reusedPath.begin(), reusedPath.end(), mortonCode);
                if (it != reusedPath.end() && (it + 1) != reusedPath.end() && *(it + 1) == neighbor_morton) {
                    neighbors.push_back(neighbor_morton);
                    continue;
                }

                //该网格是否可以布线
                if(chessboard.is_addWire(neighbor_morton)){
                    neighbors.push_back(neighbor_morton);
                }
            }
        }
    }else{
        /*
        不规则时钟方案：上下左右四个方向
        */
        auto can_wire_mortons = chessboard.getPosssibleDirection(mortonCode, isRegularClockScheme);

        //如果当前点是起点，首先需要检查邻居和起点的入度是否冲突，该操作只能执行一次，所以放在循环外面
        if (mortonCode == startMortonPos) {
            auto range = inDirections.equal_range(startMortonPos);
            for (auto it = range.first; it != range.second; ++it) {
                auto morton = it->second;
                if (std::find(can_wire_mortons.begin(), can_wire_mortons.end(), morton) != can_wire_mortons.end()) {
                    can_wire_mortons.erase(std::remove(can_wire_mortons.begin(), can_wire_mortons.end(), morton), can_wire_mortons.end());
                }
            }
        }

        for(auto &neighbor_morton : can_wire_mortons){

            //如果当前点的邻居是终点，且当前点作为终点的入度没有被占用
            if(neighbor_morton == goalMortonPos){
                //检查当前点是否和终点的入度方向冲突
                if(!drcInDegreeCheck(mortonCode)){
                    neighbors.clear();
                    neighbors.push_back(neighbor_morton);
                    break;
                }
            }
            
            //复用路径,如果当前点的邻居是复用路径的点，那么直接加入
            auto it = std::find(reusedPath.begin(), reusedPath.end(), mortonCode);
            if (it != reusedPath.end() && (it + 1) != reusedPath.end() && *(it + 1) == neighbor_morton) {
                neighbors.push_back(neighbor_morton);
                continue;
            }

            //该网格是否可以布线
            if(chessboard.is_addWire(neighbor_morton)){
                neighbors.push_back(neighbor_morton);
            }
        }
    }
    return neighbors;
}

std::vector<unsigned int> Astar::reconstructPath(const std::unordered_map<unsigned int, unsigned int>& cameFrom, unsigned int current) {
        std::vector<unsigned int > firstPath;
        while (cameFrom.find(current) != cameFrom.end()) {
            firstPath.push_back(current);
            current = cameFrom.at(current);
        }

        //如果路径复用了，需要把node的扇出节点加入
        if(is_pathReused){
            // firstPath.push_back(outDirections[startMortonPos]);
            firstPath.push_back(startMortonPos);
            std::reverse(firstPath.begin(), firstPath.end());
            auto old_path = finishRoutes[startMortonPos];
            
            for(auto &morton : firstPath){
                auto it = std::find(old_path.begin(), old_path.end(), morton);
                if(it == old_path.end()){
                    chessboard.addWire(morton);
                }
            }
        }else{
            firstPath.push_back(startMortonPos);
            std::reverse(firstPath.begin(), firstPath.end());
            finishRoutes.insert({startMortonPos, firstPath});
            for(auto &morton : firstPath){
                chessboard.addWire(morton);
            }
        }
        
        //记录node的入度方向,path中的倒数第二个元素
        inDirections.insert({goalMortonPos, firstPath[firstPath.size()-2]});
        outDirections.insert({startMortonPos, firstPath[1]});
        return firstPath;
}









/*
void Astar::findMultiPaths(unsigned int startMorton, unsigned int goalMorton_1, unsigned int goalMorton_2){
    auto path_1 = findPath(startMorton, goalMorton_1);

    if(path_1.empty()){
        return;
    }

    //删除头部和尾部元素
    path_1.erase(path_1.begin());
    path_1.pop_back();
    std::vector<std::pair<unsigned int, double>> vec;

    if(path_1.empty()){
        return;
    }else{
        for(auto &morton : path_1){
            fScore[morton] += heuristic(morton, goalMorton_2);
            vec.push_back({morton, fScore[morton]});
        }

        // 使用 sort 对 vector 进行排序，根据 pair 的第二个元素（double 值）排序
        std::sort(vec.begin(), vec.end(), [](const std::pair<unsigned int, double>& a, const std::pair<unsigned int, double>& b) {
            return a.second < b.second;
        });

        while(!vec.empty()){
            //A-B-C  A-B-D
            auto path_2 = findPath(vec[0].first, goalMorton_2);

            if(!path_2.empty()){
                //找B点
                auto it = std::find(firstPath.begin(), firstPath.end(), vec[0].first);
                //从A中找B点
                if (it != firstPath.end()) {
                    // 找到了元素，从该位置切割
                    path_2.insert(path_2.begin(), firstPath.begin(), it);
                }
                break;
            }else{
                vec.erase(vec.begin());
            }
        };

    }
}
*/



};