#include <simon/AcceleratedBistable.hpp>
#include <simon/AcceleratedCoherence.hpp>
#include <simon/SimulationMetrics.hpp>
#include <simon/simon.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class RequestedModel { Bistable, Coherence, Both };

struct CommandLine {
    std::string design_path;
    std::string vector_table_path;
    std::string output_prefix;
    std::string json_path;
    std::string csv_path;
    RequestedModel model = RequestedModel::Both;
    std::size_t repetitions = 3;
    std::size_t warmup = 0;
    bool require_equivalent = false;
    double equivalence_tolerance = 0.0;
    simon::SimulationComparisonOption comparison;
    simon::QCABistableOption bistable;
    simon::QCACoherenceOption coherence;
};

struct TimingSummary {
    std::vector<double> raw_seconds;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double standard_deviation = 0.0;
};

struct PairRecord {
    std::string model;
    std::string reference_name;
    std::string candidate_name;
    TimingSummary reference_timing;
    TimingSummary candidate_timing;
    simon::SimulationComparisonMetrics comparison;
    std::map<std::string, double> option_values;
    std::map<std::string, std::size_t> work_counters;
    std::map<std::string, bool> work_flags;
    std::string numeric_method;
};

void print_usage(std::ostream &stream) {
    stream
        << "usage: ifcn_physical_benchmark <circuit.qca> [options]\n"
        << "\n"
        << "Required comparison (always baseline versus strict accelerated):\n"
        << "  --model bistable|coherence|both   default: both\n"
        << "  --vectors table.vt               selective input vectors\n"
        << "  --repetitions N                  timed paired repetitions (default: 3)\n"
        << "  --warmup N                       untimed warmups for each engine\n"
        << "  --logic-threshold X              polarization dead zone (default: 0.1)\n"
        << "  --output-prefix PATH             write baseline/candidate RST files\n"
        << "  --json PATH                      write machine-readable report\n"
        << "  --csv PATH                       write one summary row per model\n"
        << "  --require-equivalent             non-zero exit if equivalence fails\n"
        << "  --equivalence-tolerance X        allowed max polarization error\n"
        << "\n"
        << "Shared physical/clock options:\n"
        << "  --epsilon-r X --layer-separation X --radius-effect X\n"
        << "  --amplitude X --clock-high X --clock-low X --clock-shift X\n"
        << "  --jitters J0,J1,J2,J3\n"
        << "\n"
        << "Bistable options:\n"
        << "  --samples N --max-iterations N --convergence-tolerance X --seed N\n"
        << "\n"
        << "Coherence-vector options:\n"
        << "  --temperature X --relaxation X --time-step X --duration X\n"
        << "  --steady-state-tolerance X --max-steady-state-iterations N\n"
        << "  --numeric-method euler|rk4\n";
}

std::string require_value(int argc, char **argv, int &index,
                          const std::string &argument) {
    if (++index >= argc) throw std::runtime_error(argument + " requires a value");
    return argv[index];
}

std::array<double, 4> parse_jitters(const std::string &text) {
    std::array<double, 4> jitters{};
    std::stringstream stream(text);
    std::string field;
    for (std::size_t index = 0; index < jitters.size(); ++index) {
        if (!std::getline(stream, field, ',')) {
            throw std::runtime_error("--jitters requires four comma-separated values");
        }
        jitters[index] = std::stod(field);
    }
    if (std::getline(stream, field, ',')) {
        throw std::runtime_error("--jitters requires exactly four values");
    }
    return jitters;
}

