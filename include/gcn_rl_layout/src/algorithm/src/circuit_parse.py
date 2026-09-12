import os

from lib import iFCN_Lab
import torch


def _pyg_data_type():
    """Load PyG only for the legacy GCN path.

    The fixed-clock OGDF flow uses the parser topology directly and must not
    require torch-geometric merely to construct a placement.
    """
    try:
        from torch_geometric.data import Data
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "torch-geometric is required only when the legacy GCN crossing "
            "orderer is selected"
        ) from exc
    return Data


class CircuitParser:
    VALID_PARSE_MODES = {"auto", "compact", "layered"}

    def __init__(self, filepath, parse_mode=None):
        self.filePath = filepath
        self.fileName = filepath.split('/')[-1]
        requested_mode = parse_mode or os.environ.get("IFCN_GCN_RL_PARSE_MODE", "auto")
        requested_mode = str(requested_mode).strip().lower()
        if requested_mode not in self.VALID_PARSE_MODES:
            raise ValueError(
                f"Unsupported parse_mode={requested_mode!r}; "
                f"expected one of {sorted(self.VALID_PARSE_MODES)}"
            )
        self.parse_mode_requested = requested_mode
        self.parser = iFCN_Lab.Parse()
        self.parser.parseVerilog(filepath)
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

        # Fuse a terminal output alias into its sole logic driver.  A Boolean
        # expression such as ``assign out = ~w`` is parsed as NOT -> OUTPUT;
        # physically the NOT gate itself can expose the output, so the extra
        # zero-logic tile and edge are unnecessary.  Direct output gates are
        # already represented this way by the parser and are left untouched.
        fused_output_aliases = {}
        for output_node in sorted(output_nodes):
            if output_node not in self.effective_nodes:
                continue
            if self.parser.getNodeTypeEnum(output_node) != iFCN_Lab.NodeType.Output:
                continue
            fanins = [
                int(node)
                for node in self.parser.getFaninsIndex(output_node)
                if int(node) in self.effective_nodes
            ]
            fanouts = [
                int(node)
                for node in self.parser.getFanoutsIndex(output_node)
                if int(node) in self.effective_nodes
            ]
            if len(fanins) != 1 or fanouts:
                continue
            driver = fanins[0]
            if self.parser.getNodeTypeEnum(driver) == iFCN_Lab.NodeType.Input:
                continue
            fused_output_aliases[int(output_node)] = int(driver)

        if fused_output_aliases:
            removed_outputs = set(fused_output_aliases)
            self.effective_nodes.difference_update(removed_outputs)
            self.effective_edges = [
                (int(src), int(dst))
                for src, dst in self.effective_edges
                if int(src) not in removed_outputs and int(dst) not in removed_outputs
            ]
            output_nodes.difference_update(removed_outputs)
            output_nodes.update(fused_output_aliases.values())

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


        # 映射：原始ID → GNN内部 index（用于 PyG）
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
        
    def pad_and_normalize(self, layers, target_len, total_layers):
        """
        layers      : 实际层级列表，例如 [1, 3]
        target_len  : 目标长度，比如最多 3 个
        total_layers: 总层数，用于归一化
        返回固定长度的层级向量，归一化后补 -1 表示空位
        """
        layers = layers[:target_len] + [-1] * (target_len - len(layers))
        return [l / total_layers if l >= 0 else -1.0 for l in layers]
    

    def build_pyg_data(self):
        """
        构建 PyTorch Geometric 的图数据对象
        """
        Data = _pyg_data_type()
        nodes = self.effective_nodes
        layerNodes = self.layer_nodes
        total_layers = self.total_layers
        edges = self.effective_edges
        node2layer = {n: i for i, layer in enumerate(layerNodes) for n in layer}
        
        # NodeType 按固定顺序
        # 例如 [Input, Output, Maj, And, Or, Not, Redundancy, Fanout]
        all_types = [
            iFCN_Lab.NodeType.Input,
            iFCN_Lab.NodeType.Output,
            iFCN_Lab.NodeType.Maj,
            iFCN_Lab.NodeType.And,
            iFCN_Lab.NodeType.Or,
            iFCN_Lab.NodeType.Not,
            iFCN_Lab.NodeType.Redundancy,
            iFCN_Lab.NodeType.Fanout,
        ]
        type2idx = {t: i for i, t in enumerate(all_types)}
        
        # 构建节点特征 x（类型编码 + 层级归一化）
        x_list = []
        for node in nodes:
            # 热码 机器学习中常用表示方式：创建一个全是 0 的向量，把索引 2（"INV"）的位置置为 1： [0, 0, 1, 0]
            gate = self.get_node_type(node)
            type_onehot = [0] * len(all_types)
            type_onehot[type2idx[gate]] = 1

            #该节点所处的层级
            layer_norm = node2layer[node] / total_layers
            
            # 该节点的输入节点和输出节点所处的层级
            fanin_layers = [
                node2layer[n] for n in self.get_fanins(node) if n in node2layer
            ]
            fanin_vec = self.pad_and_normalize(fanin_layers, 3, total_layers)
            fanout_layers = [
                node2layer[n] for n in self.get_fanouts(node) if n in node2layer
            ]
            fanout_vec = self.pad_and_normalize(fanout_layers, 2, total_layers)
            
            # 类型编码 + 层级归一化 + 输入、输出层级归一化
            feature_vec = type_onehot + [layer_norm] + fanin_vec + fanout_vec
            x_list.append(feature_vec)


        x = torch.tensor(x_list, dtype=torch.float)
        # 统一补齐维度
        fixed_dim = 14  # 你的训练时使用的维度：8+1+3+2
        current_dim = x.shape[1]
        if current_dim < fixed_dim:
            x = torch.nn.functional.pad(x, (0, fixed_dim - current_dim), value=0)
        elif current_dim > fixed_dim:
            x = x[:, :fixed_dim]

        # 构建 edge_index
        edge_index = torch.tensor(
            [[self.node_to_index[s], self.node_to_index[d]] for s, d in edges],
            dtype=torch.long
        ).t().contiguous()

        edge_attr = []
        for src, dst in edges:
            src_layer = node2layer[src]
            dst_layer = node2layer[dst]
            diff = dst_layer - src_layer
            abs_diff = abs(diff)

            # 查找另一个扇出终点 w
            fanouts = self.get_fanouts(src)
            other_fanout_layers = [node2layer[n] for n in fanouts if n != dst]

            if other_fanout_layers:
                other_dst_layer = other_fanout_layers[0]  # 取第一个（因为你最多两个扇出）
                layer_gap_to_other = (dst_layer - other_dst_layer) / total_layers
            else:
                layer_gap_to_other = -1.0  # 或者你也可以设为 -1.0 作为无效标记

            edge_attr.append([
                diff / total_layers,           # 层级差
                abs_diff / total_layers,       # 绝对层级差
                float(abs_diff > 1),           # 是否跨层
                float(diff < 0),               # 是否反馈
                layer_gap_to_other             # 起点另外一个扇出终点的层级差
            ])

        edge_attr = torch.tensor(edge_attr, dtype=torch.float)
        # 封装成 PyG Data 对象
        # 节点特征，让每个节点的特征向量的纬度为 N+1+3+2； 
            # N 为门类型数量、 这里最多有8种逻辑门
            # 1 为层级归一化、_
            # 3 为输入层级归一化（最多有三个输入）、
            # 2 为输出层级归一化（最多有两个输出）。
            
        # 每条边的特征向量的纬度为 5：
            # 层级差、
            # 绝对层级差、 
            # 是否跨层、
            # 是否反馈、
            # 扇出层级差
        
        data = Data(x=x, edge_index=edge_index, edge_attr=edge_attr)

        return data
    


    def build_simple_pyg_data(self):
        """
        构建极简化的 PyTorch Geometric 图数据对象，仅保留逻辑门类型和所在层级。
        保证特征维度为固定的 9 维（8 种门类型 one-hot + 1 维层级归一化）。
        """
        Data = _pyg_data_type()
        nodes = self.effective_nodes
        layer_nodes = self.layer_nodes
        total_layers = self.total_layers
        edges = self.effective_edges
        node2layer = {n: i for i, layer in enumerate(layer_nodes) for n in layer}
        # NodeType 按固定顺序
        # 例如 [Input, Output, Maj, And, Or, Not, Redundancy, Fanout]
        all_types = [
            iFCN_Lab.NodeType.Input,
            iFCN_Lab.NodeType.Output,
            iFCN_Lab.NodeType.Maj,
            iFCN_Lab.NodeType.And,
            iFCN_Lab.NodeType.Or,
            iFCN_Lab.NodeType.Not,
            iFCN_Lab.NodeType.Redundancy,
            iFCN_Lab.NodeType.Fanout,
        ]
        type2idx = {t: i for i, t in enumerate(all_types)}



        x_list = []
        for node in nodes:
            onehot = [0] * len(all_types)
            gate = self.get_node_type(node)
            onehot[type2idx[gate]] = 1
            layer_norm = node2layer[node] / total_layers
            x_list.append(onehot + [layer_norm])

        x = torch.tensor(x_list, dtype=torch.float)

        edge_index = torch.tensor(
            [[self.node_to_index[s], self.node_to_index[d]] for s, d in edges],
            dtype=torch.long
        ).t().contiguous()

        return Data(x=x, edge_index=edge_index)



    
    
    
    
    # 获取一个节点放置的先后顺序：先完成相同层级节点放置,再完成不同层级节点放置
    def get_sequence_arrangement_of_edges(self):
        edge_pairs = []

        if self.same_layer_route_pairs:
            for layer, pairs in self.same_layer_route_pairs.items():
                edge_pairs.extend(pairs)

        if self.differ_layer_route_pairs:
            edge_pairs.extend(self.differ_layer_route_pairs)

        # 映射为 GNN 索引的边
        edge_index_pairs = []
        for src, dst in edge_pairs:
            if src in self.node_to_index and dst in self.node_to_index:
                edge_index_pairs.append((
                    self.node_to_index[src],
                    self.node_to_index[dst]
                ))

        return edge_index_pairs

            


        
        


                

            
    
