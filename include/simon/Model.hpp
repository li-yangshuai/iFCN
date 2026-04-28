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

#if !defined(HFUT_SIMON_MODEL_HPP)
#define      HFUT_SIMON_MODEL_HPP

#include <string>
#include <vector>
#include <map>
#include <array>
#include <fstream>
#include <random>
#include <any>
#include <cassert>

#include <boost/iterator/permutation_iterator.hpp>
#include <boost/algorithm/apply_permutation.hpp>

#include "Util.hpp"

namespace simon {

    /*
       A a1 a2 a3 <-j
       B b1 b2 b3
       C c1 c2 c3
       ^
       |
       i
       */
    class VectorTable {
    public:
        explicit VectorTable(std::vector<std::string> ns={}, std::vector<std::vector<int>> tvs={}) :
                names{std::move(ns)}, test_vectors{std::move(tvs)}
        {}

        double operator()(std::size_t i, std::size_t j ) const {
            assert(i<names.size());
            assert(j<test_vectors.size());

            double ret  = test_vectors.at(j).at(i) ? 1.0 : -1.0;
            return ret;
        }

        void reset_names_order(const std::vector<std::string> &ns) {
            std::map<std::string, std::size_t> name_idx;
            for(std::vector<std::string>::size_type i=0; i<ns.size(); ++i) {
                name_idx.emplace(std::make_pair(ns[i], i));
            }

            std::vector<std::size_t> order;
            for(auto &name : names) {
                order.push_back(name_idx[name]);
            }

            for(auto &test_vector : test_vectors) {
                std::vector<std::size_t> o = order;
                boost::algorithm::apply_reverse_permutation(test_vector, o);
            }
        }

        std::vector<std::string>      names;
        std::vector<std::vector<int>> test_vectors;
    };

    struct Trace {
        std::string data_labels;
        TraceFunction trace_function;
        std::vector<double> data;
    };

    struct EnergyCycleDissipation {
        int cycle_index = 0;
        double bath_eV = 0.0;
        double clock_eV = 0.0;
        double io_eV = 0.0;
        double error_eV = 0.0;
        double bath_clock_eV = 0.0;
    };

    struct EnergyCellDissipation {
        std::string name;
        double x = 0.0;
        double y = 0.0;
        int layer_index = 0;
        int clock = 0;
        FCNCellFunction function = FCNCellFunction::NORMAL;
        double bath_eV = 0.0;
        double clock_eV = 0.0;
        double io_eV = 0.0;
        double error_eV = 0.0;
        double bath_clock_eV = 0.0;
    };

    struct EnergyAnalysisSummary {
        bool available = false;
        int cycle_count = 0;
        double total_bath_eV = 0.0;
        double total_clock_eV = 0.0;
        double total_io_eV = 0.0;
        double total_error_eV = 0.0;
        double total_bath_clock_eV = 0.0;
        double average_bath_eV = 0.0;
        double average_clock_eV = 0.0;
        double average_io_eV = 0.0;
        double average_error_eV = 0.0;
        double average_bath_clock_eV = 0.0;
        std::vector<EnergyCycleDissipation> cycles;
        std::vector<EnergyCellDissipation> cells;
    };

    class Result {
    public:
        std::vector<Trace> inputs;
        std::vector<Trace> outputs;
        std::vector<Trace> clocks;
        EnergyAnalysisSummary energy_analysis;

        bool empty() const {
            return inputs.empty() && outputs.empty() && clocks.empty();
        }

        void write_text_file(const std::string &ofname) const {
            //assert(!inputs.empty());
            assert(!outputs.empty());
            assert(clocks.size() == 4);

            std::ofstream ofs(ofname);

            auto col = clocks[0].data.size();

            //head
            ofs << "[SIMULATION_OUTPUT]\n";
            ofs << "[SIMULATION_DATA]\n";
            ofs << "number_samples="    << col   << "\n";
            ofs << "number_of_traces="  << inputs.size() + outputs.size() << "\n";

            //traces
            ofs << "[TRACES]" << std::endl;

            for(auto &trace : inputs) {
                ofs << "[TRACE]\n";
                ofs << "data_labels=" << trace.data_labels << "\n";
                ofs << "trace_function=" << 1 << "\n";
                ofs << "drawtrace=TRUE\n";

                ofs << "[TRACE_DATA]\n";
                for(auto &d : trace.data) {
                    ofs << d << " ";
                }
                ofs << std::endl;
                ofs << "[#TRACE_DATA]\n";

                ofs << "[#TRACE]" << std::endl;
            }

            for(auto &trace : outputs) {
                ofs << "[TRACE]\n";
                ofs << "data_labels=" << trace.data_labels << "\n";
                ofs << "trace_function=" << 2 << "\n";
                ofs << "drawtrace=TRUE\n";

                ofs << "[TRACE_DATA]\n";
                for(auto &d : trace.data) {
                    ofs << d << " ";
                }
                ofs << std::endl;
                ofs << "[#TRACE_DATA]\n";

                ofs << "[#TRACE]" << std::endl;
            }

            ofs << "[#TRACES]" << std::endl;

            //clocks
            ofs << "[CLOCKS]" << std::endl;

            for(auto &trace : clocks) {
                ofs << "[TRACE]\n";

                ofs << "data_labels=" << trace.data_labels << "\n";
                ofs << "trace_function=" << 3 << "\n";
                ofs << "drawtrace=TRUE\n";

                ofs << "[TRACE_DATA]\n";
                for(auto &d : trace.data) {
                    ofs << d << " ";
                }
                ofs << std::endl;
                ofs << "[#TRACE_DATA]\n";

                ofs << "[#TRACE]" << std::endl;
            }

            ofs << "[#CLOCKS]" << std::endl;

            //tail
            ofs << "[#SIMULATION_DATA]\n";
            ofs << "[TYPE:BUS_LAYOUT]\n";
            ofs << "[#TYPE:BUS_LAYOUT]\n";
            ofs << "[#SIMULATION_OUTPUT]" << std::endl;
        }

