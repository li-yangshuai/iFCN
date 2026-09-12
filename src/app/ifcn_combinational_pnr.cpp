// Headless access to the production C++ combinational layout algorithms.
// Emits a candidate snapshot; the Python adapter independently validates and
// exports it before making it available to downstream device mapping.
#include <autopr/algorithms/astar.h>
#include <autopr/algorithms/mapping.h>
#include <autopr/graph/circuitGraph.h>
#include <autopr/graph/parse.h>
#include <autopr/grid/grid.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace fcngraph;
std::string quote(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 32) throw std::runtime_error("Unsupported control character in circuit name");
        else out << c;
    }
    out << '"'; return out.str();
}
void orderLayers(std::vector<std::vector<int>>& layers,
                 const std::vector<std::pair<int,int>>& edges) {
    std::map<int,std::vector<int>> incoming, outgoing;
    for (auto edge : edges) { incoming[edge.second].push_back(edge.first); outgoing[edge.first].push_back(edge.second); }
    auto sweep = [&](bool forward) {
        std::map<int,double> positions;
        for (const auto& layer : layers) for (std::size_t i=0;i<layer.size();++i) positions[layer[i]]=i;
        for (std::size_t offset=0;offset<layers.size();++offset) {
            auto& layer=layers[forward ? offset : layers.size()-offset-1];
            const auto& adjacent=forward ? incoming : outgoing;
            std::map<int,double> score;
            for (int node:layer) {
                auto found=adjacent.find(node); double sum=0; std::size_t count=0;
                if (found!=adjacent.end()) for(int other:found->second) { sum+=positions[other]; ++count; }
                score[node]=count ? sum/count : positions[node];
            }
            std::stable_sort(layer.begin(),layer.end(),[&](int a,int b){return score[a]<score[b];});
            for(std::size_t i=0;i<layer.size();++i) positions[layer[i]]=i;
        }
    };
    for(int i=0;i<4;++i) { sweep(true); sweep(false); }
}
std::string validateMapping(Parse& parse, CircuitGraph& graph) {
    NodeLinkMap links;
    std::set<position> occupied;
    for(const auto& node:graph.nodeIndex_pos) {
        if(!occupied.insert(node.second).second) return "multiple nodes share a coordinate";
        links[{node.second,parse.getNodeType(node.first)}]={{},{}};
    }
    std::set<std::pair<int,int>> expected;
    for(auto edge:parse.getEffectiveEdges()) expected.insert(edge);
    if(graph.routes.size()!=expected.size()) return "route count does not equal effective-edge count";
    std::map<unsigned int,std::set<position>> sinkPorts;
    std::vector<std::vector<position>> geometry;
    for(const auto& route:graph.routes) {
        const auto edge=route.first; const auto& path=route.second;
        if(!expected.count({edge.first,edge.second})) return "unexpected route edge";
        if(path.size()<2) return "route has fewer than two coordinates";
        if(!graph.nodeIndex_pos.count(edge.first)||!graph.nodeIndex_pos.count(edge.second)) return "route references missing node";
        if(path.front()!=graph.nodeIndex_pos.at(edge.first)||path.back()!=graph.nodeIndex_pos.at(edge.second)) return "route endpoints disagree with node positions";
        if(!sinkPorts[edge.second].insert(path[path.size()-2]).second) return "multiple fanins share a physical sink port";
        std::set<position> unique;
        for(std::size_t i=0;i<path.size();++i) {
            if(!unique.insert(path[i]).second) return "route repeats a coordinate";
            if(i && std::abs(static_cast<long long>(path[i].first)-path[i-1].first)+std::abs(static_cast<long long>(path[i].second)-path[i-1].second)!=1) return "route is not four-connected";
        }
        links[{path.front(),parse.getNodeType(edge.first)}].second.push_back(path[1]);
        links[{path.back(),parse.getNodeType(edge.second)}].first.push_back(path[path.size()-2]);
        geometry.push_back(path);
    }
    for(auto& node:links) for(auto* ports:{&node.second.first,&node.second.second}) {
        std::sort(ports->begin(),ports->end()); ports->erase(std::unique(ports->begin(),ports->end()),ports->end());
    }
    Mapping mapping;
    mapping.node_mapping(links,MappingMode::Combinational);
    mapping.mapping_line(geometry,MappingMode::Combinational);
    std::string error;
    if(!mapping.validate_crossovers(&error)) return "device crossover mapping: "+error;
    return {};
}
void snapshot(const std::filesystem::path& output, Parse& parse, CircuitGraph& graph,
              const GridChessboard& board, const std::vector<std::vector<int>>& layers,
              const std::string& algorithm, bool routed, const std::string& validationError,
              double elapsed) {
    std::ofstream out(output); if(!out) throw std::runtime_error("Cannot write native candidate snapshot");
    out << "{\n\"schema\":\"ifcn.native_candidate.v1\",\"algorithm\":" << quote(algorithm)
        << ",\"routed\":" << (routed?"true":"false") << ",\"native_mapping_valid\":" << (validationError.empty()&&routed?"true":"false")
        << ",\"native_validation_error\":" << quote(validationError) << ",\"run_time_s\":" << elapsed << ",\"layers\":[";
    bool first=true;
    for(const auto& layer:layers) { if(!first) out<<','; first=false; out<<'['; bool firstNode=true; for(int id:layer) {if(!firstNode)out<<',';firstNode=false;out<<id;}out<<']'; }
    out << "],\"nodes\":["; first=true;
    for(const auto& node:graph.nodeIndex_pos) {
        if(!first)out<<',';first=false;
        out << "{\"id\":" << node.first << ",\"name\":" << quote(parse.getNodeName(node.first)) << ",\"type\":" << quote(parse.getNodeType(node.first))
            << ",\"is_input\":" << (parse.getNodeType(node.first)=="input"?"true":"false")
            << ",\"is_output\":" << (parse.getOutputNodesIndex().count(node.first)?"true":"false")
            << ",\"x\":" << node.second.first << ",\"y\":" << node.second.second << '}';
    }
    out << "],\"edges\":["; first=true;
    for(auto edge:parse.getEffectiveEdges()) {if(!first)out<<',';first=false;out<<'['<<edge.first<<','<<edge.second<<']';}
    out << "],\"routes\":["; first=true;
    for(const auto& route:graph.routes) {
        if(!first)out<<',';first=false;
        out << "{\"source\":" << route.first.first << ",\"target\":" << route.first.second << ",\"path\":[";
        bool firstPoint=true; for(auto p:route.second) {if(!firstPoint)out<<',';firstPoint=false;out<<'['<<p.first<<','<<p.second<<']';}out<<"]}";
    }
    out << "],\"cells\":["; first=true;
    for(const auto& entry:board.gridMap) {
        if(entry.second.get_current_weight()==0)continue;
        if(!first)out<<',';first=false;
        out << "{\"x\":" << entry.first.first << ",\"y\":" << entry.first.second << ",\"phase\":" << entry.second.getPhase() << '}';
    }
    const auto& stats=graph.getAdaptiveExpansionStats();
    out << "],\"expansion\":{\"inserted_rows\":"<<stats.insertedRows<<",\"inserted_columns\":"<<stats.insertedColumns<<",\"accepted_rounds\":"<<stats.acceptedRounds<<"}}\n";
    if(!out) throw std::runtime_error("Failed writing native candidate snapshot");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=4) {std::cerr<<"Usage: ifcn_combinational_pnr <compact|june_random> <input.v> <candidate.json>\n";return 2;}
        const std::string algorithm=argv[1];
        if(algorithm!="compact"&&algorithm!="june_random") throw std::runtime_error("Unknown native algorithm");
        Parse parse; parse.parseVerilog(argv[2]);
        if(!parse.get_input_num()||!parse.get_output_num()) throw std::runtime_error("Circuit must have primary inputs and outputs");
        parse.optimizeAIOG_DRC(2,2,2,2,2,2);
        if(algorithm=="june_random") parse.addLayerRedundancyNode(); else parse.optimizeBufferNode();
        parse.caculateSameLayerNodeRoutePair();
        std::vector<std::vector<int>> layers;
        for(const auto& layer:parse.getlayerNodeDivVec()) layers.emplace_back(layer.begin(),layer.end());
        GridChessboard board; Astar router(board,false,algorithm=="compact"?240.0:40.0);
        router.setAllowInterSourceWireOverlap(false);
        CircuitGraph graph(parse,argv[2],board,router);
        graph.setFitnessCallback([](const std::string& m){std::cerr<<m<<'\n';});
        const auto start=std::chrono::steady_clock::now(); bool routed=false;
        if(algorithm=="compact") {
            router.setOccupiedWirePenalty(0.0); orderLayers(layers,parse.getEffectiveEdges());
            graph.sortNodesByFixedLayerOrder(layers,1,1,2,2);
            routed=graph.routeCompactRandomClockWithExpansion(4,12,8,240.0,4);
        } else routed=graph.placeAndRouteJuneRandomClock(4,40.0,24);
        std::string error;
        if(!routed) error="Native placement/routing did not find a legal candidate";
        else if(!graph.validateAssignedRoutePhases(4)) error="Native phase contract validation failed";
        else error=validateMapping(parse,graph);
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        snapshot(argv[3],parse,graph,board,layers,algorithm,routed,error,elapsed);
        if(!error.empty()){std::cerr<<error<<'\n';return 10;}
        std::cout<<"IFCN_NATIVE_CANDIDATE_READY\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
