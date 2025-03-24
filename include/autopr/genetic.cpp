#include"genetic.h"

namespace fcngraph{

void GeneticAlgorithm::setFitnessCallback(const std::function<void(double)> &callback) {
    fitnessCallback = callback;
}

uint64_t GeneticAlgorithm::getRandomNumber(uint64_t m, uint64_t n){
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(m, n-1);
    return dis(gen);
}

void GeneticAlgorithm::crossover(Individual &parent1, Individual &parent2) {
    assert(parent1.nodeindex_morton.size() == parent2.nodeindex_morton.size() && parent2.nodeindex_morton.size() == genSize);

    uint64_t point1 = getRandomNumber(0, genSize);
    uint64_t point2 = getRandomNumber(0, genSize);
    while (point1 == point2) {
        point2 = getRandomNumber(0, genSize);
    }
    if (point1 > point2) {
        std::swap(point1, point2);
    }

    auto it1 = parent1.nodeindex_morton.begin();
    auto it2 = parent2.nodeindex_morton.begin();
    std::advance(it1, point1);
    std::advance(it2, point1);
    
    for (std::size_t i = point1; i <= point2; ++i) {
        if (it1 == parent1.nodeindex_morton.end() || it2 == parent2.nodeindex_morton.end()) break;
        std::swap(it1->second, it2->second);
        ++it1;
        ++it2;
    }
}


void GeneticAlgorithm::mutate(Individual &individual) {
    auto sz = static_cast<std::size_t>(genSize / 4);
    std::set<unsigned int> usedPositions;

    // 初始化集合，存储当前所有节点的位置
    for (const auto& entry : individual.nodeindex_morton) {
        usedPositions.insert(entry.second);
    }

    for (std::size_t i = 0; i < sz; ++i) {
        auto randomIndex = getRandomNumber(0, individual.nodeindex_morton.size());
        auto it = individual.nodeindex_morton.begin();
        std::advance(it, randomIndex);

        if (it != individual.nodeindex_morton.end()) {
            unsigned int newPos;
            do {
                newPos = chessboard.randomMortonPos();
            } while (usedPositions.find(newPos) != usedPositions.end());  // 确保位置不重复

            // 更新位置
            usedPositions.erase(it->second);  // 从集合中移除旧位置
            it->second = newPos;  // 更新到新位置
            usedPositions.insert(newPos);  // 将新位置加入集合
        }
    }
}

void GeneticAlgorithm::reserve_the_best(){
    auto it = std::max_element(populations.begin(), populations.end());
    if (it->is_routed) {
        best_individuals.push_back(*it);
        if (fitnessCallback) {
            fitnessCallback(it->getfitness());  // Call the callback with the fitness value
        }
    }
}


void GeneticAlgorithm::select_next_generation() noexcept {
    assert(!populations.empty());

    // Calculate the total fitness of the current population
    long double fitness_sum = 0.0;
    for (const auto& individual : populations) {
        fitness_sum += individual.getfitness();
    }
    if (fitness_sum == 0) return;  // Prevent division by zero

    // Calculate relative fitness and cumulative fitness
    std::vector<long double> refitness(populationSize, 0.0);
    std::vector<long double> cfitness(populationSize, 0.0);
    refitness[0] = populations[0].getfitness() / fitness_sum;
    cfitness[0] = refitness[0];

    for (std::size_t i = 1; i < populations.size(); ++i) {
        refitness[i] = populations[i].getfitness() / fitness_sum;
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
        new_populations.back() = std::move(best_individuals.back());
    }

    // Replace the old population with the new one
    populations = std::move(new_populations);
}

bool GeneticAlgorithm::gaRun(){
    static std::random_device engine;
    static std::uniform_real_distribution<double> dis(0.0, 1.0);

    if(!populations.empty()){
        populations.clear();
        best_individuals.clear();
    }

    //产生初代种群
    for(int i = 0; i < populationSize; i++){
        populations.push_back(Individual(parse, chessboard, astar));
    }

    //迭代进化
    for(int i = 0; i < generationSize; i++){
        for(auto &individual: populations){
            individual.infoReset();
            individual.computeFitness();
        }

        reserve_the_best();
        select_next_generation();

        for (auto it1 = populations.begin(), it2 = populations.begin() + 1;
            it1 < populations.end() - 1; it1 += 2, it2 += 2) {
            if (it2 == populations.end()) break;  // 如果 it2 是末尾，停止循环
            if (dis(engine) < crossoverRate) {
                crossover(*it1, *it2);
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



void GeneticAlgorithm::printLaTex(CLOCK_SCHEME  _clockType, position _northWest, position _southEast, std::map<unsigned int, unsigned int> nodeIndex_morton, std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>>routes){
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

            os << R"(\def\sz{)" << _southEast.first << "}" << std::endl;

            os << R"(\pgfmathparse{\sz-1}
\foreach \x in {0,1,...,\pgfmathresult}
  \foreach \y in {0,1,...,\pgfmathresult})" << std::endl;
            
            switch (_clockType) {
                case CLOCK_SCHEME::USE:
                    os
                            // << R"("\pgfmathparse{(mod(\y,2)!=0) ? ((mod(\y+1,4)!=0)?(1+mod(\x+1,4)):(1+mod(\x+3,4))):((mod(\y,4)==0)?(4-mod(\x+3,4)):(4-mod(\x+1,4)))})"
                            << R"("\pgfmathparse{(mod(\y,2)!=0) ? 
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
                            <<R"(\pgfmathparse{(mod(\y,6)) == 0) ? (3 - mod(\x+2,3)):
		                        ((mod(\y-1,6) == 0) ? (1 + mod(\x+4,3)) :
			                    (((mod(\y-2,6) == 0) ? (3 - mod(\x + 4,3)) :
				                (((mod(\y-3,6) == 0) ? (1 + mod(\x + 2 , 3)) :
					            (((mod((\y - 4) , 6) == 0) ? (3 - mod((\x + 3) , 3)) : (1 + mod((\x + 3) , 3))))))})"
                            << std::endl;
                    break;
            }
            os << R"(\pgfmathparse{int(\pgfmathresult)}
    \node[c\pgfmathresult]at(\x,\y){};)" << std::endl;
            os << std::endl;

            os << R"(%nodes and edges)" << std::endl;
            for(auto &node : parse.getEffectiveNodes()){
                auto node_morton = nodeIndex_morton[node];
                auto x_y = MortonChessboard::decodeMortonCode(node_morton);
                auto x = x_y.first;
                auto y = widget_H - x_y.second - 1;
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
                    auto x_y = MortonChessboard::decodeMortonCode(pos);
                    auto x = x_y.first;
                    auto y = widget_H - x_y.second - 1;
                    os << "(" << x << "," << y << ") -- ";
                }
                os << "(" << node_2 << ");" << std::endl;
            }

            os << std::endl;

            os << R"(\end{tikzpicture}
\end{document})" << std::endl;
}








};