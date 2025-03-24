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

#if !defined(HFUT_SIMON_SIMULATION_HPP)
#define      HFUT_SIMON_SIMULATION_HPP

#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <type_traits>
#include <random>
#include <iostream>

#include <boost/algorithm/apply_permutation.hpp>
#include <boost/graph/adjacency_list.hpp>

#include "Model.hpp"
#include "IO.hpp"
#include "Option.hpp"


namespace boost {
    enum vertex_x_t {vertex_x = 200};
    BOOST_INSTALL_PROPERTY(vertex, x);
    enum vertex_y_t {vertex_y = 201};
    BOOST_INSTALL_PROPERTY(vertex, y);
    enum vertex_width_t {vertex_width = 202};
    BOOST_INSTALL_PROPERTY(vertex, width);
    enum vertex_height_t {vertex_height = 203};
    BOOST_INSTALL_PROPERTY(vertex, height);
    enum vertex_clock_t {vertex_clock = 205};
    BOOST_INSTALL_PROPERTY(vertex, clock);
    enum vertex_function_t {vertex_function = 206};
    BOOST_INSTALL_PROPERTY(vertex, function);
    enum vertex_magnetization_t {vertex_magnetization = 207};
    BOOST_INSTALL_PROPERTY(vertex, magnetization);
    enum vertex_new_magnetization_t {vertex_new_magnetization = 204};
    BOOST_INSTALL_PROPERTY(vertex,new_magnetization);
}

namespace simon
{
    template<typename Host>
    struct FCNSamplingPolicy {
        std::size_t number_of_samples;
        std::size_t sampling_step;

        void initialize_sample_size() {}
    };

    template<typename Host>
    struct DiscreteSamplingPolicy  : public FCNSamplingPolicy<Host> {
        void initialize_sample_size() {
            auto &self = static_cast<Host&>(*this);
            self.number_of_samples    = self.option.number_of_samples;
            self.sampling_step        = 1;
        }
    };

    template<typename Host>
    struct ContinuousSamplingPolicy  : public FCNSamplingPolicy<Host> {
        void initialize_sample_size() {
            auto &self = static_cast<Host&>(*this);
            self.number_of_samples  = static_cast<std::size_t>(std::ceil(self.option.duration / self.option.time_step));
            if(self.number_of_samples <= 3000)
                self.sampling_step        = 1;
            else
                self.sampling_step        = static_cast<std::size_t>(self.number_of_samples - 1)/3000;
        }
    };

    template<typename Host>
    struct FCNInputPolicy {
        std::function<double(std::size_t, std::size_t)> input_generator;

        void initialize_input_generator(SimulationMode mode, std::size_t sample_n, const VectorTable &vector_table) {
            std::function<double(std::size_t, std::size_t)> generator;
            switch(mode) {
                case SimulationMode::Exhaustive:
                    generator = [sample_n](std::size_t i, std::size_t j) -> double {
                        assert(j < sample_n);

                        double four_pi_over_samples = constants::FOUR_PI / static_cast<double>(sample_n);
                        double omiga = static_cast<double>(1u<<i) * j * four_pi_over_samples;
                        double ret = (sin(omiga) >= 0) ? -1 : 1;
                        //the above equation is the same as cos(omiga-pi/2) > 0, omiga is in the right side of y axis
                        return ret;
                    };
                    break;
                case SimulationMode::Selective:
                    generator = [sample_n, &vector_table](std::size_t i, std::size_t j) -> double {
                        assert(j < sample_n);
                        double vector_table_size_over_samples = static_cast<double>(vector_table.test_vectors.size())
                                                                / static_cast<double>(sample_n);
                        return vector_table(i, static_cast<std::size_t>(j*vector_table_size_over_samples));
                    };
                    break;
            };
            input_generator = generator;
        }
    };

    template<typename Host>
    struct FCNClockPolicy {
        void initialize_clock_generator(SimulationMode mode, std::size_t sample_n,
                                        const VectorTable &vector_table, const std::vector<std::string> &inames){}
        std::function<double(std::size_t, std::size_t)> clock_generator;
    };

    template<typename Host>
    struct FCNEnginePolicy {
        //initialization
        template<typename Design>
        void initialize_io(const Design &design, VectorTable &vector_table,
                           std::vector<std::string> &inames, std::vector<std::string> &onames) {}

        template<typename Design>
        void initialize_design(Design &design) const {}

