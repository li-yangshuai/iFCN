#include <simon/AcceleratedBistable.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Run {
    simon::Result result;
    double seconds = 0.0;
    simon::AcceleratedBistableStatistics statistics;
};

Run run_baseline(const std::filesystem::path &path,
                 const simon::QCABistableOption &source_option)
{
    simon::QCADesign design;
    if (!simon::parse_design(path.string(), design)) {
        throw std::runtime_error("cannot parse " + path.string());
    }
    simon::VectorTable vectors;
    auto option = source_option;
    simon::BistableAlgorithm algorithm(option);
    Run run;
    const auto start = Clock::now();
    algorithm.run(design, vectors, run.result, simon::SimulationMode::Exhaustive);
    run.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return run;
}

Run run_accelerated(const std::filesystem::path &path,
                    const simon::QCABistableOption &source_option)
{
    simon::QCADesign design;
    if (!simon::parse_design(path.string(), design)) {
        throw std::runtime_error("cannot parse " + path.string());
    }
    simon::VectorTable vectors;
    auto option = source_option;
    simon::AcceleratedBistableAlgorithm algorithm(option);
    Run run;
    const auto start = Clock::now();
    algorithm.run(design, vectors, run.result, simon::SimulationMode::Exhaustive);
    run.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    run.statistics = algorithm.statistics();
    return run;
}

void compare_trace_group(const std::vector<simon::Trace> &expected,
                         const std::vector<simon::Trace> &actual,
                         const std::string &group,
                         double &maximum_error)
{
    if (expected.size() != actual.size()) {
        throw std::runtime_error(group + " trace count differs");
    }
    for (std::size_t trace = 0; trace < expected.size(); ++trace) {
        if (expected[trace].data_labels != actual[trace].data_labels ||
            expected[trace].trace_function != actual[trace].trace_function ||
            expected[trace].data.size() != actual[trace].data.size()) {
            throw std::runtime_error(group + " trace metadata differs");
        }
        for (std::size_t sample = 0; sample < expected[trace].data.size(); ++sample) {
            const double error =
                    std::fabs(expected[trace].data[sample] - actual[trace].data[sample]);
            maximum_error = std::max(maximum_error, error);
            if (expected[trace].data[sample] != actual[trace].data[sample]) {
                throw std::runtime_error(
                        group + " is not bitwise equivalent at trace " +
                        std::to_string(trace) + ", sample " + std::to_string(sample) +
                        "; absolute error=" + std::to_string(error));
            }
        }
    }
}

void verify(const std::filesystem::path &path,
            const simon::QCABistableOption &option)
{
    const Run baseline = run_baseline(path, option);
    const Run accelerated = run_accelerated(path, option);

    double maximum_error = 0.0;
    compare_trace_group(baseline.result.inputs, accelerated.result.inputs,
                        "inputs", maximum_error);
    compare_trace_group(baseline.result.outputs, accelerated.result.outputs,
                        "outputs", maximum_error);
    compare_trace_group(baseline.result.clocks, accelerated.result.clocks,
                        "clocks", maximum_error);

    if (accelerated.statistics.samples != option.number_of_samples) {
        throw std::runtime_error("accelerated statistics have an invalid sample count");
    }

    const double speedup = accelerated.seconds > 0.0
            ? baseline.seconds / accelerated.seconds
            : 0.0;
    std::cout << path.filename().string()
              << ", baseline_s=" << baseline.seconds
              << ", accelerated_s=" << accelerated.seconds
              << ", speedup=" << speedup
              << ", max_abs_error=" << maximum_error
              << ", sweeps=" << accelerated.statistics.sweeps
              << ", updates=" << accelerated.statistics.cell_updates
              << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc < 2) {
            throw std::runtime_error("at least one QCA design path is required");
        }

        simon::QCABistableOption option;
        option.number_of_samples = 256;
        option.max_iteration_per_sample = 100;
        option.random_seed = 20260712u;

        for (int i = 1; i < argc; ++i) {
            verify(argv[i], option);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
