// simon: C++ FCN network simulation framework
// Strict-equivalent accelerated bistable engine.

#ifndef HFUT_SIMON_ACCELERATED_BISTABLE_HPP
#define HFUT_SIMON_ACCELERATED_BISTABLE_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

#include "OrderedInteractionGraph.hpp"

namespace simon {

/**
 * Work counters for the accelerated bistable engine.
 *
 * They deliberately describe the same dense Gauss--Seidel work as the legacy
 * engine.  No cell, sample, or convergence sweep is omitted in order to gain
 * speed; acceleration comes exclusively from compiling the QCA graph into a
 * contiguous simulation kernel before the time loop.
 */
struct AcceleratedBistableStatistics {
    std::size_t total_cells = 0;
    std::size_t samples = 0;
    std::size_t sweeps = 0;
    std::size_t cell_updates = 0;
    std::size_t converged_samples = 0;
    std::size_t max_iteration_samples = 0;
    std::size_t dynamic_cells = 0;
    std::size_t directed_couplings = 0;
    std::size_t spatial_candidates = 0;
    std::size_t graph_all_pair_checks = 0;
    bool graph_spatial_buckets_used = false;
    bool graph_reused = false;
    bool precompiled_graph_rejected = false;
    double interaction_graph_seconds = 0.0;
    double energy_materialization_seconds = 0.0;
    double kernel_compilation_seconds = 0.0;
};

/**
 * A strict-equivalent implementation of BistableEngine.
 *
 * The physical equation, floating-point operation order, random per-layer
 * Gauss--Seidel schedule, convergence test, and sample/clock/input policies are
 * intentionally identical to BistableEngine.  The only difference is data
 * access: neighbour pointers, kink energies, clock-zone indices, and dynamic
 * cell classification are copied once into a packed kernel.  This removes
 * std::any_cast, function classification, and split-vector lookups from the
 * hot iteration loop without introducing a surrogate or a numerical
 * approximation.
 */
template<typename Host>
struct AcceleratedBistableEngine : public QCAEnginePolicy<Host> {
    QCABistableOption const &option;

    struct ExtrinsicProperty {
        double lambda_x = 0.0;
        double lambda_y = 0.0;
        double lambda_z = 0.0;
        std::vector<double> kink_energies;
        std::vector<QCACell*> neighbours;
    };

    explicit AcceleratedBistableEngine(const QCABistableOption &option) : option{option} {}

    const AcceleratedBistableStatistics &statistics() const noexcept {
        return statistics_;
    }

    /**
     * Build exactly the same directed interaction graph as the legacy O(N^2)
     * initialization, but query only the 3x3 neighbourhood of a radius-sized
     * spatial bucket.  Candidates are sorted by their original global design
     * index before kink energies are evaluated.  Consequently both the
     * neighbour order and every floating-point kink-energy value match the
     * baseline graph.
     */
    void initialize_design(QCADesign &design) {
        std::vector<QCACell*> all_cells;
        for (auto &layer : design) {
            for (auto &cell : layer) {
                extrinsics(cell).reset();
                extrinsics(cell).emplace<typename Host::ExtrinsicProperty>();

                layer_index(cell) = index(layer);
                if (function(cell) == FCNCellFunction::FIXED) {
                    polarization(cell) = calculate_cell_polarization(cell);
                }
                all_cells.push_back(&cell);
            }
        }

        const auto graph_begin = std::chrono::steady_clock::now();
        const OrderedInteractionGraph *graph = option.acceleration.precompiled_graph;
        graph_reused_ = graph != nullptr && graph->matches(design, option);
        precompiled_graph_rejected_ = graph != nullptr && !graph_reused_;
        if (!graph_reused_) {
            owned_graph_ = OrderedInteractionGraph::compile(
                    design, option, option.acceleration.use_spatial_buckets);
            graph = &owned_graph_;
        }

        graph_statistics_ = graph->statistics();
        interaction_graph_seconds_ = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - graph_begin).count();
        const auto materialization_begin = std::chrono::steady_clock::now();
        for (std::size_t source = 0; source < all_cells.size(); ++source) {
            auto &cell_neighbours = neighbours(*all_cells[source]);
            auto &cell_energies = kink_energies(*all_cells[source]);
            const std::size_t begin = graph->row_begin(source);
            const std::size_t end = graph->row_end(source);
            cell_neighbours.reserve(end - begin);
            cell_energies.reserve(end - begin);
            for (std::size_t coupling = begin; coupling < end; ++coupling) {
                cell_neighbours.push_back(
                        all_cells[graph->coupling(coupling).neighbour_index]);
                cell_energies.push_back(
                        graph->kink_energy(coupling, option.epsilon_r));
            }
        }
        energy_materialization_seconds_ = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - materialization_begin).count();
    }

    void before_iterations(QCADesign &design, const Result &result) {
        if (option.trace_recorder != nullptr) {
            option.trace_recorder->reset();
        }
        seed_simulation_random_generator(option.random_seed);
        const auto kernel_begin = std::chrono::steady_clock::now();
        compile_kernel(design, result);
        statistics_.kernel_compilation_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - kernel_begin).count();
    }

    void compute_current_iteration(std::size_t sample,
                                   QCADesign &design,
                                   Result &result) {
        (void)design;
        (void)result;

        bool stable = false;
        std::size_t completed_sweeps = 0;

        for (std::size_t iteration = 0;
             !stable && iteration < option.max_iteration_per_sample;
             ++iteration) {
            stable = true;
            ++completed_sweeps;

            for (auto &layer : layers_) {
                // BistableEngine starts every shuffle from [0, n).  Doing the
                // same here preserves the exact RNG consumption and update
                // order for a fixed standard-library implementation and seed.
                std::iota(layer.order.begin(), layer.order.end(), std::size_t{0});
                std::shuffle(layer.order.begin(), layer.order.end(),
                             simulation_random_generator());

                for (const std::size_t index : layer.order) {
                    PreparedCell &prepared = layer.cells[index];
                    if (prepared.polarization == nullptr) {
                        continue;
                    }

                    const double old_polarization = *prepared.polarization;
                    double field = 0.0;
                    for (std::size_t coupling_index = prepared.coupling_begin;
                         coupling_index < prepared.coupling_end;
                         ++coupling_index) {
                        const Coupling &coupling = couplings_[coupling_index];
                        // Keep the legacy summation and multiplication order.
                        field += coupling.kink_energy *
                                 *coupling.neighbour_polarization;
                    }

                    field /= 2 * (*prepared.clock_trace)[sample];

                    const double new_polarization =
                            (field > 1000.0) ? 1.0 :
                            (field < -1000.0) ? -1.0 :
                            (std::fabs(field) < 0.001) ? field :
                            field / std::sqrt(1 + field * field);

                    *prepared.polarization = new_polarization;
                    stable = stable &&
                             (std::fabs(new_polarization - old_polarization) <=
                              option.convergence_tolerance);
                    ++statistics_.cell_updates;
                }
            }

            if (option.trace_recorder != nullptr) {
                option.trace_recorder->begin_frame(
                        SimulationTraceFrame::BistableSweep,
                        sample,
                        iteration);
                for (const auto &trace_layer : design) {
                    for (const auto &trace_cell : trace_layer) {
                        option.trace_recorder->record(
                                simon::polarization(trace_cell));
                    }
                }
                option.trace_recorder->end_frame();
            }
        }

        ++statistics_.samples;
        statistics_.sweeps += completed_sweeps;
        if (stable) {
            ++statistics_.converged_samples;
        } else {
            ++statistics_.max_iteration_samples;
        }
    }

