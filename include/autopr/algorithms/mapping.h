#pragma once
#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <iterator>
#include <set>
#include <string>
#include <cstdint>

namespace fcngraph{
    using position = std::pair<unsigned int , unsigned int>;

    // One concrete cell in the layer-aware QCA layout.  The layer is physical
    // topology only: clock phase belongs to the enclosing 5x5 coarse tile,
    // so every layer at this fine-cell XY inherits phase[(x/5,y/5)].
    struct PhysicalCellSite
    {
        position xy{};
        int layer = 0;

        PhysicalCellSite() = default;
        PhysicalCellSite(position coordinate, int physicalLayer = 0)
            : xy(std::move(coordinate)), layer(physicalLayer)
        {
        }
        PhysicalCellSite(unsigned int x, unsigned int y,
                         int physicalLayer = 0)
            : xy{x, y}, layer(physicalLayer)
        {
        }

        bool operator==(const PhysicalCellSite &other) const noexcept
        {
            return xy == other.xy && layer == other.layer;
        }

        bool operator!=(const PhysicalCellSite &other) const noexcept
        {
            return !(*this == other);
        }

        bool operator<(const PhysicalCellSite &other) const noexcept
        {
            return xy < other.xy ||
                   (xy == other.xy && layer < other.layer);
        }
    };

    // Combinational layouts may trade equivalent intra-tile route shapes for
    // fewer cells.  Sequential layouts can contain deliberate detours whose
    // ordered coarse tiles carry clock/epoch latency, so those tiles must be
    // preserved during cell-level mapping.
    enum class MappingMode
    {
        Combinational,
        Sequential
    };

    using NodeLinkMap = std::map<
        std::pair<position, std::string>,
        std::pair<std::vector<position>, std::vector<position>>>;
    using RouteCellMap = std::map<
        std::pair<position, position>,
        std::vector<std::vector<position>>>;

    struct IoPortContractionStats
    {
        std::size_t moved_inputs = 0;
        std::size_t moved_outputs = 0;
        std::size_t removed_cells = 0;
        std::size_t skipped_ports = 0;

        std::size_t movedPorts() const noexcept
        {
            return moved_inputs + moved_outputs;
        }
    };

    struct MappingPositionHash
    {
        std::size_t operator()(const position& pos) const noexcept
        {
            return (static_cast<std::size_t>(pos.first) << 32) ^ static_cast<std::size_t>(pos.second);
        }
    };

    class Mapping
    {
    public:
        Mapping(){}
        ~Mapping(){}

        RouteCellMap mapping_line(
            std::vector<std::vector<position>>& _example,
            MappingMode mode = MappingMode::Combinational,
            const std::vector<unsigned int>& iterationDistances = {});
        // Reconstruct one directed, 4-neighbour physical-cell path for every
        // coarse route passed to mapping_line().  This is a geometry/export
        // view; sequential clock constraints remain on the ordered coarse
        // tiles and never count these fine-cell steps.
        std::vector<std::vector<position>> orderedPhysicalRoutes(
            const std::vector<std::vector<position>>& coarseRoutes) const;
        // Expand ordered physical geometry through the exact exported QCA
        // layers.  These extra layer sites do not create clock occurrences.
        // Lifted crossover segments use layer 2 and their entry/exit pillars
        // explicitly traverse layers 0, 1 and 2.  Every consecutive pair is
        // therefore adjacent in the realized three-dimensional cell graph.
        std::vector<std::vector<PhysicalCellSite>>
        orderedLayerAwarePhysicalRoutes(
            const std::vector<std::vector<position>>& coarseRoutes) const;
        // Exact layer-aware sites emitted by the mapping state before I/O
        // contraction.  This mirrors the QCAD exporter: node/ordinary wire
        // cells are on layer 0, crossover corridors on layer 2, and pillar
        // endpoints occupy layers 0, 1 and 2.
        std::set<PhysicalCellSite> physicalCellSites(
            const std::vector<std::vector<position>>& coarseRoutes) const;
        bool validate_crossovers(std::string* error = nullptr) const;
        void routepos_Deviate(std::vector<position>& _oneroutepos_list);
        void deviate_mapping(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list);
        std::string findInVectorPairFirst(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair);
        bool isfindpostype(std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>>& _deviate_list, position& target_pair, std::string& _type);
        void crossline_mapping(std::vector<std::vector<position>> &_routepos_list);
        void node_mapping(
            NodeLinkMap& _Nodelink,
            MappingMode mode = MappingMode::Combinational);
        IoPortContractionStats contract_io_ports(const NodeLinkMap& node_links,
                                                 RouteCellMap& route_cells);
        const std::map<position, position>& io_terminal_origins() const noexcept
        {
            return io_terminal_origins_;
        }
        void not_check(std::vector<std::vector<position>> &_routepos_list);

