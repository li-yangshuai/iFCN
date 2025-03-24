#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <simon/simon.hpp>
using namespace simon;

void parse_raw_data(const std::string &fname, simon::QCADDesignRawData &raw_data) {
    namespace x3 = boost::spirit::x3;
    std::ifstream ifs(fname);
    ifs >> std::noskipws;

    std::string content;
    std::copy(std::istream_iterator<char>(ifs), std::istream_iterator<char>(),
              std::back_inserter(content));

    bool ret = x3::phrase_parse(content.begin(), content.end(), simon::QCADDesignParser, x3::ascii::space, raw_data);
    assert(ret);

    raw_data.layers.erase(
            std::remove_if(raw_data.layers.begin(), raw_data.layers.end(),
                           [](auto &layer)->bool {
                               return layer.type != simon::QCALayerType::CELLS;
                           }),
            raw_data.layers.end()
    );
}


void parse_verification (const std::string &fname){
    
    QCADDesignRawData raw_data;
    parse_raw_data(fname, raw_data);
    REQUIRE(raw_data.version == std::string("2.000000"));

    QCADesign design;
    bool ret = parse_design(fname, design);
    REQUIRE(ret);
    REQUIRE( size(design) == raw_data.layers.size() );

    auto ast_layer_iter = raw_data.layers.begin();
    int i = 0;
    foreach_layer(design, [&ast_layer_iter, &i](auto &layer){
        REQUIRE(index(layer) == i++);
        REQUIRE(description(layer) == ast_layer_iter->description);

        auto ast_cell_iter = ast_layer_iter->objects.begin();
        if(description(layer) == "Main Cell Layer") {
            foreach_cell(layer, [&ast_cell_iter](auto &cell){
                auto &ast_cell = boost::get<simon::QCADCellRawData>(*ast_cell_iter++);

                REQUIRE(x(cell)                  == Approx(ast_cell.design_object.x).epsilon(0.0001));
                REQUIRE(y(cell)                  == Approx(ast_cell.design_object.y).epsilon(0.0001));
                REQUIRE(width(cell)              == Approx(ast_cell.cx_cell).epsilon(0.0001));
                REQUIRE(height(cell)             == Approx(ast_cell.cy_cell).epsilon(0.0001));
                REQUIRE(name(cell)               == ast_cell.label.psz);
                REQUIRE(simon::timezone(cell)    == ast_cell.clock);
                REQUIRE(function(cell)           == ast_cell.function);

                for(auto j=0u; j<4; ++j) {
                    REQUIRE(x(dots(cell)[j])         == Approx(ast_cell.cell_dots[j].x).epsilon(0.0001));
                    REQUIRE(y(dots(cell)[j])         == Approx(ast_cell.cell_dots[j].y).epsilon(0.0001));
                    REQUIRE(diameter(dots(cell)[j])  == Approx(ast_cell.cell_dots[j].diameter).epsilon(0.0001));
                    REQUIRE(charge(dots(cell)[j])    == Approx(ast_cell.cell_dots[j].charge).epsilon(0.0001));
                    REQUIRE(spin(dots(cell)[j])      == Approx(ast_cell.cell_dots[j].spin).epsilon(0.0001));
                    REQUIRE(potential(dots(cell)[j]) == Approx(ast_cell.cell_dots[j].potential).epsilon(0.0001));
                }
            });
        }

        ++ast_layer_iter;
    });
 
}

TEST_CASE( "xor-14 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-14.qca";
    parse_verification(fname);
}

TEST_CASE( "xor-28 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-28.qca";
    parse_verification(fname);
}

TEST_CASE( "xor-42 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-42.qca";
    parse_verification(fname);
}

TEST_CASE( "xor-48 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-48.qca";
    parse_verification(fname);
}

TEST_CASE( "xor-54 parsing" ) {
    std::string fname  = std::string(TESTCASE) + "/xor-54.qca";
   parse_verification(fname);
}

TEST_CASE( "xor-67 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-67.qca";
    parse_verification(fname);
}

TEST_CASE( "xor-92 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/xor-92.qca";
    parse_verification(fname);
}

TEST_CASE( "maj-5 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/maj-5.qca";
    parse_verification(fname);
}

TEST_CASE( "maj-10 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/maj-10.qca";
    parse_verification(fname);
}

TEST_CASE( "maj-18 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/maj-18.qca";
    parse_verification(fname);
}

TEST_CASE( "mul-27 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/mul-27.qca";
    parse_verification(fname);
}

TEST_CASE( "mul-77 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/mul-77.qca";
    parse_verification(fname);
}

TEST_CASE( "mul-145 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/mul-145.qca";
    parse_verification(fname);
}

TEST_CASE( "inv-4 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/inv-4.qca";
    parse_verification(fname);
}

TEST_CASE( "inv-8 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/inv-8.qca";
    parse_verification(fname);
}

TEST_CASE( "add-72 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/add-72.qca";
    parse_verification(fname);
}

TEST_CASE( "add-84 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/add-84.qca";
    parse_verification(fname);
}

TEST_CASE( "add-312 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/add-312.qca";
    parse_verification(fname);
}

TEST_CASE( "dec-101 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/dec-101.qca";
    parse_verification(fname);
}

TEST_CASE( "dec-354 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/dec-354.qca";
    parse_verification(fname);
}

TEST_CASE( "dec-360 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/dec-360.qca";
    parse_verification(fname);
}

TEST_CASE( "psm-1172 parsing" ) {

    std::string fname  = std::string(TESTCASE) + "/psm-1172.qca";
    parse_verification(fname);
}


TEST_CASE( "sim_case_1" ) {
    Trace trace1{"a", TraceFunction::INPUT, {1, 1, 1, 1, 1, 1}};
    Trace trace2{"b", TraceFunction::INPUT, {2, 2, 2, 2, 2, 2}};
    Trace trace3{"c", TraceFunction::INPUT, {0, 0, 0, 0, 0, 0}};

    REQUIRE( trace2.data_labels == "b" );
    REQUIRE( trace2.trace_function == TraceFunction::INPUT );
    REQUIRE( trace2.data[0] == Approx(2).epsilon(0.000001) );
    REQUIRE( trace2.data[1] == Approx(2).epsilon(0.000001) );
    REQUIRE( trace2.data[2] == Approx(2).epsilon(0.000001) );
    REQUIRE( trace2.data[3] == Approx(2).epsilon(0.000001) );
    REQUIRE( trace2.data[4] == Approx(2).epsilon(0.000001) );
    REQUIRE( trace2.data[5] == Approx(2).epsilon(0.000001) );
}
