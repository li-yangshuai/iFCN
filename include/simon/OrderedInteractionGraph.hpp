// simon: C++ FCN network simulation framework
// Reusable, order-preserving compilation of QCA physical interactions.

#ifndef HFUT_SIMON_ORDERED_INTERACTION_GRAPH_HPP
#define HFUT_SIMON_ORDERED_INTERACTION_GRAPH_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#include "Simulation.hpp"

namespace simon {

/**
 * Geometry-compiled QCA interaction graph.
 *
 * The graph stores neighbours by canonical design index and stores the
 * geometry-only numerator of each kink energy.  Materializing an energy for a
 * particular epsilon_r executes the same final expression as
 * calculate_inter_cell_kink_energy().  Consequently one expensive geometry
 * compilation can serve a dielectric sweep without changing neighbour order,
 * the physical equations, or the floating-point summation order in either
 * simulator.
 */
class OrderedInteractionGraph {
public:
    struct Coupling {
        std::size_t neighbour_index = 0;
        double kink_energy_numerator = 0.0;
    };

    struct Statistics {
        std::size_t cells = 0;
        std::size_t directed_couplings = 0;
        std::size_t candidate_checks = 0;
        std::size_t all_pair_checks = 0;
        bool spatial_buckets_used = false;
    };

    template<typename Option>
    static OrderedInteractionGraph compile(QCADesign &design,
                                           const Option &option,
                                           bool use_spatial_buckets = true) {
        OrderedInteractionGraph graph;
        graph.radius_effect_ = option.radius_effect;
        graph.layer_separation_ = option.layer_separation;
        graph.statistics_.spatial_buckets_used = use_spatial_buckets;

        std::vector<QCACell*> cells;
        for (auto &layer : design) {
            for (auto &cell : layer) {
                layer_index(cell) = index(layer);
                cells.push_back(&cell);
                graph.geometry_.push_back(capture_geometry(cell, index(layer)));
            }
        }

        const std::size_t cell_count = cells.size();
        graph.statistics_.cells = cell_count;
        if (cell_count > 0 &&
            cell_count - 1 <= std::numeric_limits<std::size_t>::max() /
                                      cell_count) {
            graph.statistics_.all_pair_checks = cell_count * (cell_count - 1);
        }
        graph.row_offsets_.reserve(cell_count + 1);
        graph.row_offsets_.push_back(0);

        if (use_spatial_buckets) {
            graph.compile_spatial(cells, option);
        } else {
            graph.compile_all_pairs(cells, option);
        }

        graph.statistics_.directed_couplings = graph.couplings_.size();
        return graph;
    }

    template<typename Option>
    bool matches(const QCADesign &design, const Option &option) const {
        if (radius_effect_ != option.radius_effect ||
            layer_separation_ != option.layer_separation) {
            return false;
        }

        std::size_t cell_index = 0;
        for (const auto &layer : design) {
            for (const auto &cell : layer) {
                if (cell_index >= geometry_.size() ||
                    !(geometry_[cell_index] ==
                      capture_geometry(cell, index(layer)))) {
                    return false;
                }
                ++cell_index;
            }
        }
        return cell_index == geometry_.size();
    }

    std::size_t cell_count() const noexcept {
        return geometry_.size();
    }

    std::size_t row_begin(std::size_t cell_index) const {
        return row_offsets_.at(cell_index);
    }

    std::size_t row_end(std::size_t cell_index) const {
        return row_offsets_.at(cell_index + 1);
    }

    const Coupling &coupling(std::size_t coupling_index) const {
        return couplings_.at(coupling_index);
    }

    double kink_energy(std::size_t coupling_index, double epsilon_r) const {
        return 1 / (constants::FOUR_PI_EPSILON * epsilon_r) *
               couplings_.at(coupling_index).kink_energy_numerator;
    }

    const Statistics &statistics() const noexcept {
        return statistics_;
    }

private:
    struct CellGeometry {
        double cell_x = 0.0;
        double cell_y = 0.0;
        std::size_t layer = 0;
        std::array<double, 8> dot_coordinates{};

