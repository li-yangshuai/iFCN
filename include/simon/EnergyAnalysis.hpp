#if !defined(HFUT_SIMON_QCAD_COHERENCE_ENERGY_HPP)
#define      HFUT_SIMON_QCAD_COHERENCE_ENERGY_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <utility>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <type_traits>
#include <iostream>

#include "Model.hpp"
#include "IO.hpp"
#include "Option.hpp"
#include "Util.hpp"
#include "Simulation.hpp"

namespace simon 
{
    template<typename Host>
        struct EnergyInputPolicy : public FCNInputPolicy<Host> {

            void initialize_input_generator(SimulationMode mode, std::size_t sample_n, const VectorTable &vector_table) {
                std::function<double(std::size_t, std::size_t)> generator;
                auto &self = static_cast<Host&>(*this);

                int number_samples_per_in_period = static_cast<int>(std::ceil(self.eoption.input_period/self.eoption.time_step));
                int inputs_n = self.inputs.size();

                switch(mode) {
                    case SimulationMode::Exhaustive:
                        generator = [sample_n, inputs_n, number_samples_per_in_period](std::size_t i, std::size_t j) -> double {
                            assert(j>=0 && j < sample_n);

                            int number_samples_per_in_period_input = number_samples_per_in_period * static_cast<int>(1u<<(inputs_n - i));   
                            double omiga = static_cast<double>(j % number_samples_per_in_period_input) / static_cast<double>(number_samples_per_in_period_input) * constants::TWO_PI;
                            double ret = (sin(omiga) >= 0) ? -1 : 1; 
                            //the above equation is the same as cos(omiga-pi/2) > 0, omiga is in the right side of y axis
                            return ret;
                        };
                        break;
                    case SimulationMode::Selective:
                        generator = [sample_n, &vector_table](std::size_t i, std::size_t j) -> double {
                            assert(j >= 0 && j < sample_n);
                            double vector_table_size_over_samples = static_cast<double>(vector_table.test_vectors.size())
                                / static_cast<double>(sample_n);
                            return vector_table(i, static_cast<std::size_t>(j*vector_table_size_over_samples));
                        };
                        break;
                };
                self.input_generator = generator;
            }
        };

    template<typename Host>
        struct ClockCOSPolicy  : public FCNClockPolicy<Host> {

            void initialize_clock_generator(SimulationMode mode, std::size_t sample_n,
                    const VectorTable &vector_table, const std::vector<std::string> &inames) {
                auto &self = static_cast<Host&>(*this);

                assert ( mode == SimulationMode::Exhaustive );
                (void) mode;
                (void) sample_n;
                (void) vector_table;
                (void) inames;

                auto generator = [&option=self.eoption](std::size_t i, std::size_t j) -> double {
                    double clock_prefactor = (option.high - option.low) * option.amplitude;
                    int number_samples_per_period = static_cast<int>(std::ceil(option.clock_period/option.time_step));
                    double clock_shift = (option.high + option.low) * 0.5; 

                    j = j + number_samples_per_period;
                    j = (j - i * (number_samples_per_period>>2)) % number_samples_per_period;

                    double tmp = clock_prefactor * (
                            std::cos(
                                constants::TWO_PI / static_cast<double>(number_samples_per_period) * j
                                + constants::PI * option.jitters[i] / 180.0
                                //- PI * static_cast<double>(i) / 2.0
                                )
                            ) + clock_shift + option.shift;

                    return std::clamp(tmp, option.low, option.high);
                };

                self.clock_generator = generator;
            }
        };

    template<typename Host>
        struct ClockRAMPPolicy  : public FCNClockPolicy<Host> {

