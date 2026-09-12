#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int positiveMod(int value, int mod) {
    value %= mod;
    if (value < 0) {
        value += mod;
    }
    return value;
}

void requirePhaseCount(int phaseCount) {
    if (phaseCount != 3 && phaseCount != 4) {
        throw std::invalid_argument("phase_count must be 3 or 4.");
    }
}

void requireBlockSize(int blockSize) {
    if (blockSize != 3 && blockSize != 4) {
        throw std::invalid_argument("block_size must be 3 or 4.");
    }
}

void requirePhaseValue(int value, int phaseCount) {
    if (value < 0 || value >= phaseCount) {
        throw std::runtime_error(
            "phase out of range (0.." + std::to_string(phaseCount - 1) + ")."
        );
    }
}

std::string strip0x(const std::string& raw) {
    if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) {
        return raw.substr(2);
    }
    return raw;
}

std::string cleanHex(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (char ch : strip0x(raw)) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (!std::isspace(uch)) {
            s.push_back(static_cast<char>(std::tolower(uch)));
        }
    }
    if (s.empty()) {
        s = "0";
    }
    for (char ch : s) {
        const bool isHex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        if (!isHex) {
            throw std::runtime_error("Invalid hex input.");
        }
    }
    return s;
}

std::vector<uint8_t> hexToRowBytesPadLeft(const std::string& hexInput, int blockSize) {
    std::string s = cleanHex(hexInput);
    const size_t hexDigits = static_cast<size_t>(blockSize) * 2u;
    if (s.size() < hexDigits) {
        s = std::string(hexDigits - s.size(), '0') + s;
    } else if (s.size() > hexDigits) {
        s = s.substr(s.size() - hexDigits);
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(blockSize), 0);
    for (int row = 0; row < blockSize; ++row) {
        const std::string part = s.substr(static_cast<size_t>(row) * 2u, 2);
        bytes[static_cast<size_t>(row)] = static_cast<uint8_t>(std::stoul(part, nullptr, 16));
    }
    return bytes;
}

std::vector<int> unpackRowFromByte(uint8_t rowByte, int blockSize, int phaseCount) {
    std::vector<int> row(static_cast<size_t>(blockSize), 0);
    for (int column = 0; column < blockSize; ++column) {
        const int phase = static_cast<int>((rowByte >> (2 * column)) & 0x3u);
        requirePhaseValue(phase, phaseCount);
        row[static_cast<size_t>(column)] = phase;
    }
    return row;
}

std::vector<std::vector<int>> decodePackedHexToMatrix(
    const std::string& hexInput,
    int phaseCount,
    int blockSize
) {
    requirePhaseCount(phaseCount);
    requireBlockSize(blockSize);

    const auto bytes = hexToRowBytesPadLeft(hexInput, blockSize);
    std::vector<std::vector<int>> matrix(
        static_cast<size_t>(blockSize),
        std::vector<int>(static_cast<size_t>(blockSize), 0)
    );
    for (int row = 0; row < blockSize; ++row) {
        matrix[static_cast<size_t>(row)] = unpackRowFromByte(
            bytes[static_cast<size_t>(row)],
            blockSize,
            phaseCount
        );
    }
    return matrix;
}

void validateMatrixShape(const std::vector<std::vector<int>>& matrix, int blockSize) {
    if (matrix.size() != static_cast<size_t>(blockSize)) {
        throw std::invalid_argument("matrix height does not match block_size.");
    }
    for (const auto& row : matrix) {
        if (row.size() != static_cast<size_t>(blockSize)) {
            throw std::invalid_argument("matrix width does not match block_size.");
        }
    }
}

std::string encodeMatrixToPackedHex(
    const std::vector<std::vector<int>>& matrix,
    int phaseCount,
    int blockSize
) {
    requirePhaseCount(phaseCount);
    requireBlockSize(blockSize);
    validateMatrixShape(matrix, blockSize);

    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0');
    for (int row = 0; row < blockSize; ++row) {
        uint8_t rowByte = 0;
        for (int column = 0; column < blockSize; ++column) {
            const int phase = matrix[static_cast<size_t>(row)][static_cast<size_t>(column)];
            requirePhaseValue(phase, phaseCount);
            rowByte |= static_cast<uint8_t>((phase & 0x3) << (2 * column));
        }
        oss << std::setw(2) << static_cast<int>(rowByte);
    }
    return oss.str();
}

