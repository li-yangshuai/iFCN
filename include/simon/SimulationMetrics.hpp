// SPDX-License-Identifier: MIT
// Fair, sample-aligned comparison utilities for physical QCA simulators.

#if !defined(IFCN_SIMON_SIMULATION_METRICS_HPP)
#define IFCN_SIMON_SIMULATION_METRICS_HPP

#include "Model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace simon {

struct SimulationComparisonOption {
    // Reference polarizations inside [-logic_threshold, logic_threshold] are
    // transition/metastable samples and are not assigned a Boolean value.
    double logic_threshold = 0.1;

    // Stimulus and clock traces must match before output errors are evaluated.
    // Clock energies are around 1e-22 J, hence a deliberately small abs tol.
    double stimulus_absolute_tolerance = 1e-30;
    double stimulus_relative_tolerance = 1e-12;
};

struct TraceComparisonMetrics {
    std::string label;
    std::size_t samples = 0;
    std::size_t stable_reference_samples = 0;
    std::size_t sign_matches = 0;
    std::size_t confident_matches = 0;
    std::size_t weak_candidate_samples = 0;
    double absolute_error_sum = 0.0;
    double squared_error_sum = 0.0;
    double maximum_absolute_error = 0.0;

    double mae() const noexcept {
        return samples == 0 ? 0.0 : absolute_error_sum / static_cast<double>(samples);
    }

    double rmse() const noexcept {
        return samples == 0 ? 0.0
                            : std::sqrt(squared_error_sum / static_cast<double>(samples));
    }

    double sign_agreement() const noexcept {
        return stable_reference_samples == 0
                   ? 1.0
                   : static_cast<double>(sign_matches) /
                         static_cast<double>(stable_reference_samples);
    }

    // A candidate value in the dead zone is conservatively counted as a
    // failure, even when its numerical sign happens to equal the reference.
    double confident_logic_agreement() const noexcept {
        return stable_reference_samples == 0
                   ? 1.0
                   : static_cast<double>(confident_matches) /
                         static_cast<double>(stable_reference_samples);
    }
};

struct SimulationComparisonMetrics {
    bool comparable = false;
    std::string incompatibility;
    std::size_t output_traces = 0;
    std::size_t output_samples = 0;
    std::size_t stable_reference_samples = 0;
    std::size_t sign_matches = 0;
    std::size_t confident_matches = 0;
    std::size_t weak_candidate_samples = 0;
    double absolute_error_sum = 0.0;
    double squared_error_sum = 0.0;
    double maximum_absolute_error = 0.0;
    std::vector<TraceComparisonMetrics> per_output;

    double mae() const noexcept {
        return output_samples == 0
                   ? 0.0
                   : absolute_error_sum / static_cast<double>(output_samples);
    }

    double rmse() const noexcept {
        return output_samples == 0
                   ? 0.0
                   : std::sqrt(squared_error_sum / static_cast<double>(output_samples));
    }

    double sign_agreement() const noexcept {
        return stable_reference_samples == 0
                   ? 1.0
                   : static_cast<double>(sign_matches) /
                         static_cast<double>(stable_reference_samples);
    }

    double confident_logic_agreement() const noexcept {
        return stable_reference_samples == 0
                   ? 1.0
                   : static_cast<double>(confident_matches) /
                         static_cast<double>(stable_reference_samples);
    }
};

namespace simulation_comparison_detail {

inline bool approximately_equal(double left, double right,
                                const SimulationComparisonOption &option) noexcept {
    if (!std::isfinite(left) || !std::isfinite(right)) return false;
    const double scale = std::max(std::fabs(left), std::fabs(right));
    return std::fabs(left - right) <=
           option.stimulus_absolute_tolerance + option.stimulus_relative_tolerance * scale;
}

inline std::map<std::string, const Trace *>
index_traces(const std::vector<Trace> &traces, const char *group) {
    std::map<std::string, const Trace *> indexed;
    for (const auto &trace : traces) {
        if (!indexed.emplace(trace.data_labels, &trace).second) {
            throw std::runtime_error(std::string("duplicate ") + group +
                                     " trace label: " + trace.data_labels);
        }
    }
    return indexed;
}

inline std::string validate_identical_traces(const std::vector<Trace> &reference,
                                             const std::vector<Trace> &candidate,
                                             const char *group,
                                             const SimulationComparisonOption &option) {
    std::map<std::string, const Trace *> reference_by_label;
    std::map<std::string, const Trace *> candidate_by_label;
    try {
        reference_by_label = index_traces(reference, group);
        candidate_by_label = index_traces(candidate, group);
    } catch (const std::exception &error) {
        return error.what();
    }

    if (reference_by_label.size() != candidate_by_label.size()) {
        std::ostringstream message;
        message << group << " trace count differs: reference=" << reference_by_label.size()
                << ", candidate=" << candidate_by_label.size();
        return message.str();
    }

    for (const auto &[label, reference_trace] : reference_by_label) {
        const auto candidate_it = candidate_by_label.find(label);
        if (candidate_it == candidate_by_label.end()) {
            return std::string("candidate is missing ") + group + " trace: " + label;
        }
        const Trace &candidate_trace = *candidate_it->second;
        if (reference_trace->data.size() != candidate_trace.data.size()) {
            std::ostringstream message;
            message << group << " sample count differs for '" << label
                    << "': reference=" << reference_trace->data.size()
                    << ", candidate=" << candidate_trace.data.size();
            return message.str();
        }
        for (std::size_t sample = 0; sample < reference_trace->data.size(); ++sample) {
            if (!approximately_equal(reference_trace->data[sample],
                                     candidate_trace.data[sample], option)) {
                std::ostringstream message;
                message.precision(17);
                message << group << " differs for '" << label << "' at sample " << sample
                        << ": reference=" << reference_trace->data[sample]
                        << ", candidate=" << candidate_trace.data[sample];
                return message.str();
            }
        }
    }
    return {};
}

} // namespace simulation_comparison_detail

