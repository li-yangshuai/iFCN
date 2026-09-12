#!/usr/bin/env python3
"""Build IFCN retrieval memory and offline-pretrain the universal graph agent."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import random
import time

import numpy as np
import torch

from utils import add_project_root

add_project_root()

from src.ifcn_layout_dataset import (  # noqa: E402
    IFCNLayout,
    build_ifcn_dataset,
    save_jsonl_index,
    split_by_topology,
)
from src.ifcn_offline_learning import (  # noqa: E402
    build_ifcn_clock_features,
    build_offline_ifcn_sample,
    offline_pretraining_loss,
)
from src.layout_retrieval_memory import (  # noqa: E402
    GRAPH_DESCRIPTOR_DIM,
    MEMORY_CONTEXT_DIM,
    LayoutRetrievalMemory,
    graph_descriptor_from_circuit,
)
from src.universal_graph_policy import UniversalGraphPolicy  # noqa: E402


DEFAULT_OUTPUT_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../../../results/memory_universal_agent")
)
DEFAULT_IFCN_ROOTS = [
    os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../../..")),
]


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Quality-filter IFCN demonstrations, build topology retrieval memory, "
            "and pretrain the recurrent dynamic graph policy."
        )
    )
    parser.add_argument("--ifcn-roots", nargs="+", default=DEFAULT_IFCN_ROOTS)
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--seed", type=int, default=20260710)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--epochs", type=int, default=25)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=3e-4)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--hidden-dim", type=int, default=128)
    parser.add_argument("--message-passing-steps", type=int, default=3)
    parser.add_argument("--memory-dim", type=int, default=64)
    parser.add_argument("--retrieval-top-k", type=int, default=4)
    parser.add_argument("--train-ratio", type=float, default=0.70)
    parser.add_argument("--val-ratio", type=float, default=0.15)
    parser.add_argument("--test-ratio", type=float, default=0.15)
    parser.add_argument("--max-layouts", type=int, default=0)
    parser.add_argument("--perturb-scale", type=float, default=0.30)
    parser.add_argument("--train-layouts-per-topology", type=int, default=3)
    parser.add_argument("--eval-perturbations", type=int, default=2)
    parser.add_argument("--max-train-area-ratio", type=float, default=1.50)
    parser.add_argument("--imitation-weight", type=float, default=1.0)
    parser.add_argument("--placement-weight", type=float, default=2.0)
    parser.add_argument("--route-weight", type=float, default=1.0)
    parser.add_argument("--quality-weight", type=float, default=0.25)
    parser.add_argument("--write-dataset-index", action="store_true")
    parser.add_argument("--log-interval", type=int, default=1)
    return parser.parse_args()


def resolve_device(requested: str) -> torch.device:
    if requested == "cpu":
        return torch.device("cpu")
    if requested in ("auto", "cuda") and torch.cuda.is_available():
        return torch.device("cuda")
    if requested == "cuda":
        raise RuntimeError("CUDA was requested but is unavailable")
    return torch.device("cpu")


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def record_graph(record: IFCNLayout) -> dict:
    return {
        "nodes": [
            {
                "id": int(node.node_id),
                "type": str(node.node_type),
            }
            for node in record.nodes
        ],
        "edges": [
            [int(path.source), int(path.target)]
            for path in record.paths
        ],
        "topology_hash": record.topology_hash,
    }


def record_clock_descriptor(record: IFCNLayout) -> np.ndarray:
    return build_ifcn_clock_features(record).detach().cpu().numpy()


def record_quality_metrics(record: IFCNLayout) -> dict[str, float]:
    quality = record.quality
    candidates = {
        "legal": float(quality.valid_for_training),
        "area": quality.area,
        "wirelength": quality.wirelength,
        "crossings": quality.crossings,
        "cell_count": quality.cell_count,
        "runtime": quality.runtime,
        "critical_path": quality.critical_path,
        "phase_conflicts": float(quality.phase_conflicts),
    }
    return {
        name: float(value)
        for name, value in candidates.items()
        if value is not None and math.isfinite(float(value))
    }


def record_route_hints(record: IFCNLayout) -> list[dict]:
    return [
        {
            "source": int(path.source),
            "target": int(path.target),
            "directions": list(path.directions),
            "waypoints": [list(point) for point in path.normalized_waypoints],
            "steps": int(path.steps),
            "turns": int(path.turns),
        }
        for path in record.paths
    ]


def build_retrieval_memory(records: list[IFCNLayout]) -> LayoutRetrievalMemory:
    memory = LayoutRetrievalMemory(
        graph_descriptor_dim=GRAPH_DESCRIPTOR_DIM,
        clock_descriptor_dim=15,
        context_dim=MEMORY_CONTEXT_DIM,
    )
    for record in records:
        graph = record_graph(record)
        memory.add_layout(
            topology_hash=record.topology_hash,
            graph_descriptor=graph_descriptor_from_circuit(graph),
            clock_descriptor=record_clock_descriptor(record),
            normalized_placement={
                str(node.node_id): tuple(node.normalized_position)
                for node in record.nodes
            },
            route_hints=record_route_hints(record),
            quality_metrics=record_quality_metrics(record),
            entry_id=record.layout_hash,
            metadata={
                "source_path": record.source_path,
                "circuit_name": record.circuit_name,
                "dataset_topology_hash": record.topology_hash,
                "split": "train",
            },
        )
    return memory


def retrieval_context(
    memory: LayoutRetrievalMemory,
    record: IFCNLayout,
    top_k: int,
) -> np.ndarray:
    graph = record_graph(record)
    context, _results = memory.retrieve_context(
        graph_descriptor_from_circuit(graph),
        clock_descriptor=record_clock_descriptor(record),
        topology_hash=record.topology_hash,
        top_k=max(0, int(top_k)),
        prefer_exact=False,
        exclude_hashes={record.topology_hash},
    )
    return context


def best_area_map(records: list[IFCNLayout]) -> dict[str, float]:
    output: dict[str, float] = {}
    for record in records:
        output.setdefault(record.topology_hash, math.inf)
        area = (
            float(record.quality.area)
            if record.quality.area is not None
            else math.inf
        )
        if math.isfinite(area) and area > 0.0:
            output[record.topology_hash] = min(output[record.topology_hash], area)
    return {
        topology_hash: area if math.isfinite(area) else 1.0
        for topology_hash, area in output.items()
    }


def aggregate_metrics(
    rows: list[tuple[str, dict[str, float]]],
) -> dict[str, float]:
    if not rows:
        return {}
    grouped: dict[str, list[dict[str, float]]] = {}
    for topology_hash, metrics in rows:
        grouped.setdefault(topology_hash, []).append(metrics)
    metric_names = tuple(rows[0][1])
    topology_means = {
        topology_hash: {
            name: float(np.mean([row[name] for row in topology_rows]))
            for name in metric_names
        }
        for topology_hash, topology_rows in grouped.items()
    }
    output = {
        name: float(
            np.mean([metrics[name] for metrics in topology_means.values()])
        )
        for name in metric_names
    }
    output["evaluated_samples"] = float(len(rows))
    output["evaluated_topologies"] = float(len(grouped))
    return output


def _group_by_topology(records: list[IFCNLayout]) -> dict[str, list[IFCNLayout]]:
    grouped: dict[str, list[IFCNLayout]] = {}
    for record in records:
        grouped.setdefault(record.topology_hash, []).append(record)
    return {
        topology_hash: sorted(values, key=lambda record: record.layout_hash)
        for topology_hash, values in grouped.items()
    }


def topology_balanced_epoch_records(
    records: list[IFCNLayout],
    *,
    samples_per_topology: int,
    seed: int,
) -> list[IFCNLayout]:
    grouped = _group_by_topology(records)
    rng = random.Random(int(seed))
    output = []
    topology_hashes = sorted(grouped)
    for _round in range(max(1, int(samples_per_topology))):
        round_topologies = list(topology_hashes)
        rng.shuffle(round_topologies)
        for topology_hash in round_topologies:
            choices = grouped[topology_hash]
            output.append(choices[rng.randrange(len(choices))])
    return output


def compact_training_records(
    records: list[IFCNLayout],
    best_areas: dict[str, float],
    max_area_ratio: float,
) -> list[IFCNLayout]:
    if float(max_area_ratio) <= 0.0:
        return list(records)
    selected = []
    for topology_hash, topology_records in _group_by_topology(records).items():
        best_area = float(best_areas[topology_hash])
        compact = [
            record
            for record in topology_records
            if record.quality.area is not None
            and math.isfinite(float(record.quality.area))
            and 0.0 < float(record.quality.area) <= best_area * float(max_area_ratio)
        ]
        selected.extend(compact or topology_records[:1])
    return sorted(selected, key=lambda record: (record.topology_hash, record.layout_hash))


def fixed_evaluation_seed(base_seed: int, layout_hash: str, perturbation: int) -> int:
    digest = hashlib.sha256(
        f"{int(base_seed)}:eval:{layout_hash}:{int(perturbation)}".encode("ascii")
    ).digest()
    return int.from_bytes(digest[:8], "big", signed=False)


def precompute_retrieval_contexts(
    memory: LayoutRetrievalMemory,
    records: list[IFCNLayout],
    top_k: int,
) -> dict[str, np.ndarray]:
    contexts = {}
    for record in records:
        contexts[record.layout_hash] = retrieval_context(memory, record, top_k)
    return contexts


def run_records(
    model: UniversalGraphPolicy,
    records: list[IFCNLayout],
    retrieval_contexts: dict[str, np.ndarray],
    best_areas: dict[str, float],
    args,
    device: torch.device,
    *,
    optimizer: torch.optim.Optimizer | None,
    epoch: int,
) -> dict[str, float]:
    training = optimizer is not None
    model.train(training)
    if training:
        ordered_tasks = [
            (record, 0)
            for record in topology_balanced_epoch_records(
                records,
                samples_per_topology=args.train_layouts_per_topology,
                seed=args.seed + epoch * 1_000_003,
            )
        ]
    else:
        ordered_tasks = [
            (record, perturbation)
            for record in sorted(records, key=lambda item: item.layout_hash)
            for perturbation in range(max(1, int(args.eval_perturbations)))
        ]
    metric_rows: list[tuple[str, dict[str, float]]] = []
    batch_size = max(1, int(args.batch_size))
    for record_index, (record, perturbation) in enumerate(ordered_tasks):
        if training and record_index % batch_size == 0:
            optimizer.zero_grad()
        sample_seed = (
            args.seed + epoch * 1_000_003 + record_index
            if training
            else fixed_evaluation_seed(args.seed, record.layout_hash, perturbation)
        )
        sample = build_offline_ifcn_sample(
            record,
            best_area_for_topology=best_areas[record.topology_hash],
            retrieval_features=retrieval_contexts[record.layout_hash],
            seed=sample_seed,
            perturb_scale=args.perturb_scale,
        )
        with torch.set_grad_enabled(training):
            loss, metrics = offline_pretraining_loss(
                model,
                sample,
                device,
                imitation_weight=args.imitation_weight,
                placement_weight=args.placement_weight,
                route_weight=args.route_weight,
                quality_weight=args.quality_weight,
            )
            if training:
                batch_start = (record_index // batch_size) * batch_size
                current_batch_size = min(
                    batch_size,
                    len(ordered_tasks) - batch_start,
                )
                (loss / float(current_batch_size)).backward()
                if (
                    (record_index + 1) % batch_size == 0
                    or record_index == len(ordered_tasks) - 1
                ):
                    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
                    optimizer.step()
        metric_rows.append((record.topology_hash, metrics))
    return aggregate_metrics(metric_rows)


def checkpoint_payload(model, args, memory_path, manifest, epoch, metrics):
    return {
        "schema_version": 2,
        "universal_dynamic_graph_policy": True,
        "offline_ifcn_pretrained": True,
        "model_state_dict": model.state_dict(),
        "epoch": int(epoch),
        "model": {
            "node_feature_dim": int(model.node_feature_dim),
            "edge_feature_dim": int(model.edge_feature_dim),
            "clock_feature_dim": int(model.clock_feature_dim),
            "action_feature_dim": int(model.action_feature_dim),
            "action_type_count": int(model.action_type_count),
            "retrieval_feature_dim": int(model.retrieval_feature_dim),
            "episode_feature_dim": int(model.episode_feature_dim),
            "memory_dim": int(model.memory_dim),
            "hidden_dim": int(model.hidden_dim),
            "message_passing_steps": int(model.message_passing_steps),
        },
        "retrieval_memory": os.path.abspath(memory_path),
        "dataset_manifest": manifest,
        "metrics": metrics,
        "config": vars(args),
    }


def write_json(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as output:
        json.dump(payload, output, ensure_ascii=False, indent=2)


def main():
    args = parse_args()
    if int(args.epochs) <= 0 or int(args.batch_size) <= 0:
        raise ValueError("epochs and batch-size must be positive")
    if int(args.train_layouts_per_topology) <= 0 or int(args.eval_perturbations) <= 0:
        raise ValueError(
            "train-layouts-per-topology and eval-perturbations must be positive"
        )
    set_seed(args.seed)
    device = resolve_device(args.device)
    os.makedirs(args.output_dir, exist_ok=True)
    started = time.perf_counter()
    print(f"[IFCN-Offline] scanning roots={args.ifcn_roots}")
    records = build_ifcn_dataset(
        args.ifcn_roots,
        valid_only=True,
        deduplicate=True,
        pareto_only=True,
    )
    if int(args.max_layouts) > 0:
        records = records[: int(args.max_layouts)]
    if not records:
        raise RuntimeError("no valid IFCN layouts were found")
    splits = split_by_topology(
        records,
        train_ratio=args.train_ratio,
        val_ratio=args.val_ratio,
        test_ratio=args.test_ratio,
        seed=args.seed,
    )
    if not splits["train"]:
        raise RuntimeError("topology split produced an empty training set")
    best_areas = best_area_map(records)
    offline_train_records = compact_training_records(
        splits["train"],
        best_areas,
        args.max_train_area_ratio,
    )
    if args.write_dataset_index:
        for split_name, split_records in splits.items():
            save_jsonl_index(
                split_records,
                os.path.join(args.output_dir, f"ifcn_{split_name}.jsonl"),
            )

    memory = build_retrieval_memory(splits["train"])
    memory_path = memory.save(os.path.join(args.output_dir, "layout_retrieval_memory.json"))
    context_started = time.perf_counter()
    retrieval_contexts = precompute_retrieval_contexts(
        memory,
        records,
        args.retrieval_top_k,
    )
    print(
        f"[IFCN-Offline] records={len(records)} "
        f"topologies={len({record.topology_hash for record in records})} "
        f"offline_train_records={len(offline_train_records)} "
        f"contexts_sec={time.perf_counter() - context_started:.2f}"
    )
    manifest = {
        "record_count": len(records),
        "split_counts": {name: len(values) for name, values in splits.items()},
        "offline_train_record_count": len(offline_train_records),
        "offline_train_topology_count": len(
            {record.topology_hash for record in offline_train_records}
        ),
        "max_train_area_ratio": float(args.max_train_area_ratio),
        "train_layouts_per_topology_per_epoch": int(
            args.train_layouts_per_topology
        ),
        "eval_perturbations_per_layout": int(args.eval_perturbations),
        "metric_aggregation": "topology_macro",
        "topology_counts": {
            name: len({record.topology_hash for record in values})
            for name, values in splits.items()
        },
        "topology_hashes": {
            name: sorted({record.topology_hash for record in values})
            for name, values in splits.items()
        },
        "layout_hash_digest": hashlib.sha256(
            "\n".join(sorted(record.layout_hash for record in records)).encode("ascii")
        ).hexdigest(),
    }
    write_json(os.path.join(args.output_dir, "ifcn_dataset_manifest.json"), manifest)
    model = UniversalGraphPolicy(
        hidden_dim=args.hidden_dim,
        message_passing_steps=args.message_passing_steps,
        memory_dim=args.memory_dim,
    ).to(device)
    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=args.learning_rate,
        weight_decay=args.weight_decay,
    )
    history = []
    best_validation_loss = math.inf
    best_checkpoint = os.path.join(args.output_dir, "ifcn_memory_policy_best.pt")
    final_checkpoint = os.path.join(args.output_dir, "ifcn_memory_policy_final.pt")
    validation_records = splits["val"] or splits["train"]
    for epoch in range(1, int(args.epochs) + 1):
        train_metrics = run_records(
            model,
            offline_train_records,
            retrieval_contexts,
            best_areas,
            args,
            device,
            optimizer=optimizer,
            epoch=epoch,
        )
        with torch.no_grad():
            validation_metrics = run_records(
                model,
                validation_records,
                retrieval_contexts,
                best_areas,
                args,
                device,
                optimizer=None,
                epoch=epoch,
            )
        row = {
            "epoch": int(epoch),
            **{f"train_{name}": value for name, value in train_metrics.items()},
            **{f"val_{name}": value for name, value in validation_metrics.items()},
        }
        history.append(row)
        validation_loss = float(validation_metrics.get("loss", math.inf))
        payload = checkpoint_payload(
            model,
            args,
            str(memory_path),
            manifest,
            epoch,
            row,
        )
        if validation_loss < best_validation_loss:
            best_validation_loss = validation_loss
            torch.save(payload, best_checkpoint)
        if epoch % max(1, int(args.log_interval)) == 0 or epoch == 1:
            print(
                f"[IFCN-Offline] epoch={epoch}/{args.epochs} "
                f"train_loss={train_metrics.get('loss', 0.0):.5f} "
                f"val_loss={validation_loss:.5f} "
                f"val_action_acc={validation_metrics.get('teacher_action_accuracy', 0.0):.3f}"
            )
    torch.save(
        checkpoint_payload(
            model,
            args,
            str(memory_path),
            manifest,
            int(args.epochs),
            history[-1],
        ),
        final_checkpoint,
    )
    history_path = os.path.join(args.output_dir, "ifcn_offline_training.csv")
    with open(history_path, "w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(history[0].keys()))
        writer.writeheader()
        writer.writerows(history)
    test_metrics = {}
    if splits["test"]:
        try:
            best_payload = torch.load(best_checkpoint, map_location=device, weights_only=False)
        except TypeError:
            best_payload = torch.load(best_checkpoint, map_location=device)
        model.load_state_dict(best_payload["model_state_dict"], strict=True)
        with torch.no_grad():
            test_metrics = run_records(
                model,
                splits["test"],
                retrieval_contexts,
                best_areas,
                args,
                device,
                optimizer=None,
                epoch=int(args.epochs) + 1,
            )
    summary = {
        "elapsed_sec": float(time.perf_counter() - started),
        "device": str(device),
        "best_validation_loss": float(best_validation_loss),
        "best_checkpoint": best_checkpoint,
        "final_checkpoint": final_checkpoint,
        "retrieval_memory": str(memory_path),
        "dataset_manifest": manifest,
        "test_metrics": test_metrics,
    }
    write_json(os.path.join(args.output_dir, "ifcn_offline_summary.json"), summary)
    print(f"[IFCN-Offline] completed: {json.dumps(summary, ensure_ascii=False)}")


if __name__ == "__main__":
    main()
