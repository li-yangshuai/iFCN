//simon: C++ FCN network simulation framework
//Copyright (c) 2019 HFUT
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#if !defined(HFUT_SIMON_IO_HPP)
#define      HFUT_SIMON_IO_HPP

#include <string>
#include <vector>
#include <utility>
#include <map>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cassert>

#include <iostream>


#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/ast/variant.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/map.hpp>
#include <boost/fusion/include/pair.hpp>
#include <boost/spirit/home/x3/core/parse.hpp>

#include "Model.hpp"

namespace simon {

namespace x3 = boost::spirit::x3;
    namespace asc = boost::spirit::x3::ascii;

    //////////////////////////////////////////////////////////////
    //0. Utility Parsers and Symbols
    struct IdentifierParser_class;
    const x3::rule<IdentifierParser_class, std::string> IdentifierParser = "IdentifierParser";
    auto const IdentifierParser_def = x3::raw[x3::lexeme[(x3::alpha | '_') >> *(x3::alnum | '_')]];
    BOOST_SPIRIT_DEFINE(IdentifierParser);

    struct BooleanSymbol : x3::symbols<bool> {
        BooleanSymbol() {
            add
                    ("FALSE", false)
                    ("TRUE", true);
        }
    };

    //////////////////////////////////////////////////////////////
    //1. VectorTableParser
    struct VectorTableRawData {
        std::vector<std::string>        names;
        std::vector<std::vector<int>>   test_vectors;
    };

    struct VectorTableParser_class;
    const x3::rule<VectorTableParser_class, VectorTableRawData> VectorTableParser = "VectorTableParser";
    auto const VectorTableParser_def = (IdentifierParser  % ',') >> *(x3::int_ % ',');
    BOOST_SPIRIT_DEFINE(VectorTableParser);

    //////////////////////////////////////////////////////////////
    //2. TraceParser
    struct TraceRawData {
        std::string data_labels;
        int trace_function;
        bool drawtrace;
        std::vector<double> trace_data;
    };

    struct TraceParser_class;
    const x3::rule<TraceParser_class, TraceRawData> TraceParser = "TraceParser";
    auto const TraceParser_def =
            x3::lit("[TRACE]") > x3::lit("data_labels") >
            x3::lit("=") > x3::lexeme[*(x3::char_ - x3::eol)] >
            x3::lit("trace_function") > x3::lit("=") >
            x3::int_ > x3::lit("drawtrace") >
            x3::lit("=") > BooleanSymbol() >
            x3::lit("[TRACE_DATA]") > *(x3::double_) >
            x3::lit("[#TRACE_DATA]") > x3::lit("[#TRACE]");
    BOOST_SPIRIT_DEFINE(TraceParser);

    //////////////////////////////////////////////////////////////
    //3. SimulationOutputParser
    struct SimulationOutputRawData {
        int number_samples;
        int number_of_traces;
        std::vector<TraceRawData> traces;
        std::vector<TraceRawData> clocks;
    };

    struct SimulationOutputParser_class;
    const x3::rule<SimulationOutputParser_class, SimulationOutputRawData> SimulationOutputParser = "SimulationOutputParser";
    auto const SimulationOutputParser_def =
            x3::lit("[SIMULATION_OUTPUT]")  > x3::lit("[SIMULATION_DATA]")  >
            x3::lit("number_samples")       > x3::lit("=")                  >
            x3::int_                        > x3::lit("number_of_traces")   >
            x3::lit("=")                    > x3::int_                      >
            x3::lit("[TRACES]")             > *(TraceParser)                >
            x3::lit("[#TRACES]")            > x3::lit("[CLOCKS]")           >
            *(TraceParser)                  > x3::lit("[#CLOCKS]")          >
            x3::lit("[#SIMULATION_DATA]")   > x3::lit("[TYPE:BUS_LAYOUT]")  >
            x3::lit("[#TYPE:BUS_LAYOUT]")   > x3::lit("[#SIMULATION_OUTPUT]");
    BOOST_SPIRIT_DEFINE(SimulationOutputParser);