inline SimulationComparisonMetrics compare_simulation_results(
    const Result &reference, const Result &candidate,
    const SimulationComparisonOption &option = {}) {
    SimulationComparisonMetrics metrics;

    if (!(option.logic_threshold >= 0.0) ||
        !(option.stimulus_absolute_tolerance >= 0.0) ||
        !(option.stimulus_relative_tolerance >= 0.0)) {
        metrics.incompatibility = "comparison tolerances must be non-negative";
        return metrics;
    }

    metrics.incompatibility = simulation_comparison_detail::validate_identical_traces(
        reference.inputs, candidate.inputs, "input", option);
    if (!metrics.incompatibility.empty()) return metrics;
    metrics.incompatibility = simulation_comparison_detail::validate_identical_traces(
        reference.clocks, candidate.clocks, "clock", option);
    if (!metrics.incompatibility.empty()) return metrics;

    std::map<std::string, const Trace *> reference_outputs;
    std::map<std::string, const Trace *> candidate_outputs;
    try {
        reference_outputs =
            simulation_comparison_detail::index_traces(reference.outputs, "output");
        candidate_outputs =
            simulation_comparison_detail::index_traces(candidate.outputs, "output");
    } catch (const std::exception &error) {
        metrics.incompatibility = error.what();
        return metrics;
    }
    if (reference_outputs.size() != candidate_outputs.size()) {
        std::ostringstream message;
        message << "output trace count differs: reference=" << reference_outputs.size()
                << ", candidate=" << candidate_outputs.size();
        metrics.incompatibility = message.str();
        return metrics;
    }

    for (const auto &[label, reference_trace] : reference_outputs) {
        const auto candidate_it = candidate_outputs.find(label);
        if (candidate_it == candidate_outputs.end()) {
            metrics.incompatibility = "candidate is missing output trace: " + label;
            return metrics;
        }
        const Trace &candidate_trace = *candidate_it->second;
        if (reference_trace->data.size() != candidate_trace.data.size()) {
            std::ostringstream message;
            message << "output sample count differs for '" << label
                    << "': reference=" << reference_trace->data.size()
                    << ", candidate=" << candidate_trace.data.size();
            metrics.incompatibility = message.str();
            return metrics;
        }

        TraceComparisonMetrics trace_metrics;
        trace_metrics.label = label;
        trace_metrics.samples = reference_trace->data.size();
        for (std::size_t sample = 0; sample < reference_trace->data.size(); ++sample) {
            const double reference_value = reference_trace->data[sample];
            const double candidate_value = candidate_trace.data[sample];
            if (!std::isfinite(reference_value) || !std::isfinite(candidate_value)) {
                std::ostringstream message;
                message << "non-finite output for '" << label << "' at sample " << sample;
                metrics.incompatibility = message.str();
                return metrics;
            }

            const double error = std::fabs(candidate_value - reference_value);
            trace_metrics.absolute_error_sum += error;
            trace_metrics.squared_error_sum += error * error;
            trace_metrics.maximum_absolute_error =
                std::max(trace_metrics.maximum_absolute_error, error);

            if (std::fabs(reference_value) >= option.logic_threshold) {
                ++trace_metrics.stable_reference_samples;
                const bool same_sign = std::signbit(reference_value) ==
                                       std::signbit(candidate_value);
                if (same_sign) ++trace_metrics.sign_matches;
                if (std::fabs(candidate_value) < option.logic_threshold) {
                    ++trace_metrics.weak_candidate_samples;
                } else if (same_sign) {
                    ++trace_metrics.confident_matches;
                }
            }
        }

        metrics.output_samples += trace_metrics.samples;
        metrics.stable_reference_samples += trace_metrics.stable_reference_samples;
        metrics.sign_matches += trace_metrics.sign_matches;
        metrics.confident_matches += trace_metrics.confident_matches;
        metrics.weak_candidate_samples += trace_metrics.weak_candidate_samples;
        metrics.absolute_error_sum += trace_metrics.absolute_error_sum;
        metrics.squared_error_sum += trace_metrics.squared_error_sum;
        metrics.maximum_absolute_error =
            std::max(metrics.maximum_absolute_error,
                     trace_metrics.maximum_absolute_error);
        metrics.per_output.push_back(std::move(trace_metrics));
    }

    metrics.output_traces = metrics.per_output.size();
    metrics.comparable = true;
    metrics.incompatibility.clear();
    return metrics;
}

} // namespace simon

#endif // IFCN_SIMON_SIMULATION_METRICS_HPP