CommandLine parse_command_line(int argc, char **argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        throw std::runtime_error("missing QCA design path");
    }
    CommandLine command;
    command.design_path = argv[1];

    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            std::exit(0);
        } else if (argument == "--model") {
            const std::string value = require_value(argc, argv, index, argument);
            if (value == "bistable") command.model = RequestedModel::Bistable;
            else if (value == "coherence") command.model = RequestedModel::Coherence;
            else if (value == "both") command.model = RequestedModel::Both;
            else throw std::runtime_error("unknown model: " + value);
        } else if (argument == "--vectors") {
            command.vector_table_path = require_value(argc, argv, index, argument);
        } else if (argument == "--repetitions") {
            command.repetitions = std::stoull(require_value(argc, argv, index, argument));
        } else if (argument == "--warmup") {
            command.warmup = std::stoull(require_value(argc, argv, index, argument));
        } else if (argument == "--logic-threshold") {
            command.comparison.logic_threshold =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--stimulus-absolute-tolerance") {
            command.comparison.stimulus_absolute_tolerance =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--stimulus-relative-tolerance") {
            command.comparison.stimulus_relative_tolerance =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--output-prefix") {
            command.output_prefix = require_value(argc, argv, index, argument);
        } else if (argument == "--json") {
            command.json_path = require_value(argc, argv, index, argument);
        } else if (argument == "--csv") {
            command.csv_path = require_value(argc, argv, index, argument);
        } else if (argument == "--require-equivalent") {
            command.require_equivalent = true;
        } else if (argument == "--equivalence-tolerance") {
            command.equivalence_tolerance =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--epsilon-r") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.epsilon_r = value;
            command.coherence.epsilon_r = value;
        } else if (argument == "--layer-separation") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.layer_separation = value;
            command.coherence.layer_separation = value;
        } else if (argument == "--radius-effect") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.radius_effect = value;
            command.coherence.radius_effect = value;
        } else if (argument == "--amplitude") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.amplitude = value;
            command.coherence.amplitude = value;
        } else if (argument == "--clock-high") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.high = value;
            command.coherence.high = value;
        } else if (argument == "--clock-low") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.low = value;
            command.coherence.low = value;
        } else if (argument == "--clock-shift") {
            const double value = std::stod(require_value(argc, argv, index, argument));
            command.bistable.shift = value;
            command.coherence.shift = value;
        } else if (argument == "--jitters") {
            const auto value = parse_jitters(require_value(argc, argv, index, argument));
            command.bistable.jitters = value;
            command.coherence.jitters = value;
        } else if (argument == "--samples") {
            command.bistable.number_of_samples =
                std::stoull(require_value(argc, argv, index, argument));
        } else if (argument == "--max-iterations") {
            command.bistable.max_iteration_per_sample =
                std::stoull(require_value(argc, argv, index, argument));
        } else if (argument == "--convergence-tolerance") {
            command.bistable.convergence_tolerance =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--seed") {
            command.bistable.random_seed = static_cast<std::uint32_t>(
                std::stoul(require_value(argc, argv, index, argument)));
        } else if (argument == "--temperature") {
            command.coherence.T = std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--relaxation") {
            command.coherence.relaxation =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--time-step") {
            command.coherence.time_step =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--duration") {
            command.coherence.duration =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--steady-state-tolerance") {
            command.coherence.steady_state_tolerance =
                std::stod(require_value(argc, argv, index, argument));
        } else if (argument == "--max-steady-state-iterations") {
            command.coherence.max_steady_state_iterations =
                std::stoull(require_value(argc, argv, index, argument));
        } else if (argument == "--numeric-method") {
            const std::string value = require_value(argc, argv, index, argument);
            if (value == "euler") command.coherence.algorithm = simon::NumericMethod::Euler;
            else if (value == "rk4" || value == "runge-kutta") {
                command.coherence.algorithm = simon::NumericMethod::RungeKutta;
            } else {
                throw std::runtime_error("unknown numeric method: " + value);
            }
        } else {
            throw std::runtime_error("unknown argument: " + argument);
        }
    }

    if (command.repetitions == 0) throw std::runtime_error("--repetitions must be positive");
    if (command.bistable.number_of_samples < 2) {
        throw std::runtime_error("--samples must be at least 2");
    }
    if (command.coherence.time_step <= 0.0 || command.coherence.duration <= 0.0 ||
        command.coherence.steady_state_tolerance <= 0.0 ||
        command.coherence.max_steady_state_iterations == 0) {
        throw std::runtime_error(
            "coherence time-step, duration, steady-state tolerance and iteration cap "
            "must be positive");
    }
    if (command.comparison.logic_threshold < 0.0 ||
        command.equivalence_tolerance < 0.0) {
        throw std::runtime_error("comparison tolerances must be non-negative");
    }
    return command;
}

