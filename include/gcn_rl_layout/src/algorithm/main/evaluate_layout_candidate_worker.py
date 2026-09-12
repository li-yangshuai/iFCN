#!/usr/bin/env python3
import json
import os
import sys
import time

from utils import add_project_root

add_project_root()

from src.circuit_parse import CircuitParser
from test_randomPhase import evaluate_layout_candidate


def normalize_candidate(raw_candidate):
    node_positions = {
        int(node_id): (int(coord[0]), int(coord[1]))
        for node_id, coord in raw_candidate["node_positions"].items()
    }
    return {
        "strategy": raw_candidate.get("strategy", "rl"),
        "orientation": raw_candidate["orientation"],
        "x_spacing": raw_candidate.get("x_spacing", "n/a"),
        "y_spacing": raw_candidate.get("y_spacing", "n/a"),
        "node_positions": node_positions,
        "routing_embedding_guidance": bool(raw_candidate.get("routing_embedding_guidance", False)),
    }


def normalize_embedding_scores(raw_scores):
    if not raw_scores:
        return None
    return {int(node_id): float(score) for node_id, score in raw_scores.items()}


def serialize_result(result):
    board = result["board"]
    routed_paths = {}
    routed_path_phases = {}
    for edge, path in result["routed_paths"].items():
        src, dst = int(edge[0]), int(edge[1])
        key = f"{src},{dst}"
        serialized_path = [(int(coord[0]), int(coord[1])) for coord in path]
        routed_paths[key] = serialized_path
        routed_path_phases[key] = [
            (int(coord[0]), int(coord[1]), int(board.getPhase((int(coord[0]), int(coord[1])))))
            for coord in serialized_path
        ]

    return {
        "node_positions": {
            str(node_id): [int(coord[0]), int(coord[1])]
            for node_id, coord in result["node_positions"].items()
        },
        "routed_paths": routed_paths,
        "routed_path_phases": routed_path_phases,
        "failed_edges": [[int(src), int(dst)] for src, dst in result["failed_edges"]],
        "x_spacing": result["x_spacing"],
        "y_spacing": result["y_spacing"],
        "layout_strategy": result["layout_strategy"],
        "layout_orientation": result["layout_orientation"],
        "routing_embedding_guidance": bool(result.get("routing_embedding_guidance", False)),
        "width": int(result["width"]),
        "height": int(result["height"]),
        "area": float(result["area"]),
        "io_exposure_penalty": int(result["io_exposure_penalty"]),
        "route_overhang_penalty": int(result["route_overhang_penalty"]),
        "direction_violation_count": int(result["direction_violation_count"]),
    }


def main():
    payload = json.load(sys.stdin)
    start_time = time.perf_counter()
    circuit = CircuitParser(
        os.path.abspath(payload["benchmark"]),
        parse_mode=payload.get("parse_mode", "auto"),
    )
    result = evaluate_layout_candidate(
        normalize_candidate(payload["candidate"]),
        circuit,
        int(payload["phase_cycle"]),
        int(payload["padding"]),
        int(payload["max_same_phase"]),
        embedding_scores=normalize_embedding_scores(payload.get("embedding_scores")),
    )
    serializable = serialize_result(result)
    serializable["worker_runtime_sec"] = float(time.perf_counter() - start_time)
    json.dump(serializable, sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
