// simon: C++ FCN network simulation framework
// Strict-equivalent accelerated coherence-vector engine.

#ifndef HFUT_SIMON_ACCELERATED_COHERENCE_HPP
#define HFUT_SIMON_ACCELERATED_COHERENCE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "OrderedInteractionGraph.hpp"

namespace simon {

/**
 * Work and cache counters for AcceleratedCoherenceEngine.
 *
 * The counters make it explicit that the accelerated engine integrates every
 * dynamic cell at every time step.  It does not skip clock zones, interpolate
 * samples, or replace the coherence-vector equations with a surrogate.
 */
struct AcceleratedCoherenceStatistics {
    std::size_t total_cells = 0;
    std::size_t samples = 0;
    std::size_t cell_updates = 0;
    std::size_t field_accumulations = 0;
    std::size_t dynamic_cells = 0;
    std::size_t directed_couplings = 0;
    std::size_t clock_cache_values = 0;
    std::size_t input_cache_values = 0;
    std::size_t clock_generator_evaluations = 0;
    std::size_t input_generator_evaluations = 0;
    std::size_t graph_candidate_checks = 0;
    std::size_t graph_all_pair_checks = 0;
    bool clock_cache_used = false;
    bool input_cache_used = false;
    bool graph_spatial_buckets_used = false;
    bool graph_reused = false;
    bool precompiled_graph_rejected = false;
    bool fused_integration_used = false;
    bool strict_equivalent = true;
    double interaction_graph_seconds = 0.0;
    double energy_materialization_seconds = 0.0;
    double kernel_compilation_seconds = 0.0;
    double generator_cache_seconds = 0.0;
};

/**
 * Strict-equivalent acceleration of CoherenceEngine.
 *
 * The physical model, time step, Euler/Runge--Kutta routines, floating-point
 * summation order, and in-place (Gauss--Seidel-like) cell update order are the
 * same as CoherenceEngine.  Acceleration is limited to semantics-preserving
 * work hoisting:
 *
 *  - compile the sparse neighbour graph into a contiguous per-cell kernel;
 *  - evaluate each of the four clock phases once per sample, instead of once
 *    per cell and sample;
 *  - evaluate deterministic input waveforms once and reuse them in the time
 *    loop when the bounded cache has enough space.
 *
 * Caches are only an implementation detail.  If their conservative memory
 * budget would be exceeded, the engine evaluates the original generators in
 * the hot loop and remains numerically equivalent.
 */
template<typename Host>
struct AcceleratedCoherenceEngine : public CoherenceEngine<Host> {
    using Base = CoherenceEngine<Host>;
    using ExtrinsicProperty = typename Base::ExtrinsicProperty;

    explicit AcceleratedCoherenceEngine(const QCACoherenceOption &option)
        : Base(option) {}

    const AcceleratedCoherenceStatistics &statistics() const noexcept {
        return statistics_;
    }

