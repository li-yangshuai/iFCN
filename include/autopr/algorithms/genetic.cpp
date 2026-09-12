#include"genetic.h"
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace fcngraph{

void GeneticAlgorithm::setFitnessCallback(const std::function<void(double)> &callback) {
    fitnessCallback = callback;
}

uint64_t GeneticAlgorithm::getRandomNumber(uint64_t m, uint64_t n){
    if (m >= n) throw std::invalid_argument("Empty genetic random-index range");
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(m, n-1);
    return dis(gen);
}

void GeneticAlgorithm::crossover(Individual &parent1, Individual &parent2) {
    if (genSize < 2 || parent1.nodeindex_pos.size() != genSize || parent2.nodeindex_pos.size() != genSize) return;

    uint64_t point1 = getRandomNumber(0, genSize);
    uint64_t point2 = getRandomNumber(0, genSize - 1);
    if (point2 >= point1) ++point2;
    if (point1 > point2) {
        std::swap(point1, point2);
    }

    auto it1 = parent1.nodeindex_pos.begin();
    auto it2 = parent2.nodeindex_pos.begin();
    std::advance(it1, point1);
    std::advance(it2, point1);
    
    for (std::size_t i = point1; i <= point2; ++i) {
        if (it1 == parent1.nodeindex_pos.end() || it2 == parent2.nodeindex_pos.end()) break;
        std::swap(it1->second, it2->second);
        ++it1;
        ++it2;
    }
    parent1.infoReset();
    parent2.infoReset();
}


void GeneticAlgorithm::mutate(Individual &individual) {
    individual.mutateNodes(std::max<std::size_t>(1, genSize / 4));
}

void GeneticAlgorithm::reserve_the_best(){
    Individual* best = nullptr;
    for (auto& individual : populations) {
        if (!std::isfinite(individual.getfitness()) || !individual.is_routed || !individual.validateLayout()) continue;
        if (!best || individual.getfitness() > best->getfitness()) best = &individual;
    }
    if (best && (best_individuals.empty() || best->getfitness() > best_individuals.back().getfitness())) {
        best_individuals.push_back(*best);
        if (fitnessCallback) {
            fitnessCallback(best->getfitness());
        }
    }
}


void GeneticAlgorithm::select_next_generation() {
    if (populations.empty() || populationSize == 0) return;

    // Calculate the total fitness of the current population
    long double maximum = 0.0;
    for (const auto& individual : populations) {
        if (std::isfinite(individual.getfitness())) maximum = std::max(maximum, individual.getfitness());
    }
    std::vector<long double> weights;
    for (const auto& individual : populations) {
        const auto fitness = individual.getfitness();
        weights.push_back(maximum > 0 ? (std::isfinite(fitness) && fitness > 0 ? fitness / maximum : 0) : 1);
    }
    const long double fitness_sum = std::accumulate(weights.begin(), weights.end(), 0.0L);

    // Calculate relative fitness and cumulative fitness
    std::vector<long double> refitness(populations.size(), 0.0);
    std::vector<long double> cfitness(populations.size(), 0.0);
    refitness[0] = weights[0] / fitness_sum;
    cfitness[0] = refitness[0];

    for (std::size_t i = 1; i < populations.size(); ++i) {
        refitness[i] = weights[i] / fitness_sum;
        cfitness[i] = cfitness[i - 1] + refitness[i];
    }

    // Initialize the random number generators
    static std::random_device rd;
    static std::mt19937 engine(rd());
    static std::uniform_real_distribution<double> dis(0.0, 1.0);

    // Generate the new population based on the cumulative fitness
    std::vector<Individual> new_populations;
    new_populations.reserve(populationSize);

    for (std::size_t i = 0; i < populationSize; ++i) {
        double p = dis(engine);
        auto it = std::lower_bound(cfitness.begin(), cfitness.end(), p);
        if (it != cfitness.end()) {
            std::size_t index = std::distance(cfitness.begin(), it);
            new_populations.push_back(populations[index]);
        } else {
            // If somehow no fitting index is found, push the last element
            new_populations.push_back(populations.back());
        }
    }

    // Optionally keep the best individual if not already included
    if (!best_individuals.empty()) {
        // Make sure the best individual is added to the new generation
        new_populations.back() = best_individuals.back();
    }

    // Replace the old population with the new one
    populations = std::move(new_populations);
}

bool GeneticAlgorithm::gaRun(){
    static std::random_device engine;
    static std::uniform_real_distribution<double> dis(0.0, 1.0);

    populations.clear();
    best_individuals.clear();
    if (!generationSize || !populationSize || !genSize ||
        !std::isfinite(crossoverRate) || crossoverRate < 0 || crossoverRate > 1 ||
        !std::isfinite(mutationRate) || mutationRate < 0 || mutationRate > 1) return false;

    //产生初代种群
    for(uint64_t i = 0; i < populationSize; i++){
        populations.push_back(Individual(parse, chessboard, astar));
        if (populations.back().nodeindex_pos.size() != genSize) return false;
    }

    //迭代进化
    for(uint64_t i = 0; i < generationSize; i++){
        for(auto &individual: populations){
            individual.infoReset();
            individual.computeFitness();
        }

        reserve_the_best();
        select_next_generation();

        for (std::size_t index = 0; index + 1 < populations.size(); index += 2) {
            if (dis(engine) < crossoverRate) {
                crossover(populations[index], populations[index + 1]);
            }
        }

        for (auto &individual: populations) {
            if (dis(engine) < mutationRate) {
                mutate(individual);
            }
        }
    }

    if(best_individuals.empty() ){
        return false;
    }else{
        std::sort(best_individuals.begin(), best_individuals.end());
        return true;
    }
}



void GeneticAlgorithm::printLaTex(CLOCK_SCHEME  _clockType, position _northWest, position _southEast, 
    std::map<unsigned int, position> nodeIndex_pos, std::map<std::pair<unsigned int, unsigned int>, 
    std::vector<position>> routes, std::vector<position> cross_nodes,
    const std::map<position, int> &phaseMap) {
    //坐标转换
    int widget_H = _southEast.second;
    std::string filename = parse.get_moduleName() +".tex";
    std::ofstream os(filename);
    if (!os.is_open()) {
        std::cerr << "Error opening file!" << std::endl;
        return;
    }
    


    os << R"(\documentclass[tikz,border=4mm]{standalone}
%graphics
\usepackage{pgfmath}
\usetikzlibrary{calc,arrows.meta}

\begin{document}
\begin{tikzpicture}[
scale=0.5,transform shape,
c1/.style={rectangle, fill, lightgray!50, minimum size=1cm},
c2/.style={rectangle, fill, lightgray, minimum size=1cm},
c3/.style={rectangle, fill, gray, minimum size=1cm},
c4/.style={rectangle, fill, darkgray!90, minimum size=1cm},
route/.style={->, >={Stealth[]},line width=0.8pt, blue!50},
v/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.7cm},
]
)" << std::endl;

            os << R"(\def\layoutw{)" << _southEast.first << "}" << std::endl;
            os << R"(\def\layouth{)" << _southEast.second << "}" << std::endl;

            if (!phaseMap.empty()) {
                os << R"(%phase map exported from the routed layout; raw origin is top-left (0,0))" << std::endl;
                for (unsigned int x = 0; x < _southEast.first; ++x) {
                    for (unsigned int y = 0; y < _southEast.second; ++y) {
                        const auto phaseIt = phaseMap.find({x, y});
                        const int rawPhase = (phaseIt != phaseMap.end()) ? phaseIt->second : 0;
                        int stylePhase = rawPhase + 1;
                        if (stylePhase < 1) {
                            stylePhase = 1;
                        } else if (stylePhase > 4) {
                            stylePhase = 4;
                        }
                        const unsigned int drawY = _southEast.second - y - 1;
                        os << "\\node[c" << stylePhase << "]at(" << x << "," << drawY << "){};" << std::endl;
                    }
                }
                os << std::endl;
            } else {
            os << R"(\pgfmathtruncatemacro{\xmax}{\layoutw-1}
\pgfmathtruncatemacro{\ymax}{\layouth-1}
\foreach \x in {0,1,...,\xmax}
  \foreach \y in {0,1,...,\ymax})" << std::endl;
            
            switch (_clockType) {
                case CLOCK_SCHEME::USE:
                    os
                            // << R"("\pgfmathparse{(mod(\y,2)!=0) ? ((mod(\y+1,4)!=0)?(1+mod(\x+1,4)):(1+mod(\x+3,4))):((mod(\y,4)==0)?(4-mod(\x+3,4)):(4-mod(\x+1,4)))})"
                            << R"(\pgfmathparse{(mod(\y,2)!=0) ? 
                                ((mod(\y+1,4)!=0)?(1+mod(\x+2,4)):(1+mod(\x,4))) :
                                ((mod(\y,4)==0)?(4-mod(\x+2,4)):(4-mod(\x,4)))})"
                            << std::endl;
                    break;
                case CLOCK_SCHEME::TDD:
                    os
                            // << R"(\pgfmathparse{(mod(\y, 2)!=0) ? ((mod(\y+1, 4)!=0)?(1+mod(\x+1, 4)):(1+mod(\x+3, 4))):((mod(\y, 4)==0)?(1+mod(\x, 4)):(1+mod(\x+2, 4)))})"
                            << R"(\pgfmathparse{(mod(\y, 2)!=0) ? 
                                ((mod(\y+1, 4)!=0)?(1+mod(\x+2, 4)):(1+mod(\x, 4))) :
                                ((mod(\y, 4)==0)?(1+mod(\x+3, 4)):(1+mod(\x+1, 4)))})"
                            << std::endl;
                    break;
                case CLOCK_SCHEME::CFE:
                    os
                            << R"(\pgfmathparse{(mod(\y,2)!=0) ? ((mod(\x,2)!=0)? 4 : 1) :((mod(\x,2) !=0)? 3 :2})"
                            << std::endl;
                    break;
                case CLOCK_SCHEME::RES:
                    os
                            << R"(\pgfmathparse{(mod(\y,2)!=0) ?
                                ((mod(\y+1,4)!=0)? (1+ mod(\x+1,4)): (1+mod(\x+3,4))):
                                (mod(\y,4)==0 ? (4-mod(\x+3,4)) : ((mod(\x,4)==2)? 1 :(1+ mod(\x,4))})"
                            << std::endl;
                    break;
                case CLOCK_SCHEME::BANCS:
                    os
                            <<R"(\pgfmathparse{(mod(\y,6) == 0) ? (3 - mod(\x+2,3)) :
                                ((mod(\y-1,6) == 0) ? (1 + mod(\x+4,3)) :
                                ((mod(\y-2,6) == 0) ? (3 - mod(\x+4,3)) :
                                ((mod(\y-3,6) == 0) ? (1 + mod(\x+2,3)) :
                                ((mod(\y-4,6) == 0) ? (3 - mod(\x+3,3)) : (1 + mod(\x+3,3))))))})"
                            << std::endl;
                    break;
            }
	            os << R"(\pgfmathparse{int(\pgfmathresult)}
	    \node[c\pgfmathresult]at(\x,\y){};)" << std::endl;
	            os << std::endl;
            }

	            os << R"(%nodes and edges)" << std::endl;
            for(auto &node : parse.getEffectiveNodes()){
                auto node_pos = nodeIndex_pos[node];
                auto x = node_pos.first;
                auto y = widget_H - node_pos.second - 1;
                auto nodeType = parse.getNodeType(node);
                std::string nodeName = parse.getNodeName(node);
                if(nodeType == "input"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << nodeName << "};" << std::endl;
                }else if(nodeType == "output"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << nodeName << "};" << std::endl;
                }else if(nodeType == "maj"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "M" << "};" << std::endl;
                }else if(nodeType == "and"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "\\&" << "};" << std::endl;
                }else if(nodeType == "or"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "\\textbar" << "};" << std::endl;
                }else if(nodeType == "not"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "$\\neg$" << "};" << std::endl;
                }else if(nodeType == "wire"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "w" << "};" << std::endl;
                }else if(nodeType == "fanout"){
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "F" << "};" << std::endl;
                }else{
                    os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "" << "};" << std::endl;
                }

            }

            os << std::endl;
            for(auto &route : routes){
                auto node_pair = route.first;
                auto path = route.second;
                if(path.empty()) continue;
                auto node_1 = node_pair.first;
                auto node_2 = node_pair.second;
                os << R"(\draw[route] )" << "(" << node_1 << ") -- ";
                
                for (size_t i = 1; i < path.size() - 1; ++i) {
                    auto pos = path[i];
                    auto x = pos.first;
                    auto y = widget_H - pos.second - 1;
                    os << "(" << x << "," << y << ") -- ";
                }
                os << "(" << node_2 << ");" << std::endl;
            }

            if(!cross_nodes.empty()){
                for(auto &pos : cross_nodes){
                auto x = pos.first;
                auto y = widget_H - pos.second - 1;
                os << R"(\node[red]()" << ") [v] at (" << x << "," << y << ") {" << "x" << "};" << std::endl;
            }

            }



            os << std::endl;

            os << R"(\end{tikzpicture}
\end{document})" << std::endl;
}








};
