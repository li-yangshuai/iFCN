#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <pybind11/functional.h>
#include <map>

#include "Parse.h"
#include "GridCell.hpp"
#include "AbstractNode.hpp"
#include "CZMFunction.hpp"
#include "MapChessboard.hpp"
#include "MapPhaseAStar.h"
#include "RightDownAStar.h"


// ----------- Trampoline Class for ClockStrategy 虚函数支持 -----------
class PyClockStrategy : public iFCN_Lab::ClockStrategy {
public:
    using iFCN_Lab::ClockStrategy::ClockStrategy;
    std::vector<uint64_t> generateQuadTreePhaseValues(int gridSize) override {
        PYBIND11_OVERRIDE_PURE(
            std::vector<uint64_t>,
            iFCN_Lab::ClockStrategy,
            generateQuadTreePhaseValues,
            gridSize
        );
    }
    std::string name() const override {
        PYBIND11_OVERRIDE_PURE(
            std::string,
            iFCN_Lab::ClockStrategy,
            name
        );
    }
};

// 注册 std::multimap 类型（如需用到）
PYBIND11_MAKE_OPAQUE(std::multimap<unsigned int, std::pair<unsigned int, unsigned int>>);

namespace py = pybind11;

PYBIND11_MODULE(iFCN_Lab, m) {

    // ---------- ClockStrategy 及其所有子类 -----------
    py::class_<iFCN_Lab::ClockStrategy, PyClockStrategy, std::shared_ptr<iFCN_Lab::ClockStrategy>>(m, "ClockStrategy")
        .def("generateQuadTreePhaseValues", &iFCN_Lab::ClockStrategy::generateQuadTreePhaseValues)
        .def("name", &iFCN_Lab::ClockStrategy::name);

    py::class_<iFCN_Lab::RandomClock, iFCN_Lab::ClockStrategy, std::shared_ptr<iFCN_Lab::RandomClock>>(m, "RandomClock")
        .def(py::init<uint64_t>(), py::arg("seed") = std::random_device{}())
        .def("generateQuadTreePhaseValues", &iFCN_Lab::RandomClock::generateQuadTreePhaseValues)
        .def("name", &iFCN_Lab::RandomClock::name);

    py::class_<iFCN_Lab::DWaveClock, iFCN_Lab::ClockStrategy, std::shared_ptr<iFCN_Lab::DWaveClock>>(m, "DWaveClock")
        .def(py::init<>())
        .def("generateQuadTreePhaseValues", &iFCN_Lab::DWaveClock::generateQuadTreePhaseValues)
        .def("name", &iFCN_Lab::DWaveClock::name);

    py::class_<iFCN_Lab::USEClock, iFCN_Lab::ClockStrategy, std::shared_ptr<iFCN_Lab::USEClock>>(m, "USEClock")
        .def(py::init<>())
        .def("generateQuadTreePhaseValues", &iFCN_Lab::USEClock::generateQuadTreePhaseValues)
        .def("name", &iFCN_Lab::USEClock::name);

    py::class_<iFCN_Lab::RESClock, iFCN_Lab::ClockStrategy, std::shared_ptr<iFCN_Lab::RESClock>>(m, "RESClock")
        .def(py::init<>())
        .def("generateQuadTreePhaseValues", &iFCN_Lab::RESClock::generateQuadTreePhaseValues)
        .def("name", &iFCN_Lab::RESClock::name);

    py::class_<iFCN_Lab::SplicingClock, iFCN_Lab::ClockStrategy, std::shared_ptr<iFCN_Lab::SplicingClock>>(m, "SplicingClock")
        .def(py::init<>())
        .def("generateQuadTreePhaseValues", &iFCN_Lab::SplicingClock::generateQuadTreePhaseValues)
        .def("name", &iFCN_Lab::SplicingClock::name);

    // ---------- 其它核心类绑定，保持原有即可 ----------
    py::class_<iFCN_Lab::Parse>(m, "Parse")
        .def(py::init<>())
        .def("parseVerilog", &iFCN_Lab::Parse::parseVerilog)
        .def("get_moduleName", &iFCN_Lab::Parse::get_moduleName)
        .def("optimizeAIOG_DRC", &iFCN_Lab::Parse::optimizeAIOG_DRC)
        .def("optimizeBufferNode", &iFCN_Lab::Parse::optimizeBufferNode)
        .def("optimizeNOTNode", &iFCN_Lab::Parse::optimizeNOTNode)
        .def("addLayerRedundancyNode", &iFCN_Lab::Parse::addLayerRedundancyNode)
        .def("getOriginCircuitNodeNum", &iFCN_Lab::Parse::getOriginCircuitNodeNum)
        .def("getOriginCircuitEdgeNum", &iFCN_Lab::Parse::getOriginCircuitEdgeNum)
        .def("getEffectiveNodes", &iFCN_Lab::Parse::getEffectiveNodes)
        .def("getNodeTypeString", &iFCN_Lab::Parse::getNodeTypeString)
        .def("getEffectiveEdges", &iFCN_Lab::Parse::getEffectiveEdges)
        .def("getlayerNodeDivVec", &iFCN_Lab::Parse::getlayerNodeDivVec)
        .def("getmax_layer", &iFCN_Lab::Parse::getmax_layer)
        .def("getVertexLayer", &iFCN_Lab::Parse::getVertexLayer)
        .def("getNodeTypeEnum", &iFCN_Lab::Parse::getNodeTypeEnum)
        .def("getFaninsIndex", &iFCN_Lab::Parse::getFaninsIndex)
        .def("getFanoutsIndex", &iFCN_Lab::Parse::getFanoutsIndex)
        .def("caculateSameLayerNodeRoutePair", &iFCN_Lab::Parse::caculateSameLayerNodeRoutePair)
        .def("getSameLayerNodeRoutePair", &iFCN_Lab::Parse::getSameLayerNodeRoutePair)
        .def("getDifferLayerNodeRoutePair", &iFCN_Lab::Parse::getDifferLayerNodeRoutePair)
        .def("getInputNodesIndex", &iFCN_Lab::Parse::getInputNodesIndex)
        .def("getOutputNodesIndex", &iFCN_Lab::Parse::getOutputNodesIndex)
        .def("getNodeName", &iFCN_Lab::Parse::getNodeName)
        ;

    py::enum_<iFCN_Lab::NodeType>(m, "NodeType")
        // 逐项写 value，名字和 enum 保持一致
        .value("Input",        iFCN_Lab::NodeType::Input)
        .value("Output",       iFCN_Lab::NodeType::Output)
        .value("Maj",          iFCN_Lab::NodeType::Maj)
        .value("And",          iFCN_Lab::NodeType::And)
        .value("Or",           iFCN_Lab::NodeType::Or)
        .value("Not",          iFCN_Lab::NodeType::Not)
        .value("Redundancy",   iFCN_Lab::NodeType::Redundancy)
        .value("Fanout",       iFCN_Lab::NodeType::Fanout)
        .export_values();


    py::class_<iFCN_Lab::AbstractNode>(m, "AbstractNode")
        .def(py::init<const std::string&, iFCN_Lab::NodeType>())
        .def("getNodeName", &iFCN_Lab::AbstractNode::getNodeName)
        .def("getNodeType", &iFCN_Lab::AbstractNode::getNodeType)
        .def("isInputNode", &iFCN_Lab::AbstractNode::isInputNode)
        .def("isOutputNode", &iFCN_Lab::AbstractNode::isOutputNode)
        ;

    py::class_<iFCN_Lab::GridCell>(m, "GridCell")
        .def(py::init<>())
        .def("canPlaceNode", &iFCN_Lab::GridCell::canPlaceNode)
        .def("placeNode", &iFCN_Lab::GridCell::placeNode)
        .def("canPlaceWire", &iFCN_Lab::GridCell::canPlaceWire)
        .def("placeWire", &iFCN_Lab::GridCell::placeWire)   
        .def("removeNode", &iFCN_Lab::GridCell::removeNode)
        .def("removeWire", &iFCN_Lab::GridCell::removeWire)
        .def("getPhase", &iFCN_Lab::GridCell::getPhase)
        .def("setPhase", &iFCN_Lab::GridCell::setPhase)
        .def("getNodeIndex", &iFCN_Lab::GridCell::getNodeIndex)
        .def("getNodeType", &iFCN_Lab::GridCell::getNodeType)
        .def("getCapacity", &iFCN_Lab::GridCell::getCapacity)
        .def("isEmpty", &iFCN_Lab::GridCell::isEmpty)
        ;


    m.attr("MAX_CELL_CAPACITY") = iFCN_Lab::MAX_CELL_CAPACITY;
    m.attr("WIRE_CELL_CAPACITY") = iFCN_Lab::WIRE_CELL_CAPACITY;
    m.attr("INPUT_NODE_CAPACITY") = iFCN_Lab::INPUT_NODE_CAPACITY;
    m.attr("LOGIC_NODE_CAPACITY") = iFCN_Lab::LOGIC_NODE_CAPACITY;

    py::class_<iFCN_Lab::MapChessboard>(m, "MapChessboard")
        .def(py::init<>())
        .def(py::init<std::shared_ptr<iFCN_Lab::ClockStrategy>>())
        .def("setPhaseEnabled", &iFCN_Lab::MapChessboard::setPhaseEnabled)
        .def("isPhaseEnabled", &iFCN_Lab::MapChessboard::isPhaseEnabled)
        .def("setPackedPhaseBlock4x4", &iFCN_Lab::MapChessboard::setPackedPhaseBlock4x4)
        .def("getPackedPhaseBlock4x4", &iFCN_Lab::MapChessboard::getPackedPhaseBlock4x4)
        .def("erasePackedPhaseBlock4x4", &iFCN_Lab::MapChessboard::erasePackedPhaseBlock4x4)
        .def("clearPackedPhaseBlocks4x4", &iFCN_Lab::MapChessboard::clearPackedPhaseBlocks4x4)
        .def("setPackedPhaseBlocksGrid4x4", &iFCN_Lab::MapChessboard::setPackedPhaseBlocksGrid4x4)
        .def("encodePackedPhaseBlock4x4", &iFCN_Lab::MapChessboard::encodePackedPhaseBlock4x4,
             py::arg("block_origin"), py::arg("use_tdd_for_unassigned") = true)
        .def("encodePackedPhaseBlocksGrid4x4", &iFCN_Lab::MapChessboard::encodePackedPhaseBlocksGrid4x4,
             py::arg("origin"), py::arg("blocks_x"), py::arg("blocks_y"), py::arg("use_tdd_for_unassigned") = true)
        .def("canPlaceNode", &iFCN_Lab::MapChessboard::canPlaceNode)
        .def("placeNode", &iFCN_Lab::MapChessboard::placeNode)
        .def("canPlaceWire", &iFCN_Lab::MapChessboard::canPlaceWire)
        .def("placeWire", &iFCN_Lab::MapChessboard::placeWire)
        .def("removeNode", &iFCN_Lab::MapChessboard::removeNode)
        .def("removeWire", &iFCN_Lab::MapChessboard::removeWire)
        .def("savePath", &iFCN_Lab::MapChessboard::savePath)
        .def("reset", &iFCN_Lab::MapChessboard::reset)
        .def("reset_with_phase", &iFCN_Lab::MapChessboard::reset_with_phase)
        .def("findLayoutBoard", &iFCN_Lab::MapChessboard::findLayoutBoard)
        .def("computeLayoutArea", &iFCN_Lab::MapChessboard::computeLayoutArea)
        .def("getPlacedNodeCoord", &iFCN_Lab::MapChessboard::getPlacedNodeCoord)
        .def("getPlaceNodeType", &iFCN_Lab::MapChessboard::getPlaceNodeType)
        .def("outputTexFile", &iFCN_Lab::MapChessboard::outputTexFile)
        .def("getTDDPhaseAtCoord", &iFCN_Lab::MapChessboard::getTDDPhaseAtCoord)
        .def("getPhase", &iFCN_Lab::MapChessboard::getPhase)
        .def("hasPhaseConflict", &iFCN_Lab::MapChessboard::hasPhaseConflict)
        .def("trySetPhase", &iFCN_Lab::MapChessboard::trySetPhase)
        .def("setPhase", &iFCN_Lab::MapChessboard::setPhase)
        .def_readwrite("nodeIndexToCoordMap", &iFCN_Lab::MapChessboard::nodeIndexToCoordMap)
        .def_readwrite("gridMap", &iFCN_Lab::MapChessboard::gridMap)
        .def_readwrite("nodePairRoutes", &iFCN_Lab::MapChessboard::nodePairRoutes)
        ;

    py::class_<iFCN_Lab::MapPhaseAStar>(m, "MapPhaseAStar")
        .def(py::init<iFCN_Lab::MapChessboard&, int, int, int>(),
             py::arg("board"), py::arg("phase_cycle") = 4, py::arg("padding") = 2, py::arg("max_same_phase") = 0)
        .def("route", &iFCN_Lab::MapPhaseAStar::route)
        .def("route_with_dirs", &iFCN_Lab::MapPhaseAStar::route_with_dirs,
             py::arg("src"), py::arg("dst"),
             py::arg("start_dx"), py::arg("start_dy"),
             py::arg("end_dx"), py::arg("end_dy"),
             py::arg("use_start") = true, py::arg("use_end") = true)
        .def("reset", &iFCN_Lab::MapPhaseAStar::reset)
        ;

    py::class_<iFCN_Lab::RightDownAStar>(m, "RightDownAStar")
        .def(py::init<iFCN_Lab::MapChessboard&>())
        .def("route", &iFCN_Lab::RightDownAStar::route)
        .def("reset", &iFCN_Lab::RightDownAStar::reset)
        ;
}