TimingSummary summarize(std::vector<double> seconds) {
    TimingSummary summary;
    summary.raw_seconds = std::move(seconds);
    if (summary.raw_seconds.empty()) return summary;

    summary.minimum = *std::min_element(summary.raw_seconds.begin(), summary.raw_seconds.end());
    summary.maximum = *std::max_element(summary.raw_seconds.begin(), summary.raw_seconds.end());
    summary.mean = std::accumulate(summary.raw_seconds.begin(), summary.raw_seconds.end(), 0.0) /
                   static_cast<double>(summary.raw_seconds.size());
    double squared_sum = 0.0;
    for (const double value : summary.raw_seconds) {
        const double difference = value - summary.mean;
        squared_sum += difference * difference;
    }
    summary.standard_deviation =
        std::sqrt(squared_sum / static_cast<double>(summary.raw_seconds.size()));

    auto sorted = summary.raw_seconds;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t middle = sorted.size() / 2;
    summary.median = sorted.size() % 2 == 0
                         ? 0.5 * (sorted[middle - 1] + sorted[middle])
                         : sorted[middle];
    return summary;
}

struct TimedResult {
    simon::Result result;
    double seconds = 0.0;
};

template<typename Algorithm, typename Option>
TimedResult run_algorithm(const simon::QCADesign &prototype,
                          const simon::VectorTable &vector_prototype,
                          simon::SimulationMode mode,
                          Option &option) {
    simon::QCADesign design = prototype;
    simon::VectorTable vectors = vector_prototype;
    TimedResult timed;
    const auto start = std::chrono::steady_clock::now();
    Algorithm algorithm(option);
    algorithm.run(design, vectors, timed.result, mode);
    timed.seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - start).count();
    return timed;
}

template<typename Algorithm, typename Option, typename Statistics>
std::pair<TimedResult, Statistics> run_accelerated(
    const simon::QCADesign &prototype,
    const simon::VectorTable &vector_prototype,
    simon::SimulationMode mode,
    Option &option) {
    simon::QCADesign design = prototype;
    simon::VectorTable vectors = vector_prototype;
    TimedResult timed;
    const auto start = std::chrono::steady_clock::now();
    Algorithm algorithm(option);
    algorithm.run(design, vectors, timed.result, mode);
    timed.seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - start).count();
    return {std::move(timed), algorithm.statistics()};
}

PairRecord benchmark_bistable(const CommandLine &command,
                              const simon::QCADesign &prototype,
                              const simon::VectorTable &vector_prototype,
                              simon::SimulationMode mode,
                              simon::Result &reference_result,
                              simon::Result &candidate_result) {
    auto option = command.bistable;
    for (std::size_t index = 0; index < command.warmup; ++index) {
        (void)run_algorithm<simon::BistableAlgorithm>(prototype, vector_prototype,
                                                       mode, option);
        (void)run_accelerated<simon::AcceleratedBistableAlgorithm,
                              simon::QCABistableOption,
                              simon::AcceleratedBistableStatistics>(
            prototype, vector_prototype, mode, option);
    }

    std::vector<double> reference_seconds;
    std::vector<double> candidate_seconds;
    simon::AcceleratedBistableStatistics final_statistics;
    for (std::size_t repetition = 0; repetition < command.repetitions; ++repetition) {
        auto run_reference = [&]() {
            auto run = run_algorithm<simon::BistableAlgorithm>(
                prototype, vector_prototype, mode, option);
            reference_seconds.push_back(run.seconds);
            reference_result = std::move(run.result);
        };
        auto run_candidate = [&]() {
            auto run = run_accelerated<simon::AcceleratedBistableAlgorithm,
                                       simon::QCABistableOption,
                                       simon::AcceleratedBistableStatistics>(
                prototype, vector_prototype, mode, option);
            candidate_seconds.push_back(run.first.seconds);
            candidate_result = std::move(run.first.result);
            final_statistics = run.second;
        };
        // Alternate AB/BA to reduce systematic thermal/frequency bias.
        if (repetition % 2 == 0) {
            run_reference();
            run_candidate();
        } else {
            run_candidate();
            run_reference();
        }
    }

    PairRecord record;
    record.model = "bistable";
    record.reference_name = "BistableAlgorithm";
    record.candidate_name = "AcceleratedBistableAlgorithm";
    record.reference_timing = summarize(std::move(reference_seconds));
    record.candidate_timing = summarize(std::move(candidate_seconds));
    record.comparison = simon::compare_simulation_results(
        reference_result, candidate_result, command.comparison);
    record.option_values = {
        {"epsilon_r", option.epsilon_r},
        {"layer_separation_nm", option.layer_separation},
        {"radius_effect_nm", option.radius_effect},
        {"amplitude", option.amplitude},
        {"clock_high_j", option.high},
        {"clock_low_j", option.low},
        {"clock_shift_j", option.shift},
        {"number_of_samples", static_cast<double>(option.number_of_samples)},
        {"max_iteration_per_sample", static_cast<double>(option.max_iteration_per_sample)},
        {"convergence_tolerance", option.convergence_tolerance},
        {"random_seed", static_cast<double>(option.random_seed)},
    };
    for (std::size_t index = 0; index < option.jitters.size(); ++index) {
        record.option_values["jitter_" + std::to_string(index) + "_degrees"] =
            option.jitters[index];
    }
    record.work_counters = {
        {"samples", final_statistics.samples},
        {"sweeps", final_statistics.sweeps},
        {"cell_updates", final_statistics.cell_updates},
        {"converged_samples", final_statistics.converged_samples},
        {"max_iteration_samples", final_statistics.max_iteration_samples},
        {"dynamic_cells", final_statistics.dynamic_cells},
        {"directed_couplings", final_statistics.directed_couplings},
        {"spatial_candidates", final_statistics.spatial_candidates},
    };
    return record;
}

