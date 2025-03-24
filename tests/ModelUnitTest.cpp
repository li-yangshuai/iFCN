#include "catch.hpp"
#include <simon/simon.hpp>
using namespace simon;

TEST_CASE( "QCADot Test Suite" ){

SECTION( "dot_with_default_value" ) {

    QCADot dot1;

    REQUIRE( x(dot1)             == Approx(0).epsilon(0.001));
    REQUIRE( y(dot1)             == Approx(0).epsilon(0.001));
    REQUIRE( diameter(dot1)      == Approx(5.0).epsilon(0.001));
    REQUIRE( charge(dot1)        == Approx(0).epsilon(0.001));
    REQUIRE( spin(dot1)          == Approx(0).epsilon(0.001));
    REQUIRE( potential(dot1)     == Approx(0).epsilon(0.001));
}

SECTION( "dot_with_assigned_value" ) {

    QCADot dot2;
    x           (dot2) = 1;
    y           (dot2) = 2;
    diameter    (dot2) = 3;
    charge      (dot2) = 4;
    spin        (dot2) = 5;
    potential   (dot2) = 6;

    REQUIRE( x(dot2)            == Approx(1).epsilon(0.001));
    REQUIRE( y(dot2)            == Approx(2).epsilon(0.001));
    REQUIRE( diameter(dot2)     == Approx(3).epsilon(0.001));
    REQUIRE( charge(dot2)       == Approx(4).epsilon(0.001));
    REQUIRE( spin(dot2)         == Approx(5).epsilon(0.001));
    REQUIRE( potential(dot2)    == Approx(6).epsilon(0.001));
}
}

TEST_CASE( "QCA QCACell Suite" ){

SECTION( "cell_with_default_value" ) {

    QCACell cell1;

    REQUIRE( name(cell1)            ==  "");
    REQUIRE( x(cell1)               == Approx(0).epsilon(0.001)); 
    REQUIRE( y(cell1)               == Approx(0).epsilon(0.001));
    REQUIRE( width(cell1)           == Approx(18).epsilon(0.001));
    REQUIRE( height(cell1)          == Approx(18).epsilon(0.001));
    REQUIRE( polarization(cell1)    == Approx(0).epsilon(0.001));
    REQUIRE( function(cell1)        == FCNCellFunction::NORMAL);
    REQUIRE( layer_index(cell1)     == 0);
    REQUIRE( simon::timezone(cell1)  == 0);
    REQUIRE( x(dots(cell1).at(0))   == Approx(0).epsilon(0.001));
}

SECTION( "cell_with_assigned_value" ) {
    
    QCACell cell2;
    name               (cell2) = "cell2";
    x                  (cell2) = 1;
    y                  (cell2) = 2;
    width              (cell2) = 3;
    height             (cell2) = 4;
    polarization       (cell2) = 0;
    function           (cell2) = FCNCellFunction::NORMAL;
 
    simon::timezone    (cell2) = 1;
    layer_index        (cell2) = 0;
 
    REQUIRE( name(cell2)            ==  "cell2");
    REQUIRE( x(cell2)               == Approx(1).epsilon(0.001)); 
    REQUIRE( y(cell2)               == Approx(2).epsilon(0.001));
    REQUIRE( width(cell2)           == Approx(3).epsilon(0.001));
    REQUIRE( height(cell2)          == Approx(4).epsilon(0.001));
    REQUIRE( polarization(cell2)    == Approx(0).epsilon(0.001));
    REQUIRE( function(cell2)        == FCNCellFunction::NORMAL);
    REQUIRE( layer_index(cell2)     == 0);
    REQUIRE( simon::timezone(cell2) == 1);
    REQUIRE( x(dots(cell2).at(0))   == Approx(0).epsilon(0.001));
    }
}

