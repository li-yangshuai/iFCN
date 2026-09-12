#include"individual.h"
#include "autopr/graph/circuitGraph.h"
#include <cmath>
#include <limits>

namespace fcngraph{

void Individual::randomlyPlaceNodes() {
    infoReset();
    nodeindex_pos.clear();
    static thread_local std::size_t variation = 0;
    if (placeClockReachableNodes(variation++)) return;
    std::set<position> diffPos;
    // Reserve the constrained launch sites before unconstrained internal nodes
    // can occupy them. Primary inputs must start in the first clock phase;
    // their later global epoch alignment is still checked after routing.
    for (const bool primaryInput : {true, false}) {
        for (auto node : parse.getEffectiveNodes()) {
            if ((parse.getNodeType(node) == "input") != primaryInput) continue;
            position pos;
            if (!findUnusedPosition(diffPos, pos, primaryInput)) {
                nodeindex_pos.clear();
                validation_error = primaryInput
                    ? "fixed grid has insufficient free phase-1 primary-input sites"
                    : "fixed grid has insufficient free node sites";
                return;
            }
            diffPos.insert(pos);
            nodeindex_pos[node] = pos;
        }
    }
}

std::int64_t Individual::primaryInputDiagonal() const {
    if (chessboard.patternData != &tdd_pattern[0][0]) return -1;
    std::size_t inputs = 0;
    for (const auto node : parse.getEffectiveNodes()) inputs += parse.getNodeType(node) == "input";
    std::map<std::uint64_t, std::size_t> capacities;
    for (unsigned y = chessboard.chessboard_nw.second; y < chessboard.chessboard_se.second; ++y)
        for (unsigned x = chessboard.chessboard_nw.first; x < chessboard.chessboard_se.first; ++x)
            if (chessboard.getCoorPos_Phase(x, y) == 1) ++capacities[std::uint64_t(x) + y];
    // One shared launch diagonal across the population also survives crossover.
    // Prefer the earliest sufficient diagonal to leave space for downstream gates.
    for (const auto& entry : capacities) if (entry.second >= inputs) return entry.first;
    return -2;
}

bool Individual::placeClockReachableNodes(std::size_t variation) {
    infoReset();
    nodeindex_pos.clear();
    const auto nw = chessboard.chessboard_nw, se = chessboard.chessboard_se;
    if (se.first <= nw.first || se.second <= nw.second) return false;
    const auto capacity = std::uint64_t(se.first - nw.first) * (se.second - nw.second);
    if (capacity > 4096 || capacity <= parse.getEffectiveNodes().size()) return false;
    const auto diagonal = primaryInputDiagonal();
    if (diagonal == -2) return false;
    using Adjacency = std::map<position, std::vector<position>>;
    Adjacency forward, reverse;
    std::vector<position> points;
    for (unsigned y = nw.second; y < se.second; ++y) for (unsigned x = nw.first; x < se.first; ++x) {
        const position from{x, y};
        points.push_back(from);
        for (const auto to : chessboard.getPosssibleDirection(from)) {
            if (to.first < nw.first || to.second < nw.second || to.first >= se.first || to.second >= se.second) continue;
            if ((chessboard.getCoorPos_Phase(to.first, to.second) + 4 - chessboard.getCoorPos_Phase(x, y)) % 4 != 1) continue;
            forward[from].push_back(to);
            reverse[to].push_back(from);
        }
    }
    const auto distances = [](const Adjacency& graph, position start, const std::set<position>& blocked) {
        std::map<position, unsigned> result{{start, 0}};
        std::queue<position> pending;
        pending.push(start);
        while (!pending.empty()) {
            const auto from = pending.front(); pending.pop();
            const auto found = graph.find(from);
            if (found == graph.end()) continue;
            for (const auto to : found->second) if (!blocked.count(to) && !result.count(to)) {
                result[to] = result.at(from) + 1;
                pending.push(to);
            }
        }
        return result;
    };
    std::vector<unsigned> inputs, order;
    std::map<unsigned, std::vector<unsigned>> fanins, fanouts;
    std::map<unsigned, std::size_t> remaining;
    for (const auto node : parse.getEffectiveNodes()) {
        remaining[node] = 0;
        if (parse.getNodeType(node) == "input") inputs.push_back(node);
    }
    for (const auto edge : parse.getEffectiveEdges()) {
        fanins[edge.second].push_back(edge.first);
        fanouts[edge.first].push_back(edge.second);
        ++remaining[edge.second];
    }
    std::set<unsigned> ready;
    for (const auto& node : remaining) if (!node.second) ready.insert(node.first);
    while (!ready.empty()) {
        const auto node = *ready.begin(); ready.erase(ready.begin()); order.push_back(node);
        for (const auto sink : fanouts[node]) if (--remaining[sink] == 0) ready.insert(sink);
    }
    if (inputs.empty() || order.size() != parse.getEffectiveNodes().size()) return false;
    // A bounded placement seed, not an alternate router: all actual routes are
    // still produced by A* and independently accepted by validateLayout().
    for (std::size_t trial = 0; trial < 8; ++trial) {
        const auto hub = points[(points.size() / 2 + variation * 17 + trial * 13) % points.size()];
        std::map<unsigned, std::vector<position>> launchLayers;
        for (const auto& entry : distances(reverse, hub, {})) {
            const auto p = entry.first;
            if (chessboard.getCoorPos_Phase(p.first, p.second) == 1 &&
                (diagonal < 0 || std::uint64_t(p.first) + p.second == std::uint64_t(diagonal)))
                launchLayers[entry.second].push_back(p);
        }
        std::vector<position> launches;
        for (const auto& layer : launchLayers) if (layer.second.size() >= inputs.size()) { launches = layer.second; break; }
        if (launches.empty()) continue;
        std::rotate(launches.begin(), launches.begin() + variation % launches.size(), launches.end());
        std::map<unsigned, position> placed;
        std::map<unsigned, unsigned> epochs;
        std::set<position> occupied;
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            placed[inputs[i]] = launches[i]; epochs[inputs[i]] = 0; occupied.insert(launches[i]);
        }
        bool complete = true;
        for (const auto node : order) {
            if (placed.count(node)) continue;
            if (fanins[node].empty()) { complete = false; break; }
            std::vector<std::pair<unsigned, std::map<position, unsigned>>> arrivals;
            for (const auto source : fanins[node]) arrivals.push_back({epochs.at(source), distances(forward, placed.at(source), occupied)});
            std::vector<std::pair<unsigned, position>> choices;
            for (const auto point : points) {
                if (occupied.count(point)) continue;
                unsigned epoch = 0; bool compatible = true, first = true;
                for (const auto& arrival : arrivals) {
                    const auto found = arrival.second.find(point);
                    if (found == arrival.second.end() || found->second == 0) { compatible = false; break; }
                    const auto value = arrival.first + found->second;
                    if (!first && epoch != value) { compatible = false; break; }
                    epoch = value; first = false;
                }
                if (compatible) choices.push_back({epoch, point});
            }
            if (choices.empty()) { complete = false; break; }
            std::sort(choices.begin(), choices.end());
            const auto limit = std::find_if(choices.begin(), choices.end(), [&](const auto& choice) { return choice.first != choices.front().first; }) - choices.begin();
            const auto choice = choices[(variation + node) % std::size_t(limit)];
            epochs[node] = choice.first; placed[node] = choice.second; occupied.insert(choice.second);
        }
        if (complete) { nodeindex_pos = std::move(placed); return true; }
    }
    return false;
}