PairRecord benchmark_coherence(const CommandLine &command,
                               const simon::QCADesign &prototype,
                               const simon::VectorTable &vector_prototype,
                               simon::SimulationMode mode,
                               simon::Result &reference_result,
                               simon::Result &candidate_result) {
    auto option = command.coherence;
    for (std::size_t index = 0; index < command.warmup; ++index) {
        (void)run_algorithm<simon::CoherenceAlgorithm>(prototype, vector_prototype,
                                                       mode, option);
        (void)run_accelerated<simon::AcceleratedCoherenceAlgorithm,
                              simon::QCACoherenceOption,
                              simon::AcceleratedCoherenceStatistics>(
            prototype, vector_prototype, mode, option);
    }

    std::vector<double> reference_seconds;
    std::vector<double> candidate_seconds;
    simon::AcceleratedCoherenceStatistics final_statistics;
    for (std::size_t repetition = 0; repetition < command.repetitions; ++repetition) {
        auto run_reference = [&]() {
            auto run = run_algorithm<simon::CoherenceAlgorithm>(
                prototype, vector_prototype, mode, option);
            reference_seconds.push_back(run.seconds);
            reference_result = std::move(run.result);
        };
        auto run_candidate = [&]() {
            auto run = run_accelerated<simon::AcceleratedCoherenceAlgorithm,
                                       simon::QCACoherenceOption,
                                       simon::AcceleratedCoherenceStatistics>(
                prototype, vector_prototype, mode, option);
            candidate_seconds.push_back(run.first.seconds);
            candidate_result = std::move(run.first.result);
            final_statistics = run.second;
        };
        if (repetition % 2 == 0) {
            run_reference();
            run_candidate();
        } else {
            run_candidate();
            run_reference();
        }
    }

    PairRecord record;
    record.model = "coherence";
    record.reference_name = "CoherenceAlgorithm";
    record.candidate_name = "AcceleratedCoherenceAlgorithm";
    record.reference_timing = summarize(std::move(reference_seconds));
    record.candidate_timing = summarize(std::move(candidate_seconds));
    record.comparison = simon::compare_simulation_results(
        reference_result, candidate_result, command.comparison);
    record.numeric_method = option.algorithm == simon::NumericMethod::Euler
                                ? "euler" : "runge_kutta";
    record.option_values = {
        {"epsilon_r", option.epsilon_r},
        {"layer_separation_nm", option.layer_separation},
        {"radius_effect_nm", option.radius_effect},
        {"amplitude", option.amplitude},
        {"clock_high_j", option.high},
        {"clock_low_j", option.low},
        {"clock_shift_j", option.shift},
        {"temperature_k", option.T},
        {"relaxation_s", option.relaxation},
        {"time_step_s", option.time_step},
        {"duration_s", option.duration},
        {"steady_state_tolerance", option.steady_state_tolerance},
        {"max_steady_state_iterations",
         static_cast<double>(option.max_steady_state_iterations)},
    };
    for (std::size_t index = 0; index < option.jitters.size(); ++index) {
        record.option_values["jitter_" + std::to_string(index) + "_degrees"] =
            option.jitters[index];
    }
    record.work_counters = {
        {"samples", final_statistics.samples},
        {"cell_updates", final_statistics.cell_updates},
        {"field_accumulations", final_statistics.field_accumulations},
        {"dynamic_cells", final_statistics.dynamic_cells},
        {"directed_couplings", final_statistics.directed_couplings},
        {"clock_cache_values", final_statistics.clock_cache_values},
        {"input_cache_values", final_statistics.input_cache_values},
        {"clock_generator_evaluations", final_statistics.clock_generator_evaluations},
        {"input_generator_evaluations", final_statistics.input_generator_evaluations},
        {"graph_candidate_checks", final_statistics.graph_candidate_checks},
        {"graph_all_pair_checks", final_statistics.graph_all_pair_checks},
    };
    record.work_flags = {
        {"clock_cache_used", final_statistics.clock_cache_used},
        {"input_cache_used", final_statistics.input_cache_used},
        {"strict_equivalent", final_statistics.strict_equivalent},
    };
    return record;
}

