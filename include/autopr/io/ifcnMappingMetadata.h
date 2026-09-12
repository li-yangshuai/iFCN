#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <autopr/algorithms/mapping.h>

namespace fcngraph {

struct IfcnMappingModeResolution
{
    MappingMode mode = MappingMode::Combinational;
    bool explicitMode = false;
    bool inferredFromIterationDistance = false;
    bool inferredFromLegacyFlow = false;
};

inline const char *mappingModeName(MappingMode mode)
{
    return mode == MappingMode::Sequential ? "sequential" : "combinational";
}

class IfcnMappingModeResolver
{
public:
    void observeModeValue(std::string value)
    {
        value = normalize(std::move(value));
        MappingMode parsedMode;
        if (value == "sequential") {
            parsedMode = MappingMode::Sequential;
        } else if (value == "combinational") {
            parsedMode = MappingMode::Combinational;
        } else {
            throw std::runtime_error("unsupported IFCN mapping mode: " + value);
        }

        if (explicitMode_.has_value() && *explicitMode_ != parsedMode) {
            throw std::runtime_error("conflicting IFCN mapping mode declarations");
        }
        explicitMode_ = parsedMode;
    }

    void observeFlowValue(std::string value)
    {
        value = normalize(std::move(value));
        if (value == "sequential register-cut p&r v0" ||
            value == "sampled-state physical-feedback p&r experiment v0") {
            legacySequentialFlow_ = true;
        }
    }

    void observeIterationDistance(unsigned long long distance)
    {
        positiveIterationDistance_ = positiveIterationDistance_ || distance > 0;
    }

    bool hasExplicitMode() const noexcept
    {
        return explicitMode_.has_value();
    }

    IfcnMappingModeResolution resolve() const
    {
        IfcnMappingModeResolution result;
        if (explicitMode_.has_value()) {
            result.mode = *explicitMode_;
            result.explicitMode = true;
            if (result.mode == MappingMode::Combinational &&
                positiveIterationDistance_) {
                throw std::runtime_error(
                    "combinational IFCN declares a positive iteration distance");
            }
            return result;
        }

        if (positiveIterationDistance_) {
            result.mode = MappingMode::Sequential;
            result.inferredFromIterationDistance = true;
        } else if (legacySequentialFlow_) {
            result.mode = MappingMode::Sequential;
            result.inferredFromLegacyFlow = true;
        }
        return result;
    }

private:
    static std::string normalize(std::string value)
    {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }).base();
        if (first >= last) {
            return {};
        }
        value = std::string(first, last);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::optional<MappingMode> explicitMode_;
    bool positiveIterationDistance_ = false;
    bool legacySequentialFlow_ = false;
};

} // namespace fcngraph
