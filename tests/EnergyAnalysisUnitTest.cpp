#include "catch.hpp"
#include <simon/simon.hpp>
#include <cassert>

using namespace simon;

void energy_sim (std::string &fname, Result &result){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    assert(ret);

    EnergyAnalysisOption option;
    VectorTable vector_table;

    COSEnergyAnalysisAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Exhaustive);
 
}

TEST_CASE("xor-28 energy anlysis") {
    std::cout << "xor-28" << std::endl;
    std::string fname  = std::string(TESTCASE) + "/xor-28.qca";
    Result result;
    energy_sim(fname, result);
    result.write_text_file("xor-28_e.rst");
} 


TEST_CASE("xor-92 energy anlysis") {
    std::cout << "xor-92" << std::endl;
    std::string fname  = std::string(TESTCASE) + "/xor-92.qca";
    Result result;
    energy_sim(fname, result);
    result.write_text_file("xor-92_e.rst");
} 

TEST_CASE("add-35 energy anlysis") {
    std::cout << "add-35" << std::endl;
    std::string fname  = std::string(TESTCASE) + "/add-35.qca";
    Result result;
    energy_sim(fname, result);
    result.write_text_file("add-35_e.rst");
} 

TEST_CASE("add-124 energy anlysis") {
    std::cout << "add-124" << std::endl;
    std::string fname  = std::string(TESTCASE) + "/add-124.qca";
    Result result;
    energy_sim(fname, result);
    result.write_text_file("add-124_e.rst");
} 