    void initialize_design(QCADesign &design) {
        statistics_ = {};
        initialize_sparse_design(design);
        const auto kernel_begin = std::chrono::steady_clock::now();
        compile_kernel(design);
        statistics_.kernel_compilation_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - kernel_begin).count();
        const auto cache_begin = std::chrono::steady_clock::now();
        build_generator_caches();
        statistics_.generator_cache_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cache_begin).count();
    }

    void make_iterations(QCADesign &design, Result &result) {
        (void)design;
        auto &self = static_cast<Host&>(*this);

        for (std::size_t sample = 0; sample < self.number_of_samples; ++sample) {
            const bool feed_result = ((sample % self.sampling_step) == 0);

            for (std::size_t input = 0; input < self.inputs.size(); ++input) {
                double input_data = 0.0;
                if (statistics_.input_cache_used) {
                    input_data = input_cache_[sample * self.inputs.size() + input];
                } else {
                    input_data = self.input_generator(input, sample);
                }

                polarization(*self.inputs[input]) = input_data;
                if (feed_result) {
                    result.inputs[input].data[sample / self.sampling_step] = input_data;
                }
            }
            if (!statistics_.input_cache_used) {
                statistics_.input_generator_evaluations += self.inputs.size();
            }

            compute_current_iteration(sample, design, result);

            if (feed_result) {
                for (std::size_t output = 0; output < self.outputs.size(); ++output) {
                    result.outputs[output].data[sample / self.sampling_step] =
                            polarization(*self.outputs[output]);
                }
            }
        }
    }

    void compute_current_iteration(std::size_t sample,
                                   QCADesign &design,
                                   Result &result) {
        (void)design;
        (void)result;

        auto &self = static_cast<Host&>(*this);
        const double time = this->option.time_step * static_cast<double>(sample);

        // The four values are shared by every cell in the corresponding clock
        // zone.  Loading them once also keeps the hot cell traversal local.
        std::array<double, 4> cached_gamma{};
        if (statistics_.clock_cache_used) {
            const std::size_t clock_offset = sample * cached_gamma.size();
            for (std::size_t phase = 0; phase < cached_gamma.size(); ++phase) {
                cached_gamma[phase] = clock_cache_[clock_offset + phase];
            }
        }

        for (PreparedCell &prepared : cells_) {
            double field = 0.0;
            const std::size_t coupling_end =
                    prepared.coupling_begin + prepared.coupling_count;
            for (std::size_t coupling_index = prepared.coupling_begin;
                 coupling_index < coupling_end;
                 ++coupling_index) {
                const Coupling &coupling = couplings_[coupling_index];
                // Preserve the legacy multiplication and accumulation order.
                field += coupling.kink_energy * polarization(*coupling.neighbour);
            }

            const double lambda_x_old = prepared.state->lambda_x;
            const double lambda_y_old = prepared.state->lambda_y;
            const double lambda_z_old = prepared.state->lambda_z;

            double gamma = 0.0;
            if (statistics_.clock_cache_used) {
                gamma = cached_gamma[prepared.clock_zone];
            } else {
                gamma = self.clock_generator(prepared.clock_zone, sample);
            }

            // Fused evaluation computes the field/clock-only magnitude and
            // thermal factor once.  The component-specific Euler/RK stages
            // retain the exact legacy expressions and parenthesization.
            IntegratedState next;
            if (this->option.acceleration.use_fused_integration) {
                next = integrate_fused(
                        field, gamma, lambda_x_old, lambda_y_old, lambda_z_old);
            } else {
                // Exact legacy component paths provide an explicit ablation of
                // common-expression fusion without changing graph storage.
                next = IntegratedState{
                        this->lambda_x_next(time, field, gamma,
                                            lambda_x_old, lambda_y_old,
                                            lambda_z_old),
                        this->lambda_y_next(time, field, gamma,
                                            lambda_x_old, lambda_y_old,
                                            lambda_z_old),
                        this->lambda_z_next(time, field, gamma,
                                            lambda_x_old, lambda_y_old,
                                            lambda_z_old)};
            }

            prepared.state->lambda_x = next.x;
            prepared.state->lambda_y = next.y;
            prepared.state->lambda_z = next.z;
            polarization(*prepared.cell) = -next.z;
        }

        statistics_.cell_updates += cells_.size();
        statistics_.field_accumulations += statistics_.directed_couplings;
        if (!statistics_.clock_cache_used) {
            statistics_.clock_generator_evaluations += cells_.size();
        }
        ++statistics_.samples;
        this->record_trace_frame(design,
                                 SimulationTraceFrame::CoherenceTimeStep,
                                 sample);
    }

