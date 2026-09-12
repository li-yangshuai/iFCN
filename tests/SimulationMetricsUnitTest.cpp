#include <simon/SimulationMetrics.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

simon::Trace make_trace(std::string label, std::vector<double> data) {
    simon::Trace trace;
    trace.data_labels = std::move(label);
    trace.data = std::move(data);
    return trace;
}

simon::Result make_reference() {
    simon::Result result;
    result.inputs.push_back(make_trace("A", {-1.0, 1.0, -1.0}));
    result.inputs.push_back(make_trace("B", {-1.0, -1.0, 1.0}));
    result.outputs.push_back(make_trace("Y", {1.0, -1.0, 0.05}));
    result.clocks.push_back(make_trace("Clock 0", {3.8e-23, 9.8e-22, 3.8e-23}));
    return result;
}

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

void require_close(double actual, double expected, const std::string &message) {
    if (std::fabs(actual - expected) > 1e-12) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual) +
                                 ", expected=" + std::to_string(expected));
    }
}

} // namespace

int main() {
    try {
        const auto reference = make_reference();

        auto identical = reference;
        std::swap(identical.inputs[0], identical.inputs[1]);
        const auto exact = simon::compare_simulation_results(reference, identical);
        require(exact.comparable, exact.incompatibility);
        require_close(exact.mae(), 0.0, "exact MAE");
        require_close(exact.rmse(), 0.0, "exact RMSE");
        require_close(exact.maximum_absolute_error, 0.0, "exact max error");
        require_close(exact.sign_agreement(), 1.0, "exact sign agreement");
        require_close(exact.confident_logic_agreement(), 1.0,
                      "exact confident agreement");

        auto perturbed = reference;
        perturbed.outputs[0].data = {0.5, 0.05, -0.05};
        const auto measured = simon::compare_simulation_results(reference, perturbed);
        require(measured.comparable, measured.incompatibility);
        require(measured.output_samples == 3, "output sample count");
        require(measured.stable_reference_samples == 2, "stable sample count");
        require(measured.sign_matches == 1, "sign match count");
        require(measured.confident_matches == 1, "confident match count");
        require(measured.weak_candidate_samples == 1, "weak candidate count");
        require_close(measured.mae(), 0.55, "perturbed MAE");
        require_close(measured.rmse(), std::sqrt((0.25 + 1.1025 + 0.01) / 3.0),
                      "perturbed RMSE");
        require_close(measured.maximum_absolute_error, 1.05, "perturbed max error");
        require_close(measured.sign_agreement(), 0.5, "perturbed sign agreement");
        require_close(measured.confident_logic_agreement(), 0.5,
                      "perturbed confident agreement");

        auto unfair = reference;
        unfair.inputs[0].data[1] = -1.0;
        const auto rejected = simon::compare_simulation_results(reference, unfair);
        require(!rejected.comparable, "different input waveform must be rejected");
        require(rejected.incompatibility.find("input differs") != std::string::npos,
                "input mismatch explanation");

        std::cout << "simulation metric tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "simulation metric tests failed: " << error.what() << '\n';
        return 1;
    }
}