        void write_energy_analysis_file(const std::string &ofname) const {
            std::ofstream ofs(ofname);
            if (!ofs) {
                return;
            }

            auto function_name = [](FCNCellFunction function) -> const char* {
                switch (function) {
                    case FCNCellFunction::INPUT:
                        return "INPUT";
                    case FCNCellFunction::OUTPUT:
                        return "OUTPUT";
                    case FCNCellFunction::FIXED:
                        return "FIXED";
                    case FCNCellFunction::LAST_FUNCTION:
                        return "LAST_FUNCTION";
                    case FCNCellFunction::NORMAL:
                    default:
                        return "NORMAL";
                }
            };

            ofs << "[ENERGY_ANALYSIS]\n";
            ofs << "available=" << (energy_analysis.available ? "TRUE" : "FALSE") << "\n";
            ofs << "cycle_count=" << energy_analysis.cycle_count << "\n";
            ofs << "total_bath_eV=" << energy_analysis.total_bath_eV << "\n";
            ofs << "total_clock_eV=" << energy_analysis.total_clock_eV << "\n";
            ofs << "total_io_eV=" << energy_analysis.total_io_eV << "\n";
            ofs << "total_error_eV=" << energy_analysis.total_error_eV << "\n";
            ofs << "total_bath_clock_eV=" << energy_analysis.total_bath_clock_eV << "\n";
            ofs << "average_bath_eV=" << energy_analysis.average_bath_eV << "\n";
            ofs << "average_clock_eV=" << energy_analysis.average_clock_eV << "\n";
            ofs << "average_io_eV=" << energy_analysis.average_io_eV << "\n";
            ofs << "average_error_eV=" << energy_analysis.average_error_eV << "\n";
            ofs << "average_bath_clock_eV=" << energy_analysis.average_bath_clock_eV << "\n";

            ofs << "[PER_CYCLE]\n";
            ofs << "cycle,E_bath_eV,E_clk_eV,E_io_eV,E_error_eV,E_bath_clk_eV\n";
            for (const auto &cycle : energy_analysis.cycles) {
                ofs << cycle.cycle_index << ","
                    << cycle.bath_eV << ","
                    << cycle.clock_eV << ","
                    << cycle.io_eV << ","
                    << cycle.error_eV << ","
                    << cycle.bath_clock_eV << "\n";
            }
            ofs << "[#PER_CYCLE]\n";

            ofs << "[PER_CELL]\n";
            ofs << "name,layer,clock,x,y,function,E_bath_eV,E_clk_eV,E_io_eV,E_error_eV,E_bath_clk_eV\n";
            for (const auto &cell : energy_analysis.cells) {
                ofs << cell.name << ","
                    << cell.layer_index << ","
                    << cell.clock << ","
                    << cell.x << ","
                    << cell.y << ","
                    << function_name(cell.function) << ","
                    << cell.bath_eV << ","
                    << cell.clock_eV << ","
                    << cell.io_eV << ","
                    << cell.error_eV << ","
                    << cell.bath_clock_eV << "\n";
            }
            ofs << "[#PER_CELL]\n";
            ofs << "[#ENERGY_ANALYSIS]" << std::endl;
        }
    };

#define GEN_ACCESSING_API(Type, field)                                              \
    inline auto field(const Type &data) -> const decltype(data.field)& {            \
        return data.field;                                                          \
    }                                                                               \
    inline auto field(Type &data)       ->       decltype(data.field)& {            \
        return data.field;                                                          \
    }

    template<typename Layer>
    inline auto size(const Layer &layer) -> decltype(layer.cells.size()) {
        return layer.cells.size();
    }

    template<typename Layer>
    inline auto begin(const Layer &layer) -> decltype(layer.cells.cbegin()){
        return layer.cells.begin();
    }

    template<typename Layer>
    inline auto begin(Layer &layer) -> decltype(layer.cells.begin()) {
        return layer.cells.begin();
    }

    template<typename Layer>
    inline auto end(const Layer &layer) -> decltype(layer.cells.cend()) {
        return layer.cells.cend();
    }

    template<typename Layer>
    inline auto end(Layer &layer) -> decltype(layer.cells.end()) {
        return layer.cells.end();
    }