        void initialize_result(Result &result, std::size_t number_of_clock_phases) const {
            auto &self = static_cast<const Host&>(*this);
            assert(!self.inames.empty() && "iname is empty!");
            assert(!self.onames.empty() && "oname is empty!");

            for (auto name : self.inames) {
                result.inputs.emplace_back();
                result.inputs.back().data_labels = name;
                result.inputs.back().trace_function = TraceFunction::INPUT;
                result.inputs.back().data.resize((self.number_of_samples - 1) / self.sampling_step + 1);
            }

            for (auto name : self.onames) {
                result.outputs.emplace_back();
                result.outputs.back().data_labels = name;
                result.outputs.back().trace_function = TraceFunction::OUTPUT;
                result.outputs.back().data.resize((self.number_of_samples - 1) / self.sampling_step + 1);
            }

            for (std::size_t i = 0; i < number_of_clock_phases; ++i) {
                result.clocks.emplace_back();
                result.clocks.back().data_labels = std::string("Clock ") + std::to_string(i);
                result.clocks.back().trace_function = TraceFunction::CLOCK;

                for (std::size_t j = 0; j < self.number_of_samples; ++j) {
                    if (j % self.sampling_step == 0)
                        result.clocks.back().data.emplace_back(self.clock_generator(i, j));
                }
            }
        }

        //iteration
        template<typename Design>
        void before_iterations(Design &design, const Result &result) {(void)design; (void)result;}

        template<typename Design>
        void make_iterations(Design &design, Result &result) {(void)design; (void)result;}

        template<typename Design>
        void after_iterations(const Design &design, const Result &result) const {(void)design; (void)result;}

        template<typename Design>
        void run(Design &design, VectorTable &vector_table, Result &result, SimulationMode mode,
                 std::vector<std::string> inames={}, std::vector<std::string> onames={}) {
            (void)design;
            (void)vector_table;
            (void)result;
            (void)mode;
            (void)inames;
            (void)onames;
        }
    };

    template<template<typename> class Engine, template<typename> class ...Policy>
    class SimulationDriver : public Engine<SimulationDriver<Engine, Policy...>>, public Policy<SimulationDriver<Engine, Policy...>>... {
    public:
        template<typename Option>
        explicit SimulationDriver(Option &option) : Engine<SimulationDriver<Engine, Policy...>>(option) {}
    };

    //////////////////////////1. sampling size policy///////////////////////////////////////


    //////////////////////////2. clock generator policy//////////////////////////////////
    /*
    *      omiga                j
    * -----------------  = ------------- (Exhaustive simluation have 2*input_num cycle)
    * 2*input_num *2*pi      sample_num
    *
    *   omiga                   j
    * ----------------  = ---------------- (Selective simulation, each test vector pocess one clock cycle)
    *  input_num*2*pi        sample_num
    */
    template<typename Host>
    struct QCAClockPolicy  : public FCNClockPolicy<Host> {

        void initialize_clock_generator(SimulationMode mode, std::size_t sample_n,
                                        const VectorTable &vector_table, const std::vector<std::string> &inames) {
            auto &self = static_cast<Host&>(*this);

            std::size_t cycle_n = 0;
            switch(mode) {
                case SimulationMode::Exhaustive:
                    cycle_n = 1u<<(1+inames.size());
                    break;
                case SimulationMode::Selective:
                    assert(!vector_table.names.empty());
                    cycle_n = vector_table.test_vectors.size();
                    break;
            }

            auto generator = [cycle_n, sample_n, &option=self.option](std::size_t i, std::size_t j) -> double {
                double prefactor = (option.high - option.low) * option.amplitude;
                double two_pi_cycles_over_samples = static_cast<double>(cycle_n) * constants::TWO_PI / static_cast<double>(sample_n);
                double tmp = prefactor * (
                        std::cos(
                                two_pi_cycles_over_samples * static_cast<double>(j)
                                + constants::PI * option.jitters[i] / 180.0
                                - constants::PI * static_cast<double>(i) / 2.0
                        )
                ) + option.shift;

                return std::clamp(tmp, option.low, option.high);
            };

            self.clock_generator = generator;
        }
    };

    //////////////////////////3. free function definition//////////////////////////////////
    inline double calculate_cell_polarization(QCACell &cell) {
        double ret;
        ret = ( charge( dots(cell)[0] ) - charge( dots(cell)[1] ) ) * constants::OVER_QCHARGE;
        return ret;
    };

    inline void update_cell_polarization(QCACell &cell, double new_polarization) {
        double half_charge_polarization = constants::HALF_QCHARGE * new_polarization;
        double charge1 = constants::HALF_QCHARGE + half_charge_polarization;
        double charge2 = constants::HALF_QCHARGE - half_charge_polarization;
        charge( dots(cell)[0] ) = charge1;
        charge( dots(cell)[2] ) = charge1;
        charge( dots(cell)[1] ) = charge2;
        charge( dots(cell)[3] ) = charge2;
    };

    inline double calculate_inter_dot_squared_horizontal_distance(const QCADot &dot1, const QCADot &dot2) {
        double ret = 0.0;
        ret += std::pow(x(dot1) - x(dot2), 2);
        ret += std::pow(y(dot1) - y(dot2), 2);
        return ret;
    };

    template<typename Option>
    inline double calculate_inter_cell_squared_vertical_distance(const QCACell &cell1, const QCACell &cell2, const Option &opt) {
        double ret = opt.layer_separation * std::abs( layer_index(cell1) - layer_index(cell2) );
        ret = std::pow(ret, 2);
        return ret;
    };