    //////////////////////////////////////////////////////////////
    //1. QCADDesignObject
    struct QCADDesignObjectRawData {
        double x       = 0.0;
        double y       = 0.0;
        bool bSelected = false;
        unsigned red   = 0;
        unsigned green = 0;
        unsigned blue  = 0;
        double xWorld  = 0.0;
        double yWorld  = 0.0;
        double cxWorld = 0.0;
        double cyWorld = 0.0;
    };

    struct QCADDesignObjectParser_class;
    const x3::rule<QCADDesignObjectParser_class, QCADDesignObjectRawData> QCADDesignObjectParser = "QCADDesignObjectParser";
    auto const QCADDesignObjectParser_def =
            x3::lit("[TYPE:QCADDesignObject]") >
            x3::lit("x") > x3::lit("=") > x3::double_ >
            x3::lit("y") > x3::lit("=") > x3::double_ >
            x3::lit("bSelected") > x3::lit("=") > BooleanSymbol() >
            x3::lit("clr.red") > x3::lit("=") > x3::uint_ >
            x3::lit("clr.green") > x3::lit("=") > x3::uint_ >
            x3::lit("clr.blue") > x3::lit("=") > x3::uint_ >
            x3::lit("bounding_box.xWorld") > x3::lit("=") > x3::double_ >
            x3::lit("bounding_box.yWorld") > x3::lit("=") > x3::double_ >
            x3::lit("bounding_box.cxWorld") > x3::lit("=") > x3::double_ >
            x3::lit("bounding_box.cyWorld") > x3::lit("=") > x3::double_ >
            x3::lit("[#TYPE:QCADDesignObject]");
    BOOST_SPIRIT_DEFINE(QCADDesignObjectParser);

    //////////////////////////////////////////////////////////////
    //2. QCADLabel
    struct QCADLabelRawData {
        QCADDesignObjectRawData design_object;
        std::string psz;
    };

    struct QCADLabelParser_class;
    const x3::rule<QCADLabelParser_class, QCADLabelRawData> QCADLabelParser = "QCADLabelParser";
    auto const QCADLabelParser_def =
            x3::lit("[TYPE:QCADLabel]")                                          >
            x3::lit("[TYPE:QCADStretchyObject]")                                 >
            QCADDesignObjectParser                                               >
            x3::lit("[#TYPE:QCADStretchyObject]")                                >
            x3::lit("psz") >> x3::lit("=") >> x3::lexeme[*(x3::char_ - x3::eol)] >
            x3::lit("[#TYPE:QCADLabel]")
    ;
    BOOST_SPIRIT_DEFINE(QCADLabelParser);

    //////////////////////////////////////////////////////////////
    //3. QCADCellDot
    struct QCADCellDotRawData {
        double x         = 0.0;
        double y         = 0.0;
        double diameter  = 0.0;
        double charge    = 0.0;
        double spin      = 0.0;
        double potential = 0.0;
    };

    struct QCADCellDotParser_class;
    const x3::rule<QCADCellDotParser_class, QCADCellDotRawData> QCADCellDotParser = "QCADCellDotParser";
    auto const QCADCellDotParser_def =
            x3::lit("[TYPE:CELL_DOT]") >
            x3::lit("x")            > x3::lit("=") > x3::double_ >
            x3::lit("y")            > x3::lit("=") > x3::double_ >
            x3::lit("diameter")     > x3::lit("=") > x3::double_ >
            x3::lit("charge")       > x3::lit("=") > x3::double_ >
            x3::lit("spin")         > x3::lit("=") > x3::double_ >> -x3::lit("#QNAN0") >
            x3::lit("potential")    > x3::lit("=") > x3::double_ >> -x3::lit("#QNAN0") >
            x3::lit("[#TYPE:CELL_DOT]");
    BOOST_SPIRIT_DEFINE(QCADCellDotParser);

    //////////////////////////////////////////////////////////////
    //4. Substrate
    struct QCADSubstrateRawData {
        QCADDesignObjectRawData design_object;
        double grid_spacing = 0.0;
    };