std::vector<std::vector<int>> readPhaseGridFromFile(const std::string& path, int phaseCount) {
    std::ifstream fin(path);
    if (!fin) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    std::vector<std::vector<int>> grid;
    std::string line;
    size_t expectedWidth = 0;

    while (std::getline(fin, line)) {
        const bool allSpace = std::all_of(line.begin(), line.end(), [](char ch) {
            return std::isspace(static_cast<unsigned char>(ch)) != 0;
        });
        if (allSpace) {
            continue;
        }

        std::istringstream iss(line);
        std::vector<int> row;
        int value = 0;
        while (iss >> value) {
            requirePhaseValue(value, phaseCount);
            row.push_back(value);
        }
        if (row.empty()) {
            continue;
        }

        if (expectedWidth == 0) {
            expectedWidth = row.size();
        }
        if (row.size() != expectedWidth) {
            throw std::runtime_error("Inconsistent row width in file.");
        }
        grid.push_back(std::move(row));
    }

    if (grid.empty()) {
        throw std::runtime_error("Empty grid in file.");
    }
    return grid;
}

std::vector<std::string> encodeBlocksLeftToRightTopToBottom(
    const std::vector<std::vector<int>>& grid,
    int phaseCount,
    int blockSize
) {
    const int height = static_cast<int>(grid.size());
    const int width = static_cast<int>(grid[0].size());
    if (height % blockSize != 0 || width % blockSize != 0) {
        throw std::runtime_error("Grid size must be multiples of block_size.");
    }

    std::vector<std::string> hexList;
    for (int by = 0; by < height; by += blockSize) {
        for (int bx = 0; bx < width; bx += blockSize) {
            std::vector<std::vector<int>> block(
                static_cast<size_t>(blockSize),
                std::vector<int>(static_cast<size_t>(blockSize), 0)
            );
            for (int row = 0; row < blockSize; ++row) {
                for (int column = 0; column < blockSize; ++column) {
                    block[static_cast<size_t>(row)][static_cast<size_t>(column)] =
                        grid[static_cast<size_t>(by + row)][static_cast<size_t>(bx + column)];
                }
            }
            hexList.push_back(encodeMatrixToPackedHex(block, phaseCount, blockSize));
        }
    }
    return hexList;
}

int evaluatePolynomial(const std::vector<int>& coefficients, int x, int phaseCount) {
    int result = 0;
    int xPower = 1;
    for (int coefficient : coefficients) {
        result = positiveMod(result + coefficient * xPower, phaseCount);
        xPower = positiveMod(xPower * x, phaseCount);
    }
    return result;
}