    template<typename Option>
    inline double calculate_inter_cell_distance(const QCACell &cell1, const QCACell &cell2, const Option &opt) {
        double ret = calculate_inter_cell_squared_vertical_distance(cell1, cell2, opt);
        ret += std::pow(x(cell1) - x(cell2), 2);
        ret += std::pow(y(cell1) - y(cell2), 2);
        ret = std::sqrt(ret);
        return ret;
    };

    template<typename Option>
    inline double calculate_inter_cell_kink_energy(const QCACell &cell1, const QCACell &cell2, const Option &opt) {
        using constants::QCHARGE_SQUARE_OVER_FOUR;

        static double same_polarization[4][4] = {
                {QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR},
                {-QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR},
                {QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR},
                {-QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR}
        };

        static double diff_polarization[4][4] = {
                {-QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR},
                {QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR},
                {-QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR},
                {QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR, QCHARGE_SQUARE_OVER_FOUR, -QCHARGE_SQUARE_OVER_FOUR}
        };

        double vertical_distance_squared =
                calculate_inter_cell_squared_vertical_distance(cell1, cell2, opt);

        double distance    = 0.0;
        double energy_same = 0.0;
        double energy_diff = 0.0;

        //1e-9 is nano meter unit
        for(std::size_t i=0; i<4; ++i) {
            for(std::size_t j=0; j<4; ++j) {
                distance = sqrt(vertical_distance_squared +
                                calculate_inter_dot_squared_horizontal_distance(dots(cell1)[i], dots(cell2)[j]));
                energy_same += 1e9 * same_polarization[i][j] / distance;
                energy_diff += 1e9 * diff_polarization[i][j] / distance;
            }
        }
        return 1 / (constants::FOUR_PI_EPSILON * opt.epsilon_r) * (energy_diff - energy_same);
    };

    //////////////////////////5. simulation engine policy///////////////////////////////////
    template<typename Host>
    struct QCAEnginePolicy : public FCNEnginePolicy<Host> {
        std::vector<std::string> inames;
        std::vector<std::string> onames;
        std::vector<QCACell*> inputs;
        std::vector<QCACell*> outputs;

        //initializations
        void initialize_io(QCADesign &design, VectorTable &vector_table,
                           std::vector<std::string> &inames, std::vector<std::string> &onames) {
            bool permute_io_order = !inames.empty() || !onames.empty();

            auto &self = static_cast<Host&>(*this);
            self.inputs.clear();
            self.outputs.clear();

            if(permute_io_order) {
                self.inames = std::move(inames);
                self.onames = std::move(onames);

                for (auto &layer : design) {
                    for (auto &cell : layer) {
                        if(function(cell) == FCNCellFunction::INPUT)
                            self.inputs.push_back(&cell);
                        else if(function(cell) == FCNCellFunction::OUTPUT)
                            self.outputs.push_back(&cell);
                    }
                }

                //reset input order
                std::map<std::string, std::size_t> iname_map;
                for(std::vector<std::string>::size_type i=0; i<self.inames.size(); ++i) {
                    iname_map[self.inames[i]] = i;
                }

                std::vector<std::size_t> order;
                for(auto &pcell : self.inputs) {
                    order.push_back( iname_map[ name(*pcell) ] );
                }

                boost::algorithm::apply_reverse_permutation(self.inputs, order);
                vector_table.reset_names_order(self.inames);//vector table

                //reset output order
                std::map<std::string, int> oname_map;
                for(std::vector<std::string>::size_type i=0; i<self.onames.size(); ++i) {
                    oname_map[self.onames[i]] = i;
                }

                order.clear();
                for(auto &pcell : self.outputs) {
                    order.push_back( oname_map[ name(*pcell) ] );
                }

                boost::algorithm::apply_reverse_permutation(self.outputs, order);
            } else {
                self.onames.clear();
                self.inames.clear();

                for (auto &layer : design) {
                    for (auto &cell : layer) {
                        if(function(cell) == FCNCellFunction::INPUT) {
                            self.inputs.push_back(&cell);
                            self.inames.push_back(name(cell));
                        }
                        else if(function(cell) == FCNCellFunction::OUTPUT) {
                            self.outputs.push_back(&cell);
                            self.onames.push_back(name(cell));
                        }
                    }
                }
            }
        }

        void initialize_design(QCADesign &design) const {

            for(auto &layer :design) {
                for (auto &cell : layer) {
                    extrinsics(cell).reset();
                    extrinsics(cell).emplace<typename Host::ExtrinsicProperty >();

                    layer_index(cell) = index(layer);
                    if (function(cell) == FCNCellFunction::FIXED) {
                        polarization(cell) = calculate_cell_polarization(cell);
                    }
                }
            }

            auto &self = static_cast<const Host&>(*this);
            for (auto &layer1 : design) {
                for (auto &cell1 : layer1) {

                    for (auto &layer2 : design) {
                        for (auto &cell2 : layer2) {

                            if (&cell1 != &cell2 &&
                                calculate_inter_cell_distance(cell1, cell2, self.option) <= self.option.radius_effect) {
                                neighbours(cell1).push_back(&cell2);
                                kink_energies(cell1).push_back(
                                        calculate_inter_cell_kink_energy(cell1, cell2, self.option));
                            }
                        }
                    }
                }
            }
        }

