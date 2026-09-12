// Headless access to the production C++ combinational layout algorithms.
// Emits a candidate snapshot; the Python adapter independently validates and
// exports it before making it available to downstream device mapping.
#include <autopr/algorithms/astar.h>
#include <autopr/algorithms/mapping.h>
#include <autopr/algorithms/combinationalValidation.h>
#include <autopr/algorithms/irregularLayout.h>
#include <autopr/graph/circuitGraph.h>
#include <autopr/graph/parse.h>
#include <autopr/grid/grid.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
using namespace fcngraph;
std::string quote(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 32) throw std::runtime_error("Unsupported control character in circuit name");
        else out << c;
    }
    out << '"'; return out.str();
}
void diagnostics(std::ostream& out, const IrregularLayoutResult& result) {
    out << ",\"selected_seed\":" << quote(result.selectedSeed)
        << ",\"search_budget_expired\":" << (result.budgetExpired ? "true" : "false")
        << ",\"objective\":\"occupied_tile_area_then_physical_cells_then_route_length\""
        << ",\"metrics\":{\"width\":" << result.metrics.width
        << ",\"height\":" << result.metrics.height << ",\"area_tiles\":" << result.metrics.area
        << ",\"physical_cells\":" << result.metrics.physicalCells << "},\"attempts\":[";
    bool first = true;
    for (const auto& attempt : result.attempts) {
        if (!first) out << ',';
        first = false;
        out << "{\"seed\":" << quote(attempt.seed) << ",\"valid\":" << (attempt.valid ? "true" : "false")
            << ",\"budget_expired\":" << (attempt.budgetExpired ? "true" : "false")
            << ",\"seconds\":" << attempt.elapsedSeconds << ",\"area_tiles\":" << attempt.metrics.area
            << ",\"physical_cells\":" << attempt.metrics.physicalCells << ",\"error\":" << quote(attempt.error) << '}';
    }
    out << ']';
}
void snapshot(std::ostream& out, Parse& parse, CircuitGraph& graph,
              const GridChessboard& board, const std::vector<std::vector<int>>& layers,
              const std::string& algorithm, bool routed, const std::string& validationError,
              double elapsed, const IrregularLayoutResult& result) {
    out << "{\n\"schema\":\"ifcn.native_candidate.v1\",\"algorithm\":" << quote(algorithm)
        << ",\"routed\":" << (routed?"true":"false") << ",\"native_mapping_valid\":" << (validationError.empty()&&routed?"true":"false")
        << ",\"native_validation_error\":" << quote(validationError) << ",\"run_time_s\":" << elapsed << ",\"layers\":[";
    bool first=true;
    for(const auto& layer:layers) { if(!first) out<<','; first=false; out<<'['; bool firstNode=true; for(int id:layer) {if(!firstNode)out<<',';firstNode=false;out<<id;}out<<']'; }
    out << "],\"nodes\":["; first=true;
    for(const auto& node:graph.nodeIndex_pos) {
        if(!first)out<<',';first=false;
        out << "{\"id\":" << node.first << ",\"name\":" << quote(parse.getNodeName(node.first)) << ",\"type\":" << quote(parse.getNodeType(node.first))
            << ",\"is_input\":" << (parse.getNodeType(node.first)=="input"?"true":"false")
            << ",\"is_output\":" << (parse.getOutputNodesIndex().count(node.first)?"true":"false")
            << ",\"x\":" << node.second.first << ",\"y\":" << node.second.second << '}';
    }
    out << "],\"edges\":["; first=true;
    for(auto edge:parse.getEffectiveEdges()) {if(!first)out<<',';first=false;out<<'['<<edge.first<<','<<edge.second<<']';}
    out << "],\"routes\":["; first=true;
    for(const auto& route:graph.routes) {
        if(!first)out<<',';first=false;
        out << "{\"source\":" << route.first.first << ",\"target\":" << route.first.second << ",\"path\":[";
        bool firstPoint=true; for(auto p:route.second) {if(!firstPoint)out<<',';firstPoint=false;out<<'['<<p.first<<','<<p.second<<']';}out<<"]}";
    }
    out << "],\"cells\":["; first=true;
    for(const auto& entry:board.gridMap) {
        if(entry.second.get_current_weight()==0)continue;
        if(!first)out<<',';first=false;
        out << "{\"x\":" << entry.first.first << ",\"y\":" << entry.first.second << ",\"phase\":" << entry.second.getPhase() << '}';
    }
    out << ']';
    diagnostics(out, result);
    out << "}\n";
    if(!out) throw std::runtime_error("Failed writing native candidate snapshot");
}

