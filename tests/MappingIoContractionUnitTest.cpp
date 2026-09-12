#include <autopr/algorithms/mapping.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using fcngraph::Mapping;
using fcngraph::NodeLinkMap;
using fcngraph::RouteCellMap;
using fcngraph::position;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool contains(const std::vector<position>& cells, const position& target)
{
    return std::find(cells.begin(), cells.end(), target) != cells.end();
}

bool is_tile_edge_port(const position& cell)
{
    const unsigned int local_x = cell.first % 5;
    const unsigned int local_y = cell.second % 5;
    return ((local_x == 0 || local_x == 4) && local_y == 2) ||
           ((local_y == 0 || local_y == 4) && local_x == 2);
}

void contracts_input_to_the_logic_frontier()
{
    NodeLinkMap nodes;
    nodes[{{0, 0}, "input"}] = {{}, {{0, 1}}};
    nodes[{{0, 3}, "maj"}] = {{{0, 2}}, {}};

    Mapping mapping;
    mapping.node_mapping(nodes);
    RouteCellMap routes;
    auto& segment = routes[{{0, 0}, {0, 3}}].emplace_back();
    for (unsigned int y = 5; y <= 14; ++y) {
        segment.emplace_back(2, y);
    }

    const auto stats = mapping.contract_io_ports(nodes, routes);
    require(stats.moved_inputs == 1, "the primary input was not contracted");
    require(stats.removed_cells == 12, "unexpected number of removed input-stem cells");
    require(contains(mapping.nodecell_list["input"], {2, 14}),
            "the input did not reach the logic frontier");
    require(!contains(mapping.nodecell_list["normal"], {2, 3}),
            "the obsolete input stem was retained");
    require(routes[{{0, 0}, {0, 3}}].empty(),
            "the consumed route prefix was retained");
    require(mapping.io_terminal_origins().at({2, 14}) == position{0, 0},
            "the moved input lost its original name owner");
}

void stops_input_on_its_first_fanout()
{
    NodeLinkMap nodes;
    nodes[{{0, 0}, "input"}] = {{}, {{1, 0}}};
    nodes[{{3, 0}, "maj"}] = {{{2, 0}}, {}};
    nodes[{{2, 2}, "maj"}] = {{{2, 1}}, {}};

    Mapping mapping;
    mapping.node_mapping(nodes);
    RouteCellMap routes;
    routes[{{0, 0}, {3, 0}}] = {{{5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2}}};
    routes[{{0, 0}, {2, 2}}] = {{{5, 2}, {6, 2}, {7, 2}, {7, 3}, {7, 4}}};

    const auto stats = mapping.contract_io_ports(nodes, routes);
    require(stats.moved_inputs == 1, "the fanout input was not contracted");
    require(contains(mapping.nodecell_list["input"], {5, 2}),
            "the input did not stop on the last 5x5 boundary before fanout");
    require(is_tile_edge_port(mapping.nodecell_list["input"].front()),
            "the contracted fanout input is not at a legal 5x5 edge port");
    require(contains(routes[{{0, 0}, {3, 0}}].front(), {8, 2}),
            "the horizontal fanout suffix was removed");
    require(contains(routes[{{0, 0}, {2, 2}}].front(), {7, 3}),
            "the vertical fanout suffix was removed");
}

void never_consumes_a_crossover()
{
    NodeLinkMap nodes;
    nodes[{{0, 0}, "input"}] = {{}, {{1, 0}}};
    nodes[{{3, 0}, "maj"}] = {{{2, 0}}, {}};

    Mapping mapping;
    mapping.node_mapping(nodes);
    RouteCellMap routes;
    routes[{{0, 0}, {3, 0}}] = {{{5, 2}, {6, 2}, {7, 2}, {8, 2}}};
    mapping.crossline_list[{{0, 0}, {3, 0}}] = {{{7, 2}}};

    mapping.contract_io_ports(nodes, routes);
    require(contains(mapping.nodecell_list["input"], {5, 2}),
            "the input did not return to the 5x5 boundary before a crossover");
    require(contains(routes[{{0, 0}, {3, 0}}].front(), {7, 2}),
            "the protected crossover was removed");
}

void accepts_a_legal_crossover_edge_as_io()
{
    NodeLinkMap nodes;
    nodes[{{0, 0}, "input"}] = {{}, {{1, 0}}};
    nodes[{{3, 0}, "maj"}] = {{{2, 0}}, {}};

    Mapping mapping;
    mapping.node_mapping(nodes);
    RouteCellMap routes;
    routes[{{0, 0}, {3, 0}}] = {{{5, 2}, {6, 2}, {7, 2}, {8, 2}}};
    mapping.crossline_list[{{0, 0}, {3, 0}}] = {{{5, 2}, {6, 2}, {7, 2}}};

    const auto stats = mapping.contract_io_ports(nodes, routes);
    require(stats.moved_inputs == 1, "the crossover-edge input was not contracted");
    require(contains(mapping.nodecell_list["input"], {5, 2}),
            "the legal first-layer crossover edge was not used as IO");
    require(contains(mapping.crossline_list.begin()->second.front(), {5, 2}),
            "the upper crossover description was modified");
}

void contracts_output_back_toward_its_driver()
{
    NodeLinkMap nodes;
    nodes[{{0, 0}, "maj"}] = {{}, {{1, 0}}};
    nodes[{{3, 0}, "output"}] = {{{2, 0}}, {}};

    Mapping mapping;
    mapping.node_mapping(nodes);
    RouteCellMap routes;
    auto& segment = routes[{{0, 0}, {3, 0}}].emplace_back();
    for (unsigned int x = 5; x <= 14; ++x) {
        segment.emplace_back(x, 2);
    }

    const auto stats = mapping.contract_io_ports(nodes, routes);
    require(stats.moved_outputs == 1, "the primary output was not contracted");
    require(contains(mapping.nodecell_list["output"], {5, 2}),
            "the output did not reach its driver frontier");
    require(mapping.io_terminal_origins().at({5, 2}) == position{3, 0},
            "the moved output lost its original name owner");
}

} // namespace

int main()
{
    try {
        contracts_input_to_the_logic_frontier();
        stops_input_on_its_first_fanout();
        never_consumes_a_crossover();
        accepts_a_legal_crossover_edge_as_io();
        contracts_output_back_toward_its_driver();
        std::cout << "IO contraction tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "IO contraction tests failed: " << error.what() << '\n';
        return 1;
    }
}