        void make_iterations(QCADesign &design, Result &result) {
            auto &self = static_cast<Host&>(*this);

            bool feed_result = false;
            for(std::size_t j=0; j<self.number_of_samples; ++j) {
                feed_result = ((j % self.sampling_step) == 0);

                double input_data{0};
                for(std::size_t i=0; i<self.inputs.size(); ++i) {
                    input_data = self.input_generator(i,j);
                    polarization( *(self.inputs[i]) ) = input_data;
                    if(feed_result) {
                        result.inputs[i].data[j/self.sampling_step] = input_data;
                    }
                }

                self.compute_current_iteration(j, design, result);

                if(feed_result) {
                    for(std::size_t i=0; i<self.outputs.size(); ++i) {
                        result.outputs[i].data[j/self.sampling_step] = polarization( *self.outputs[i] );
                    }
                }
            }
        }

        void run(QCADesign &design, VectorTable &vector_table, Result &result, SimulationMode mode,
                 std::vector<std::string> inames={}, std::vector<std::string> onames={}) {
            auto &self = static_cast<Host&>(*this);
            //initilizations
            self.initialize_sample_size();
            self.initialize_io(design, vector_table, inames, onames);

            self.initialize_input_generator(mode, self.number_of_samples, vector_table);
            self.initialize_clock_generator(mode, self.number_of_samples, vector_table, self.inames);

            self.initialize_design(design);
            self.initialize_result(result, 4);

            //iteration
            self.before_iterations(design, result);
            self.make_iterations(design, result);
            self.after_iterations(design, result);
        }

    protected:
        //Extrinsic Property Accessing API
        static inline std::vector<QCACell*> &neighbours(QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->neighbours;
        }
        static inline const std::vector<QCACell*> &neighbours(const QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->neighbours;
        }
        static inline std::vector<double> &kink_energies(QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->kink_energies;
        }
        static inline const std::vector<double> &kink_energies(const QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->kink_energies;
        }
    };

    template<typename Host>
    struct BistableEngine : public QCAEnginePolicy<Host> {
        QCABistableOption const &option;

        struct ExtrinsicProperty {
            double lambda_x;
            double lambda_y;
            double lambda_z;
            std::vector<double> kink_energies;
            std::vector<QCACell*>  neighbours;
        };

        explicit BistableEngine(const QCABistableOption &option) : option{option} {}

        void before_iterations(QCADesign &design, const Result &result) {
            (void)design;
            (void)result;
        }

        void compute_current_iteration(std::size_t j, QCADesign &design, Result &result) const {
            bool stable     = false;
            double old_pola = 0.0;
            double new_pola = 0.0;
            double tmp      = 0.0;

            auto &self = static_cast<const Host&>(*this);
            for(std::size_t k=0; !stable && k<self.option.max_iteration_per_sample; ++k) {
                stable = true;

                for(auto &layer : design ) {
                    foreach_cell_randomly(layer, [&](auto &cell){

                        if(function(cell) != FCNCellFunction::INPUT &&
                           function(cell) != FCNCellFunction::FIXED ) {

                            old_pola = polarization(cell);

                            new_pola = 0.0;
                            tmp      = 0.0;

                            for(std::size_t i=0; i<neighbours(cell).size(); ++i) {
                                tmp += kink_energies(cell)[i] *
                                       polarization( *neighbours(cell)[i] );
                            }

                            tmp /= 2 * result.clocks[simon::timezone(cell)].data[j];

                            new_pola =  (tmp            > 1000.0  ) ?    1   :
                                        (tmp            < -1000.0 ) ?   -1   :
                                        (std::fabs(tmp) < 0.001   ) ?   tmp  :
                                        tmp / std::sqrt(1 + tmp*tmp);

                            polarization(cell) = new_pola;

                            stable = (std::fabs(new_pola - old_pola) <= self.option.convergence_tolerance);
                        }
                    });
                }
            }
        };

    protected:
        using QCAEnginePolicy<Host>::kink_energies;
        using QCAEnginePolicy<Host>::neighbours;
    };

    template<typename Host>
    struct CoherenceEngine : public QCAEnginePolicy<Host> {
        QCACoherenceOption const &option;

        struct ExtrinsicProperty {
            double lambda_x;
            double lambda_y;
            double lambda_z;
            std::vector<double> kink_energies;
            std::vector<QCACell*>  neighbours;
        };

        explicit CoherenceEngine(const QCACoherenceOption &option) : option{option}, HBAR_OVER_KBT{constants::HBAR * constants::over_kB / option.T}
        {}

        void before_iterations(QCADesign &design, const Result &result) {
            converge_to_steady_state(design, result);
        }