            void initialize_clock_generator(SimulationMode mode, std::size_t sample_n,
                    const VectorTable &vector_table, const std::vector<std::string> &inames) {
                auto &self = static_cast<Host&>(*this);

                assert ( mode == SimulationMode::Exhaustive );
                (void) mode;
                (void) sample_n;
                (void) vector_table;
                (void) inames;

                auto generator = [&option=self.eoption](std::size_t i, std::size_t j) -> double {
                    int number_samples_per_period = static_cast<int>(std::ceil(option.clock_period/option.time_step));
                    double slope_factor = (option.high - option.low)/option.clock_slope*option.time_step; 

                    j = j + number_samples_per_period;
                    j = (j - i * (number_samples_per_period>>2)) % number_samples_per_period;

                    double tmp = 0;
                    if((int)(j) < (number_samples_per_period>>1))
                        tmp = option.low + slope_factor * (double)(j) + option.shift;
                    else {
                        j = j - (number_samples_per_period>>1);
                        tmp = option.high - slope_factor * (double)(j) + option.shift;
                    }
                    return std::clamp(tmp, option.low, option.high);
                };
                self.clock_generator = generator;
            }
        };

    template<typename Host>
        struct ClockGAUSSPolicy  : public FCNClockPolicy<Host> {

            void initialize_clock_generator(SimulationMode mode, std::size_t sample_n,
                    const VectorTable &vector_table, const std::vector<std::string> &inames) {
                auto &self = static_cast<Host&>(*this);
                assert ( mode == SimulationMode::Exhaustive );
                (void) mode;
                (void) sample_n;
                (void) vector_table;
                (void) inames;

                auto generator = [&option=self.eoption](std::size_t i, std::size_t j) -> double {
                    int number_samples_per_period = static_cast<int>(std::ceil(option.clock_period/option.time_step));
                    double sigma = option.clock_slope / (3.0 * option.time_step);

                    j = j + number_samples_per_period;
                    j = (j - i * (number_samples_per_period>>2)) % number_samples_per_period;

                    double tmp = 0;
                    if((int)(j) < (number_samples_per_period>>1)){
                        if((j == 0) || (j==1))
                            tmp = option.low + option.shift;
                        else tmp = option.high - (option.high - option.low) * exp(-0.5*(std::pow(((double)j)/sigma, 2))) + option.shift;
                    }
                    else {
                        j = j - (number_samples_per_period>>1);
                        if((j == 0) || (j==1))
                            tmp = option.high + option.shift;
                        else tmp = option.low + (option.high - option.low) * exp(-0.5*(std::pow(((double)j)/sigma, 2))) + option.shift;
                    }
                    return std::clamp(tmp, option.low, option.high);
                };
                self.clock_generator = generator;
            }
        };

    template<typename Host>
        struct EnergyAnalysisPolicy : public CoherenceEngine<Host> {
            EnergyAnalysisOption const &eoption;

            struct ExtrinsicProperty : public CoherenceEngine<Host>::ExtrinsicProperty {
                double diss_bath;
                double diss_clk;
                double diss_io;
                double diss_in;
                double diss_out;
                double Old_Gamma;
                double Old_PEK_io;
                double Old_PEK_in;
                double Old_PEK_out;
                std::vector<double> int_diss_bath;
                std::vector<double> int_diss_clk;
                std::vector<double> int_diss_io;
                std::vector<double> int_diss_in;
                std::vector<double> int_diss_out;
                int diss_idx;
                double old_next_clock_zone;
            };

            explicit EnergyAnalysisPolicy(const EnergyAnalysisOption &option) : CoherenceEngine<Host>(option), eoption{option}
            {}

            void initialize_design(QCADesign &design) const {
                QCAEnginePolicy<Host>::initialize_design(design);

                int clock_cycles = static_cast<int>(std::ceil(eoption.duration / eoption.clock_period));
                for(auto &layer : design) {
                    for(auto &cell : layer) {
                        if (simon::timezone(cell) == 0){
                            diss_idx(cell) = 1;
                        }
                        else diss_idx(cell) = -1;

                        int_diss_bath(cell).resize(clock_cycles + 3);
                        int_diss_clk(cell).resize(clock_cycles + 3);
                        int_diss_io(cell).resize(clock_cycles + 3);
                        int_diss_in(cell).resize(clock_cycles + 3);
                        int_diss_out(cell).resize(clock_cycles + 3);
                    }
                }
            }

            using CoherenceEngine<Host>::lambda_x_next;
            using CoherenceEngine<Host>::lambda_y_next;
            using CoherenceEngine<Host>::lambda_z_next;


