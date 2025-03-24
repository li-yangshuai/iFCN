#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <simon/simon.hpp>
using namespace simon;

TEST_CASE( "qca_suite" ){
        std::string content("85  50 50 100 A 0 INPUT 1");
        NMLCell cell;

        namespace x3 = boost::spirit::x3;

        bool ret = x3::phrase_parse(content.begin(), content.end(),
                                    simon::NMLCellParser, x3::ascii::space, cell);
        REQUIRE(ret);

        REQUIRE(x(cell)              == 85);
        REQUIRE(y(cell)              == 50); 
        REQUIRE(width(cell)          == 50);
        REQUIRE(height(cell)         == 100);
        REQUIRE(name(cell)           =="A");
        REQUIRE(clock(cell)          == 0);
        REQUIRE(function(cell)       == FCNCellFunction::INPUT);
        REQUIRE(magnetization(cell)  == Approx(1).epsilon(0.0001));
    }