TEST_CASE( "QCA Bistablecell Suite" ){

    using namespace simon;

SECTION( "bistablecell_with_assigned_value" ) {

    //construction of cell
    QCACell neighbour;
    QCACell cell;
    //setting intrinsic property
    name               (cell) = "bistablecell";
    x                  (cell) = 10;
    y                  (cell) = 10;
    width              (cell) = 10;
    height             (cell) = 10;
    polarization       (cell) = 0;
    function           (cell) = FCNCellFunction::NORMAL;
    simon::timezone (cell) = 1;
    layer_index        (cell) = 0;
    //create extrinsic property
    struct ExtrinsicProperty {
        std::vector<double>          kink_energies;
        std::vector<QCACell* >       neighbours;
    };
    extrinsics(cell).emplace< ExtrinsicProperty >();
    //set extrinsic property
    auto pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell)); 
    pexp->kink_energies.push_back(1.0);
    pexp->neighbours.push_back(&neighbour);
    //assertions
    REQUIRE( name(cell)                       == "bistablecell");
    REQUIRE( x(cell)                          == Approx(10).epsilon(0.001));
    REQUIRE( y(cell)                          == Approx(10).epsilon(0.001));
    REQUIRE( width(cell)                      == Approx(10).epsilon(0.001));
    REQUIRE( height(cell)                     == Approx(10).epsilon(0.001));
    REQUIRE( polarization(cell)               == Approx(0).epsilon(0.001));
    REQUIRE( function(cell)                   == FCNCellFunction::NORMAL);
    REQUIRE( layer_index(cell)                == 0);
    REQUIRE( simon::timezone(cell)            == 1);
    REQUIRE( x(dots(cell).at(0))              == Approx(0).epsilon(0.001));
    REQUIRE( pexp->kink_energies.at(0)        == 1.0);
    REQUIRE( name(*(pexp->neighbours.at(0)))  == "" );
    }

}

TEST_CASE( "QCA CoherenceCell Suite"){
   
    using namespace simon;

SECTION( "coherencecell_with_assiREQUIREned_value" ){
    
    QCACell neighbour;
    QCACell cell;

    name               (cell) = "coherencecell";
    x                  (cell) = 10;
    y                  (cell) = 10;
    width              (cell) = 10;
    height             (cell) = 10;
    polarization       (cell) = 0;
    function           (cell) = FCNCellFunction::NORMAL;
    simon::timezone    (cell) = 1;
    layer_index        (cell) = 0;
   
    struct ExtrinsicProperty {
        double lambda_x;
        double lambda_y;
        double lambda_z;
        std::vector<double> kink_energies;
        std::vector<QCACell*>  neighbours;
    };
    extrinsics(cell).emplace< ExtrinsicProperty >();
    
    auto pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell)); 
    pexp->lambda_x            = 2.0;
    pexp->lambda_y            = 2.0;
    pexp->lambda_z            = 2.0;
    pexp->kink_energies.push_back(1.0);
    pexp->neighbours.push_back(&neighbour);
    
    REQUIRE( x(cell)                         == Approx(10).epsilon(0.001));
    REQUIRE( y(cell)                         == Approx(10).epsilon(0.001));
    REQUIRE( width(cell)                     == Approx(10).epsilon(0.001));
    REQUIRE( height(cell)                    == Approx(10).epsilon(0.001));
    REQUIRE( polarization(cell)              == Approx(0).epsilon(0.001));
    REQUIRE( function(cell)                  == FCNCellFunction::NORMAL );
    REQUIRE( layer_index(cell)               == 0);
    REQUIRE( simon::timezone(cell)           == 1);
    REQUIRE( x(dots(cell).at(0))             == Approx(0).epsilon(0.001));
    REQUIRE( pexp->kink_energies.at(0)       == Approx(1.0).epsilon(0.001));
    REQUIRE( name(*(pexp->neighbours.at(0))) == "" );
    REQUIRE( pexp->lambda_x                  == Approx(2.0).epsilon(0.001));      
    REQUIRE( pexp->lambda_y                  == Approx(2.0).epsilon(0.001));
    REQUIRE( pexp->lambda_z                  == Approx(2.0).epsilon(0.001));
}
}