bool Individual::findUnusedPosition(const std::set<position>& used, position& result, bool primaryInput) const {
    const auto nw = chessboard.chessboard_nw, se = chessboard.chessboard_se;
    if (se.first <= nw.first || se.second <= nw.second) return false;
    const auto capacity = static_cast<std::uint64_t>(se.first - nw.first) * (se.second - nw.second);
    if (used.size() >= capacity) return false;
    static thread_local std::mt19937 generator(std::random_device{}());
    const auto diagonal = primaryInput ? primaryInputDiagonal() : -1;
    if (diagonal == -2) return false;
    const auto available = [&](position point) {
        return !used.count(point) &&
               (!primaryInput || chessboard.getCoorPos_Phase(point.first, point.second) == 1) &&
               (diagonal < 0 || std::uint64_t(point.first) + point.second == std::uint64_t(diagonal));
    };
    std::uniform_int_distribution<unsigned int> x(nw.first, se.first - 1), y(nw.second, se.second - 1);
    for (int attempt = 0; attempt < 64; ++attempt) {
        result = {x(generator), y(generator)};
        if (available(result)) return true;
    }
    // A crowded finite board must not turn mutation/initialization into an
    // unbounded random retry loop. Bounds match set_clockType's half-open grid.
    for (unsigned int row = nw.second; row < se.second; ++row)
        for (unsigned int col = nw.first; col < se.first; ++col)
            if (available({col, row})) { result = {col, row}; return true; }
    return false;
}