        bool operator==(const CellGeometry &other) const noexcept {
            return cell_x == other.cell_x &&
                   cell_y == other.cell_y &&
                   layer == other.layer &&
                   dot_coordinates == other.dot_coordinates;
        }
    };

    struct IndexedCell {
        std::size_t index = 0;
    };

    static CellGeometry capture_geometry(const QCACell &cell,
                                         std::size_t layer) {
        CellGeometry geometry;
        geometry.cell_x = x(cell);
        geometry.cell_y = y(cell);
        geometry.layer = layer;
        for (std::size_t dot = 0; dot < 4; ++dot) {
            geometry.dot_coordinates[2 * dot] = x(dots(cell)[dot]);
            geometry.dot_coordinates[2 * dot + 1] = y(dots(cell)[dot]);
        }
        return geometry;
    }

    template<typename Option>
    void append_if_coupled(std::size_t source_index,
                           std::size_t candidate_index,
                           const std::vector<QCACell*> &cells,
                           const Option &option) {
        if (source_index == candidate_index) {
            return;
        }
        ++statistics_.candidate_checks;
        if (calculate_inter_cell_distance(*cells[source_index],
                                          *cells[candidate_index],
                                          option) <= option.radius_effect) {
            couplings_.push_back(Coupling{
                    candidate_index,
                    calculate_inter_cell_kink_energy_numerator(
                            *cells[source_index], *cells[candidate_index], option)});
        }
    }

    template<typename Option>
    void compile_all_pairs(const std::vector<QCACell*> &cells,
                           const Option &option) {
        for (std::size_t source = 0; source < cells.size(); ++source) {
            for (std::size_t candidate = 0; candidate < cells.size(); ++candidate) {
                append_if_coupled(source, candidate, cells, option);
            }
            row_offsets_.push_back(couplings_.size());
        }
    }

    template<typename Option>
    void compile_spatial(const std::vector<QCACell*> &cells,
                         const Option &option) {
        const double bucket_size = std::max(1.0, option.radius_effect);
        const auto bucket_coordinate = [bucket_size](double value) {
            return static_cast<long long>(std::floor(value / bucket_size));
        };

        std::map<std::pair<long long, long long>, std::vector<IndexedCell>> buckets;
        for (std::size_t index = 0; index < cells.size(); ++index) {
            QCACell *cell = cells[index];
            buckets[{bucket_coordinate(x(*cell)), bucket_coordinate(y(*cell))}]
                    .push_back(IndexedCell{index});
        }

        std::vector<IndexedCell> candidates;
        for (std::size_t source = 0; source < cells.size(); ++source) {
            QCACell *source_cell = cells[source];
            const long long bucket_x = bucket_coordinate(x(*source_cell));
            const long long bucket_y = bucket_coordinate(y(*source_cell));
            candidates.clear();

            for (long long dx = -1; dx <= 1; ++dx) {
                for (long long dy = -1; dy <= 1; ++dy) {
                    const auto found = buckets.find({bucket_x + dx,
                                                     bucket_y + dy});
                    if (found != buckets.end()) {
                        candidates.insert(candidates.end(),
                                          found->second.begin(),
                                          found->second.end());
                    }
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                      [](const IndexedCell &left, const IndexedCell &right) {
                          return left.index < right.index;
                      });
            for (const IndexedCell &candidate : candidates) {
                append_if_coupled(source, candidate.index, cells, option);
            }
            row_offsets_.push_back(couplings_.size());
        }
    }

    double radius_effect_ = 0.0;
    double layer_separation_ = 0.0;
    std::vector<CellGeometry> geometry_;
    std::vector<std::size_t> row_offsets_;
    std::vector<Coupling> couplings_;
    Statistics statistics_;
};

} // namespace simon

#endif // HFUT_SIMON_ORDERED_INTERACTION_GRAPH_HPP