TEST_CASE( "QCA QCALayer Suite" ){

    using namespace simon;

SECTION( "layer_with_bistablecell_assigned_value" ){

    QCACell cell1;
    QCACell cell2;

    struct ExtrinsicProperty {
        std::vector<double>        kink_energies;
        std::vector<QCACell* >     neighbours;
    };
    extrinsics(cell1).emplace< ExtrinsicProperty >();
    extrinsics(cell2).emplace< ExtrinsicProperty >();

    auto pexp1 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell1)); 
    pexp1->kink_energies.push_back(1.0);
    pexp1->neighbours.push_back(&cell2);

    auto pexp2 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell2)); 
    pexp2 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell2)); 
    pexp2->kink_energies.push_back(1.0);
    pexp2->neighbours.push_back(&cell1);

    QCALayer layer;
    index         (layer) = 0;
    description   (layer) = "layer";
    layer.cells.push_back(cell1);
    layer.cells.push_back(cell2);

    for(auto &cell : layer){
        REQUIRE( name(cell)                      == "");
        REQUIRE( x(cell)                         == Approx(0).epsilon(0.001));     
        REQUIRE( y(cell)                         == Approx(0).epsilon(0.001));    
        auto pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell)); 
        pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell)); 
        REQUIRE( pexp->kink_energies.at(0)       == Approx(1.0).epsilon(0.001)); 
        REQUIRE( name(*(pexp->neighbours.at(0))) == "" );       
     }
     REQUIRE( index(layer)                       == 0 );
     REQUIRE( description(layer)                 == "layer" );
    }

SECTION( "layer_with_coherencecell_assigned_value" ){

    QCACell cell1;
    QCACell cell2;

    struct ExtrinsicProperty {
        double lambda_x;
        double lambda_y;
        double lambda_z;
        std::vector<double>           kink_energies;
        std::vector<QCACell* >        neighbours;
    };
    extrinsics(cell1).emplace< ExtrinsicProperty >();
    extrinsics(cell2).emplace< ExtrinsicProperty >();

    auto pexp1 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell1)); 
    pexp1->lambda_x  = 1.0;
    pexp1->lambda_y  = 1.0;
    pexp1->lambda_z  = 1.0;
    pexp1->kink_energies.push_back(1.0);
    pexp1->neighbours.push_back(&cell2);

    auto pexp2 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell2)); 
    pexp2->lambda_x  = 1.0;
    pexp2->lambda_y  = 1.0;
    pexp2->lambda_z  = 1.0;
    pexp2->kink_energies.push_back(1.0);
    pexp2->kink_energies.push_back(1.0);
    pexp2->neighbours.push_back(&cell1);

    QCALayer layer;
    index         (layer) = 0;
    description   (layer) = "layer";
    layer.cells.push_back(cell1);
    layer.cells.push_back(cell2);

    for(auto &cell : layer){
        REQUIRE( name(cell)                      == "");
        REQUIRE( x(cell)                         == Approx(0).epsilon(0.001));
        REQUIRE( y(cell)                         == Approx(0).epsilon(0.001));
        auto pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell));
        REQUIRE( pexp->lambda_x                  == Approx(1.0).epsilon(0.001));
        REQUIRE( pexp->lambda_y                  == Approx(1.0).epsilon(0.001));
        REQUIRE( pexp->lambda_z                  == Approx(1.0).epsilon(0.001));
        REQUIRE( pexp->kink_energies.at(0)       == Approx(1.0).epsilon(0.001));    
        REQUIRE( name(*(pexp->neighbours.at(0))) == "" );       
     } 
    REQUIRE( index(layer)         == 0 );
    REQUIRE( description(layer)   == "layer" ); }
}