    template<typename Layer, typename Fn>
    inline void foreach_cell(const Layer &layer, Fn &&fn) {
        std::for_each(begin(layer), end(layer), fn);
    }

    template<typename Layer, typename Fn>
    inline void foreach_cell(Layer &layer, Fn &&fn) {
        std::for_each(begin(layer), end(layer), fn);
    }

    template<typename Layer, typename Fn>
    inline void foreach_cell_randomly(Layer &layer, Fn &&fn) {
        static std::vector<std::size_t> index;
        static std::random_device rd;
        static std::mt19937 g(rd());

        index.clear();
        for(std::size_t i=0; i<layer.cells.size(); ++i){
            index.push_back(i);
        }

        std::shuffle(index.begin(), index.end(),g);
        auto first = boost::iterators::make_permutation_iterator(begin(layer), index.begin());
        auto last  = boost::iterators::make_permutation_iterator(begin(layer), index.end());
        std::for_each(first, last, fn);
    }

    template<typename Design>
    inline auto size(const Design &design) -> decltype(design.layers.size()) {
        return design.layers.size();
    }

    template<typename Design>
    inline auto begin(const Design &design) -> decltype(design.layers.cbegin()) {
        return design.layers.cbegin();
    }

    template<typename Design>
    inline auto begin(Design &design) -> decltype(design.layers.begin()) {
        return design.layers.begin();
    }

    template<typename Design>
    inline auto end(const Design &design) -> decltype(design.layers.cend()) {
        return design.layers.cend();
    }

    template<typename Design>
    inline auto end(Design &design) -> decltype(design.layers.end()){
        return design.layers.end();
    }

    template<typename Design, typename Fn>
    inline void foreach_layer(const Design &design, Fn &&fn)  {
        std::for_each(begin(design), end(design), fn);
    }

    template<typename Design, typename Fn>
    inline void foreach_layer(Design &design, Fn &&fn)  {
        std::for_each(begin(design), end(design), fn);
    }

    struct QCADot {
        double x = 0.0;
        double y = 0.0;
        double diameter = 5.0;
        double charge = 0.0;
        double spin = 0.0;
        double potential = 0.0;
    };

    GEN_ACCESSING_API(QCADot, x)
    GEN_ACCESSING_API(QCADot, y)
    GEN_ACCESSING_API(QCADot, diameter)
    GEN_ACCESSING_API(QCADot, charge)
    GEN_ACCESSING_API(QCADot, spin)
    GEN_ACCESSING_API(QCADot, potential)

    struct QCACell {
        std::string name;
        double x = 0.0;
        double y = 0.0;
        double width = 18;
        double height = 18;
        double polarization = 0.0;
        FCNCellFunction function = FCNCellFunction::NORMAL;
        int timezone = 0;
        int layer_index = 0;
        std::array<QCADot, 4> dots;
        std::any extrinsics;
        QCACellMode cellMode;
    };

    GEN_ACCESSING_API(QCACell, name)
    GEN_ACCESSING_API(QCACell, x)
    GEN_ACCESSING_API(QCACell, y)
    GEN_ACCESSING_API(QCACell, width)
    GEN_ACCESSING_API(QCACell, height)
    GEN_ACCESSING_API(QCACell, polarization)
    GEN_ACCESSING_API(QCACell, function)
    GEN_ACCESSING_API(QCACell, timezone)
    GEN_ACCESSING_API(QCACell, layer_index)
    GEN_ACCESSING_API(QCACell, dots)
    GEN_ACCESSING_API(QCACell, extrinsics)
    GEN_ACCESSING_API(QCACell, cellMode)

    struct QCALayer {
        int index = 0;
        std::string description;
        std::vector<QCACell> cells;
    };
    GEN_ACCESSING_API(QCALayer, index)
    GEN_ACCESSING_API(QCALayer, description)
    GEN_ACCESSING_API(QCALayer, cells)

    struct QCADesign {
        std::vector<QCALayer> layers;
    };
    GEN_ACCESSING_API(QCADesign, layers)

    struct NMLCell {
        long x       = 0;
        long y       = 0;
        long width   = 0;
        long height  = 0;
        std::string name;
        int clock    = 0;
        FCNCellFunction function = FCNCellFunction::NORMAL;
        double magnetization = 0.0;
    };
    GEN_ACCESSING_API(NMLCell, x)
    GEN_ACCESSING_API(NMLCell, y)
    GEN_ACCESSING_API(NMLCell, width)
    GEN_ACCESSING_API(NMLCell, height)
    GEN_ACCESSING_API(NMLCell, name)
    GEN_ACCESSING_API(NMLCell, clock)
    GEN_ACCESSING_API(NMLCell, function)
    GEN_ACCESSING_API(NMLCell, magnetization)

    struct NMLLayer {
        std::vector<NMLCell> cells;
    };
    GEN_ACCESSING_API(NMLLayer, cells)

    struct NMLDesign {
        std::vector<NMLLayer> layers;
    };
    GEN_ACCESSING_API(NMLDesign, layers)

#undef GEN_ACCESSING_API
}//namespace simon

#endif