std::string json_escape(const std::string &value) {
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default: escaped << character; break;
        }
    }
    return escaped.str();
}

void ensure_parent_directory(const std::string &path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
}

void write_timing_json(std::ostream &output, const TimingSummary &timing) {
    output << "{\"median_seconds\":" << timing.median
           << ",\"mean_seconds\":" << timing.mean
           << ",\"stddev_seconds\":" << timing.standard_deviation
           << ",\"min_seconds\":" << timing.minimum
           << ",\"max_seconds\":" << timing.maximum
           << ",\"raw_seconds\":[";
    for (std::size_t index = 0; index < timing.raw_seconds.size(); ++index) {
        if (index != 0) output << ',';
        output << timing.raw_seconds[index];
    }
    output << "]}";
}

void write_comparison_json(std::ostream &output,
                           const simon::SimulationComparisonMetrics &comparison) {
    output << "{\"comparable\":" << (comparison.comparable ? "true" : "false")
           << ",\"incompatibility\":\"" << json_escape(comparison.incompatibility) << '"'
           << ",\"output_traces\":" << comparison.output_traces
           << ",\"output_samples\":" << comparison.output_samples
           << ",\"stable_reference_samples\":" << comparison.stable_reference_samples
           << ",\"sign_matches\":" << comparison.sign_matches
           << ",\"confident_matches\":" << comparison.confident_matches
           << ",\"weak_candidate_samples\":" << comparison.weak_candidate_samples
           << ",\"sign_agreement\":" << comparison.sign_agreement()
           << ",\"confident_logic_agreement\":"
           << comparison.confident_logic_agreement()
           << ",\"mae\":" << comparison.mae()
           << ",\"rmse\":" << comparison.rmse()
           << ",\"max_absolute_error\":" << comparison.maximum_absolute_error
           << ",\"per_output\":[";
    for (std::size_t index = 0; index < comparison.per_output.size(); ++index) {
        if (index != 0) output << ',';
        const auto &trace = comparison.per_output[index];
        output << "{\"label\":\"" << json_escape(trace.label) << '"'
               << ",\"samples\":" << trace.samples
               << ",\"stable_reference_samples\":" << trace.stable_reference_samples
               << ",\"sign_agreement\":" << trace.sign_agreement()
               << ",\"confident_logic_agreement\":"
               << trace.confident_logic_agreement()
               << ",\"weak_candidate_samples\":" << trace.weak_candidate_samples
               << ",\"mae\":" << trace.mae()
               << ",\"rmse\":" << trace.rmse()
               << ",\"max_absolute_error\":" << trace.maximum_absolute_error << '}';
    }
    output << "]}";
}

