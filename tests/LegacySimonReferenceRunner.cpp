// This source is intentionally compiled against a frozen external simon
// include directory, never against the in-tree accelerated implementation.
#include <simon/simon.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    try {
        if (argc != 6) {
            throw std::runtime_error(
                    "usage: legacy_runner design.qca output-prefix "
                    "bistable-samples coherence-duration coherence-time-step");
        }
        const std::string design_path = argv[1];
        const std::string output_prefix = argv[2];

        {
            simon::QCADesign design;
            if (!simon::parse_design(design_path, design)) {
                throw std::runtime_error("cannot parse design for bistable run");
            }
            simon::QCABistableOption option;
            option.number_of_samples = std::stoull(argv[3]);
            simon::VectorTable vectors;
            simon::Result result;
            simon::BistableAlgorithm algorithm(option);
            algorithm.run(design, vectors, result,
                          simon::SimulationMode::Exhaustive);
            result.write_text_file(output_prefix + "_bistable_legacy.rst");
        }

        {
            simon::QCADesign design;
            if (!simon::parse_design(design_path, design)) {
                throw std::runtime_error("cannot parse design for coherence run");
            }
            simon::QCACoherenceOption option;
            option.duration = std::stod(argv[4]);
            option.time_step = std::stod(argv[5]);
            simon::VectorTable vectors;
            simon::Result result;
            simon::CoherenceAlgorithm algorithm(option);
            algorithm.run(design, vectors, result,
                          simon::SimulationMode::Exhaustive);
            result.write_text_file(output_prefix + "_coherence_legacy.rst");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
