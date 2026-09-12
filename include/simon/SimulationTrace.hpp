// simon: C++ FCN network simulation framework
// Deterministic certificates for complete internal physical-simulation traces.

#ifndef HFUT_SIMON_SIMULATION_TRACE_HPP
#define HFUT_SIMON_SIMULATION_TRACE_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace simon {

enum class SimulationTraceFrame : std::uint64_t {
    BistableSweep = 1,
    CoherenceSteadyStateSweep = 2,
    CoherenceTimeStep = 3,
};

/**
 * Streaming certificate over internal simulation states.
 *
 * Every double is hashed by its exact IEEE-754 object representation.  Tests
 * may additionally retain all words for collision-free direct comparison on
 * small and moderate designs.  Formal runs that retain the words compare the
 * complete sequence directly; the digest is then only a compact identifier.
 */
class SimulationTraceRecorder {
public:
    explicit SimulationTraceRecorder(bool retain_words = false)
        : retain_words_(retain_words) {
        reset();
    }

    void reset() {
        hash_a_ = UINT64_C(1469598103934665603);
        hash_b_ = UINT64_C(0x9e3779b97f4a7c15);
        frame_count_ = 0;
        value_count_ = 0;
        words_.clear();
    }

    void begin_frame(SimulationTraceFrame frame,
                     std::size_t primary_index,
                     std::size_t secondary_index = 0) {
        append_word(UINT64_C(0x4946434e4652414d)); // "IFCNFRAM"
        append_word(static_cast<std::uint64_t>(frame));
        append_word(static_cast<std::uint64_t>(primary_index));
        append_word(static_cast<std::uint64_t>(secondary_index));
        ++frame_count_;
    }

    void record(double value) {
        static_assert(sizeof(double) == sizeof(std::uint64_t),
                      "trace certificates require 64-bit doubles");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        append_word(bits);
        ++value_count_;
    }

    void end_frame() {
        append_word(UINT64_C(0x4946434e454e4421)); // "IFCNEND!"
    }

    std::size_t frame_count() const noexcept { return frame_count_; }
    std::size_t value_count() const noexcept { return value_count_; }
    std::uint64_t hash_a() const noexcept { return hash_a_; }
    std::uint64_t hash_b() const noexcept { return hash_b_; }

    const std::vector<std::uint64_t> &words() const noexcept { return words_; }

    bool equivalent_to(const SimulationTraceRecorder &other) const noexcept {
        if (frame_count_ != other.frame_count_ ||
            value_count_ != other.value_count_ ||
            hash_a_ != other.hash_a_ || hash_b_ != other.hash_b_) {
            return false;
        }
        if (retain_words_ && other.retain_words_) return words_ == other.words_;
        return true;
    }

    std::string digest() const {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0')
               << std::setw(16) << hash_a_
               << std::setw(16) << hash_b_;
        return stream.str();
    }

private:
    void append_word(std::uint64_t word) {
        // FNV-1a plus an independent avalanche-style stream.  The optional
        // retained-word mode is used by unit tests when a digest alone is not
        // considered sufficient evidence.
        hash_a_ ^= word;
        hash_a_ *= UINT64_C(1099511628211);

        hash_b_ ^= word + UINT64_C(0x9e3779b97f4a7c15) +
                   (hash_b_ << 6) + (hash_b_ >> 2);
        hash_b_ ^= hash_b_ >> 30;
        hash_b_ *= UINT64_C(0xbf58476d1ce4e5b9);
        hash_b_ ^= hash_b_ >> 27;
        hash_b_ *= UINT64_C(0x94d049bb133111eb);
        hash_b_ ^= hash_b_ >> 31;

        if (retain_words_) words_.push_back(word);
    }

    bool retain_words_ = false;
    std::uint64_t hash_a_ = 0;
    std::uint64_t hash_b_ = 0;
    std::size_t frame_count_ = 0;
    std::size_t value_count_ = 0;
    std::vector<std::uint64_t> words_;
};

} // namespace simon

#endif // HFUT_SIMON_SIMULATION_TRACE_HPP
