#pragma once

#include"individual.h"
#include<random>
#include"mortonGrid.h"
#include <functional>

namespace fcngraph{

class GeneticAlgorithm{

public:
    GeneticAlgorithm(Parse &_parse, MortonChessboard &_chessboard, Astar &_astar,
    uint64_t _generationSize, uint64_t _populationSize, 
    double _crossoverRate, double _mutationRate):
        parse(_parse), 
        chessboard(_chessboard), 
        astar(_astar),
        generationSize(_generationSize),
        populationSize(_populationSize),
        crossoverRate(_crossoverRate),
        mutationRate(_mutationRate)
    {
        genSize = parse.getEffectiveNodes().size();
        populations.reserve(populationSize);
        best_individuals.reserve(generationSize);
    }

    
    uint64_t getRandomNumber(uint64_t m, uint64_t n);
    void crossover(Individual &parent1, Individual &parent2);
    void mutate(Individual &individual);

    void reserve_the_best();
    void select_next_generation() noexcept;
    bool gaRun();

    //print node_pos
    auto getNodePos() const {
        return  best_individuals.back().nodeindex_morton;
    }

    auto getRoutes() const {
        return best_individuals.back().routes;
    }
    
    void printLaTex(CLOCK_SCHEME  _clockType, position _northWest, position _southEast, std::map<unsigned int, unsigned int> nodeIndex_morton, std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>>routes);
    

    std::vector<Individual> populations;       //种群
    std::vector<Individual> best_individuals;  //一代产生一个最优

    //回调函数
    void setFitnessCallback(const std::function<void(double)> &callback);


private:
    Parse &parse;
    MortonChessboard &chessboard;
    Astar &astar;
    uint64_t generationSize;
    uint64_t populationSize;
    double crossoverRate;
    double mutationRate;
    uint64_t genSize;

    //回调打印的信息用；
    std::function<void(double)> fitnessCallback;

};

};