        std::map<std::pair<position, position>, std::vector<std::vector<position>>> deviatemapping_list;//线映射坐标
        std::map<std::pair<position, position>, std::vector<std::vector<position>>> crossline_list;//交叉线映射坐标cross
        std::map<std::string, std::vector<position>> nodecell_list;//节点的组成元胞映射坐标

        std::vector<std::pair<std::pair<position, position>, position>> oneroutepos_list_examp;
        std::vector<std::pair<std::pair<std::pair<position, position>, std::pair<position, position>>, position>> temppos_list_examp;
        std::vector<std::vector<position>> example = {
        {{5, 5}, {4, 5}, {4, 4}, {5, 4}, {6, 4}, {7, 4}, {8, 4}, {8, 3}, {7, 3}, {6, 3}},
        {{2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {6, 7}, {6, 6}, {6, 5}, {6, 4}, {6, 3}},
        {{6, 2}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {3, 2}, {3, 3}},
        {{2, 6}, {3, 6}, {4, 6}, {4, 5}, {4, 4}, {4, 3}, {3, 3}},
        {{6, 2}, {6, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5}},
        {{2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {6, 5}, {5, 5}},
        {{3, 3}, {3, 4}, {3, 5}, {2, 5}},
        {{2, 5}, {1, 5}, {1, 6}, {1, 7}, {1, 8}, {2, 8}}};
        
    private:
        std::vector<std::vector<position>> routepos_list;//作为mapping的输入
        std::map<std::pair<position, position>, std::vector<std::pair<position, std::string>>> deviate_list;
        struct DeviateLookupEntry
        {
            std::pair<position, position> first_route_key{};
            std::size_t first_index = 0;
            std::string first_type;
            std::uint16_t type_mask = 0;
            bool initialized = false;
        };
        std::unordered_map<position, DeviateLookupEntry, MappingPositionHash> deviate_lookup;
        std::unordered_set<position, MappingPositionHash> multi_output_not_input_boundaries;
        std::map<position, position> io_terminal_origins_;
        static std::uint16_t deviateTypeMask(const std::string& type);
        void updateDeviateLookup(const std::pair<position, position>& route_key,
                                 const std::vector<std::pair<position, std::string>>& route_entries);
        // std::map<std::pair<position, position>, std::vector<std::vector<position>>> deviatemapping_list;//线映射坐标
        // std::multimap<std::pair<position, position>, std::vector<position>> crossline_list;//交叉线映射坐标
        // std::vector<std::vector<position>> example = {
        // {{5, 5}, {4, 5}, {4, 4}, {5, 4}, {6, 4}, {7, 4}, {8, 4}, {8, 3}, {7, 3}, {6, 3}},
        // {{2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {6, 7}, {6, 6}, {6, 5}, {6, 4}, {6, 3}},
        // {{6, 2}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {3, 2}, {3, 3}},
        // {{2, 6}, {3, 6}, {4, 6}, {4, 5}, {4, 4}, {4, 3}, {3, 3}},
        // {{6, 2}, {6, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5}},
        // {{2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {6, 5}, {5, 5}},
        // {{3, 3}, {3, 4}, {3, 5}, {2, 5}},
        // {{2, 5}, {1, 5}, {1, 6}, {1, 7}, {1, 8}, {2, 8}}};
    };
    
    
};