    struct QCADSubstrateParser_class;
    const x3::rule<QCADSubstrateParser_class, QCADSubstrateRawData> QCADSubstrateParser = "QCADSubstrateParser";
    auto const QCADSubstrateParser_def =
            x3::lit("[TYPE:QCADSubstrate]")                         >>
            x3::lit("[TYPE:QCADStretchyObject]")                    >>
            QCADDesignObjectParser                                  >>
            x3::lit("[#TYPE:QCADStretchyObject]")                   >>
            x3::lit("grid_spacing") >> x3::lit("=") >> x3::double_  >>
            x3::lit("[#TYPE:QCADSubstrate]");
    BOOST_SPIRIT_DEFINE(QCADSubstrateParser);

    //////////////////////////////////////////////////////////////
    //5. QCADCell
    struct QCADCellModeSymbol : x3::symbols<QCACellMode> {
        QCADCellModeSymbol() {
            add
                    ("QCAD_CELL_MODE_NORMAL",    QCACellMode::NORMAL)
                    ("QCAD_CELL_MODE_CROSSOVER", QCACellMode::CROSSOVER)
                    ("QCAD_CELL_MODE_VERTICAL",  QCACellMode::VERTICAL)
                    ("QCAD_CELL_MODE_CLUSTER",   QCACellMode::CLUSTER)
                    ;
        }
    };

    struct QCADCellFunctionSymbol : x3::symbols<FCNCellFunction> {
        QCADCellFunctionSymbol() {
            add
                    ("QCAD_CELL_NORMAL",        FCNCellFunction::NORMAL)
                    ("QCAD_CELL_INPUT",         FCNCellFunction::INPUT)
                    ("QCAD_CELL_OUTPUT",        FCNCellFunction::OUTPUT)
                    ("QCAD_CELL_FIXED",         FCNCellFunction::FIXED)
                    ("QCAD_CELL_LAST_FUNCTION", FCNCellFunction::LAST_FUNCTION)
                    ;
        }
    };

    struct QCADCellRawData {
        QCADDesignObjectRawData design_object;
        double cx_cell        = 0.0;
        double cy_cell        = 0.0;
        double dot_diameter   = 0.0;
        int clock             = 0;
        QCACellMode mode         = QCACellMode::NORMAL;
        FCNCellFunction function = FCNCellFunction::NORMAL;
        int num_dots          = 0;
        std::vector<QCADCellDotRawData> cell_dots;
        QCADLabelRawData label;
    };


    struct QCADCellParser_class;
    const x3::rule<QCADCellParser_class, QCADCellRawData> QCADCellParser = "QCADCellParser";
    auto const QCADCellParser_def =
            x3::lit("[TYPE:QCADCell]") >
            QCADDesignObjectParser >
            x3::lit("cell_options.cxCell")       > x3::lit("=") > x3::double_ >
            x3::lit("cell_options.cyCell")       > x3::lit("=") > x3::double_ >
            x3::lit("cell_options.dot_diameter") > x3::lit("=") > x3::double_ >
            x3::lit("cell_options.clock")        > x3::lit("=") > x3::int_ >
            x3::lit("cell_options.mode")         > x3::lit("=") > QCADCellModeSymbol() >
            x3::lit("cell_function")             > x3::lit("=") > QCADCellFunctionSymbol() >
            x3::lit("number_of_dots")            > x3::lit("=") > x3::int_ >
            *QCADCellDotParser                   >> (-QCADLabelParser) >
            x3::lit("[#TYPE:QCADCell]");
    BOOST_SPIRIT_DEFINE(QCADCellParser);

    //////////////////////////////////////////////////////////////
    //6. QCADLayer
    struct QCADLayerTypeSymbol : x3::symbols<QCALayerType> {
        QCADLayerTypeSymbol() {
            add
                    ("0", QCALayerType::SUBSTRATE)
                    ("1", QCALayerType::CELLS)
                    ("2", QCALayerType::CLOCKING)
                    ("3", QCALayerType::DRAWING)
                    ("4", QCALayerType::DISTRIBUTION)
                    ("5", QCALayerType::LAST_TYPE)
                    ;
        }
    };