            void compute_current_iteration(std::size_t j, QCADesign &design, Result &result) const {
                (void)result;

                double PEK = 0;
                double lambda_x_    = 0;
                double lambda_y_    = 0;
                double lambda_z_    = 0;
                double lambda_x_new = 0;
                double lambda_y_new = 0;
                double lambda_z_new = 0;
                double clock_value  = 0;
                //int num_neighbours = 0;
                double t = eoption.time_step * static_cast<double>(j);
                int clock_cycles = static_cast<int>(std::ceil(eoption.duration/eoption.clock_period));

                //FST
                double clock_middle          = (eoption.high + eoption.low) * 0.5;
                double dt                    = eoption.time_step;  //dt for integration of energy
                double old_clock_value       = 0;
                double k_old_next_clock_zone = 0;
                double old_PEK_io  = 0;
                double old_PEK_in  = 0;
                double old_PEK_out = 0;
                double PEK_in      = 0;
                double PEK_out     = 0;
                int cell_x         = 0;
                //int cell_y = 0;
                int cell_x_nb      = 0;
                int idx            = -1;
                double Pol_in      = 0;
                double Pol_out     = 0;

                auto &self = static_cast<const Host&>(*this);

                for(auto &layer : design) {
                    for(auto &cell : layer) {

                        if(function(cell) == FCNCellFunction::INPUT || function(cell) == FCNCellFunction::FIXED)
                            continue;

                        clock_value = self.clock_generator(simon::timezone(cell), j);
                        PEK = 0;

                        //calculate the sum of neighboring polarizations
                        auto &vec_n = neighbours(cell);
                        auto &vec_k = kink_energies(cell);

                        std::size_t neighbour_size = vec_n.size();
                        for(std::size_t i=0; i<neighbour_size; ++i) {
                            PEK += kink_energies(cell)[i] * polarization( *neighbours(cell)[i] );
                        }

                        //FST
                        old_clock_value  = Old_Gamma(cell);
                        Old_Gamma(cell)  = clock_value;
                        old_PEK_io       = Old_PEK_io(cell);
                        Old_PEK_io(cell) = PEK;

                        //FST: determine PEK of left cells = in and out
                        cell_x  = x(cell);
                        //cell_y = cell.template get<field::y>();
                        PEK_in  = 0;
                        PEK_out = 0;
                        Pol_in  = 0;
                        Pol_out = 0;
                        for(std::size_t i=0; i<neighbour_size; ++i) {
                            cell_x_nb = x(*vec_n.at(i));

                            if (cell_x_nb < cell_x) {
                                PEK_in += vec_k.at(i) * polarization(*vec_n.at(i));
                                Pol_in += polarization(*vec_n.at(i));
                            }

                            if (cell_x_nb > cell_x) {
                                PEK_out += vec_k.at(i) * polarization(*vec_n.at(i));
                                Pol_out += polarization(*vec_n.at(i));
                            }
                        }

                        old_PEK_in        = Old_PEK_in(cell);
                        old_PEK_out       = Old_PEK_out(cell);
                        Old_PEK_in(cell)  = PEK_in;
                        Old_PEK_out(cell) = PEK_out;

                        lambda_x_ = lambda_x(cell);
                        lambda_y_ = lambda_y(cell);
                        lambda_z_ = lambda_z(cell);

                        lambda_x_new = lambda_x_next(t, PEK, clock_value, lambda_x_, lambda_y_, lambda_z_);
                        lambda_y_new = lambda_y_next(t, PEK, clock_value, lambda_x_, lambda_y_, lambda_z_);
                        lambda_z_new = lambda_z_next(t, PEK, clock_value, lambda_x_, lambda_y_, lambda_z_);

                        lambda_x(cell) = lambda_x_new;
                        lambda_y(cell) = lambda_y_new;
                        lambda_z(cell) = lambda_z_new;

                        polarization(cell) = lambda_z_new;

                        diss_bath(cell) = calculate_diss_bath(lambda_x_new, lambda_z_new, clock_value, PEK);
                        diss_clk(cell)  = calculate_diss_clk(lambda_x_new, clock_value, old_clock_value);
                        diss_io(cell)   = calculate_diss_io(lambda_z_, PEK, old_PEK_io);
                        diss_in(cell)   = calculate_diss_io(lambda_z_, PEK_in, old_PEK_in);
                        diss_out(cell)  = calculate_diss_io(lambda_z_, PEK_out, old_PEK_out);

                        //estimate clock of neighbour clock zone
                        double next_clk_zone = self.clock_generator((simon::timezone(cell) + 1) % 4, j);
                        k_old_next_clock_zone = old_next_clock_zone(cell);
                        old_next_clock_zone(cell) = next_clk_zone;

                        if((k_old_next_clock_zone < clock_middle) && (next_clk_zone > clock_middle)) 
                        {
                            idx = diss_idx(cell);

                            if(idx != -1) {
                                int_diss_bath(cell).at(idx) = int_diss_bath(cell).at(0) * dt / eoption.relaxation;
                                int_diss_clk(cell).at(idx)  = int_diss_clk(cell).at(0) * dt; 
                                int_diss_io(cell).at(idx)   = int_diss_io(cell).at(0) * dt; 
                                int_diss_in(cell).at(idx)   = int_diss_in(cell).at(0) * dt; 
                                int_diss_out(cell).at(idx)  = int_diss_out(cell).at(0) * dt; 
                                ++idx;
                            }
                            else {
                                idx = 1;
                            }

                            //std::cout << "j " << j << "\tidx " << idx << "\nclock_cycles+3 " << clock_cycles+3 << std::endl;
                            assert(idx < (clock_cycles + 3));
                            diss_idx(cell)            = idx;
                            int_diss_bath(cell).at(0) = 0; 
                            int_diss_clk(cell).at(0)  = 0; 
                            int_diss_io(cell).at(0)   = 0; 
                            int_diss_in(cell).at(0)   = 0; 
                            int_diss_out(cell).at(0)  = 0; 
                        }

                        int_diss_bath(cell).at(0) += diss_bath(cell);
                        int_diss_clk(cell).at(0)  += diss_clk(cell);
                        int_diss_io(cell).at(0)   += diss_io(cell);
                        int_diss_in(cell).at(0)   += diss_in(cell);
                        int_diss_out(cell).at(0)  += diss_out(cell);
                    }
                }
            }