TEST_CASE( "QCA QCADesign Suite" ){

SECTION( "design_with_bistablecell_assigned_value") {
  
    QCACell cell1;
    QCACell cell2;

    struct ExtrinsicProperty {
        std::vector<double>        kink_energies;
        std::vector<QCACell* >     neighbours;
    };
    extrinsics(cell1).emplace< ExtrinsicProperty >();
    extrinsics(cell2).emplace< ExtrinsicProperty >();

    ExtrinsicProperty* pexp1;
    pexp1 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell1)); 
    pexp1->kink_energies.push_back(1.0);
    pexp1->neighbours.push_back(&cell2);

    ExtrinsicProperty* pexp2;
    pexp2 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell2)); 
    pexp2->kink_energies.push_back(1.0);
    pexp2->neighbours.push_back(&cell1);

    QCALayer layer1;
    index         (layer1) = 0;
    description   (layer1) = "layer";
    layer1.cells.push_back(cell1);
    QCALayer layer2;
    index         (layer2) = 0;
    description   (layer2) = "layer";
    layer2.cells.push_back(cell2);

    QCADesign design;
    design.layers.push_back(layer1);
    design.layers.push_back(layer2);

    for(auto &layer : design ){
        for(auto &cell : layer){
            REQUIRE( name(cell)                       == "");
            REQUIRE( x(cell)                          == Approx(0).epsilon(0.001));
            REQUIRE( y(cell)                          == Approx(0).epsilon(0.001));
            auto pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell));
            REQUIRE( pexp->kink_energies.at(0)        == Approx(1.0).epsilon(0.001));
            REQUIRE( name(*(pexp->neighbours.at(0)))  == "" );       
         }
         REQUIRE( index(layer)          == 0 );
         REQUIRE( description(layer)    == "layer" );
    }
}
    
SECTION( "design_with_coherencecell_assigned_value" ) {

    QCACell cell1;
    QCACell cell2;

    struct ExtrinsicProperty {
        double lambda_x;
        double lambda_y;
        double lambda_z;
        std::vector<double>        kink_energies;
        std::vector<QCACell* >     neighbours;
    };
    extrinsics(cell1).emplace< ExtrinsicProperty >();
    extrinsics(cell2).emplace< ExtrinsicProperty >();

    auto pexp1 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell1)); 
    pexp1->lambda_x  = 1.0;
    pexp1->lambda_y  = 1.0;
    pexp1->lambda_z  = 1.0;
    pexp1->kink_energies.push_back(1.0);
    pexp1->neighbours.push_back(&cell2);

    auto pexp2 = std::any_cast< ExtrinsicProperty >(&extrinsics(cell2)); 
    pexp2->lambda_x  = 1.0;
    pexp2->lambda_y  = 1.0;
    pexp2->lambda_z  = 1.0;
    pexp2->kink_energies.push_back(1.0);
    pexp2->kink_energies.push_back(1.0);
    pexp2->neighbours.push_back(&cell1);

    QCALayer layer1;
    index         (layer1) = 0;
    description   (layer1) = "layer";
    layer1.cells.push_back(cell1);
    QCALayer layer2;
    index         (layer2) = 0;
    description   (layer2) = "layer";
    layer2.cells.push_back(cell2);

    QCADesign design;
    design.layers.push_back(layer1);
    design.layers.push_back(layer2);

    for(auto &layer : design ) {
        for(auto &cell : layer ) {
            REQUIRE( name(cell)                         == "");
            REQUIRE( x(cell)                            == Approx(0).epsilon(0.001));
            REQUIRE( y(cell)                            == Approx(0).epsilon(0.001));
            auto pexp = std::any_cast< ExtrinsicProperty >(&extrinsics(cell));
            REQUIRE( pexp->lambda_x                     == Approx(1.0).epsilon(0.001));
            REQUIRE( pexp->lambda_y                     == Approx(1.0).epsilon(0.001));
            REQUIRE( pexp->lambda_z                     == Approx(1.0).epsilon(0.001)); 
            REQUIRE( pexp->kink_energies.at(0)          == Approx(1.0).epsilon(0.001));
            REQUIRE( name(*(pexp->neighbours.at(0)))    ==  "" );       
        }

        REQUIRE( index(layer)       == 0 );
        REQUIRE( description(layer) == "layer" ); 
    }
}
}
