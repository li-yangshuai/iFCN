#include <simon/OrderedInteractionGraph.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void verify(const std::filesystem::path &path) {
    simon::QCADesign design;
    if (!simon::parse_design(path.string(), design)) {
        throw std::runtime_error("cannot parse " + path.string());
    }
    simon::QCABistableOption option;
    auto spatial = simon::OrderedInteractionGraph::compile(design, option, true);

    simon::QCADesign dense_design;
    if (!simon::parse_design(path.string(), dense_design)) {
        throw std::runtime_error("cannot reparse " + path.string());
    }
    auto dense = simon::OrderedInteractionGraph::compile(
            dense_design, option, false);
    if (spatial.statistics().directed_couplings !=
        dense.statistics().directed_couplings) {
        throw std::runtime_error("spatial and all-pairs coupling counts differ");
    }

    std::vector<simon::QCACell *> cells;
    for (auto &layer : design) {
        for (auto &cell : layer) cells.push_back(&cell);
    }
    for (std::size_t source = 0; source < cells.size(); ++source) {
        const auto spatial_begin = spatial.row_begin(source);
        const auto dense_begin = dense.row_begin(source);
        const auto spatial_size = spatial.row_end(source) - spatial_begin;
        const auto dense_size = dense.row_end(source) - dense_begin;
        if (spatial_size != dense_size) {
            throw std::runtime_error("spatial and all-pairs row sizes differ");
        }
        for (std::size_t offset = 0; offset < spatial_size; ++offset) {
            const auto spatial_index = spatial_begin + offset;
            const auto dense_index = dense_begin + offset;
            const auto &left = spatial.coupling(spatial_index);
            const auto &right = dense.coupling(dense_index);
            if (left.neighbour_index != right.neighbour_index ||
                left.kink_energy_numerator != right.kink_energy_numerator) {
                throw std::runtime_error(
                        "spatial compilation changed ordered coupling data");
            }
            for (const double epsilon_r : {6.5, 9.7, 12.9, 16.1, 20.0}) {
                option.epsilon_r = epsilon_r;
                const double direct = simon::calculate_inter_cell_kink_energy(
                        *cells[source], *cells[left.neighbour_index], option);
                if (spatial.kink_energy(spatial_index, epsilon_r) != direct) {
                    throw std::runtime_error(
                            "materialized kink energy differs from direct formula");
                }
            }
        }
    }

    simon::QCADesign same_design;
    if (!simon::parse_design(path.string(), same_design) ||
        !spatial.matches(same_design, option)) {
        throw std::runtime_error("identical reparsed geometry was rejected");
    }
    option.epsilon_r = 7.3;
    if (!spatial.matches(same_design, option)) {
        throw std::runtime_error("epsilon-only change invalidated reusable graph");
    }
    option.radius_effect += 1.0;
    if (spatial.matches(same_design, option)) {
        throw std::runtime_error("radius change did not invalidate graph");
    }
    option.radius_effect -= 1.0;
    if (!simon::layers(same_design).empty() &&
        !simon::cells(simon::layers(same_design).front()).empty()) {
        simon::x(simon::cells(simon::layers(same_design).front()).front()) += 0.25;
        if (spatial.matches(same_design, option)) {
            throw std::runtime_error("geometry change did not invalidate graph");
        }
    }

    std::cout << path.filename().string()
              << ", cells=" << spatial.statistics().cells
              << ", couplings=" << spatial.statistics().directed_couplings
              << ", candidates=" << spatial.statistics().candidate_checks
              << "/" << spatial.statistics().all_pair_checks << '\n';
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            throw std::runtime_error("at least one QCA design path is required");
        }
        for (int index = 1; index < argc; ++index) verify(argv[index]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