    struct QCADLayerStatusSymbol : x3::symbols<QCALayerStatus> {
        QCADLayerStatusSymbol() {
            add
                    ("0", QCALayerStatus::ACTIVE)
                    ("1", QCALayerStatus::VISIBLE)
                    ("2", QCALayerStatus::HIDDEN)
                    ("3", QCALayerStatus::LAST_STATUS)
                    ;
        }
    };

    struct QCADLayerRawData {
        QCALayerType type;
        QCALayerStatus status;
        std::string description;
        std::vector<x3::variant<QCADLabelRawData, QCADSubstrateRawData, QCADCellRawData>> objects;
    };

    struct QCADLayerParser_class;
    const x3::rule<QCADLayerParser_class, QCADLayerRawData> QCADLayerParser = "QCADLayerParser";
    auto const QCADLayerParser_def =
            x3::lit("[TYPE:QCADLayer]") >
            x3::lit("type")           > x3::lit("=") > QCADLayerTypeSymbol()               >
            x3::lit("status")         > x3::lit("=") > QCADLayerStatusSymbol()             >
            x3::lit("pszDescription") > x3::lit("=") > x3::lexeme[*(x3::char_ - x3::eol)]  >
            (*(QCADLabelParser|QCADSubstrateParser|QCADCellParser)) >
            x3::lit("[#TYPE:QCADLayer]");
    BOOST_SPIRIT_DEFINE(QCADLayerParser);

    //////////////////////////////////////////////////////////////
    //7. QCADBus
    struct QCADBusFunctionSymbol : x3::symbols<FCNCellFunction> {
        QCADBusFunctionSymbol() {
            add
                    ("0", FCNCellFunction::NORMAL)
                    ("1", FCNCellFunction::INPUT)
                    ("2", FCNCellFunction::OUTPUT)
                    ("3", FCNCellFunction::FIXED)
                    ("4", FCNCellFunction::LAST_FUNCTION)
                    ;
        }
    };

    struct QCADBusRawData {
        std::string  bus_name;
        FCNCellFunction bus_function;
        std::vector<int> bus_data;
    };

    struct QCADBusParser_class;
    const x3::rule<QCADBusParser_class, QCADBusRawData> QCADBusParser = "QCADBusParser";
    auto const QCADBusParser_def =
            x3::lit("[TYPE:BUS]")   >>
            x3::lit("pszName")      >> x3::lit("=") >> x3::lexeme[*(x3::char_ - x3::eol)] >>
            x3::lit("bus_function") >> x3::lit("=") >> QCADBusFunctionSymbol() >>
            x3::lit("[BUS_DATA]")   >>
            (*x3::int_)             >>
            x3::lit("[#BUS_DATA]")  >>
            x3::lit("[#TYPE:BUS]");
    BOOST_SPIRIT_DEFINE(QCADBusParser);

    //////////////////////////////////////////////////////////////
    //8. QCADBusLayout
    struct QCADBusLayoutRawData {
        std::vector<QCADBusRawData> bus_layout;
    };

    struct QCADBusLayoutParser_class;
    const x3::rule<QCADBusLayoutParser_class, QCADBusLayoutRawData> QCADBusLayoutParser = "QCADBusLayoutParser";
    auto const QCADBusLayoutParser_def =
            x3::lit("[TYPE:BUS_LAYOUT]")    >
            (*QCADBusParser)                >
            x3::lit("[#TYPE:BUS_LAYOUT]");
    BOOST_SPIRIT_DEFINE(QCADBusLayoutParser);

    //////////////////////////////////////////////////////////////
    //9. QCADDesign
    struct QCADDesignRawData {
        std::string version;
        std::vector<QCADLayerRawData> layers;
        QCADBusLayoutRawData layout;
    };

    struct QCADDesignParser_class;
    const x3::rule<QCADDesignParser_class, QCADDesignRawData> QCADDesignParser = "QCADDesignParser";
    auto const QCADDesignParser_def =
            x3::lit("[VERSION]")                            >
            x3::lit("qcadesigner_version") > x3::lit("=")   > x3::lexeme[*(x3::char_ - x3::eol)] >
            x3::lit("[#VERSION]")                           >
            x3::lit("[TYPE:DESIGN]")                        >
            (*QCADLayerParser)                              >>
            (-QCADBusLayoutParser)                          >
            x3::lit("[#TYPE:DESIGN]");
    BOOST_SPIRIT_DEFINE(QCADDesignParser);