            void after_iterations(const QCADesign &design, const Result &result) const {
                (void) result;
                
                //print energy
                int clock_cycles = static_cast<int>(std::ceil(eoption.duration/eoption.clock_period));

                int max_idx = 1;
                int idx = clock_cycles + 3;
                std::vector<double> E_error_total(idx, 0.0);
                std::vector<double> E_bath_total(idx, 0.0);
                std::vector<double> E_clk_total(idx, 0.0);
                std::vector<double> E_io_total(idx, 0.0);
                //std::vector<double> E_error(idx, 0.0);    //sum of E_bath, E_clk, E_io = Error
                
                for(auto &layer : const_cast<QCADesign&>(design)) {
                    for(auto &cell : layer) {

                        int k_diss_idx = diss_idx(cell);
                        if(simon::timezone(cell) == 0){
                            --k_diss_idx;
                            diss_idx(cell) = k_diss_idx;
                        }

                        if(max_idx < k_diss_idx)
                            max_idx = k_diss_idx;

                        for (idx = 2; idx < k_diss_idx; ++idx) {
                            E_error_total[idx] += int_diss_bath(cell).at(idx);
                            E_error_total[idx] += int_diss_clk(cell).at(idx);
                            E_error_total[idx] += int_diss_io(cell).at(idx);

                            E_bath_total[idx]  += int_diss_bath(cell).at(idx);
                            E_clk_total[idx]   += int_diss_clk(cell).at(idx);
                            E_io_total[idx]    += int_diss_io(cell).at(idx);

                            //E_error[idx] = cell.extrinsic_property().template get<engine_category_t<SelfType>, field::int_diss_bath>().at(idx);
                            //E_error[idx] += cell.extrinsic_property().template get<engine_category_t<SelfType>, field::int_diss_clk>().at(idx);
                            //E_error[idx] += cell.extrinsic_property().template get<engine_category_t<SelfType>, field::int_diss_io>().at(idx);
                        }
                    }
                }

                using constants::QCHARGE; 

                double E_bath_all  = 0;
                double E_clk_all   = 0;
                double E_io_all    = 0;
                double E_error_all = 0;
                for (idx = 2; idx < max_idx; ++idx) {
                    E_bath_all  += E_bath_total[idx] / QCHARGE;
                    E_clk_all   += E_clk_total[idx] / QCHARGE;
                    E_io_all    += E_io_total[idx] / QCHARGE;
                    E_error_all += E_error_total[idx] / QCHARGE;
                    //std::cout << "idx " << idx << "\tE_error : " << E_error[idx]/QCHARGE << "eV" << std::endl;
                    //std::cout << "idx " << idx << std::endl;
                    //std::cout << "E_bath_total[idx] / QCHARGE " << E_bath_total[idx] << "/" << QCHARGE << std::endl;
                    //std::cout << "E_clk_total[idx] / QCHARGE " << E_clk_total[idx] << "/" << QCHARGE << std::endl;
                    //std::cout << "E_error_total[idx] / QCHARGE " << E_error_total[idx] << "/" << QCHARGE << std::endl;
                } 

                std::cout << "\nTotal energy dissipation " << std::endl;
                std::cout << "E_bath_all : "               << E_bath_all             << "eV" << std::endl;
                std::cout << "E_clk_all : "                << E_clk_all              << "eV" << std::endl;
                std::cout << "E_io_all : "                 << E_io_all               << "eV" << std::endl;
                std::cout << "E_error_all : "              << E_error_all            << "eV" << std::endl;
                std::cout << "E_bath_all+E_clk_all : "     << E_bath_all + E_clk_all << "eV" << std::endl;

                std::cout << "\nAverage energy dissipation per cycle " << std::endl;
                std::cout << "E_bath_all/n : "                         << E_bath_all / (max_idx - 2.0)                << "eV" << std::endl;
                std::cout << "E_clk_all/n : "                          << E_clk_all / (max_idx - 2.0)                 << "eV" << std::endl;
                std::cout << "E_io_all/n : "                           << E_io_all / (max_idx - 2.0)                  << "eV" << std::endl;
                std::cout << "E_error_all/n : "                        << E_error_all / (max_idx - 2.0)               << "eV" << std::endl;
                std::cout << "E_bath_all+E_clk_all/n : "               << (E_bath_all + E_clk_all) / (max_idx - 2.0)  << "eV" << std::endl;

            }