bool fitRowPolynomial(
    const std::vector<int>& row,
    int phaseCount,
    std::vector<int>& coefficients
) {
    const int degreeCount = static_cast<int>(row.size());
    int candidateCount = 1;
    for (int i = 0; i < degreeCount; ++i) {
        candidateCount *= phaseCount;
    }

    std::vector<int> candidate(static_cast<size_t>(degreeCount), 0);
    for (int code = 0; code < candidateCount; ++code) {
        int tmp = code;
        for (int i = 0; i < degreeCount; ++i) {
            candidate[static_cast<size_t>(i)] = tmp % phaseCount;
            tmp /= phaseCount;
        }

        bool matched = true;
        for (int x = 0; x < degreeCount; ++x) {
            if (evaluatePolynomial(candidate, x, phaseCount) != row[static_cast<size_t>(x)]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            coefficients = candidate;
            return true;
        }
    }
    return false;
}

std::string polynomialToString(const std::vector<int>& coefficients) {
    std::ostringstream oss;
    bool first = true;
    for (int i = static_cast<int>(coefficients.size()) - 1; i >= 0; --i) {
        const int coefficient = coefficients[static_cast<size_t>(i)];
        if (coefficient == 0) {
            continue;
        }
        if (!first) {
            oss << "+";
        }
        if (coefficient != 1 || i == 0) {
            oss << coefficient;
        }
        if (i > 0) {
            oss << "x";
        }
        if (i > 1) {
            oss << "^" << i;
        }
        first = false;
    }
    if (first) {
        oss << "0";
    }
    return oss.str();
}

void printMatrix(const std::vector<std::vector<int>>& matrix) {
    for (const auto& row : matrix) {
        for (int value : row) {
            std::cout << value << " ";
        }
        std::cout << "\n";
    }
}

void printRowPolynomials(const std::vector<std::vector<int>>& matrix, int phaseCount) {
    for (size_t rowIndex = 0; rowIndex < matrix.size(); ++rowIndex) {
        std::vector<int> coefficients;
        std::cout << "Row " << (rowIndex + 1) << ": ";
        if (!fitRowPolynomial(matrix[rowIndex], phaseCount, coefficients)) {
            std::cout << "not representable as degree<" << matrix[rowIndex].size()
                      << " polynomial over Z" << phaseCount << "\n";
            continue;
        }
        std::cout << polynomialToString(coefficients) << "  (";
        for (size_t i = 0; i < coefficients.size(); ++i) {
            if (i > 0) {
                std::cout << ", ";
            }
            std::cout << "c" << i << "=" << coefficients[i];
        }
        std::cout << ")\n";
    }
}

int promptInt(const std::string& prompt) {
    std::cout << prompt;
    int value = 0;
    if (!(std::cin >> value)) {
        throw std::runtime_error("Failed to read integer input.");
    }
    return value;
}

std::string promptString(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    if (!(std::cin >> value)) {
        throw std::runtime_error("Failed to read string input.");
    }
    return value;
}

void printUsage(const char* programName) {
    std::cout
        << "Usage:\n"
        << "  " << programName << " [--phase-count 3|4] [--block-size 3|4] [--mode 1|2|3|4]\n"
        << "  " << programName << " --phase-count 3 --block-size 3 --mode 1 --hex 0x...\n"
        << "  " << programName << " --phase-count 3 --block-size 3 --mode 3 --file phase_grid.txt\n\n"
        << "Modes:\n"
        << "  1  packed hex -> phase matrix\n"
        << "  2  phase matrix -> packed hex\n"
        << "  3  file grid -> packed hex per block\n"
        << "  4  validate and count encodable file blocks\n\n"
        << "Packing format keeps the existing 2-bit-per-cell hex layout. For 3-phase mode,\n"
        << "phase value 3 is invalid and rejected.\n";
}

struct Options {
    int phaseCount = 4;
    int blockSize = 4;
    int mode = 0;
    std::string hexInput;
    std::string filePath;
    bool showHelp = false;
};

Options parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(name + " requires a value.");
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
        } else if (arg == "--phase-count" || arg == "-p") {
            options.phaseCount = std::stoi(requireValue(arg));
        } else if (arg == "--block-size" || arg == "-b") {
            options.blockSize = std::stoi(requireValue(arg));
        } else if (arg == "--mode" || arg == "-m") {
            options.mode = std::stoi(requireValue(arg));
        } else if (arg == "--hex") {
            options.hexInput = requireValue(arg);
        } else if (arg == "--file") {
            options.filePath = requireValue(arg);
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }
    return options;
}

void readInteractiveOptions(Options& options) {
    options.phaseCount = promptInt("Phase count? (3 or 4): ");
    options.blockSize = promptInt("Block size? (3 or 4, default old format is 4): ");
    options.mode = promptInt(
        "Mode? (1 = hex -> matrix, 2 = matrix -> hex, "
        "3 = file(grid) -> hex per block, 4 = validate file blocks): "
    );
}

void runDecodeMode(Options& options) {
    if (options.hexInput.empty()) {
        options.hexInput = promptString("Enter a packed hex value (e.g., 0xD20244D2): ");
    }

    const auto matrix = decodePackedHexToMatrix(
        options.hexInput,
        options.phaseCount,
        options.blockSize
    );
    std::cout << "\nClock Phase Matrix (" << options.phaseCount << "-phase, "
              << options.blockSize << "x" << options.blockSize << ") [pack decoded]:\n";
    printMatrix(matrix);

    std::cout << "\nRow polynomials over Z" << options.phaseCount
              << " [best-effort fit]:\n";
    printRowPolynomials(matrix, options.phaseCount);
}

void runEncodeMode(const Options& options) {
    std::vector<std::vector<int>> matrix(
        static_cast<size_t>(options.blockSize),
        std::vector<int>(static_cast<size_t>(options.blockSize), 0)
    );

    std::cout << "Enter " << (options.blockSize * options.blockSize)
              << " integers (0.." << (options.phaseCount - 1)
              << ") in row-major order for a " << options.blockSize
              << "x" << options.blockSize << " matrix:\n";
    for (int row = 0; row < options.blockSize; ++row) {
        for (int column = 0; column < options.blockSize; ++column) {
            if (!(std::cin >> matrix[static_cast<size_t>(row)][static_cast<size_t>(column)])) {
                throw std::runtime_error("Failed to read matrix value.");
            }
        }
    }

    const std::string hexCode = encodeMatrixToPackedHex(
        matrix,
        options.phaseCount,
        options.blockSize
    );
    std::cout << "\nHex code: 0x" << hexCode << "\n";
}