void write_json_report(const CommandLine &command, simon::SimulationMode mode,
                       const std::vector<PairRecord> &records) {
    if (command.json_path.empty()) return;
    ensure_parent_directory(command.json_path);
    std::ofstream output(command.json_path);
    if (!output) throw std::runtime_error("cannot write JSON: " + command.json_path);
    output << std::setprecision(17)
           << "{\"schema_version\":1,\"circuit\":\""
           << json_escape(std::filesystem::absolute(command.design_path).string()) << '"'
           << ",\"simulation_mode\":\""
           << (mode == simon::SimulationMode::Selective ? "selective" : "exhaustive") << '"'
           << ",\"vector_table\":";
    if (command.vector_table_path.empty()) output << "null";
    else output << '"' << json_escape(std::filesystem::absolute(
                                  command.vector_table_path).string()) << '"';
    output << ",\"repetitions\":" << command.repetitions
           << ",\"warmup\":" << command.warmup
           << ",\"logic_threshold\":" << command.comparison.logic_threshold
           << ",\"comparisons\":[";
    for (std::size_t index = 0; index < records.size(); ++index) {
        if (index != 0) output << ',';
        const auto &record = records[index];
        output << "{\"model\":\"" << record.model << '"'
               << ",\"reference\":\"" << record.reference_name << '"'
               << ",\"candidate\":\"" << record.candidate_name << '"'
               << ",\"speedup\":"
               << (record.candidate_timing.median > 0.0
                       ? record.reference_timing.median /
                             record.candidate_timing.median : 0.0)
               << ",\"reference_timing\":";
        write_timing_json(output, record.reference_timing);
        output << ",\"candidate_timing\":";
        write_timing_json(output, record.candidate_timing);
        output << ",\"options\":{";
        bool first = true;
        for (const auto &[name, value] : record.option_values) {
            if (!first) output << ',';
            output << '"' << json_escape(name) << "\":" << value;
            first = false;
        }
        if (!record.numeric_method.empty()) {
            if (!first) output << ',';
            output << "\"numeric_method\":\"" << record.numeric_method << '"';
        }
        output << "},\"work\":{";
        first = true;
        for (const auto &[name, value] : record.work_counters) {
            if (!first) output << ',';
            output << '"' << json_escape(name) << "\":" << value;
            first = false;
        }
        for (const auto &[name, value] : record.work_flags) {
            if (!first) output << ',';
            output << '"' << json_escape(name) << "\":"
                   << (value ? "true" : "false");
            first = false;
        }
        output << "},\"accuracy\":";
        write_comparison_json(output, record.comparison);
        output << '}';
    }
    output << "]}\n";
}

std::string csv_escape(const std::string &value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"') escaped += '"';
        escaped += character;
    }
    escaped += '"';
    return escaped;
}

void write_csv_report(const CommandLine &command, simon::SimulationMode mode,
                      const std::vector<PairRecord> &records) {
    if (command.csv_path.empty()) return;
    ensure_parent_directory(command.csv_path);
    std::ofstream output(command.csv_path);
    if (!output) throw std::runtime_error("cannot write CSV: " + command.csv_path);
    output << "circuit,model,simulation_mode,vector_table,repetitions,"
              "reference_median_seconds,candidate_median_seconds,speedup,"
              "stable_reference_samples,sign_agreement,confident_logic_agreement,"
              "mae,rmse,max_absolute_error,weak_candidate_samples,comparable,incompatibility\n";
    output << std::setprecision(17);
    for (const auto &record : records) {
        const auto &accuracy = record.comparison;
        const double speedup = record.candidate_timing.median > 0.0
                                   ? record.reference_timing.median /
                                         record.candidate_timing.median
                                   : 0.0;
        output << csv_escape(std::filesystem::absolute(command.design_path).string()) << ','
               << record.model << ','
               << (mode == simon::SimulationMode::Selective ? "selective" : "exhaustive") << ','
               << csv_escape(command.vector_table_path) << ',' << command.repetitions << ','
               << record.reference_timing.median << ',' << record.candidate_timing.median << ','
               << speedup << ',' << accuracy.stable_reference_samples << ','
               << accuracy.sign_agreement() << ',' << accuracy.confident_logic_agreement() << ','
               << accuracy.mae() << ',' << accuracy.rmse() << ','
               << accuracy.maximum_absolute_error << ',' << accuracy.weak_candidate_samples << ','
               << (accuracy.comparable ? "true" : "false") << ','
               << csv_escape(accuracy.incompatibility) << '\n';
    }
}