            protected:
            using QCAEnginePolicy<Host>::kink_energies;
            using QCAEnginePolicy<Host>::neighbours;
            using CoherenceEngine<Host>::lambda_x;
            using CoherenceEngine<Host>::lambda_y;
            using CoherenceEngine<Host>::lambda_z;

            static inline double &diss_bath(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_bath;
            }
            static inline const double &diss_bath(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_bath;
            }
            static inline double &diss_clk(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_clk;
            }
            static inline const double &diss_clk(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_clk;
            }
            static inline double &diss_io(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_io;
            }
            static inline const double &diss_io(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_io;
            }
            static inline double &diss_in(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_in;
            }
            static inline const double &diss_in(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_in;
            }
            static inline double &diss_out(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_out;
            }
            static inline const double &diss_out(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_out;
            }
            static inline double &Old_Gamma(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_Gamma;
            }
            static inline const double &Old_Gamma(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_Gamma;
            }
            static inline double &Old_PEK_io(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_PEK_io;
            }
            static inline const double &Old_PEK_io(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_PEK_io;
            }
            static inline double &Old_PEK_in(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_PEK_in;
            }
            static inline const double &Old_PEK_in(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_PEK_in;
            }
            static inline double &Old_PEK_out(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_PEK_out;
            }
            static inline const double &Old_PEK_out(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->Old_PEK_out;
            }
            static inline std::vector<double> &int_diss_bath(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_bath;
            }
            static inline const std::vector<double> &int_diss_bath(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_bath;
            }
            static inline std::vector<double> &int_diss_clk(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_clk;
            }
            static inline const std::vector<double> &int_diss_clk(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_clk;
            }
            static inline std::vector<double> &int_diss_io(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_io;
            }
            static inline const std::vector<double> &int_diss_io(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_io;
            }
            static inline std::vector<double> &int_diss_in(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_in;
            }
            static inline const std::vector<double> &int_diss_in(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_in;
            }
            static inline std::vector<double> &int_diss_out(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_out;
            }
            static inline const std::vector<double> &int_diss_out(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->int_diss_out;
            }
            static inline int &diss_idx(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_idx;
            }
            static inline const int &diss_idx(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->diss_idx;
            }
            static inline double &old_next_clock_zone(QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->old_next_clock_zone;
            }
            static inline const double &old_next_clock_zone(const QCACell &cell) {
                return std::any_cast<typename Host::ExtrinsicProperty>(&extrinsics(cell))->old_next_clock_zone;
            }