void runFileEncodeMode(Options& options) {
    if (options.filePath.empty()) {
        options.filePath = promptString("Enter filepath (e.g., filler_phase.txt): ");
    }

    const auto grid = readPhaseGridFromFile(options.filePath, options.phaseCount);
    const int height = static_cast<int>(grid.size());
    const int width = static_cast<int>(grid[0].size());
    if (height % options.blockSize != 0 || width % options.blockSize != 0) {
        throw std::runtime_error("rows/cols must be multiples of block_size.");
    }
    std::cout << "Loaded grid: " << height << " x " << width << "\n";

    const auto hexBlocks = encodeBlocksLeftToRightTopToBottom(
        grid,
        options.phaseCount,
        options.blockSize
    );

    const int blocksPerRow = width / options.blockSize;
    for (size_t i = 0; i < hexBlocks.size(); ++i) {
        const int by = (static_cast<int>(i) / blocksPerRow) * options.blockSize;
        const int bx = (static_cast<int>(i) % blocksPerRow) * options.blockSize;
        std::cout << "[row=" << by << ", col=" << bx << "] 0x" << hexBlocks[i] << "\n";
    }
}

void runValidationMode(Options& options) {
    if (options.filePath.empty()) {
        options.filePath = promptString("Enter filepath (e.g., filler_phase.txt): ");
    }

    const auto grid = readPhaseGridFromFile(options.filePath, options.phaseCount);
    const int height = static_cast<int>(grid.size());
    const int width = static_cast<int>(grid[0].size());
    if (height % options.blockSize != 0 || width % options.blockSize != 0) {
        throw std::runtime_error("rows/cols must be multiples of block_size.");
    }
    std::cout << "Loaded grid: " << height << " x " << width << "\n";

    int totalBlocks = 0;
    int packSuccess = 0;
    int packFail = 0;
    for (int by = 0; by < height; by += options.blockSize) {
        for (int bx = 0; bx < width; bx += options.blockSize) {
            std::vector<std::vector<int>> block(
                static_cast<size_t>(options.blockSize),
                std::vector<int>(static_cast<size_t>(options.blockSize), 0)
            );
            for (int row = 0; row < options.blockSize; ++row) {
                for (int column = 0; column < options.blockSize; ++column) {
                    block[static_cast<size_t>(row)][static_cast<size_t>(column)] =
                        grid[static_cast<size_t>(by + row)][static_cast<size_t>(bx + column)];
                }
            }

            try {
                (void)encodeMatrixToPackedHex(block, options.phaseCount, options.blockSize);
                ++packSuccess;
            } catch (const std::exception&) {
                ++packFail;
            }
            ++totalBlocks;
        }
    }

    std::cout << "Total " << options.blockSize << "x" << options.blockSize
              << " blocks: " << totalBlocks << "\n";
    std::cout << "Pack success: " << packSuccess << "\n";
    std::cout << "Pack fail:    " << packFail << "\n";
    if (totalBlocks > 0) {
        std::cout << "Pack success rate: "
                  << (100.0 * static_cast<double>(packSuccess) / static_cast<double>(totalBlocks))
                  << " %\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options options = parseArgs(argc, argv);
        if (options.showHelp) {
            printUsage(argv[0]);
            return 0;
        }

        if (argc == 1) {
            readInteractiveOptions(options);
        } else if (options.mode == 0) {
            if (!options.hexInput.empty()) {
                options.mode = 1;
            } else if (!options.filePath.empty()) {
                options.mode = 3;
            }
        }

        requirePhaseCount(options.phaseCount);
        requireBlockSize(options.blockSize);

        switch (options.mode) {
            case 1:
                runDecodeMode(options);
                break;
            case 2:
                runEncodeMode(options);
                break;
            case 3:
                runFileEncodeMode(options);
                break;
            case 4:
                runValidationMode(options);
                break;
            default:
                printUsage(argv[0]);
                throw std::invalid_argument("Unknown or missing mode.");
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