        void compute_current_iteration(std::size_t j, QCADesign &design, Result &result) const {
            (void)result;
            double PEk = 0;
            double lambda_x_ = 0;
            double lambda_y_ = 0;
            double lambda_z_ = 0;
            double lambda_x_new = 0;
            double lambda_y_new = 0;
            double lambda_z_new = 0;
            double t = option.time_step * static_cast<double>(j);

            for(auto &layer : design) {
                for(auto &cell : layer) {

                    if(function(cell) == FCNCellFunction::INPUT || function(cell) == FCNCellFunction::FIXED)
                        continue;

                    PEk = 0;
                    std::size_t neighbour_size = neighbours(cell).size();
                    for(std::size_t i=0; i<neighbour_size; ++i) {
                        PEk += kink_energies(cell)[i] * polarization( *neighbours(cell)[i] );
                    }

                    lambda_x_ = lambda_x(cell);
                    lambda_y_ = lambda_y(cell);
                    lambda_z_ = lambda_z(cell);

                    auto &self = static_cast<const Host&>(*this);
                    double cur_gammar = self.clock_generator(simon::timezone(cell), j);
                    lambda_x_new = lambda_x_next(t, PEk, cur_gammar, lambda_x_, lambda_y_, lambda_z_);
                    lambda_y_new = lambda_y_next(t, PEk, cur_gammar, lambda_x_, lambda_y_, lambda_z_);
                    lambda_z_new = lambda_z_next(t, PEk, cur_gammar, lambda_x_, lambda_y_, lambda_z_);

                    lambda_x(cell) = lambda_x_new;
                    lambda_y(cell) = lambda_y_new;
                    lambda_z(cell) = lambda_z_new;
                    polarization(cell) = -lambda_z_new;
                }
            }
        }

    protected:
        using QCAEnginePolicy<Host>::kink_energies;
        using QCAEnginePolicy<Host>::neighbours;

        static inline double &lambda_x(QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->lambda_x;
        }
        static inline const double &lambda_x(const QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->lambda_x;
        }
        static inline double &lambda_y(QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->lambda_y;
        }
        static inline const double &lambda_y(const QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->lambda_y;
        }
        static inline double &lambda_z(QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->lambda_z;
        }
        static inline const double &lambda_z(const QCACell &cell) {
            return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->lambda_z;
        }

        void converge_to_steady_state(QCADesign &design, const Result &result) const {
            bool stable = false;
            double PEk = 0;
            double old_lambda_x = 0;
            double old_lambda_y = 0;
            double old_lambda_z = 0;

            while(!stable) {
                stable = true;
                for(auto &layer : design) {
                    for(auto &cell : layer) {

                        if(function(cell) == FCNCellFunction::INPUT || function(cell) == FCNCellFunction::FIXED)
                            continue;

                        PEk = 0;

                        std::size_t neighbour_size = neighbours(cell).size();
                        for(std::size_t i=0; i<neighbour_size; ++i) {
                            PEk += kink_energies(cell)[i] *
                                   polarization( *neighbours(cell)[i] );
                        }

                        old_lambda_x = lambda_x(cell);
                        old_lambda_y = lambda_y(cell);
                        old_lambda_z = lambda_z(cell);

                        double lambda_x_ = lambda_ss_x(PEk, result.clocks[simon::timezone(cell)].data[0]);
                        double lambda_y_ = lambda_ss_y(PEk, result.clocks[simon::timezone(cell)].data[0]);
                        double lambda_z_ = lambda_ss_z(PEk, result.clocks[simon::timezone(cell)].data[0]);

                        lambda_x(cell) = lambda_x_;
                        lambda_y(cell) = lambda_y_;
                        lambda_z(cell) = lambda_z_;
                        polarization(cell) = lambda_z_;

                        stable = !(
                                std::fabs(lambda_x_ - old_lambda_x) > 1e-7 ||
                                std::fabs(lambda_y_ - old_lambda_y) > 1e-7 ||
                                std::fabs(lambda_z_ - old_lambda_z) > 1e-7
                        );
                    }
                }
            }
        }

        double lambda_ss_x(double PEk, double Gamma) const {
            double ret = 2.0 * Gamma * constants::over_hbar /
                         magnitude_energy_vector(PEk, Gamma) *
                         tanh(temp_ratio(PEk, Gamma, option.T));
            return ret;
        }

        double lambda_ss_y(double PEk, double Gamma) const {
            (void)PEk;
            (void)Gamma;
            return 0.0;
        }

        double lambda_ss_z(double PEk, double Gamma) const {
            double ret = -PEk * constants::over_hbar /
                         magnitude_energy_vector(PEk, Gamma) *
                         tanh(temp_ratio(PEk, Gamma, option.T));
            return ret;
        }

        double magnitude_energy_vector(double p, double g) const {
            double ret = std::hypot(2 * g, p) * constants::OVER_HBAR;
            return ret;
        }

        double temp_ratio(double p, double g, double T) const {
            double ret = std::hypot(g, p * 0.5) / (T * constants::kB);
            return ret;
        }

