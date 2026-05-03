#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fcngraph::phase_codec {

using PhaseMatrix = std::vector<std::vector<int>>;
using PhaseCoord = std::pair<unsigned int, unsigned int>;

struct EncodedPhaseTile {
    unsigned int tileX = 0;
    unsigned int tileY = 0;
    std::string hex;
};

PhaseMatrix decodePackedHexToMatrix(const std::string &hexInput,
                                    int phaseCount,
                                    int blockSize);

std::string encodeMatrixToPackedHex(const PhaseMatrix &matrix,
                                    int phaseCount,
                                    int blockSize);

std::vector<EncodedPhaseTile> encodePhaseMapToTiles(const std::map<PhaseCoord, int> &phaseMap,
                                                    int phaseCount,
                                                    int blockSize,
                                                    int gridWidth = 0,
                                                    int gridHeight = 0);

} // namespace fcngraph::phase_codec
