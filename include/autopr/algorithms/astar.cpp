#include"astar.h"

namespace fcngraph{

bool Astar::isNodeCell(const position& pos) const
{
    const auto cell = chessboard.gridMap.find(pos);
    if (cell == chessboard.gridMap.end()) {
        return false;
    }

    const auto weight = cell->second.get_current_weight();
    return weight >= NODE_WEIGHT && ((weight - NODE_WEIGHT) % WIRE_WEIGHT == 0);
}

std::vector<position> Astar::findPath(const position& startPosInput, const position& goalPosInput, bool isOneFanout) {

    reusedPath.clear();
    reusedSuccessor.clear();
    startPos = startPosInput;
    goalPos = goalPosInput;
    is_pathReused = false;
    if (!isInsideSearchBounds(startPos) || !isInsideSearchBounds(goalPos)) {
        return {};
    }
    
    std::unordered_map<position, position, PositionHash> cameFrom;      // 记录父节点，value是父节点
    std::unordered_map<position, double, PositionHash> gScore;          //当前节点到起点的消耗，这里实际作用是当做关闭列表使用
    std::unordered_map<position, double, PositionHash> fScore;          //总寻路消耗
    std::unordered_set<position, PositionHash> closedSet;
    std::set<std::pair<double, position>> openSet;                      //开启列表记录总消耗，<F, pos>
    position startPosLocal = startPos;

    //如果路径复用，起点设置为复用路径的起点
    if(isOneFanout){
        if(outDirections.find(startPosLocal) != outDirections.end()){
            const auto reusableStart = outDirections[startPosLocal];
            const auto finishedRoute = finishRoutes.find(startPos);
            const bool hasReusableInterior =
                finishedRoute != finishRoutes.end() &&
                finishedRoute->second.size() > 2 &&
                !isNodeCell(reusableStart);

            if (hasReusableInterior) {
                //复用路径，当前起点直接设置为出度方向，保存出度方向和真正起点的映射关系
                is_pathReused = true;

                reusedPath = finishedRoute->second;
                reusedPath.pop_back();
                reusedPath.erase(reusedPath.begin());
                for (std::size_t index = 0; index + 1 < reusedPath.size(); ++index) {
                    reusedSuccessor[reusedPath[index]] = reusedPath[index + 1];
                }

                startPosLocal = reusableStart;
                cameFrom[startPosLocal] = startPos;
                gScore[startPosLocal] = 1.0;
            }else{
                gScore[startPosLocal] = 0.0;
            }
        }else{
            gScore[startPosLocal] = 0.0;  
        }
    }else{
        gScore[startPosLocal] = 0.0;  
    }

    // f = 曼哈顿 + g
    fScore[startPosLocal] = heuristic(startPosLocal, goalPos) + gScore[startPosLocal];
    //把起点放入开启列表
    openSet.insert({fScore[startPosLocal], startPosLocal}); 

    while (!openSet.empty()){

        //说明这个路径找了很多格子都没有找到，没有必要遍历整个格子
        double current_f = openSet.begin()->first;
        if(current_f > maxSearchCost) break;

        //开启列表中f值最小的位置作为当前节点
        position current = openSet.begin()->second; 

        //如果当前节点为终点，直接返回
        if(current == goalPos){
           return reconstructPath(cameFrom, current);
        }

        //开启列表中删除当前点
        openSet.erase(openSet.begin()); 
        if (!closedSet.insert(current).second) {
            continue;
        }

        //遍历所有可行邻居，这里是首先依据时钟方案选择可布线路径，然后是是否是边界的判断
        auto neighbors = getNeighbors(current, cameFrom);

        //对于获取到的所有可布线节点，进行计算
        for (auto neighbor : neighbors) {
            if (closedSet.count(neighbor) != 0) {
                continue;
            }

            //因为这些邻居节点相对于当前节点都移动了一个单位，所以邻居的g值增加1
            double tentative_gScore = gScore[current] + 1.0; 

            const auto reusedIter = reusedSuccessor.find(current);
            const bool followsReusedPath = is_pathReused &&
                reusedIter != reusedSuccessor.end() && reusedIter->second == neighbor;
            if(followsReusedPath){
                // 同一扇出源的主干复用优先，普通重叠线在下面加罚。
                tentative_gScore -= 1;
            }else if(neighbor != goalPos){
                auto cell = chessboard.gridMap.find(neighbor);
                if(cell != chessboard.gridMap.end() && cell->second.get_current_weight() >= WIRE_WEIGHT){
                    tentative_gScore += occupiedWirePenalty >= 0.0
                        ? occupiedWirePenalty
                        : (maxSearchCost > 100.0 ? 6.0 : 0.0);
                }
            }
            
            const auto knownScore = gScore.find(neighbor);
            if (knownScore == gScore.end() || tentative_gScore + 1e-9 < knownScore->second) {
                if (knownScore != gScore.end()) {
                    const auto oldF = fScore.find(neighbor);
                    if (oldF != fScore.end()) {
                        openSet.erase({oldF->second, neighbor});
                    }
                }
                cameFrom[neighbor] = current;
                gScore[neighbor] = tentative_gScore;
                const double estimatedScore = tentative_gScore + heuristic(neighbor, goalPos);
                fScore[neighbor] = estimatedScore;
                if (estimatedScore <= maxSearchCost) {
                    openSet.insert({estimatedScore, neighbor});
                }
            }
        }
    }

    return {};
}


double Astar::heuristic(const position& a, const position& b){
    unsigned int x1 = a.first, y1 = a.second;
    unsigned int x2 = b.first, y2 = b.second;
    return std::abs((int)x2 - (int)x1) + abs((int)y2 - (int)y1);
}

bool Astar::drcInDegreeCheck(const position& current_neighbor){

    //检查当前点是否和终点的入度方向冲突
    auto ranger = inDirections.equal_range(goalPos);
    for(auto it = ranger.first; it != ranger.second; ++it){
        if(it->second == current_neighbor){
            return true;
        }
    }

    //检查当前点是否和终点的出度方向冲突
    if(outDirections.find(goalPos) != outDirections.end()){
        if(outDirections[goalPos] == current_neighbor)
            return true;
    }
    return false;
}

/* A* （布线）算法使用的*/
std::vector<position> Astar::getNeighbors(
    const position& pos,
    const std::unordered_map<position, position, PositionHash>& cameFrom){
    //可以布线的邻居网格
    std::vector<position> neighbors;

    const auto stepOrientation = [](const position &from, const position &to) {
        return from.second == to.second
            ? WireOrientation::Horizontal
            : WireOrientation::Vertical;
    };
    const auto pathAlreadyUsesSource = [&](const position &current,
                                           const position &source) {
        position cursor = current;
        while (true) {
            const auto ownership = wireOwnership.find(cursor);
            if (ownership != wireOwnership.end() &&
                ownership->second.find(source) != ownership->second.end()) {
                return true;
            }
            const auto parent = cameFrom.find(cursor);
            if (parent == cameFrom.end()) {
                break;
            }
            cursor = parent->second;
        }
        return false;
    };
    const auto continuesStraightAcrossCurrent = [&](const position &neighbor) {
        if (allowInterSourceWireOverlap) {
            return true;
        }
        const auto ownership = wireOwnership.find(pos);
        if (ownership == wireOwnership.end()) {
            return true;
        }
        bool currentBelongsToAnotherSource = false;
        for (const auto &sourceUse : ownership->second) {
            if (sourceUse.first != startPos) {
                currentBelongsToAnotherSource = true;
                break;
            }
        }
        if (!currentBelongsToAnotherSource) {
            return true;
        }
        const auto parent = cameFrom.find(pos);
        if (parent == cameFrom.end()) {
            return false;
        }
        const int deltaX = static_cast<int>(pos.first) -
                           static_cast<int>(parent->second.first);
        const int deltaY = static_cast<int>(pos.second) -
                           static_cast<int>(parent->second.second);
        return static_cast<int>(neighbor.first) == static_cast<int>(pos.first) + deltaX &&
               static_cast<int>(neighbor.second) == static_cast<int>(pos.second) + deltaY;
    };
    const auto canEnterWireCell = [&](const position &current,
                                      const position &neighbor) {
        if (allowInterSourceWireOverlap) {
            return true;
        }
        const auto ownership = wireOwnership.find(neighbor);
        if (ownership == wireOwnership.end()) {
            const auto cell = chessboard.gridMap.find(neighbor);
            return cell == chessboard.gridMap.end() ||
                   cell->second.get_current_weight() < WIRE_WEIGHT;
        }
        if (ownership->second.size() != 1) {
            return false;
        }
        const auto &existingSourceUse = *ownership->second.begin();
        if (existingSourceUse.first == startPos ||
            existingSourceUse.second.size() != 1 ||
            existingSourceUse.second.count(WireOrientation::Bend) != 0) {
            return false;
        }
        const WireOrientation existingOrientation =
            *existingSourceUse.second.begin();
        if (existingOrientation == stepOrientation(current, neighbor)) {
            return false;
        }
        if (pathAlreadyUsesSource(current, existingSourceUse.first)) {
            return false;
        }
        return true;
    };

    //规律时钟方案
    if(isRegularClockScheme){
        if(chessboard.directionMap.find(pos) != chessboard.directionMap.end()){

            //时钟方案决定的所有可布线邻居
            auto can_wire_positions = chessboard.directionMap.at(pos);

            //再从时钟方案决定的可布线中再筛选是否被占用的网格
            for(auto &neighbor_pos : can_wire_positions){

                //边界不要
                if(chessboard.gridMap.find(neighbor_pos) == chessboard.gridMap.end())
                    continue;

                if (!isInsideSearchBounds(neighbor_pos))
                    continue;

                if (!continuesStraightAcrossCurrent(neighbor_pos))
                    continue;

                //如果当前点的邻居是终点，且当前点作为终点的入度没有被占用
                if(neighbor_pos == goalPos){
                    //检查当前点是否和终点的入度方向冲突
                    if(!drcInDegreeCheck(pos)){
                        neighbors.clear();
                        neighbors.push_back(neighbor_pos);
                        break;
                    }
                }
                
                const auto reused = reusedSuccessor.find(pos);
                if (reused != reusedSuccessor.end() && reused->second == neighbor_pos) {
                    if (!isNodeCell(neighbor_pos)) {
                        neighbors.push_back(neighbor_pos);
                    }
                    continue;
                }

                if (isNodeCell(neighbor_pos) ||
                    !canEnterWireCell(pos, neighbor_pos)) {
                    continue;
                }

                //该网格是否可以布线
                if(chessboard.is_addWire(neighbor_pos)){
                    neighbors.push_back(neighbor_pos);
                }
            }
        }
    }else{
        /*
        不规则时钟方案：上下左右四个方向
        */
        auto can_wire_positions = chessboard.getPosssibleDirection(pos, isRegularClockScheme);

        //如果当前点是起点，首先需要检查邻居和起点的入度是否冲突，该操作只能执行一次，所以放在循环外面
        if (pos == startPos) {
            auto range = inDirections.equal_range(startPos);
            for (auto it = range.first; it != range.second; ++it) {
                auto blocked_pos = it->second;
                if (std::find(can_wire_positions.begin(), can_wire_positions.end(), blocked_pos) != can_wire_positions.end()) {
                    can_wire_positions.erase(std::remove(can_wire_positions.begin(), can_wire_positions.end(), blocked_pos), can_wire_positions.end());
                }
            }
        }

        for(auto &neighbor_pos : can_wire_positions){

            if (!isInsideSearchBounds(neighbor_pos))
                continue;

            if (!continuesStraightAcrossCurrent(neighbor_pos))
                continue;

            //如果当前点的邻居是终点，且当前点作为终点的入度没有被占用
            if(neighbor_pos == goalPos){
                //检查当前点是否和终点的入度方向冲突
                if(!drcInDegreeCheck(pos)){
                    neighbors.clear();
                    neighbors.push_back(neighbor_pos);
                    break;
                }
            }
            
            //复用路径,如果当前点的邻居是复用路径的点，那么直接加入
            const auto reused = reusedSuccessor.find(pos);
            if (reused != reusedSuccessor.end() && reused->second == neighbor_pos) {
                if (!isNodeCell(neighbor_pos)) {
                    neighbors.push_back(neighbor_pos);
                }
                continue;
            }

            if (isNodeCell(neighbor_pos) ||
                !canEnterWireCell(pos, neighbor_pos)) {
                continue;
            }

            //该网格是否可以布线
            if(chessboard.is_addWire(neighbor_pos)){
                neighbors.push_back(neighbor_pos);
            }
        }
    }
    return neighbors;
}

void Astar::recordRouteOwnership(const std::vector<position>& path)
{
    if (allowInterSourceWireOverlap || path.size() < 3) {
        return;
    }

    for (std::size_t index = 1; index + 1 < path.size(); ++index) {
        const position &previous = path[index - 1];
        const position &current = path[index];
        const position &next = path[index + 1];
        WireOrientation orientation = WireOrientation::Bend;
        if (previous.second == current.second && current.second == next.second) {
            orientation = WireOrientation::Horizontal;
        } else if (previous.first == current.first && current.first == next.first) {
            orientation = WireOrientation::Vertical;
        }
        wireOwnership[current][startPos].insert(orientation);
    }
}

std::vector<position> Astar::reconstructPath(const std::unordered_map<position, position, PositionHash>& cameFrom, const position& currentPos) {
        position current = currentPos;
        std::vector<position> firstPath;
        while (cameFrom.find(current) != cameFrom.end()) {
            firstPath.push_back(current);
            current = cameFrom.at(current);
        }

        //如果路径复用了，需要把node的扇出节点加入
        if(is_pathReused){
            // firstPath.push_back(outDirections[startMortonPos]);
            firstPath.push_back(startPos);
            std::reverse(firstPath.begin(), firstPath.end());
            auto old_path = finishRoutes[startPos];
            
            for(auto &pos : firstPath){
                auto it = std::find(old_path.begin(), old_path.end(), pos);
                if(it == old_path.end()){
                    chessboard.addWire(pos);
                }
            }
        }else{
            firstPath.push_back(startPos);
            std::reverse(firstPath.begin(), firstPath.end());
            finishRoutes.insert({startPos, firstPath});
            for(auto &pos : firstPath){
                chessboard.addWire(pos);
            }
        }

        recordRouteOwnership(firstPath);
        
        //记录node的入度方向,path中的倒数第二个元素
        inDirections.insert({goalPos, firstPath[firstPath.size()-2]});
        outDirections.insert({startPos, firstPath[1]});
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
