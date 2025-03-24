#include"individual.h"

namespace fcngraph{

void Individual::randomlyPlaceNodes() {
    std::set<unsigned int> diffPos;
    for (auto node : parse.getEffectiveNodes()) {
        unsigned int pos;
        do {
            pos = chessboard.randomMortonPos();
        } while (diffPos.find(pos) != diffPos.end());  // 确保位置不重复
        diffPos.insert(pos);  // 将新位置添加到集合中
        nodeindex_morton[node] = pos;
    }
}

void Individual::sameLevelRouting() {
    assert(routes.empty() && layerRoutesLength.empty());
    for(auto &layer_pair : parse.getSameLayerNodeRoutePair()) {
        auto layer = layer_pair.first;
        auto node_pair = layer_pair.second;
        for(auto &node : node_pair) {
            auto nodeStartPosIt = nodeindex_morton.find(node.first);
            auto nodeEndPosIt = nodeindex_morton.find(node.second);
            if (nodeStartPosIt != nodeindex_morton.end() && nodeEndPosIt != nodeindex_morton.end()) {
                unsigned int nodeStartPos = nodeStartPosIt->second;
                unsigned int nodeEndPos = nodeEndPosIt->second;
                //检查node的属性 只有针对与门、或门、择多门时候，考虑唯一扇出方向
                bool isOneFanout = false;
                if(parse.getm_vertexType(node.first) == typeid(AndNode) ||
                parse.getm_vertexType(node.first) == typeid(OrNode) ||
                parse.getm_vertexType(node.first) == typeid(MajNode)){
                    isOneFanout = true;
                }
                auto path = astar.findPath(nodeStartPos, nodeEndPos, isOneFanout);
                routes.insert({node, path});
                layerRoutesLength.insert({layer, path.size()});
            }
        }
    }
}

void Individual::diffLevelRouting() {
    for(auto &node_pair : parse.getDifferLayerNodeRoutePair()) {
        auto nodeStartPosIt = nodeindex_morton.find(node_pair.first);
        auto nodeEndPosIt = nodeindex_morton.find(node_pair.second);
        if (nodeStartPosIt != nodeindex_morton.end() && nodeEndPosIt != nodeindex_morton.end()) {
            unsigned int nodeStartPos = nodeStartPosIt->second;
            unsigned int nodeEndPos = nodeEndPosIt->second;
            //检查node的属性 只有针对与门、或门、择多门时候，考虑唯一扇出方向
            bool isOneFanout = false;
            if(parse.getm_vertexType(node_pair.first) == typeid(AndNode) ||
            parse.getm_vertexType(node_pair.first) == typeid(OrNode) ||
            parse.getm_vertexType(node_pair.first) == typeid(MajNode)){
                isOneFanout = true;
            }
            auto path = astar.findPath(nodeStartPos, nodeEndPos, isOneFanout);
            routes.insert({node_pair, path});
        }
    }
}


void Individual::add_placement_value_to(){
    assert(!nodeindex_morton.empty());
    // 检查 nodeindex_morton 中的位置是否唯一
    std::set<unsigned int> positions;
    bool unique = true;
    for (const auto& v : nodeindex_morton) {
        if (!positions.insert(v.second).second) {
            unique = false;
            break;
        }
    }

    if (unique) {
        for (const auto& v : nodeindex_morton) {
            chessboard.placeNode(v.second);
        }
        is_placed = true;
        fitness += 1.0;
    } else {
        is_placed = false;
    }
}

void Individual::add_routing_value_to(){
    if(!is_placed) return;
    sameLevelRouting(); 
    diffLevelRouting();
    is_routed = true;
    int cnt = 0;
    for(auto&v : routes){
        if(!v.second.empty()){
            cnt ++;
            fitness += 2.0;
        }else{
            is_routed = false;
        }
    }
    fitness += 10.0 * cnt /static_cast<double>(nodeindex_morton.size());
}

void Individual::add_synchronization_value_to() {
    if(!is_placed) return;
    is_synced = is_routed;

    auto is_zero = [](auto len) -> bool { return len == 0; };
    std::vector<long double> path_diff_val;

    for (auto it = layerRoutesLength.begin(); it != layerRoutesLength.end(); ) {
        auto key = it->first; 
        auto range = layerRoutesLength.equal_range(key);
        size_t count = std::distance(range.first, range.second); // 获取key对应的values数量

        if (count < 2) {
            it = range.second;
            continue;
        } else {
            std::vector<unsigned int> path_length;
            for (auto rangeIt = range.first; rangeIt != range.second; ++rangeIt) {
                path_length.push_back(rangeIt->second);
            }

            auto max_len = *std::max_element(path_length.begin(), path_length.end());
            auto min_len = *std::min_element(path_length.begin(), path_length.end());

            is_synced &= (max_len == min_len);

            unsigned int max_count = 0;
            for (size_t i = 0; i < path_length.size(); ++i) {
                unsigned int count = std::count(path_length.begin(), path_length.end(), path_length[i]);
                if (count > max_count) {
                    max_count = count;
                }
            }
            auto diffnum = path_length.size() - max_count;
            if (diffnum == 0) {
                path_diff_val.push_back(0.5);
            } else if (diffnum <= path_length.size() / 2) {
                path_diff_val.push_back(diffnum);
            }

            if (std::all_of(path_length.begin(), path_length.end(), is_zero)) {
                path_diff_val.push_back(10000);
            } else if (std::any_of(path_length.begin(), path_length.end(), is_zero)) {
                path_diff_val.push_back(100);
            } else {
                path_diff_val.push_back(0.5L + max_len - min_len);
            }
            it = range.second;
        }
    }

    long double diff_val = 0.0;
    for (auto &diff : path_diff_val) {
        diff_val += 15.0L / diff;
    }
    fitness += diff_val;
}



void Individual::add_area_value_to() {
        if(!is_placed) return;
    std::set<unsigned int> x_positions;
    std::set<unsigned int> y_positions;
    for (auto &pair : nodeindex_morton) {
        std::pair<unsigned int, unsigned int> x_y = MortonChessboard::decodeMortonCode(pair.second);
        x_positions.insert(x_y.first);
        y_positions.insert(x_y.second);
    }

    int max_x = *(x_positions.rbegin());
    int max_y = *(y_positions.rbegin());
    int farea = max_x * max_y;

    unsigned int width = chessboard.chessboard_se.first - chessboard.chessboard_nw.first;
    unsigned int height = chessboard.chessboard_se.second - chessboard.chessboard_nw.second;

    long double area_f = static_cast<double>(width * height) / static_cast<double>(farea);
    fitness += 7.0 * area_f;
}



void Individual::computeFitness(){
    add_placement_value_to();
    add_routing_value_to();
    // add_synchronization_value_to();
    add_area_value_to();
}







};