#!/usr/bin/env python3
"""Compare GCN and OGDF while keeping parser layers and crossing metric fixed."""

import argparse
import json
import os
import random
import sys
from pathlib import Path


ALGORITHM_ROOT = Path(__file__).resolve().parents[1] / "src" / "algorithm"
if str(ALGORITHM_ROOT) not in sys.path:
    sys.path.insert(0, str(ALGORITHM_ROOT))

import numpy as np
import torch

from src.circuit_parse import CircuitParser
from src.gcn_model_less_node import normal_graph_generate_2ddwave


def set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def main():
    parser = argparse.ArgumentParser(
        description="Compare fixed-layer GCN and OGDF crossing minimization.",
    )
    parser.add_argument("benchmarks", nargs="+", help="Verilog benchmark paths")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--gcn-epochs", type=int, default=60)
    parser.add_argument("--json", default="", help="Optional JSON result path")
    args = parser.parse_args()

    os.environ["IFCN_GCN_EPOCHS"] = str(max(0, args.gcn_epochs))
    records = []
    for benchmark in args.benchmarks:
        benchmark = os.path.abspath(benchmark)
        circuit = CircuitParser(benchmark)
        data = circuit.build_pyg_data()
        for orderer in ("gcn", "ogdf"):
            set_seed(args.seed)
            _, _, _, _, metrics = normal_graph_generate_2ddwave(
                data,
                circuit.layer_nodes,
                circuit.effective_edges,
                circuit.node_to_index,
                circuit.filePath,
                save_training_curve=False,
                crossing_orderer=orderer,
            )
            records.append(
                {
                    "benchmark": benchmark,
                    "circuit": Path(benchmark).stem,
                    "orderer": orderer,
                    "nodes": int(sum(len(nodes) for nodes in circuit.layer_nodes)),
                    "edges": int(len(circuit.effective_edges)),
                    "layers": int(len(circuit.layer_nodes)),
                    "adjacent_crossings": int(metrics["adjacent_crossings"]),
                    "order_seconds": float(metrics["total_seconds"]),
                    "ogdf_kernel_ms": metrics.get("kernel_ms"),
                }
            )

    print("circuit,orderer,nodes,edges,layers,crossings,order_seconds")
    for record in records:
        print(
            f"{record['circuit']},{record['orderer']},{record['nodes']},"
            f"{record['edges']},{record['layers']},{record['adjacent_crossings']},"
            f"{record['order_seconds']:.6f}"
        )

    if args.json:
        output_path = os.path.abspath(args.json)
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        with open(output_path, "w", encoding="utf-8") as output:
            json.dump(records, output, ensure_ascii=False, indent=2)
        print(f"comparison JSON: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

