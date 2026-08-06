#!/usr/bin/env python3
"""Build the publication plot from the frozen random-clock experiment CSV."""

import csv
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DATA = ROOT / "experiments/random_clock_toy_maj_20260722/combined_results.csv"


def load_rows():
    rows = []
    with DATA.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            rows.append(
                {
                    "dataset": row["dataset"],
                    "model": row["model"],
                    "cells": int(row["qca_cell_count"]),
                    "speedup": float(row["speedup"]),
                }
            )
    return rows


def main():
    rows = load_rows()
    colors = {"bistable": "#2457A6", "coherence": "#D67A17"}
    markers = {"TOY": "o", "MAJ": "^"}

    plt.rcParams.update(
        {
            "font.family": "serif",
            "font.size": 8.2,
            "axes.labelsize": 8.5,
            "xtick.labelsize": 7.5,
            "ytick.labelsize": 7.5,
            "legend.fontsize": 7.2,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )
    fig, ax = plt.subplots(figsize=(3.38, 2.35))

    for dataset in ("TOY", "MAJ"):
        for model in ("bistable", "coherence"):
            selected = [
                row for row in rows
                if row["dataset"] == dataset and row["model"] == model
            ]
            ax.scatter(
                [row["cells"] for row in selected],
                [row["speedup"] for row in selected],
                s=24,
                marker=markers[dataset],
                facecolor=colors[model],
                edgecolor="white",
                linewidth=0.45,
                alpha=0.92,
                zorder=3,
            )

    ax.axhline(3.22154, color=colors["bistable"], lw=0.85, ls="--", alpha=0.75)
    ax.axhline(4.12571, color=colors["coherence"], lw=0.85, ls="--", alpha=0.75)
    ax.text(88, 3.24, r"Bistable suite $3.22\times$", color=colors["bistable"],
            fontsize=6.9, va="bottom")
    ax.text(88, 4.15, r"Coherence suite $4.13\times$", color=colors["coherence"],
            fontsize=6.9, va="bottom")

    ax.set_xscale("log")
    ax.set_xlim(70, 6200)
    ax.set_ylim(2.85, 4.70)
    ax.set_xlabel("Physical QCA cells (log scale)")
    ax.set_ylabel("Baseline / compiled runtime")
    ax.set_xticks([100, 300, 1000, 3000])
    ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
    ax.grid(True, which="major", color="#D8DEE9", lw=0.55, zorder=0)
    ax.grid(True, which="minor", axis="x", color="#EEF1F5", lw=0.4, zorder=0)
    for spine in ax.spines.values():
        spine.set_color("#7B8493")
        spine.set_linewidth(0.65)

    legend_items = [
        Line2D([0], [0], marker="o", color="none", markerfacecolor=colors["bistable"],
               markeredgecolor="white", markersize=5.5, label="Bistable"),
        Line2D([0], [0], marker="o", color="none", markerfacecolor=colors["coherence"],
               markeredgecolor="white", markersize=5.5, label="Coherence"),
        Line2D([0], [0], marker="o", color="#555555", markerfacecolor="none",
               markersize=5.0, lw=0, label="TOY"),
        Line2D([0], [0], marker="^", color="#555555", markerfacecolor="none",
               markersize=5.0, lw=0, label="MAJ"),
    ]
    ax.legend(handles=legend_items, ncol=2, loc="lower right", frameon=True,
              borderpad=0.35, columnspacing=0.8, handletextpad=0.35,
              framealpha=0.96, edgecolor="#C8CED8")

    fig.tight_layout(pad=0.55)
    fig.savefig(HERE / "random_clock_scaling.pdf", bbox_inches="tight")
    fig.savefig(HERE / "random_clock_scaling.png", dpi=300, bbox_inches="tight")


if __name__ == "__main__":
    main()