        double slope_x(double t, double PEk, double Gamma,
                       double lambda_x, double lambda_y, double lambda_z) const {
            (void)t;
            (void)lambda_z;

            double mag = magnitude_energy_vector(PEk, Gamma);
            double ret = (2.0*Gamma*constants::over_hbar / mag * tanh(HBAR_OVER_KBT * mag * 0.5) - lambda_x)
                         / option.relaxation - (PEk * lambda_y * constants::over_hbar);
            return ret;
        }

        double slope_y(double t, double PEk, double Gamma,
                       double lambda_x, double lambda_y, double lambda_z) const {
            (void)t;

            double ret = (option.relaxation * (PEk * lambda_x + 2.0 * Gamma * lambda_z) -
                          constants::hbar * lambda_y) / (option.relaxation * constants::hbar);
            return ret;
        }

        double slope_z(double t, double PEk, double Gamma,
                       double lambda_x, double lambda_y, double lambda_z) const {
            (void)t;
            (void)lambda_x;

            double mag = magnitude_energy_vector(PEk, Gamma);
            double ret = (-PEk * tanh(HBAR_OVER_KBT * mag * 0.5) -
                          mag * (2.0*Gamma*option.relaxation * lambda_y + constants::hbar * lambda_z)) /
                         (option.relaxation * constants::hbar * mag);
            return ret;
        }

        double lambda_x_next(double t, double PEk, double Gamma,
                             double lambda_x, double lambda_y, double lambda_z) const {
            auto &self = static_cast<const Host&>(*this);

            double k1 = option.time_step * self.slope_x (t, PEk, Gamma, lambda_x, lambda_y, lambda_z);
            double k2 = 0, k3 = 0, k4 = 0;
            double ret = 0;

            if (NumericMethod::RungeKutta == option.algorithm) {
                k2 = option.time_step * self.slope_x(t, PEk, Gamma, lambda_x+k1/2, lambda_y, lambda_z);
                k3 = option.time_step * self.slope_x(t, PEk, Gamma, lambda_x+k2/2, lambda_y, lambda_z);
                k4 = option.time_step * self.slope_x(t, PEk, Gamma, lambda_x+k3,   lambda_y, lambda_z);
                ret = lambda_x + k1/6 + k2/3 + k3/3 + k4/6;
                return ret;
            }
            if (NumericMethod::Euler== option.algorithm) {
                ret = lambda_x + k1;
                return ret;
            }
            return ret;
        }

        double lambda_y_next(double t, double PEk, double Gamma,
                             double lambda_x, double lambda_y, double lambda_z) const {
            auto &self = static_cast<const Host&>(*this);

            double k1 = option.time_step * self.slope_y(t, PEk, Gamma, lambda_x, lambda_y, lambda_z);
            double k2 = 0, k3 = 0, k4 = 0;
            double ret = 0;

            if (NumericMethod::RungeKutta == option.algorithm) {
                k2 = option.time_step * self.slope_y(t, PEk, Gamma, lambda_x, lambda_y+k1/2, lambda_z);
                k3 = option.time_step * self.slope_y(t, PEk, Gamma, lambda_x, lambda_y+k2/2, lambda_z);
                k4 = option.time_step * self.slope_y(t, PEk, Gamma, lambda_x, lambda_y+k3,   lambda_z);
                ret = lambda_y + k1/6 + k2/3 + k3/3 + k4/6;
                return ret;
            }
            if (NumericMethod::Euler == option.algorithm) {
                ret = lambda_y + k1;
                return ret;
            }
            return ret;
        }

        double lambda_z_next (double t, double PEk, double Gamma,
                              double lambda_x, double lambda_y, double lambda_z) const {
            auto &self = static_cast<const Host&>(*this);

            double k1 = option.time_step * self.slope_z(t, PEk, Gamma, lambda_x, lambda_y, lambda_z);
            double k2 = 0, k3 = 0, k4 = 0;
            double ret = 0;

            if (NumericMethod::RungeKutta == option.algorithm) {
                k2 = option.time_step * self.slope_z(t, PEk, Gamma, lambda_x, lambda_y, lambda_z+k1/2);
                k3 = option.time_step * self.slope_z(t, PEk, Gamma, lambda_x, lambda_y, lambda_z+k2/2);
                k4 = option.time_step * self.slope_z(t, PEk, Gamma, lambda_x, lambda_y, lambda_z+k3);
                ret = lambda_z + k1/6 + k2/3 + k3/3 + k4/6;
                return ret;
            }
            if (NumericMethod::Euler == option.algorithm) {
                ret = lambda_z + k1;
                return ret;
            }
            return ret;
        }


        double HBAR_OVER_KBT;
    };

    //////////////////////////6. simulation driver definition//////////////////////////////////
    using BistableAlgorithm  = SimulationDriver<BistableEngine,  DiscreteSamplingPolicy,   FCNInputPolicy, QCAClockPolicy>;
    using CoherenceAlgorithm = SimulationDriver<CoherenceEngine, ContinuousSamplingPolicy, FCNInputPolicy, QCAClockPolicy>;