    //////////////////////////////////////////////////////////////
    //0. Cell Function Symbols
    struct NMLCellFunctionSymbol : x3::symbols<FCNCellFunction> {
        NMLCellFunctionSymbol() {
            add
                    ("NORMAL", FCNCellFunction::NORMAL)
                    ("INPUT", FCNCellFunction::INPUT)
                    ("OUTPUT", FCNCellFunction::OUTPUT)
                    ("FIXED", FCNCellFunction::FIXED)
                    ("LAST_FUNCTION", FCNCellFunction::LAST_FUNCTION)
                    ;
        }
    };

    //////////////////////////////////////////////////////////////
    //1. NMLCell
    struct NMLCellParser_class;
    const x3::rule<NMLCellParser_class, NMLCell> NMLCellParser = "NMLCellParser";
    auto const NMLCellParser_def =
            x3::long_ > x3::long_ > x3::long_ > x3::long_ >>
            -IdentifierParser >> x3::int_ > NMLCellFunctionSymbol() > x3::double_;
    BOOST_SPIRIT_DEFINE(NMLCellParser);

    //////////////////////////////////////////////////////////////
    //2. NMLLayer
    struct NMLLayerParser_class;
    const x3::rule<NMLLayerParser_class, NMLLayer> NMLLayerParser = "NMLLayerParser";
    auto const NMLLayerParser_def =
            x3::lit("[QCALayer]") >>
                                  *(NMLCellParser) >>
                                  x3::lit("[#QCALayer]");
    BOOST_SPIRIT_DEFINE(NMLLayerParser);

    //////////////////////////////////////////////////////////////
    //3. NMLDesignRawData
    struct NMLDesignParser_class;
    const x3::rule<NMLDesignParser_class, NMLDesign> NMLDesignParser= "NMLDesignParser";
    auto const NMLDesignParser_def =
            x3::omit[x3::lit("#") >> x3::lexeme[*(~x3::char_("\n"))]] >>
                                                                      x3::lit("[QCADesign]") >>
                                                                      *(NMLLayerParser) >>
                                                                      x3::lit("[#QCADesign]");
    BOOST_SPIRIT_DEFINE(NMLDesignParser);
    

    inline bool parse_vector_table(const std::string &test_vector_file_path, VectorTable &vector_table) {
        namespace x3 = boost::spirit::x3;

        VectorTableRawData raw_data;
        std::ifstream ifs(test_vector_file_path);
        ifs >> std::noskipws;

        std::string content;
        std::copy(std::istream_iterator<char>(ifs), std::istream_iterator<char>(),
                  std::back_inserter(content));

        bool ret = x3::phrase_parse(content.begin(), content.end(), VectorTableParser, x3::ascii::space, raw_data);
        assert(ret);

        vector_table.names        = std::move(raw_data.names);
        vector_table.test_vectors = std::move(raw_data.test_vectors);

        return ret;
    }

    namespace detail {
        class LayerObjectVisitor : public boost::static_visitor<> {
        public:
            explicit LayerObjectVisitor(QCALayer &layer) : layer{layer} {
            }

            void operator()(const QCADCellRawData &raw_data) {
                auto &cell = layer.cells.emplace_back();

                //cell property
                x(cell) = raw_data.design_object.x;
                y(cell) = raw_data.design_object.y;

                //default data
                width(   cell) = raw_data.cx_cell;
                height(  cell) = raw_data.cy_cell;
                name(    cell) = raw_data.label.psz;
                timezone(cell) = raw_data.clock;
                layer_index(cell) = index(layer);
                function(cell) = raw_data.function;
                cellMode(cell) = raw_data.mode;

                //dbc_dot_property
                for(auto i=0; i<4; ++i) {
                    x(        dots(cell)[i]) = raw_data.cell_dots[i].x;
                    y(        dots(cell)[i]) = raw_data.cell_dots[i].y;
                    diameter( dots(cell)[i]) = raw_data.cell_dots[i].diameter;
                    charge(   dots(cell)[i]) = raw_data.cell_dots[i].charge;
                    spin(     dots(cell)[i]) = raw_data.cell_dots[i].spin;
                    potential(dots(cell)[i]) = raw_data.cell_dots[i].potential;
                }
            }
            void operator()(const QCADLabelRawData     &raw_data) {(void)raw_data;};
            void operator()(const QCADSubstrateRawData &raw_data) {(void)raw_data;};
        private:
            QCALayer &layer;
        };
    }//namespace detail