void Individual::mutateNodes(std::size_t count) {
    if (nodeindex_pos.empty() || count == 0) return;
    std::set<position> used;
    for (const auto& node : nodeindex_pos) used.insert(node.second);
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> index(0, nodeindex_pos.size() - 1);
    bool changed = false;
    for (std::size_t i = 0; i < count; ++i) {
        auto node = nodeindex_pos.begin();
        std::advance(node, index(generator));
        position replacement;
        if (!findUnusedPosition(used, replacement, parse.getNodeType(node->first) == "input")) continue;
        used.erase(node->second);
        node->second = replacement;
        used.insert(replacement);
        changed = true;
    }
    if (changed) infoReset();
}

void Individual::sameLevelRouting() {
    assert(routes.empty() && layerRoutesLength.empty());
    for(auto &layer_pair : parse.getSameLayerNodeRoutePair()) {
        auto layer = layer_pair.first;
        auto node_pair = layer_pair.second;
        for(auto &node : node_pair) {
            auto nodeStartPosIt = nodeindex_pos.find(node.first);
            auto nodeEndPosIt = nodeindex_pos.find(node.second);
            if (nodeStartPosIt != nodeindex_pos.end() && nodeEndPosIt != nodeindex_pos.end()) {
                position nodeStartPos = nodeStartPosIt->second;
                position nodeEndPos = nodeEndPosIt->second;
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
        auto nodeStartPosIt = nodeindex_pos.find(node_pair.first);
        auto nodeEndPosIt = nodeindex_pos.find(node_pair.second);
        if (nodeStartPosIt != nodeindex_pos.end() && nodeEndPosIt != nodeindex_pos.end()) {
            position nodeStartPos = nodeStartPosIt->second;
            position nodeEndPos = nodeEndPosIt->second;
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
    if (nodeindex_pos.empty() || nodeindex_pos.size() != parse.getEffectiveNodes().size()) return;
    // 检查 nodeindex_pos 中的位置是否唯一
    std::set<position> positions;
    bool unique = true;
    for (const auto& v : nodeindex_pos) {
        if (v.second.first < chessboard.chessboard_nw.first ||
            v.second.second < chessboard.chessboard_nw.second ||
            v.second.first >= chessboard.chessboard_se.first ||
            v.second.second >= chessboard.chessboard_se.second) {
            unique = false;
            break;
        }
        if (parse.getNodeType(v.first) == "input" &&
            chessboard.getCoorPos_Phase(v.second.first, v.second.second) != 1) {
            unique = false;
            break;
        }
        if (!positions.insert(v.second).second) {
            unique = false;
            break;
        }
    }

    if (unique) {
        for (const auto& v : nodeindex_pos) {
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
    is_routed = routes.size() == parse.getEffectiveEdges().size();
    int cnt = 0;
    for(auto&v : routes){
        if(!v.second.empty()){
            cnt ++;
            fitness += 2.0;
        }else{
            is_routed = false;
        }
    }
    fitness += 10.0 * cnt /static_cast<double>(nodeindex_pos.size());
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
    if (!is_placed || nodeindex_pos.empty()) return;
    unsigned int minX = std::numeric_limits<unsigned int>::max(), minY = minX, maxX = 0, maxY = 0;
    const auto include = [&](const position& point) {
        minX = std::min(minX, point.first); minY = std::min(minY, point.second);
        maxX = std::max(maxX, point.first); maxY = std::max(maxY, point.second);
    };
    for (const auto& node : nodeindex_pos) include(node.second);
    for (const auto& route : routes) for (const auto& point : route.second) include(point);
    const long double area = (static_cast<long double>(maxX) - minX + 1) *
                             (static_cast<long double>(maxY) - minY + 1);
    const long double width = static_cast<long double>(chessboard.chessboard_se.first) - chessboard.chessboard_nw.first;
    const long double height = static_cast<long double>(chessboard.chessboard_se.second) - chessboard.chessboard_nw.second;
    if (width > 0 && height > 0) fitness += 7.0L * width * height / area;
}

bool Individual::validateLayout() {
    is_synced = false;
    validation_error.clear();
    if (!is_placed || !is_routed) { validation_error = "placement or routing is incomplete"; return false; }
    GridChessboard snapshot;
    Astar snapshotRouter(snapshot, false);
    CircuitGraph graph(parse, "", snapshot, snapshotRouter);
    graph.nodeIndex_pos.insert(nodeindex_pos.begin(), nodeindex_pos.end());
    graph.routes = routes;
    const auto add = [&](const position& point, bool node) {
        if (point.first < chessboard.chessboard_nw.first || point.second < chessboard.chessboard_nw.second ||
            point.first >= chessboard.chessboard_se.first || point.second >= chessboard.chessboard_se.second)
            return false;
        // The fixed pattern is authoritative (also used by GUI export).
        // GridCell phases in the shared GA scratch board are reset per individual.
        auto& cell = snapshot.gridMap[point];
        cell.setPhase(chessboard.getCoorPos_Phase(point.first, point.second));
        if (node) cell.put_node();
        else if (cell.get_current_weight() == 0) cell.put_wire();
        return true;
    };
    for (const auto& node : nodeindex_pos) if (!add(node.second, true)) {
        validation_error = "placed node lies outside the fixed clock grid"; return false;
    }
    for (const auto& route : routes) for (const auto& point : route.second) if (!add(point, false)) {
        validation_error = "route lies outside the fixed clock grid"; return false;
    }
    const auto result = validateCombinationalLayout(parse, graph, 4, 4);
    validation_error = result.error;
    is_synced = result.valid;
    return is_synced;
}


void Individual::caculate_crossover_value(){
    cross_nodes_pos.clear();
    std::map<position, std::set<unsigned int>> owners;
    for (const auto& route : routes)
        for (std::size_t i = 1; i + 1 < route.second.size(); ++i)
            owners[route.second[i]].insert(route.first.first);
    for (const auto& cell : owners) if (cell.second.size() > 1) cross_nodes_pos.push_back(cell.first);
}






void Individual::computeFitness(){
    add_placement_value_to();
    add_routing_value_to();
    if (is_routed) validateLayout();
    // Routing progress comes first. Compact but incomplete/unsynchronized
    // placements must not dominate the population through the area reward.
    if (is_synced) add_area_value_to();
    if (!std::isfinite(fitness) || fitness < 0) fitness = 0;
    // caculate_crossover_value();
}







};