    //////////////////////////7. NMLSim Algorithm implementation//////////////////////////////////
    using boost::vecS;
    using boost::undirectedS;
    using boost::property;
    using boost::vertex_x_t;
    using boost::vertex_y_t;
    using boost::vertex_width_t;
    using boost::vertex_height_t;
    using boost::vertex_name_t;
    using boost::vertex_clock_t;
    using boost::vertex_function_t;
    using boost::vertex_magnetization_t;
    using boost::vertex_new_magnetization_t;
    using boost::edge_weight_t;

    typedef boost::adjacency_list<
            vecS,
            vecS,
            undirectedS,
            property<vertex_x_t,double,
                    property<vertex_y_t,double,
                            property<vertex_width_t,double,
                                    property<vertex_height_t,double,
                                            property<vertex_name_t,std::string,
                                                    property<vertex_clock_t,int,
                                                            property<vertex_function_t,FCNCellFunction,
                                                                    property<vertex_magnetization_t,double,
                                                                            property<vertex_new_magnetization_t, double>
                                                                    >
                                                            >
                                                    >
                                            >
                                    >
                            >
                    >
            >,
            property<edge_weight_t,double>
    > NMLGraph;

    using NMLVertex             = typename boost::graph_traits<NMLGraph>::vertex_descriptor;
    using NMLEdge               = typename boost::graph_traits<NMLGraph>::edge_descriptor;
    using NMLVertexIterator     = typename boost::graph_traits<NMLGraph>::vertex_iterator;
    using NMLAdjVertexIterator  = typename boost::graph_traits<NMLGraph>::adjacency_iterator;

        template<typename Host>
        struct NMLClockPolicy : public FCNClockPolicy<Host> {
            void initialize_clock_generator(SimulationMode mode, std::size_t sample_n,
                                            const VectorTable &vector_table, const std::vector<std::string> &inames) {
                auto &self = static_cast<Host&>(*this);

                //TODO:only support SimulationMode::Exhaustive now
                assert(mode == SimulationMode::Exhaustive);

                std::size_t cycle_n = 0;
                switch(mode) {
                    case SimulationMode::Exhaustive:
                        cycle_n = 1u<<(1+inames.size());
                        break;
                    case SimulationMode::Selective:
                        assert(!vector_table.names.empty());
                        cycle_n = vector_table.test_vectors.size();
                        break;
                }

                auto generator = [cycle_n, sample_n](std::size_t i, std::size_t j) -> double {
                    double two_pi_cycles_over_samples = static_cast<double>(cycle_n) * constants::TWO_PI / static_cast<double>(sample_n);
                    double tmp = std::cos( two_pi_cycles_over_samples * static_cast<double>(j) - constants::TWO_PI*(i+1)/3.0 );
                    tmp = (std::clamp(tmp, -0.7, -0.3) + 0.5)*5;

                    return tmp;
                };

                self.clock_generator = generator;
            }
        };

        template<typename Host>
        class NMLSimEngine : public FCNEnginePolicy<Host> {
        private:
            std::vector<const NMLCell*> inputs;
            std::vector<const NMLCell*> outputs;
            const NMLOption &option;
        public:
            explicit NMLSimEngine(const NMLOption &opt) : option{opt} {}

            template<typename Design>
            void initialize_io(const Design &design, VectorTable &vector_table,
                               std::vector<std::string> &inames, std::vector<std::string> &onames) {
                for(auto &cell : cells(layers(design)[0])) {
                    if(cell.function == FCNCellFunction::INPUT) {
                        inputs.push_back(&cell);
                    } else if(cell.function == FCNCellFunction::OUTPUT) {
                        outputs.push_back(&cell);
                    }
                }

                if(!inames.empty()) {
                    std::map<std::string, std::size_t> iname_map;
                    for(std::vector<std::string>::size_type i=0; i<inames.size(); ++i) {
                        iname_map[inames[i]] = i;
                    }

                    std::vector<std::size_t> order;
                    for(auto cell : inputs) {
                        order.push_back( iname_map[ cell->name ] );
                    }

                    boost::algorithm::apply_reverse_permutation(inputs, order);
                    vector_table.reset_names_order(inames);//vector table
                }

                if(!onames.empty()) {
                    std::map<std::string, int> oname_map;
                    for(std::vector<std::string>::size_type i=0; i<onames.size(); ++i) {
                        oname_map[onames[i]] = i;
                    }

                    std::vector<std::size_t> order;
                    for(auto cell : outputs) {
                        order.push_back( oname_map[ cell->name ] );
                    }

                    boost::algorithm::apply_reverse_permutation(outputs, order);
                }
            }

