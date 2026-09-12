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
    simon::SimulationTraceRecorder baseline_trace(true);
    simon::SimulationTraceRecorder accelerated_trace(true);
    auto baseline_option = option;
    auto accelerated_option = option;
    baseline_option.trace_recorder = &baseline_trace;
    accelerated_option.trace_recorder = &accelerated_trace;

    const Run baseline = run_baseline(path, baseline_option);
    const Run accelerated = run_accelerated(path, accelerated_option);

    double maximum_error = 0.0;
    compare_trace_group(baseline.result.inputs, accelerated.result.inputs,
                        "inputs", maximum_error);
    compare_trace_group(baseline.result.outputs, accelerated.result.outputs,
                        "outputs", maximum_error);
    compare_trace_group(baseline.result.clocks, accelerated.result.clocks,
                        "clocks", maximum_error);
    if (!baseline_trace.equivalent_to(accelerated_trace)) {
        throw std::runtime_error(
                "complete bistable internal-state trajectory differs: " +
                baseline_trace.digest() + " != " + accelerated_trace.digest());
    }

    simon::SimulationTraceRecorder dense_trace(true);
    auto dense_option = option;
    dense_option.acceleration.use_spatial_buckets = false;
    dense_option.trace_recorder = &dense_trace;
    const Run dense = run_accelerated(path, dense_option);
    compare_trace_group(baseline.result.outputs, dense.result.outputs,
                        "all-pairs ablation outputs", maximum_error);
    if (dense.statistics.graph_spatial_buckets_used) {
        throw std::runtime_error("all-pairs graph ablation was not selected");
    }
    if (!baseline_trace.equivalent_to(dense_trace)) {
        throw std::runtime_error("all-pairs internal-state trajectory differs");
    }

    simon::QCADesign compilation_design;
    if (!simon::parse_design(path.string(), compilation_design)) {
        throw std::runtime_error("cannot parse graph compilation design");
    }
    simon::OrderedInteractionGraph graph =
            simon::OrderedInteractionGraph::compile(compilation_design, option, true);
    for (const double epsilon_r : {6.5, 9.7, 12.9, 16.1, 20.0}) {
        simon::SimulationTraceRecorder sweep_baseline_trace(true);
        simon::SimulationTraceRecorder sweep_accelerated_trace(true);
        auto sweep_option = option;
        sweep_option.epsilon_r = epsilon_r;
        sweep_option.acceleration.precompiled_graph = &graph;
        auto baseline_sweep_option = sweep_option;
        auto accelerated_sweep_option = sweep_option;
        baseline_sweep_option.trace_recorder = &sweep_baseline_trace;
        accelerated_sweep_option.trace_recorder = &sweep_accelerated_trace;
        const Run sweep_baseline = run_baseline(path, baseline_sweep_option);
        const Run sweep_accelerated = run_accelerated(
                path, accelerated_sweep_option);
        compare_trace_group(sweep_baseline.result.outputs,
                            sweep_accelerated.result.outputs,
                            "epsilon-sweep reused-graph outputs", maximum_error);
        if (!sweep_baseline_trace.equivalent_to(sweep_accelerated_trace)) {
            throw std::runtime_error(
                    "epsilon-sweep internal trajectory differs at epsilon_r=" +
                    std::to_string(epsilon_r));
        }
        if (!sweep_accelerated.statistics.graph_reused ||
            sweep_accelerated.statistics.precompiled_graph_rejected) {
            throw std::runtime_error("valid precompiled graph was not reused");
        }
    }

    auto invalid_option = option;
    invalid_option.radius_effect += 1.0;
    invalid_option.acceleration.precompiled_graph = &graph;
    const Run invalid_baseline = run_baseline(path, invalid_option);
    const Run invalid_accelerated = run_accelerated(path, invalid_option);
    compare_trace_group(invalid_baseline.result.outputs,
                        invalid_accelerated.result.outputs,
                        "invalid-cache fallback outputs", maximum_error);
    if (!invalid_accelerated.statistics.precompiled_graph_rejected ||
        invalid_accelerated.statistics.graph_reused) {
        throw std::runtime_error("incompatible graph did not trigger cold fallback");
    }

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
              << ", state_frames=" << baseline_trace.frame_count()
              << ", state_values=" << baseline_trace.value_count()
              << ", state_digest=" << baseline_trace.digest()
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