    inline bool parse_design(const std::string &design_file_path, QCADesign &design) {
        namespace x3 = boost::spirit::x3;
        std::ifstream ifs(design_file_path);
        ifs >> std::noskipws;

        std::string content;
        std::copy(std::istream_iterator<char>(ifs), std::istream_iterator<char>(),
                  std::back_inserter(content));

        QCADDesignRawData raw_data;
        bool ret = x3::phrase_parse(content.begin(), content.end(),
                                    QCADDesignParser, x3::ascii::space, raw_data);
        assert(ret);

        //erase non cell layers
        raw_data.layers.erase(
                std::remove_if(raw_data.layers.begin(), raw_data.layers.end(),
                               [](auto &layer)->bool {
                                   return layer.type != QCALayerType::CELLS;
                               }),
                raw_data.layers.end()
        );

        //reserve storage for layers
        layers(design).reserve(raw_data.layers.size());

        for(std::vector<QCADLayerRawData>::size_type i = 0; i < raw_data.layers.size(); ++i ) {
            auto &layer = layers(design).emplace_back();
            index      (layer) = i;
            description(layer) = raw_data.layers[i].description;

            detail::LayerObjectVisitor layer_visitor(layer);
            for( const auto &obj : raw_data.layers[i].objects ) {
                boost::apply_visitor(layer_visitor, obj);
            }
        }
        //

        return ret;
    }

    inline bool parse_design(const std::string &design_file_path, NMLDesign &design) {
	    namespace x3 = boost::spirit::x3;

	    std::ifstream ifs(design_file_path);
	    ifs >> std::noskipws;

	    std::string content;
	    std::copy(std::istream_iterator<char>(ifs), std::istream_iterator<char>(),
			    std::back_inserter(content));

	    return x3::phrase_parse(content.begin(), content.end(),
			    NMLDesignParser, x3::ascii::space, design);
    }

}//namespace simon

//simon
BOOST_FUSION_ADAPT_STRUCT(simon::VectorTableRawData, names, test_vectors)

BOOST_FUSION_ADAPT_STRUCT(simon::TraceRawData, data_labels,
                          trace_function, drawtrace, trace_data)

BOOST_FUSION_ADAPT_STRUCT(simon::SimulationOutputRawData, number_samples,
                          number_of_traces, traces, clocks)

//qca
BOOST_FUSION_ADAPT_STRUCT(simon::QCADDesignObjectRawData,
        x, y, bSelected, red, green, blue, xWorld, yWorld, cxWorld, cyWorld)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADLabelRawData,
        design_object, psz)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADSubstrateRawData,
        design_object, grid_spacing)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADCellDotRawData,
        x, y, diameter, charge, spin, potential)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADCellRawData,
        design_object, cx_cell, cy_cell, dot_diameter, clock, 
        mode, function, num_dots, cell_dots, label)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADLayerRawData,
        type, status, description, objects)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADBusRawData,
       bus_name, bus_function, bus_data)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADBusLayoutRawData,
       bus_layout)

BOOST_FUSION_ADAPT_STRUCT(simon::QCADDesignRawData,
       version, layers, layout)

//nml
BOOST_FUSION_ADAPT_STRUCT(simon::NMLCell,
                          x, y, width, height, name, clock, function, magnetization)

BOOST_FUSION_ADAPT_STRUCT(simon::NMLLayer, cells)

BOOST_FUSION_ADAPT_STRUCT(simon::NMLDesign, layers)

#endif