            void compute_current_sample(std::size_t j, NMLGraph &g, Result &result) const {
                using boost::tie;
                using boost::vertex_magnetization;
                using boost::vertex_new_magnetization;
                using boost::vertex_function;
                using boost::vertex_clock;
                using boost::edge_weight;

                double tmp;
                NMLVertexIterator vit,vend;
                for(tie(vit, vend)=vertices(g);vit != vend;++vit) {
                    if(get(vertex_function, g, *vit) == FCNCellFunction::FIXED ||
                       get(vertex_function, g, *vit) == FCNCellFunction::INPUT )
                        continue;

                    if(get(vertex_function, g, *vit) == FCNCellFunction::OUTPUT && result.clocks[get(vertex_clock, g, *vit)].data[j] < 0 ) {
                        get(vertex_new_magnetization, g, *vit) = 0;
                        continue;
                    }

                    tmp = get(vertex_magnetization, g, *vit);

                    NMLAdjVertexIterator adj_vit,adj_vend;
                    NMLEdge cur_edge;
                    bool existed;
                    for(tie(adj_vit,adj_vend)=adjacent_vertices(*vit,g); adj_vit!=adj_vend; ++adj_vit){
                        tie(cur_edge, existed) = edge(*vit,*adj_vit,g);
                        tmp += get(vertex_magnetization, g, *adj_vit) * get(edge_weight, g, cur_edge);
                    }

                    tmp *= static_cast<Host>(this)->clock(get(boost::vertex_clock, g, *vit), j);
                    get(vertex_new_magnetization, g, *vit) = tmp>=1 ? 1 : tmp<=-1? -1 : tmp;//std::abs(tmp)<0.001 ? 0 : tmp;
                }

                for(auto its=vertices(g); its.first!=its.second; ++its.first) {
                    get(vertex_magnetization, g, *its.first) = get(vertex_new_magnetization, g, *its.first);
                }
            }

            template<typename Design, typename Cell = decltype(cells(layers(std::declval<Design>())[0])[0])>
            void run(Design &design, VectorTable &vector_table, Result &result, SimulationMode mode,
                     std::vector<std::string> inames={}, std::vector<std::string> onames={}) {
                auto &self = static_cast<Host&>(*this);
                //initilizations
                self.initialize_sample_size();
                self.initialize_input_generator(mode, self.number_of_samples, vector_table);
                self.initialize_clock_generator(mode, self.number_of_samples, vector_table, inames);

                initialize_io(design, vector_table, inames, onames);
                //initialize_design(design);
                self.initialize_result(result, inames, onames, 3);

                NMLGraph graph;
                std::map<const Cell*, NMLVertex> cell_map;
                setup_nml_graph(design, graph, cell_map);

                //iteration
                before_iterations(design, result);
                make_iterations(design, result);
                after_iterations(design, result);
            }

        private:
            template<typename Design, typename Cell = decltype(cells(layers(std::declval<Design>())[0])[0])>
            void setup_nml_graph(const Design &design, NMLGraph &g, std::map<const Cell*, NMLVertex> &cell_map) {
                //1.create vertices
                assert(layers(design).size() == 1);

                for(auto const &cell : layers(design)[0].cells) {
                    NMLVertex v = boost::add_vertex(g);
                    //add all the properties for the current cell
                    get(boost::vertex_x,g,v)                 =   x(cell);
                    get(boost::vertex_y,g,v)                 =   y(cell);
                    get(boost::vertex_width,g,v)             =   width(cell);
                    get(boost::vertex_height,g,v)            =   height(cell);
                    get(boost::vertex_name,g,v)              =   name(cell);
                    get(boost::vertex_clock,g,v)             =   clock(cell);
                    get(boost::vertex_function,g,v)          =   function(cell);
                    get(boost::vertex_magnetization,g,v)     =   magnetization(cell);

                    cell_map[&cell] = v;
                }

                //2.create edges
                long dx=0, dy=0;
                for(size_t s1=0; s1 < cells(layers(design)[0]).size()-1; ++s1) {
                    for(size_t s2=s1+1; s2 < cells(layers(design)[0]).size(); ++s2){
                        auto const &c1 = cells(layers(design)[0])[s1];
                        auto const &c2 = cells(layers(design)[0])[s2];
                        auto &v1 = cell_map[&c1];
                        auto &v2 = cell_map[&c2];

                        dx = std::abs(x(c1) - x(c2));
                        dy = std::abs(y(c1) - y(c2));

                        NMLEdge e;
                        bool inserted = true;

                        if(dx==60 && dy==0) {
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, -0.98, g);
                        } else if(dx==120 && dy==0){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, -0.19, g);
                        } else if(dx==0 && dy==124){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, 1.00, g);
                        } else if(dx==60 && dy==124){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, 0.30, g);
                        } else if(dx==120 && dy==124){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, 0.03, g);
                        } else if(dx==0 && dy==248){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, 0.07, g);
                        } else if(dx==60 && dy==248){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, 0.06, g);
                        } else if(dx==120 && dy==248){
                            boost::tie(e, inserted) = boost::add_edge(v1, v2, 0.03, g);
                        }

                        assert(inserted);
                    }
                }
            }
        };

        using NMLSimAlgorithm = SimulationDriver<NMLSimEngine, DiscreteSamplingPolicy, FCNInputPolicy, NMLClockPolicy>;
}//namespace simon

#endif