private:
    struct Coupling {
        double kink_energy = 0.0;
        double *neighbour_polarization = nullptr;
    };

    struct PreparedCell {
        double *polarization = nullptr;
        const std::vector<double> *clock_trace = nullptr;
        std::size_t coupling_begin = 0;
        std::size_t coupling_end = 0;
    };

    struct PreparedLayer {
        std::vector<PreparedCell> cells;
        std::vector<std::size_t> order;
    };

    void compile_kernel(QCADesign &design, const Result &result) {
        statistics_ = {};
        statistics_.total_cells = graph_statistics_.cells;
        statistics_.spatial_candidates = graph_statistics_.candidate_checks;
        statistics_.graph_all_pair_checks = graph_statistics_.all_pair_checks;
        statistics_.graph_spatial_buckets_used =
                graph_statistics_.spatial_buckets_used;
        statistics_.graph_reused = graph_reused_;
        statistics_.precompiled_graph_rejected = precompiled_graph_rejected_;
        statistics_.interaction_graph_seconds = interaction_graph_seconds_;
        statistics_.energy_materialization_seconds =
                energy_materialization_seconds_;
        layers_.clear();
        layers_.reserve(size(design));
        couplings_.clear();

        std::size_t coupling_count = 0;
        for (const auto &source_layer : design) {
            for (const auto &cell : source_layer) {
                if (function(cell) != FCNCellFunction::INPUT &&
                    function(cell) != FCNCellFunction::FIXED) {
                    coupling_count += neighbours(cell).size();
                }
            }
        }
        couplings_.reserve(coupling_count);

        for (auto &source_layer : design) {
            PreparedLayer prepared_layer;
            prepared_layer.cells.resize(size(source_layer));
            prepared_layer.order.resize(size(source_layer));

            std::size_t cell_index = 0;
            for (auto &cell : source_layer) {
                PreparedCell &prepared = prepared_layer.cells[cell_index++];
                if (function(cell) == FCNCellFunction::INPUT ||
                    function(cell) == FCNCellFunction::FIXED) {
                    continue;
                }

                prepared.polarization = &simon::polarization(cell);
                prepared.clock_trace =
                        &result.clocks[simon::timezone(cell)].data;
                prepared.coupling_begin = couplings_.size();

                const auto &cell_neighbours = neighbours(cell);
                const auto &cell_energies = kink_energies(cell);
                for (std::size_t i = 0; i < cell_neighbours.size(); ++i) {
                    couplings_.push_back(
                            Coupling{cell_energies[i],
                                     &simon::polarization(*cell_neighbours[i])});
                }
                prepared.coupling_end = couplings_.size();

                ++statistics_.dynamic_cells;
                statistics_.directed_couplings +=
                        prepared.coupling_end - prepared.coupling_begin;
            }

            layers_.push_back(std::move(prepared_layer));
        }
    }

    using QCAEnginePolicy<Host>::kink_energies;
    using QCAEnginePolicy<Host>::neighbours;

    std::vector<PreparedLayer> layers_;
    std::vector<Coupling> couplings_;
    AcceleratedBistableStatistics statistics_;
    OrderedInteractionGraph owned_graph_;
    OrderedInteractionGraph::Statistics graph_statistics_;
    bool graph_reused_ = false;
    bool precompiled_graph_rejected_ = false;
    double interaction_graph_seconds_ = 0.0;
    double energy_materialization_seconds_ = 0.0;
};

using AcceleratedBistableAlgorithm =
        SimulationDriver<AcceleratedBistableEngine,
                         DiscreteSamplingPolicy,
                         FCNInputPolicy,
                         QCAClockPolicy>;

} // namespace simon

#endif // HFUT_SIMON_ACCELERATED_BISTABLE_HPP
