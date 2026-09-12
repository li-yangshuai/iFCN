// Small, argument-driven wrapper around fiction's official GOLD implementation.
// This file is copied into a pinned fiction checkout's experiments directory so
// it is compiled with the exact same dependency graph as the upstream examples.

#include <fiction/algorithms/physical_design/graph_oriented_layout_design.hpp>
#include <fiction/algorithms/verification/equivalence_checking.hpp>
#include <fiction/io/network_reader.hpp>
#include <fiction/types.hpp>

#include <mockturtle/utils/stopwatch.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using Params = fiction::graph_oriented_layout_design_params;
using GateLayout =
    fiction::gate_level_layout<fiction::clocked_layout<fiction::tile_based_layout<fiction::cartesian_layout<>>>>;

struct Options
{
    Params::effort_mode mode = Params::effort_mode::HIGH_EFFICIENCY;
    std::uint64_t timeoutMs = 60000;
    std::vector<std::string> inputs{};
};

Options parseOptions(int argc, char **argv)
{
    Options options{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument{argv[index]};
        const auto value = [&](const char *option) {
            if (index + 1 >= argc)
            {
                throw std::runtime_error(std::string{"missing value for "} + option);
            }
            return std::string{argv[++index]};
        };
        if (argument == "--mode")
        {
            const auto mode = value("--mode");
            if (mode == "high-efficiency")
            {
                options.mode = Params::effort_mode::HIGH_EFFICIENCY;
            }
            else if (mode == "high-effort")
            {
                options.mode = Params::effort_mode::HIGH_EFFORT;
            }
            else if (mode == "highest-effort")
            {
                options.mode = Params::effort_mode::HIGHEST_EFFORT;
            }
            else
            {
                throw std::runtime_error("unsupported GOLD effort mode: " + mode);
            }
        }
        else if (argument == "--timeout-ms")
        {
            options.timeoutMs = std::stoull(value("--timeout-ms"));
            if (options.timeoutMs == 0)
            {
                throw std::runtime_error("--timeout-ms must be positive");
            }
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            throw std::runtime_error("unknown option: " + argument);
        }
        else
        {
            options.inputs.push_back(argument);
        }
    }
    if (options.inputs.empty())
    {
        throw std::runtime_error(
            "usage: fiction_gold_subset [--mode high-efficiency|high-effort|highest-effort] "
            "[--timeout-ms N] network.v [...]");
    }
    return options;
}

std::string modeName(const Params::effort_mode mode)
{
    switch (mode)
    {
        case Params::effort_mode::HIGH_EFFICIENCY:
            return "HIGH_EFFICIENCY";
        case Params::effort_mode::HIGH_EFFORT:
            return "HIGH_EFFORT";
        case Params::effort_mode::HIGHEST_EFFORT:
            return "HIGHEST_EFFORT";
        case Params::effort_mode::MAXIMUM_EFFORT:
            return "MAXIMUM_EFFORT";
    }
    return "UNKNOWN";
}

std::string equivalenceName(const fiction::eq_type result)
{
    if (result == fiction::eq_type::STRONG)
    {
        return "STRONG";
    }
    if (result == fiction::eq_type::WEAK)
    {
        return "WEAK";
    }
    return "NO";
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto options = parseOptions(argc, argv);
        std::cout << "benchmark,mode,cost,timeout_ms,status,timeout_boundary,width_tiles,height_tiles,area_tiles,"
                     "gates,wires,crossings,runtime_pnr_s,equivalence\n";
        for (const auto &input : options.inputs)
        {
            const auto benchmark = std::filesystem::path{input}.stem().string();
            try
            {
                std::ostringstream diagnostics{};
                fiction::network_reader<fiction::tec_ptr> reader{input, diagnostics};
                const auto networks = reader.get_networks();
                if (networks.empty())
                {
                    throw std::runtime_error("network reader returned no network: " + diagnostics.str());
                }
                auto network = *networks.front();

                Params params{};
                params.mode = options.mode;
                params.cost = Params::cost_objective::AREA;
                params.return_first = false;
                params.timeout = options.timeoutMs;
                params.verbose = false;

                fiction::graph_oriented_layout_design_stats stats{};
                const auto layout = fiction::graph_oriented_layout_design<GateLayout, fiction::tec_nt>(
                    network, params, &stats);
                if (!layout.has_value())
                {
                    const auto runtimeSeconds = mockturtle::to_seconds(stats.time_total);
                    std::cout << benchmark << ',' << modeName(options.mode) << ",AREA," << options.timeoutMs
                              << ",NO_LAYOUT,"
                              << (runtimeSeconds * 1000.0 >= static_cast<double>(options.timeoutMs) * 0.999)
                              << ",0,0,0,0,0,0," << std::setprecision(12) << runtimeSeconds
                              << ",NOT_RUN\n";
                    continue;
                }

                const auto equivalence = fiction::equivalence_checking<fiction::technology_network, GateLayout>(
                    network, *layout);
                const auto width = stats.x_size;
                const auto height = stats.y_size;
                const auto runtimeSeconds = mockturtle::to_seconds(stats.time_total);
                std::cout << benchmark << ',' << modeName(options.mode) << ",AREA," << options.timeoutMs
                          << ",PASS,"
                          << (runtimeSeconds * 1000.0 >= static_cast<double>(options.timeoutMs) * 0.999) << ','
                          << width << ',' << height << ',' << (width * height) << ','
                          << stats.num_gates << ',' << stats.num_wires << ',' << stats.num_crossings << ','
                          << std::setprecision(12) << runtimeSeconds << ','
                          << equivalenceName(equivalence) << '\n';
            }
            catch (const std::exception &error)
            {
                std::cerr << benchmark << ": " << error.what() << '\n';
                std::cout << benchmark << ',' << modeName(options.mode) << ",AREA," << options.timeoutMs
                          << ",ERROR,false,0,0,0,0,0,0,0,NOT_RUN\n";
            }
        }
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