void writeExclusiveSnapshot(const std::filesystem::path& output, const std::string& contents) {
    const auto temporary = std::filesystem::path(output.string() + ".part");
    const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor < 0)
        throw std::runtime_error("Cannot create candidate snapshot without overwriting: " +
                                 output.string() + ": " + std::strerror(errno));
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto count = ::write(descriptor, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            const std::string error = std::strerror(errno);
            ::close(descriptor);
            std::filesystem::remove(temporary);
            throw std::runtime_error("Failed writing candidate snapshot: " + error);
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(descriptor) != 0) {
        const std::string error = std::strerror(errno);
        std::filesystem::remove(temporary);
        throw std::runtime_error("Failed closing candidate snapshot: " + error);
    }
    // Publish only complete JSON files. A hard link atomically refuses an
    // existing destination, unlike rename(), which may replace one.
    try {
        std::filesystem::create_hard_link(temporary, output);
    } catch (...) {
        std::filesystem::remove(temporary);
        throw;
    }
    std::filesystem::remove(temporary);
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 4 || std::string(argv[1]) != "irregular") {
            std::cerr << "Usage: ifcn_combinational_pnr irregular <input.v> <candidate.json> [--budget seconds] [--attempts count] [--candidates-dir directory]\n";
            return 2;
        }
        IrregularLayoutOptions options;
        std::filesystem::path candidatesDirectory;
        for (int i = 4; i < argc; ++i) {
            const std::string flag = argv[i];
            if (++i == argc) throw std::runtime_error("Missing value for " + flag);
            std::size_t parsed = 0;
            const std::string value = argv[i];
            if (flag == "--candidates-dir") {
                if (!candidatesDirectory.empty() || value.empty())
                    throw std::runtime_error("--candidates-dir requires one nonempty directory path");
                candidatesDirectory = std::filesystem::absolute(value).lexically_normal();
                continue;
            }
            if (flag == "--budget") options.timeBudgetSeconds = std::stod(value, &parsed);
            else if (flag == "--attempts") options.maxAttempts = std::stoi(value, &parsed);
            else throw std::runtime_error("Unknown option: " + flag);
            if (parsed != value.size()) throw std::runtime_error("Invalid numeric value for " + flag);
        }
        const auto started = std::chrono::steady_clock::now();
        std::size_t candidateNumber = 0;
        if (!candidatesDirectory.empty()) {
            if (std::filesystem::exists(candidatesDirectory)) {
                if (!std::filesystem::is_directory(candidatesDirectory) ||
                    !std::filesystem::is_empty(candidatesDirectory))
                    throw std::runtime_error("Candidate directory must be empty; refusing to overwrite: " +
                                             candidatesDirectory.string());
            } else {
                std::filesystem::create_directories(candidatesDirectory);
            }
            candidatesDirectory = std::filesystem::weakly_canonical(candidatesDirectory);
            const auto finalPath = std::filesystem::weakly_canonical(std::filesystem::absolute(argv[3]));
            if (finalPath.parent_path() == candidatesDirectory &&
                finalPath.filename().string().rfind("candidate_", 0) == 0)
                throw std::runtime_error("Final output conflicts with reserved candidate snapshot filenames");
            options.onCandidate = [&](Parse& parse, const GraphDrawCandidate& candidate,
                                      const CombinationalLayoutMetrics& metrics, const std::string& seed) {
                if (!metrics.valid) throw std::runtime_error("Observer received an invalid candidate");
                GridChessboard board;
                Astar router(board, false, 240.0);
                CircuitGraph graph(parse, argv[2], board, router);
                graph.nodeIndex_pos = candidate.nodePositions;
                graph.routes = candidate.routes;
                board.gridMap = candidate.gridCells;
                std::vector<std::vector<int>> layers;
                for (const auto& layer : parse.getlayerNodeDivVec()) layers.emplace_back(layer.begin(), layer.end());
                IrregularLayoutResult observation;
                observation.success = true;
                observation.selectedSeed = seed;
                observation.metrics = metrics;
                observation.elapsedSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count();
                std::ostringstream data;
                snapshot(data, parse, graph, board, layers, "irregular", true, {},
                         observation.elapsedSeconds, observation);
                std::ostringstream name;
                name << "candidate_" << std::setw(6) << std::setfill('0') << ++candidateNumber << ".json";
                writeExclusiveSnapshot(candidatesDirectory / name.str(), data.str());
            };
        }
        const auto result = searchIrregularLayout(argv[2], options,
            [](const GraphDrawSearchProgress& progress) {
                if (progress.improved) std::cerr << progress.message << '\n';
            });
        if (!result.success || !result.parse) {
            std::ofstream out(argv[3]);
            if (!out) throw std::runtime_error("Cannot write failure diagnostic");
            out << "{\"schema\":\"ifcn.native_candidate.v1\",\"algorithm\":\"irregular\","
                << "\"routed\":false,\"native_mapping_valid\":false,\"native_validation_error\":"
                << quote(result.error) << ",\"run_time_s\":" << result.elapsedSeconds
                << ",\"layers\":[],\"nodes\":[],\"edges\":[],\"routes\":[],\"cells\":[]";
            diagnostics(out, result);
            out << "}\n";
            std::cerr << result.error << '\n';
            return 10;
        }
        auto& parse = *result.parse;
        GridChessboard board;
        Astar router(board, false, 240.0);
        CircuitGraph graph(parse, argv[2], board, router);
        graph.nodeIndex_pos = result.layout.nodePositions;
        graph.routes = result.layout.routes;
        board.gridMap = result.layout.gridCells;
        std::vector<std::vector<int>> layers;
        for (const auto& layer : parse.getlayerNodeDivVec()) layers.emplace_back(layer.begin(), layer.end());
        const auto validation = validateCombinationalLayout(parse, graph, 4, 4);
        std::ofstream out(argv[3]);
        if (!out) throw std::runtime_error("Cannot write native candidate snapshot");
        snapshot(out, parse, graph, board, layers, "irregular", result.success,
                 validation.error, result.elapsedSeconds, result);
        if (!validation.valid) { std::cerr << validation.error << '\n'; return 10; }
        std::cout << "IFCN_NATIVE_CANDIDATE_READY\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
