#include "catch.hpp"

#include <simon/simon.hpp>

using namespace simon;

//bistable with exhaustive
void bistable_sim (std::string &fname, Result &result){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    REQUIRE(ret);

    QCABistableOption option;
    VectorTable vector_table;

    BistableAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Exhaustive);
 
}

//bistable with selective
void bistable_sim (std::string &fname, std::string &vfname, Result &result ){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    REQUIRE(ret);

    QCABistableOption option;
    VectorTable vector_table;
    
    bool parsing_result = parse_vector_table(vfname, vector_table);
    REQUIRE( parsing_result );

    BistableAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Selective);
 
}

//coherence with exhaustive
void coherence_sim (std::string &fname, Result &result){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    REQUIRE(ret);

    QCACoherenceOption option;
    VectorTable vector_table;

    CoherenceAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Exhaustive);
 
}

//coherence with selective
void coherence_sim (std::string &fname, std::string &vfname, Result &result ){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    REQUIRE(ret);

    QCACoherenceOption option;
    VectorTable vector_table;
    
    bool parsing_result = parse_vector_table(vfname, vector_table);
    REQUIRE( parsing_result );

    CoherenceAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Selective);
 
}


TEST_CASE( "xor-14 simulation") {

    std::string fname  = std::string(TESTCASE) + "/xor-14.qca";

    SECTION( "xor-14 bistable exhaustive simulation" ){
   
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-14_b_e.rst");
}
    SECTION( "xor-14 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-14_b_s.rst");
}


    SECTION( "xor-14 coherence exhaustive simulation" ){
       
        Result result;
        coherence_sim(fname, result);
        result.write_text_file("xor-14_c_e.rst");
}
    SECTION( "xor-14 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-14_c_s.rst");
}
} 


TEST_CASE( "xor-28 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-28.qca";
    
    SECTION( "xor-28 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-28_b_e.rst");
}
/*    SECTION( "xor-28 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-28_b_s.rst");
}*/
    SECTION( "xor-28 coherence exhaustive simulation" ){
       
        Result result;
        coherence_sim(fname, result);
        result.write_text_file("xor-28_c_e.rst");
}
/*    SECTION( "xor-28 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-28_c_s.rst");
}*/
}

TEST_CASE( "xor-42 simulation" ) {
    
    std::string fname  = std::string(TESTCASE) + "/xor-42.qca";
    
    SECTION( "xor-42 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-42_b_e.rst");
}
/*    SECTION( "xor-42 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-42_b_s.rst");
}*/
    SECTION( "xor-42 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-42_c_e.rst");
}
/*    SECTION( "xor-42 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-42_c_s.rst");
}*/
}

TEST_CASE( "xor-48 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-48.qca";
    
    SECTION( "xor-48 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-48_b_e.rst");
}
/*    SECTION( "xor-48 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-48_b_s.rst");
}*/  
    SECTION( "xor-48 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-48_c_e.rst");
}
/*    SECTION( "xor-48 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-48_c_s.rst");
}*/
}

TEST_CASE( "xor-54 simulation" ) {
 
    std::string fname  = std::string(TESTCASE) + "/xor-54.qca";
   
    SECTION( "xor-54 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-54_b_e.rst");
}
/*   SECTION( "xor-54 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-54_b_s.rst");
}*/
    SECTION( "xor-54 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-54_c_e.rst");
}
/*    SECTION( "xor-54 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-54_c_s.rst");
}*/
}

TEST_CASE( "xor-67 simulstion" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-67.qca";
    
    SECTION( "xor-67 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-67_b_e.rst");
}
/*    SECTION( "xor-67 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-67_b_s.rst");
}*/ 
     SECTION( "xor-67 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-67_c_e.rst");
}
/*     SECTION( "xor-67 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-67_c_s.rst");
}*/
}

TEST_CASE( "xor-92 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-92.qca";
    
    SECTION( "xor-92 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-92_b_e.rst");
}
/*    SECTION( "xor-92 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("xor-92_b_s.rst");
} */

    SECTION( "xor-92 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("xor-92_c_e.rst");
}
/*    SECTION( "xor-92 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("xor-92_c_s.rst");
}*/

}

TEST_CASE( "maj-5 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/maj-5.qca";
    
    SECTION( "maj-5 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("maj-5_b_e.rst");
}
/*    SECTION( "maj-5 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("maj-5_b_s.rst");
} */ 
    SECTION( "maj-5 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("maj-5_c_e.rst");
}
/*    SECTION( "maj-5 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("maj-5_c_s.rst");
}*/
}

