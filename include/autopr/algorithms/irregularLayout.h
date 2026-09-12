#pragma once

#include "graphDrawSearch.h"
#include "combinationalValidation.h"
#include <memory>

namespace fcngraph {
class Parse;

// Borrowed synchronous observation of a DRC-clean candidate. Parse is mutable
// only because its legacy query API is non-const; observers must not modify it
// or retain references after the call. Observation does not change ranking.
using IrregularCandidateCallback = std::function<void(
    Parse&, const GraphDrawCandidate&, const CombinationalLayoutMetrics&, const std::string& seed)>;

struct IrregularLayoutOptions {
    int phaseCount = 4;
    int maxAttempts = 320;
    double timeBudgetSeconds = 120.0;
    IrregularCandidateCallback onCandidate;
};

struct IrregularLayoutAttempt {
    std::string seed;
    bool valid = false;
    bool budgetExpired = false;
    double elapsedSeconds = 0.0;
    CombinationalLayoutMetrics metrics;
    std::string error;
};

struct IrregularLayoutResult {
    bool success = false;
    bool budgetExpired = false;
    std::string error;
    std::string selectedSeed;
    std::shared_ptr<Parse> parse;
    GraphDrawCandidate layout;
    CombinationalLayoutMetrics metrics;
    std::vector<IrregularLayoutAttempt> attempts;
    double elapsedSeconds = 0.0;
};

// One irregular-clock P&R entry point for GUI, CLI and benchmarks. Placement
// seeds share clock closure, exact physical DRC, area ranking and refinement.
// Only validated candidates compete; expiration retains the best completed one.
IrregularLayoutResult searchIrregularLayout(
    const std::string& sourceFile,
    const IrregularLayoutOptions& options = {},
    const GraphDrawSearchCallback& progress = {});
}