void print_record(const PairRecord &record) {
    const double speedup = record.candidate_timing.median > 0.0
                               ? record.reference_timing.median /
                                     record.candidate_timing.median
                               : 0.0;
    std::cout << std::setprecision(10)
              << "model=" << record.model << '\n'
              << "reference_median_seconds=" << record.reference_timing.median << '\n'
              << "candidate_median_seconds=" << record.candidate_timing.median << '\n'
              << "speedup=" << speedup << '\n'
              << std::boolalpha
              << "comparable=" << record.comparison.comparable << '\n';
    if (!record.comparison.comparable) {
        std::cout << "incompatibility=" << record.comparison.incompatibility << '\n';
        return;
    }
    std::cout << "stable_reference_samples="
              << record.comparison.stable_reference_samples << '\n'
              << "logic_sign_agreement=" << record.comparison.sign_agreement() << '\n'
              << "confident_logic_agreement="
              << record.comparison.confident_logic_agreement() << '\n'
              << "mae=" << record.comparison.mae() << '\n'
              << "rmse=" << record.comparison.rmse() << '\n'
              << "max_absolute_error="
              << record.comparison.maximum_absolute_error << '\n';
}

void write_waveforms(const CommandLine &command, const std::string &model,
                     const simon::Result &reference,
                     const simon::Result &candidate) {
    if (command.output_prefix.empty()) return;
    const std::string reference_path = command.output_prefix + "_" + model + "_baseline.rst";
    const std::string candidate_path = command.output_prefix + "_" + model + "_accelerated.rst";
    ensure_parent_directory(reference_path);
    reference.write_text_file(reference_path);
    candidate.write_text_file(candidate_path);
}

bool equivalent(const PairRecord &record, double tolerance) {
    return record.comparison.comparable &&
           record.comparison.confident_logic_agreement() == 1.0 &&
           record.comparison.maximum_absolute_error <= tolerance;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const CommandLine command = parse_command_line(argc, argv);

        simon::QCADesign prototype;
        if (!simon::parse_design(command.design_path, prototype)) {
            throw std::runtime_error("cannot parse QCA design: " + command.design_path);
        }
        simon::VectorTable vector_prototype;
        simon::SimulationMode mode = simon::SimulationMode::Exhaustive;
        if (!command.vector_table_path.empty()) {
            if (!simon::parse_vector_table(command.vector_table_path, vector_prototype)) {
                throw std::runtime_error("cannot parse vector table: " +
                                         command.vector_table_path);
            }
            mode = simon::SimulationMode::Selective;
        }

        std::vector<PairRecord> records;
        if (command.model == RequestedModel::Bistable ||
            command.model == RequestedModel::Both) {
            simon::Result reference;
            simon::Result candidate;
            records.push_back(benchmark_bistable(command, prototype, vector_prototype,
                                                  mode, reference, candidate));
            write_waveforms(command, "bistable", reference, candidate);
            print_record(records.back());
        }
        if (command.model == RequestedModel::Coherence ||
            command.model == RequestedModel::Both) {
            simon::Result reference;
            simon::Result candidate;
            records.push_back(benchmark_coherence(command, prototype, vector_prototype,
                                                   mode, reference, candidate));
            write_waveforms(command, "coherence", reference, candidate);
            print_record(records.back());
        }

        write_json_report(command, mode, records);
        write_csv_report(command, mode, records);

        bool fair = true;
        bool passed = true;
        for (const auto &record : records) {
            fair = fair && record.comparison.comparable;
            passed = passed && equivalent(record, command.equivalence_tolerance);
        }
        if (!fair) return 2;
        if (command.require_equivalent && !passed) return 3;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ifcn_physical_benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
