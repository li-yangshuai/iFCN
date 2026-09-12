#pragma once

#include <algorithm>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <autopr/algorithms/mapping.h>

namespace fcngraph {

// Node type records the Boolean gate; output membership is an independent
// interface property because a primary output may also drive another gate.
class IfcnPrimaryOutputMetadata
{
public:
    bool observeLine(const std::string &line)
    {
        static const std::regex key(R"(^\s*#\s*primary\s+output\s+nodes\b.*$)", std::regex::icase);
        static const std::regex value(R"(^\s*#\s*primary\s+output\s+nodes\s*:\s*(\d+(?:\s*,\s*\d+)*)\s*$)", std::regex::icase);
        if (!std::regex_match(line, key)) return false;
        if (explicit_) throw std::runtime_error("duplicate IFCN primary output declaration");
        std::smatch match;
        if (!std::regex_match(line, match, value))
            throw std::runtime_error("malformed IFCN primary output nodes declaration");
        const std::string values = match[1].str();
        static const std::regex number(R"(\d+)");
        for (auto it = std::sregex_iterator(values.begin(), values.end(), number);
             it != std::sregex_iterator(); ++it) {
            const auto id = std::stoull(it->str());
            if (id > static_cast<unsigned long long>(std::numeric_limits<int>::max()) ||
                !nodes_.insert(static_cast<int>(id)).second)
                throw std::runtime_error("duplicate or out-of-range IFCN primary output node ID");
        }
        explicit_ = true;
        return true;
    }

    bool isExplicit() const noexcept { return explicit_; }
    const std::set<int> &nodes() const noexcept { return nodes_; }

private:
    bool explicit_ = false;
    std::set<int> nodes_;
};

struct IfcnObservationNode
{
    std::string name;
    std::string type;
    position pos{0, 0};
};

// Select existing layer-zero observation sites without changing a gate
// template, routing, clocking, or cell count. Existing terminal cells retain
// their positions, including an explicitly contracted I/O terminal.
inline std::map<position, std::string> resolveIfcnPrimaryOutputCells(
    const IfcnPrimaryOutputMetadata &metadata,
    const std::map<int, IfcnObservationNode> &nodes,
    const std::map<std::pair<int, int>, std::vector<position>> &routes,
    const Mapping &mapping)
{
    std::map<position, std::string> result;
    const auto &nodeCells = mapping.nodecell_list;
    for (const int id : metadata.nodes()) {
        const auto nodeIt = nodes.find(id);
        if (nodeIt == nodes.end())
            throw std::runtime_error("IFCN primary output references unknown node " + std::to_string(id));
        const auto &node = nodeIt->second;
        if (node.type == "input")
            throw std::runtime_error("IFCN primary output cannot replace a driven input cell");
        std::vector<position> terminals;
        const auto outputs = nodeCells.find("output");
        if (outputs != nodeCells.end()) {
            for (const auto cell : outputs->second) {
                position owner{cell.first / 5, cell.second / 5};
                const auto origin = mapping.io_terminal_origins().find(cell);
                if (origin != mapping.io_terminal_origins().end()) owner = origin->second;
                if (owner == node.pos) terminals.push_back(cell);
            }
        }
        position cell;
        if (terminals.size() == 1) {
            cell = terminals.front();
        } else if (!terminals.empty()) {
            throw std::runtime_error("ambiguous mapped terminal for IFCN primary output " + node.name);
        } else {
            const auto route = std::find_if(routes.begin(), routes.end(), [&](const auto &entry) {
                return entry.first.first == id && entry.second.size() >= 2;
            });
            if (route == routes.end())
                throw std::runtime_error("IFCN primary output has no mapped terminal or outgoing route: " + node.name);
            const auto next = route->second[1];
            cell = {node.pos.first * 5U + 2U, node.pos.second * 5U + 2U};
            if (next.first < node.pos.first) cell.first -= 2;
            else if (next.first > node.pos.first) cell.first += 2;
            else if (next.second < node.pos.second) cell.second -= 2;
            else if (next.second > node.pos.second) cell.second += 2;
            else throw std::runtime_error("IFCN primary output route has no launch direction");
        }
        for (const auto *kind : {"input", "fix0", "fix1"}) {
            const auto list = nodeCells.find(kind);
            if (list != nodeCells.end() &&
                std::find(list->second.begin(), list->second.end(), cell) != list->second.end())
                throw std::runtime_error("IFCN primary output observation cell is not passive: " + node.name);
        }
        if (!result.emplace(cell, node.name).second)
            throw std::runtime_error("multiple IFCN primary outputs share one observation cell");
    }
    return result;
}

} // namespace fcngraph
