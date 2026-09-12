import os

from lib import iFCN_Lab


class CircuitParser:
    VALID_PARSE_MODES = {"auto", "compact", "layered"}

    def __init__(self, filepath, parse_mode=None):
        self.filePath = filepath
        self.fileName = filepath.split('/')[-1]
        requested_mode = parse_mode or os.environ.get("IFCN_LAYOUT_PARSE_MODE", "auto")
        requested_mode = str(requested_mode).strip().lower()
        if requested_mode not in self.VALID_PARSE_MODES:
            raise ValueError(
                f"Unsupported parse_mode={requested_mode!r}; "
                f"expected one of {sorted(self.VALID_PARSE_MODES)}"
            )
        self.parse_mode_requested = requested_mode
        self.parser = iFCN_Lab.Parse()
        # The fixed right/down router has two independent fanin ports.
        # Preserve majority semantics using the shared two-input gate basis;
        # the native irregular parser continues to retain real MAJ gates.
        self.parser.parseVerilog(filepath, False)
        self.moduleName = self.parser.get_moduleName()
        self.parser.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2)
        self.originCircuitNodeNum = self.parser.getOriginCircuitNodeNum()
        self.originCircuitEdgeNum = self.parser.getOriginCircuitEdgeNum()
        self.parse_mode_resolved = self._resolve_parse_mode(requested_mode)
        if self.parse_mode_resolved == "layered":
            self.parser.addLayerRedundancyNode()
        else:
            self.parser.optimizeBufferNode()
        self.parser.caculateSameLayerNodeRoutePair()

        # 获取图的基本信息
        self.effective_nodes = set(self.parser.getEffectiveNodes())
        self.effective_edges = list(self.parser.getEffectiveEdges())
        self.node_type_overrides = {}
        output_nodes = set(self.parser.getOutputNodesIndex())

        # The shared parser owns named output terminals and safe NOT-output
        # fusion. Keep its remaining aliases: replacing an explicit terminal
        # with a differently named driver loses the source port in exporters
        # that read the native parser's names. A fanout driver could also be
        # removed by the simplification below. Retain the compatibility field
        # used by existing snapshot readers without a second fusion pass.
        fused_output_aliases = {}

        # Bypass zero-logic fanout vertices.  They are parser artifacts used
        # to express an electrical branch, not Boolean gates; the right/down
        # router already represents a branch by shared wire prefixes.  Keeping
        # the vertex consumes a tile and can block both legal launch ports.
        # Reconnecting its sole driver directly to every sink preserves the
        # circuit function and is applied uniformly to all benchmarks.
        #
        # There is one useful exception.  If that driver also feeds an
        # inverter with several sinks, reuse the otherwise redundant fanout
        # vertex as a duplicate inverter and split the inverted sinks between
        # the two copies.  This is ordinary fanout decomposition: it preserves
        # the Boolean network while avoiding two inverted branches competing
        # for the same right/down launch ports.
        fused_fanout_nodes = {}
        fanout_optimization = os.environ.get(
            "IFCN_FANOUT_OPTIMIZATION", "optimize"
        ).strip().lower()
        if fanout_optimization not in {"optimize", "keep"}:
            raise ValueError(
                "IFCN_FANOUT_OPTIMIZATION must be 'optimize' or 'keep'"
            )
        while True:
            if fanout_optimization == "keep":
                break
            effective_fanins = {int(node): [] for node in self.effective_nodes}
            effective_fanouts = {int(node): [] for node in self.effective_nodes}
            for src, dst in self.effective_edges:
                src, dst = int(src), int(dst)
                if src in effective_fanouts and dst in effective_fanins:
                    effective_fanouts[src].append(dst)
                    effective_fanins[dst].append(src)

            removable = None
            for node in sorted(self.effective_nodes):
                node = int(node)
                if node in output_nodes:
                    continue
                if self.getNodeTypeEnum(node) != iFCN_Lab.NodeType.Fanout:
                    continue
                if len(effective_fanins.get(node, ())) != 1:
                    continue
                removable = node
                break
            if removable is None:
                break

            driver = int(effective_fanins[removable][0])
            sinks = [int(node) for node in effective_fanouts.get(removable, ())]
            inverter_candidates = [
                int(node)
                for node in effective_fanouts.get(driver, ())
                if int(node) != removable
                and self.getNodeTypeEnum(int(node)) == iFCN_Lab.NodeType.Not
                and len(effective_fanouts.get(int(node), ())) > 1
            ]

            if inverter_candidates:
                inverter = min(inverter_candidates)
                inverted_sinks = [
                    int(node) for node in effective_fanouts.get(inverter, ())
                ]
                moved_inverted_sinks = inverted_sinks[1:]
                removed_edges = {
                    (removable, sink) for sink in sinks
                }
                removed_edges.add((driver, removable))
                removed_edges.update(
                    (inverter, sink) for sink in moved_inverted_sinks
                )
                self.effective_edges = [
                    (int(src), int(dst))
                    for src, dst in self.effective_edges
                    if (int(src), int(dst)) not in removed_edges
                ]
                existing_edges = set(self.effective_edges)
                replacement_edges = [
                    *((driver, sink) for sink in sinks),
                    (driver, removable),
                    *((removable, sink) for sink in moved_inverted_sinks),
                ]
                for edge in replacement_edges:
                    if edge not in existing_edges and edge[0] != edge[1]:
                        self.effective_edges.append(edge)
                        existing_edges.add(edge)
                self.node_type_overrides[removable] = iFCN_Lab.NodeType.Not
                fused_fanout_nodes[removable] = {
                    "driver": driver,
                    "sinks": sinks,
                    "mode": "duplicate_not",
                    "source_not": inverter,
                    "moved_not_sinks": moved_inverted_sinks,
                }
            else:
                self.effective_nodes.remove(removable)
                self.effective_edges = [
                    (int(src), int(dst))
                    for src, dst in self.effective_edges
                    if int(src) != removable and int(dst) != removable
                ]
                existing_edges = set(self.effective_edges)
                for sink in sinks:
                    edge = (driver, sink)
                    if edge not in existing_edges and driver != sink:
                        self.effective_edges.append(edge)
                        existing_edges.add(edge)
                fused_fanout_nodes[removable] = {
                    "driver": driver,
                    "sinks": sinks,
                    "mode": "bypass",
                }

        self.effective_nodes_num = len(self.effective_nodes)
        self.effective_edges_num = len(self.effective_edges)
        self.layer_nodes = [
            [int(node) for node in layer if int(node) in self.effective_nodes]
            for layer in self.parser.getlayerNodeDivVec()
        ]
        self.layer_nodes = [layer for layer in self.layer_nodes if layer]
        self.total_layers = len(self.layer_nodes)
        self.same_layer_route_pairs = self.parser.getSameLayerNodeRoutePair()
        self.differ_layer_route_pairs = self.parser.getDifferLayerNodeRoutePair()
        effective_edge_set = {
            (int(src), int(dst)) for src, dst in self.effective_edges
        }
        self.same_layer_route_pairs = {
            int(layer): [
                (int(src), int(dst))
                for src, dst in pairs
                if (int(src), int(dst)) in effective_edge_set
            ]
            for layer, pairs in self.same_layer_route_pairs.items()
        }
        self.differ_layer_route_pairs = [
            (int(src), int(dst))
            for src, dst in self.differ_layer_route_pairs
            if (int(src), int(dst)) in effective_edge_set
        ]
        assigned_route_pairs = {
            (int(src), int(dst))
            for pairs in self.same_layer_route_pairs.values()
            for src, dst in pairs
        }
        assigned_route_pairs.update(self.differ_layer_route_pairs)
        self.differ_layer_route_pairs.extend(
            (int(src), int(dst))
            for src, dst in self.effective_edges
            if (int(src), int(dst)) not in assigned_route_pairs
        )
        self.differ_layer_route_pairs_num = len(self.differ_layer_route_pairs)
        self.parse_cache_key = (
            f"{self.parse_mode_resolved}_"
            f"on{int(self.originCircuitNodeNum)}_oe{int(self.originCircuitEdgeNum)}_"
            f"en{int(self.effective_nodes_num)}_ee{int(self.effective_edges_num)}_"
            f"dl{int(self.differ_layer_route_pairs_num)}"
        )
        
        self.getInputNodesIndex = self.parser.getInputNodesIndex()
        self.getOutputNodesIndex = output_nodes
        self.InputNodesNum = len(self.getInputNodesIndex)
        self.OutputNodesNum = len(self.getOutputNodesIndex)
        self.fused_output_aliases = fused_output_aliases
        self.fused_fanout_nodes = fused_fanout_nodes

        self.effective_fanins = {int(node): [] for node in self.effective_nodes}
        self.effective_fanouts = {int(node): [] for node in self.effective_nodes}
        for src, dst in self.effective_edges:
            src, dst = int(src), int(dst)
            self.effective_fanouts[src].append(dst)
            self.effective_fanins[dst].append(src)


        # 映射：原始ID → 连续节点索引
        self.node_to_index = {n: i for i, n in enumerate(self.effective_nodes)}
        self.index_to_node = {i: n for i, n in enumerate(self.effective_nodes)}

        print(f"-------------------Orginal circuit info-------------------")
        print(f"circuit name : {filepath}")
        print(f"node number: {self.originCircuitNodeNum}")
        print(f"edges number: {self.parser.getOriginCircuitEdgeNum()}")
        print(f"-------------------After optimize-------------------")
        print(f"parse mode: requested={self.parse_mode_requested}, resolved={self.parse_mode_resolved}")
        print(f"nodes number: {self.effective_nodes_num}")
        print(f"edges number: {self.effective_edges_num}")
        print(f"total layers: {self.total_layers}")
        print(f"differ-layer route pairs: {self.differ_layer_route_pairs_num}")

    def _resolve_parse_mode(self, requested_mode):
        if requested_mode != "auto":
            return requested_mode
        return "compact"

    def getNodeTypeString(self, node_id):
        if self.node_type_overrides.get(int(node_id)) == iFCN_Lab.NodeType.Not:
            return "not"
        return self.parser.getNodeTypeString(node_id)


    def getNodeTypeEnum(self, node_id):
        if int(node_id) in self.node_type_overrides:
            return self.node_type_overrides[int(node_id)]
        return self.parser.getNodeTypeEnum(node_id)

    def get_layer_of_node(self, node_id):
        return self.parser.getVertexLayer(node_id)

    def get_node_type(self, node_id):
        return self.getNodeTypeEnum(node_id)

    def get_fanins(self, node_id):
        return list(self.effective_fanins.get(int(node_id), ()))

    def get_fanouts(self, node_id):
        return list(self.effective_fanouts.get(int(node_id), ()))

    def getNodeName(self, node_id):
        return self.parser.getNodeName(node_id)
        
    def get_sequence_arrangement_of_edges(self):
        edge_pairs = []

        if self.same_layer_route_pairs:
            for layer, pairs in self.same_layer_route_pairs.items():
                edge_pairs.extend(pairs)

        if self.differ_layer_route_pairs:
            edge_pairs.extend(self.differ_layer_route_pairs)

        # 映射为连续节点索引的边
        edge_index_pairs = []
        for src, dst in edge_pairs:
            if src in self.node_to_index and dst in self.node_to_index:
                edge_index_pairs.append((
                    self.node_to_index[src],
                    self.node_to_index[dst]
                ))

        return edge_index_pairs