            using CoherenceEngine<Host>::temp_ratio;

            double slope_x(double t, double PEK, double Gamma,
                    double lambda_x, double lambda_y, double lambda_z) const {
                (void)t;
                (void)lambda_z;

                double mag_noh = std::hypot(2.0*Gamma, PEK);
                double ret = (-2.0*Gamma / mag_noh * tanh(temp_ratio(PEK, Gamma, eoption.T)) - lambda_x)
                    / eoption.relaxation + (PEK * lambda_y * constants::over_hbar);
                return ret;
            }

            double slope_y(double t, double PEK, double Gamma,
                    double lambda_x, double lambda_y, double lambda_z) const {
                (void)t;

                double ret = constants::over_hbar * (PEK * lambda_x + 2.0 * Gamma * lambda_z) - lambda_y / eoption.relaxation;
                return ret;
            }

            double slope_z(double t, double PEK, double Gamma,
                    double lambda_x, double lambda_y, double lambda_z) const {
                (void)t;
                (void)lambda_x;

                double mag_noh = std::hypot(2.0*Gamma, PEK);
                double ret = -2.0 * Gamma * lambda_y * constants::over_hbar - (lambda_z - PEK/mag_noh*tanh(temp_ratio(PEK, Gamma, eoption.T))) / eoption.relaxation; 
                return ret;
            }

            friend class CoherenceEngine<Host>;//for lambda_x_next, lambda_y_next, lambda_z_next

            double calculate_diss_bath(double lambda_x, double lambda_z, double Gamma, double PEK) const{
                double mag_noh = std::hypot(2.0*Gamma, PEK);
                //double l_ss_x = -2.0 * Gamma / mag_noh * tanh(temp_ratio(PEK, Gamma, opt.T));
                //double l_ss_z = PEK / mag_noh * tanh(temp_ratio(PEK, Gamma, opt.T));
                //double ret = 0.5 * (-2.0*Gamma*(l_ss_x-lambda_x) + PEK*(l_ss_z-lambda_z));
                double ret = 0.5 * (2.0*Gamma*lambda_x - PEK*lambda_z + tanh(temp_ratio(PEK, Gamma, eoption.T)) * mag_noh);
                return ret;
            }

            double calculate_diss_clk(double lambda_x, double Gamma, double Old_Gamma) const{
                double ret = -1.0 * lambda_x * (Gamma - Old_Gamma) / eoption.time_step;
                return ret;
            }

            double calculate_diss_io(double lambda_z, double PEK, double Old_PEK) const{
                double ret = 0.5 * lambda_z * (PEK- Old_PEK) / eoption.time_step;
                return ret;
            }
        };

    //////////////////////////6. simulation driver definition//////////////////////////////////
    template<template<typename> class ClockPolicy>
        using EnergyAnalysisAlgorithm = SimulationDriver<EnergyAnalysisPolicy, ContinuousSamplingPolicy, EnergyInputPolicy, ClockPolicy>;
    using COSEnergyAnalysisAlgorithm = EnergyAnalysisAlgorithm<ClockCOSPolicy>;
    using RAMPEnergyAnalysisAlgorithm = EnergyAnalysisAlgorithm<ClockRAMPPolicy>;
    using GAUSSEnergyAnalysisAlgorithm = EnergyAnalysisAlgorithm<ClockGAUSSPolicy>;


}//namespace simon

#endif