TEST_CASE( "maj-10 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/maj-10.qca";
   
    SECTION( "maj-10 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("maj-10_b_e.rst");
}
    SECTION( "maj-10 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("maj-10_c_e.rst");
}
}

TEST_CASE( "maj-18 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/maj-18.qca";
    
    SECTION( "maj-18 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("maj-18_b_e.rst");
}
    SECTION( "maj-18 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("maj-18_c_e.rst");
}
}

TEST_CASE( "mul-27 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/mul-27.qca";
    
    SECTION( "mul-27 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("mul-27_b_e.rst");
}
/*    SECTION( "mul-27 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("mul-27_b_s.rst");
} */ 
    SECTION( "mul-27 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("mul-27_c_e.rst");
}
/*    SECTION( "mul-27 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("mul-27_c_s.rst");
}*/
}

TEST_CASE( "mul-77 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/mul-77.qca";
   
    SECTION( "mul-77 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("mul-77_b_e.rst");
}
    SECTION( "mul-77 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("mul-77_c_e.rst");
}
}

TEST_CASE( "mul-145 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/mul-145.qca";

    SECTION( "mul-145 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("mul-145_b_e.rst");
}
    SECTION( "mul-145 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("mul-145_c_e.rst");
}
}

TEST_CASE( "inv-4 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/inv-4.qca";
    
    SECTION( "inv-4 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("inv-4_b_e.rst");
}
/*    SECTION( "inv-4 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("inv-4_b_s.rst");
}*/  
    SECTION( "inv-4 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("inv-4_c_e.rst");
}
/*    SECTION( "inv-4 coherence selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("inv-4_c_s.rst");
}*/
}

TEST_CASE( "inv-8 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/inv-8.qca";
    
    SECTION( "inv-8 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("inv-8_b_e.rst");
}
    SECTION( "inv-8 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("inv-8_c_e.rst");
}
}

TEST_CASE( "add-72 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/add-72.qca";

    SECTION( "add-72 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("add-72_b_e.rst");
}
/*  SECTION( "add-72 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("add-72_b_s.rst");
}  */
    SECTION( "add-72 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("add-72_c_e.rst");
} 
/*    SECTION( "add-72 coherence selective simulation" ){

        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("add-72_c_s.rst");
}*/
}

TEST_CASE( "add-84 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/add-84.qca";
    
    SECTION( "add-84 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("add-84_b_e.rst");
}
    SECTION( "add-84 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("add-84_c_e.rst");
}
}

TEST_CASE( "add-312 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/add-312.qca";
    
    SECTION( "add-312 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("add-312_b_e.rst");
}
    SECTION( "add-312 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("add-312_c_e.rst");
}
}

TEST_CASE( "dec-101 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/dec-101.qca";
    
    SECTION( "dec-101 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("dec-101_b_e.rst");
}
/*    SECTION( "dec-101 bistable selective simulation" ){
   
        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("dec-101_b_s.rst");
}*/
    SECTION( "dec-101 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("dec-101_c_e.rst");
}
/*    SECTION( "dec-101 coherence selective simulation" ){

        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("dec-101_c_s.rst");
}*/
}

TEST_CASE( "dec-354 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/dec-354.qca";

    SECTION( "dec-354 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("dec-354_b_e.rst");
}
    SECTION( "dec-354 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("dec-354_c_e.rst");
}
}

TEST_CASE( "dec-360 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/dec-360.qca";
   
    SECTION( "dec-360 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("dec-360_b_e.rst");
}
    SECTION( "dec-360 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("dec-360_c_e.rst");
}
}

TEST_CASE( "psm-1172 simulation" ) {

    std::string fname  = std::string(TESTCASE) + "/psm-1172.qca";
    
    SECTION( "psm-1172 bistable exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("psm-1172_b_e.rst");
}
/*   SECTION( "psm-1172 bistable selective simulation" ){

        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        bistable_sim(fname, vfname, result);
        result.write_text_file("dec-101_b_s.rst");
}*/

    SECTION( "psm-1172 coherence exhaustive simulation" ){
         
        Result result;
        bistable_sim(fname, result);
        result.write_text_file("psm-1172_c_e.rst");
}
/*    SECTION( "psm-1172 coherence selective simulation" ){

        Result result;
        std::string vfname = std::string(TESTCASE) + "/engine_sample_vector_table.vt";
        coherence_sim(fname, vfname, result);
        result.write_text_file("psm-1172_c_s.rst");
}*/

}