private:
    struct Coupling {
        double kink_energy = 0.0;
        QCACell *neighbour = nullptr;
    };

    struct PreparedCell {
        QCACell *cell = nullptr;
        ExtrinsicProperty *state = nullptr;
        std::size_t clock_zone = 0;
        std::size_t coupling_begin = 0;
        std::size_t coupling_count = 0;
    };

    struct IntegratedState {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    static bool byte_size_fits(std::size_t value_count,
                               std::size_t budget_bytes) noexcept {
        return value_count <= budget_bytes / sizeof(double);
    }

    IntegratedState integrate_fused(double field,
                                    double gamma,
                                    double lambda_x,
                                    double lambda_y,
                                    double lambda_z) const {
        // Exactly the same two expressions used by magnitude_energy_vector()
        // and slope_{x,z}(), evaluated once because neither depends on the RK
        // component sub-stage.
        const double magnitude =
                std::hypot(2 * gamma, field) * constants::OVER_HBAR;
        const double thermal =
                std::tanh(this->HBAR_OVER_KBT * magnitude * 0.5);

        auto slope_x_fused = [&](double x) {
            return (2.0 * gamma * constants::over_hbar / magnitude * thermal - x)
                   / this->option.relaxation
                   - (field * lambda_y * constants::over_hbar);
        };
        auto slope_y_fused = [&](double y) {
            return (this->option.relaxation *
                            (field * lambda_x + 2.0 * gamma * lambda_z)
                    - constants::hbar * y)
                   / (this->option.relaxation * constants::hbar);
        };
        auto slope_z_fused = [&](double z) {
            return (-field * thermal
                    - magnitude *
                              (2.0 * gamma * this->option.relaxation * lambda_y
                               + constants::hbar * z))
                   / (this->option.relaxation * constants::hbar * magnitude);
        };

        const double xk1 = this->option.time_step * slope_x_fused(lambda_x);
        const double yk1 = this->option.time_step * slope_y_fused(lambda_y);
        const double zk1 = this->option.time_step * slope_z_fused(lambda_z);

        if (this->option.algorithm == NumericMethod::RungeKutta) {
            const double xk2 = this->option.time_step *
                               slope_x_fused(lambda_x + xk1 / 2);
            const double xk3 = this->option.time_step *
                               slope_x_fused(lambda_x + xk2 / 2);
            const double xk4 = this->option.time_step *
                               slope_x_fused(lambda_x + xk3);

            const double yk2 = this->option.time_step *
                               slope_y_fused(lambda_y + yk1 / 2);
            const double yk3 = this->option.time_step *
                               slope_y_fused(lambda_y + yk2 / 2);
            const double yk4 = this->option.time_step *
                               slope_y_fused(lambda_y + yk3);

            const double zk2 = this->option.time_step *
                               slope_z_fused(lambda_z + zk1 / 2);
            const double zk3 = this->option.time_step *
                               slope_z_fused(lambda_z + zk2 / 2);
            const double zk4 = this->option.time_step *
                               slope_z_fused(lambda_z + zk3);

            return IntegratedState{
                    lambda_x + xk1 / 6 + xk2 / 3 + xk3 / 3 + xk4 / 6,
                    lambda_y + yk1 / 6 + yk2 / 3 + yk3 / 3 + yk4 / 6,
                    lambda_z + zk1 / 6 + zk2 / 3 + zk3 / 3 + zk4 / 6};
        }

        if (this->option.algorithm == NumericMethod::Euler) {
            return IntegratedState{
                    lambda_x + xk1,
                    lambda_y + yk1,
                    lambda_z + zk1};
        }

        // Match the legacy lambda_*_next fall-through for an unknown method.
        return IntegratedState{};
    }

    /**
     * Build exactly the same directed radius graph as the legacy all-pairs
     * scan, but query only the 3x3 spatial-bucket neighbourhood.  Sorting each
     * candidate set by its original global cell index is essential: it keeps
     * neighbour insertion order, and therefore PEk floating-point summation
     * order, identical to the baseline.
     */
    void initialize_sparse_design(QCADesign &design) {
        std::vector<QCACell*> all_cells;
        for (auto &layer : design) {
            for (auto &cell : layer) {
                extrinsics(cell).reset();
                extrinsics(cell).template emplace<ExtrinsicProperty>();
                layer_index(cell) = index(layer);
                if (function(cell) == FCNCellFunction::FIXED) {
                    polarization(cell) = calculate_cell_polarization(cell);
                }
                all_cells.push_back(&cell);
            }
        }

        const auto graph_begin = std::chrono::steady_clock::now();
        const OrderedInteractionGraph *graph =
                this->option.acceleration.precompiled_graph;
        statistics_.graph_reused =
                graph != nullptr && graph->matches(design, this->option);
        statistics_.precompiled_graph_rejected =
                graph != nullptr && !statistics_.graph_reused;
        if (!statistics_.graph_reused) {
            owned_graph_ = OrderedInteractionGraph::compile(
                    design,
                    this->option,
                    this->option.acceleration.use_spatial_buckets);
            graph = &owned_graph_;
        }

        const OrderedInteractionGraph::Statistics &graph_statistics =
                graph->statistics();
        statistics_.total_cells = graph_statistics.cells;
        statistics_.graph_candidate_checks = graph_statistics.candidate_checks;
        statistics_.graph_all_pair_checks = graph_statistics.all_pair_checks;
        statistics_.graph_spatial_buckets_used =
                graph_statistics.spatial_buckets_used;
        statistics_.interaction_graph_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - graph_begin).count();

        const auto materialization_begin = std::chrono::steady_clock::now();
        for (std::size_t source = 0; source < all_cells.size(); ++source) {
            auto &cell_neighbours = this->neighbours(*all_cells[source]);
            auto &cell_energies = this->kink_energies(*all_cells[source]);
            const std::size_t begin = graph->row_begin(source);
            const std::size_t end = graph->row_end(source);
            cell_neighbours.reserve(end - begin);
            cell_energies.reserve(end - begin);
            for (std::size_t coupling = begin; coupling < end; ++coupling) {
                cell_neighbours.push_back(
                        all_cells[graph->coupling(coupling).neighbour_index]);
                cell_energies.push_back(
                        graph->kink_energy(coupling, this->option.epsilon_r));
            }
        }
        statistics_.energy_materialization_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - materialization_begin).count();
    }

    void compile_kernel(QCADesign &design) {
        cells_.clear();
        couplings_.clear();

        for (auto &layer : design) {
            for (auto &cell : layer) {
                if (function(cell) == FCNCellFunction::INPUT ||
                    function(cell) == FCNCellFunction::FIXED) {
                    continue;
                }

                PreparedCell prepared;
                prepared.cell = &cell;
                prepared.state =
                        std::any_cast<ExtrinsicProperty>(&extrinsics(cell));
                prepared.clock_zone = simon::timezone(cell);
                prepared.coupling_begin = couplings_.size();

                const auto &cell_neighbours = this->neighbours(cell);
                const auto &cell_energies = this->kink_energies(cell);
                for (std::size_t i = 0; i < cell_neighbours.size(); ++i) {
                    couplings_.push_back(
                            Coupling{cell_energies[i], cell_neighbours[i]});
                }
                prepared.coupling_count =
                        couplings_.size() - prepared.coupling_begin;

                statistics_.directed_couplings += prepared.coupling_count;
                cells_.push_back(std::move(prepared));
            }
        }

        statistics_.dynamic_cells = cells_.size();
        statistics_.fused_integration_used =
                this->option.acceleration.use_fused_integration;
    }

    void build_generator_caches() {
        auto &self = static_cast<Host&>(*this);
        clock_cache_.clear();
        input_cache_.clear();

        const std::size_t samples = self.number_of_samples;
        constexpr std::size_t phases = 4;

        if (this->option.acceleration.use_clock_cache &&
            samples <= std::numeric_limits<std::size_t>::max() / phases) {
            const std::size_t clock_values = phases * samples;
            if (byte_size_fits(
                    clock_values,
                    this->option.acceleration.cache_budget_bytes)) {
                clock_cache_.resize(clock_values);
                for (std::size_t sample = 0; sample < samples; ++sample) {
                    for (std::size_t phase = 0; phase < phases; ++phase) {
                        clock_cache_[sample * phases + phase] =
                                self.clock_generator(phase, sample);
                        ++statistics_.clock_generator_evaluations;
                    }
                }
                statistics_.clock_cache_used = true;
                statistics_.clock_cache_values = clock_values;
            }
        }

        const std::size_t clock_bytes =
                statistics_.clock_cache_values * sizeof(double);
        const std::size_t remaining_budget =
                this->option.acceleration.cache_budget_bytes > clock_bytes
                        ? this->option.acceleration.cache_budget_bytes - clock_bytes
                        : 0;

        const std::size_t input_count = self.inputs.size();
        if (this->option.acceleration.use_input_cache && input_count != 0 &&
            samples <= std::numeric_limits<std::size_t>::max() / input_count) {
            const std::size_t input_values = input_count * samples;
            if (byte_size_fits(input_values, remaining_budget)) {
                input_cache_.resize(input_values);
                for (std::size_t sample = 0; sample < samples; ++sample) {
                    for (std::size_t input = 0; input < input_count; ++input) {
                        input_cache_[sample * input_count + input] =
                                self.input_generator(input, sample);
                        ++statistics_.input_generator_evaluations;
                    }
                }
                statistics_.input_cache_used = true;
                statistics_.input_cache_values = input_values;
            }
        }
    }

    std::vector<PreparedCell> cells_;
    std::vector<Coupling> couplings_;
    std::vector<double> clock_cache_;
    std::vector<double> input_cache_;
    OrderedInteractionGraph owned_graph_;
    AcceleratedCoherenceStatistics statistics_;
};

using AcceleratedCoherenceAlgorithm =
        SimulationDriver<AcceleratedCoherenceEngine,
                         ContinuousSamplingPolicy,
                         FCNInputPolicy,
                         QCAClockPolicy>;

} // namespace simon

#endif // HFUT_SIMON_ACCELERATED_COHERENCE_HPP
