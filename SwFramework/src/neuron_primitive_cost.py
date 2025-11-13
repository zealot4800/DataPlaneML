from __future__ import annotations

import math
from pathlib import Path
from typing import Dict, List

import matplotlib.pyplot as plt
import numpy as np

RESULTS_DIR = Path(__file__).resolve().parents[1] / "Results"
RESULTS_DIR.mkdir(parents=True, exist_ok=True)

FEATURE_SIZES = np.arange(1, 11)
PRIMITIVE_MAP: Dict[str, int] = {"N1": 1, "N2": 2, "N3": 3}

SHARED_REGS_PER_FEATURE = 1.0
# Match the reference plot where the per-neuron resource of N2 is ~92% of N1
# when F = 1. Solving ((shared + 2*private)/2) / (shared + private) = target
# for `private` gives private = shared * ((2r - 1)/(2(1 - r))).
TARGET_RELATIVE_RESOURCE_AT_F1 = 0.92
PRIVATE_REG_FACTOR = SHARED_REGS_PER_FEATURE * (
    ((2 * TARGET_RELATIVE_RESOURCE_AT_F1) - 1) / (2 * (1 - TARGET_RELATIVE_RESOURCE_AT_F1))
)

BASE_PIPELINE_CYCLES = 6


def per_neuron_resource_cost(num_neurons: int, feature_size: int) -> float:
    shared_component = SHARED_REGS_PER_FEATURE * feature_size
    private_component = PRIVATE_REG_FACTOR * (feature_size**2)
    total_resources = shared_component + num_neurons * private_component
    return total_resources / num_neurons


def collect_relative_resource_data() -> Dict[str, List[float]]:
    curves: Dict[str, List[float]] = {name: [] for name in PRIMITIVE_MAP}
    for f in FEATURE_SIZES:
        baseline = per_neuron_resource_cost(PRIMITIVE_MAP["N1"], int(f))
        for name, neurons in PRIMITIVE_MAP.items():
            cost = per_neuron_resource_cost(neurons, int(f))
            curves[name].append(cost / baseline)
    return curves


def plot_relative_resources(curves: Dict[str, List[float]]) -> None:
    plt.figure(figsize=(8, 6))
    styles = {
        "N1": {"marker": "o", "color": "#1f77b4"},
        "N2": {"marker": "s", "color": "#ff7f0e"},
        "N3": {"marker": "^", "color": "#2ca02c"},
    }
    for name, values in curves.items():
        plt.plot(
            FEATURE_SIZES,
            values,
            label=name,
            marker=styles[name]["marker"],
            color=styles[name]["color"],
            linewidth=2,
        )
    plt.xlabel("Feature Size (F)")
    plt.ylabel("Relative resource per neuron (baseline N1)")
    plt.ylim(0.85, 1.01)
    plt.xlim(1, 10)
    plt.grid(True, linestyle="--", linewidth=0.5, alpha=0.6)
    plt.legend(framealpha=0.9)
    plt.tight_layout()
    plt.savefig(RESULTS_DIR / "Normalized_resource.png", dpi=300)
    plt.close()


def cycles_for_feature_size(feature_size: int) -> int:
    reduction_depth = max(1, math.ceil(math.log2(feature_size + 1)))
    return BASE_PIPELINE_CYCLES + reduction_depth


def plot_latency_curve() -> None:
    cycles = [cycles_for_feature_size(int(f)) for f in FEATURE_SIZES]
    plt.figure(figsize=(8, 6))
    plt.plot(
        FEATURE_SIZES,
        cycles,
        marker="o",
        linewidth=2,
        color="#0d6efd",
    )
    plt.xlabel("Feature Size (F)")
    plt.ylabel("No. of Cycles")
    plt.xticks(FEATURE_SIZES)
    plt.yticks(range(min(cycles), max(cycles) + 1))
    plt.grid(True, linestyle="--", linewidth=0.5, alpha=0.6)
    plt.tight_layout()
    plt.savefig(RESULTS_DIR / "size_vs_latency.png", dpi=300)
    plt.close()


def main() -> None:
    curves = collect_relative_resource_data()
    plot_relative_resources(curves)
    plot_latency_curve()
    print(f"Saved neuron primitive cost plots under: {RESULTS_DIR}")


if __name__ == "__main__":
    main()
