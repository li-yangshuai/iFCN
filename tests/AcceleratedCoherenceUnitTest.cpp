#include <simon/AcceleratedCoherence.hpp>

#include <algorithm>
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
    simon::AcceleratedCoherenceStatistics statistics;
};

Run run_baseline(const std::filesystem::path &path,
                 const simon::QCACoherenceOption &source_option)
{
    simon::QCADesign design;
    if (!simon::parse_design(path.string(), design)) {
        throw std::runtime_error("cannot parse " + path.string());
    }

    simon::VectorTable vectors;
    auto option = source_option;
    simon::CoherenceAlgorithm algorithm(option);
    Run run;
    const auto start = Clock::now();
    algorithm.run(design, vectors, run.result, simon::SimulationMode::Exhaustive);
    run.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return run;
}

Run run_accelerated(const std::filesystem::path &path,
                    const simon::QCACoherenceOption &source_option)
{
    simon::QCADesign design;
    if (!simon::parse_design(path.string(), design)) {
        throw std::runtime_error("cannot parse " + path.string());
    }

    simon::VectorTable vectors;
    auto option = source_option;
    simon::AcceleratedCoherenceAlgorithm algorithm(option);
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
                        std::to_string(trace) + ", sample " +
                        std::to_string(sample) + "; absolute error=" +
                        std::to_string(error));
            }
        }
    }
}

void verify(const std::filesystem::path &path,
            const simon::QCACoherenceOption &option)
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
                "complete coherence internal-state trajectory differs: " +
                baseline_trace.digest() + " != " + accelerated_trace.digest());
    }

    simon::SimulationTraceRecorder ablation_trace(true);
    auto ablation_option = option;
    ablation_option.acceleration.use_spatial_buckets = false;
    ablation_option.acceleration.use_clock_cache = false;
    ablation_option.acceleration.use_input_cache = false;
    ablation_option.acceleration.use_fused_integration = false;
    ablation_option.trace_recorder = &ablation_trace;
    const Run ablation = run_accelerated(path, ablation_option);
    compare_trace_group(baseline.result.outputs, ablation.result.outputs,
                        "all-disabled ablation outputs", maximum_error);
    if (!baseline_trace.equivalent_to(ablation_trace)) {
        throw std::runtime_error("all-disabled internal-state trajectory differs");
    }
    if (ablation.statistics.graph_spatial_buckets_used ||
        ablation.statistics.clock_cache_used ||
        ablation.statistics.input_cache_used ||
        ablation.statistics.fused_integration_used) {
        throw std::runtime_error("coherence ablation switches were not disabled");
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

    simon::SimulationTraceRecorder fallback_trace(true);
    auto fallback_option = option;
    fallback_option.acceleration.cache_budget_bytes = 0;
    fallback_option.trace_recorder = &fallback_trace;
    const Run fallback = run_accelerated(path, fallback_option);
    compare_trace_group(baseline.result.outputs, fallback.result.outputs,
                        "cache-budget fallback outputs", maximum_error);
    if (!baseline_trace.equivalent_to(fallback_trace)) {
        throw std::runtime_error("cache-budget fallback trajectory differs");
    }
    if (fallback.statistics.clock_cache_used ||
        fallback.statistics.input_cache_used ||
        fallback.statistics.clock_generator_evaluations == 0) {
        throw std::runtime_error("zero cache budget did not activate generator fallback");
    }

    const std::size_t expected_samples = static_cast<std::size_t>(
            std::ceil(option.duration / option.time_step));
    if (accelerated.statistics.samples != expected_samples) {
        throw std::runtime_error("accelerated statistics have an invalid sample count");
    }
    if (!accelerated.statistics.strict_equivalent ||
        !accelerated.statistics.clock_cache_used ||
        !accelerated.statistics.input_cache_used) {
        throw std::runtime_error("strict accelerated caches were not enabled");
    }

    const double speedup = accelerated.seconds > 0.0
            ? baseline.seconds / accelerated.seconds
            : 0.0;
    std::cout << path.filename().string()
              << ", method="
              << (option.algorithm == simon::NumericMethod::Euler ? "Euler" : "RungeKutta")
              << ", baseline_s=" << baseline.seconds
              << ", accelerated_s=" << accelerated.seconds
              << ", speedup=" << speedup
              << ", max_abs_error=" << maximum_error
              << ", state_frames=" << baseline_trace.frame_count()
              << ", state_values=" << baseline_trace.value_count()
              << ", state_digest=" << baseline_trace.digest()
              << ", updates=" << accelerated.statistics.cell_updates
              << ", graph_checks=" << accelerated.statistics.graph_candidate_checks
              << "/" << accelerated.statistics.graph_all_pair_checks
              << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc != 2) {
            throw std::runtime_error("one QCA design path is required");
        }

        simon::QCACoherenceOption option;
        option.time_step = 1e-16;
        // Long enough to exercise the cached hot loop while keeping CTest
        // comfortably below one second for the small xor-14 regression case.
        option.duration = 4096 * option.time_step;

        option.algorithm = simon::NumericMethod::Euler;
        verify(argv[1], option);

        option.algorithm = simon::NumericMethod::RungeKutta;
        verify(argv[1], option);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
