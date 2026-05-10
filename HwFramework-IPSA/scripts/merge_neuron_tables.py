from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any, Dict, Iterable


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_BASE = REPO_ROOT / "rP4-compiler" / "rp4c" / "basic.json"
DEFAULT_OVERLAY_REL = [
    pathlib.Path("sw-src") / "tables" / "metadata_context.json",
    pathlib.Path("sw-src") / "tables" / "sigmoid_table.json",
    pathlib.Path("sw-src") / "tables" / "processor.json",
]


def deep_merge(dst: Dict[str, Any], src: Dict[str, Any]) -> Dict[str, Any]:
    """Recursively merge dictionaries, letting overlay keys win."""
    for key, value in src.items():
        if isinstance(value, dict) and isinstance(dst.get(key), dict):
            dst[key] = deep_merge(dst[key], value)
        else:
            dst[key] = value
    return dst


def ensure_gateway_defaults(config: Dict[str, Any]) -> None:
    """Ensure each processor includes a gateway stanza."""
    default_gateway = {
        "expressions": [],
        "next_table": {
            "default": {"type": "STAGE", "value": -1},
            "entries": []
        }
    }

    for idx in range(16):
        key = f"processor_{idx}"
        proc = config.get(key)
        if isinstance(proc, dict) and "gateway" not in proc:
            proc["gateway"] = json.loads(json.dumps(default_gateway))


def has_neuron_operator(node: Any) -> bool:
    if isinstance(node, dict):
        if node.get("op") in {"npb", "sigmoid"}:
            return True
        return any(has_neuron_operator(value) for value in node.values())
    if isinstance(node, list):
        return any(has_neuron_operator(value) for value in node)
    return False


def validate_neuron_overlay(config: Dict[str, Any]) -> tuple[int, int, int, int]:
    metadata = config.get("metadata")
    contexts = config.get("neuron_primitive_contexts")
    sigmoid_table = config.get("sigmoid_table")
    sigmoid_entries = sigmoid_table.get("entries") if isinstance(sigmoid_table, dict) else None

    if not isinstance(metadata, list) or not metadata:
        raise SystemExit("Validation failed: merged config is missing metadata headers.")
    if not isinstance(contexts, list) or not contexts:
        raise SystemExit("Validation failed: merged config is missing neuron_primitive_contexts.")
    if not isinstance(sigmoid_entries, list) or not sigmoid_entries:
        raise SystemExit("Validation failed: merged config is missing sigmoid_table entries.")

    neuron_processor_count = 0
    for idx in range(16):
        proc = config.get(f"processor_{idx}")
        if isinstance(proc, dict) and has_neuron_operator(proc.get("executor")):
            neuron_processor_count += 1

    if neuron_processor_count == 0:
        raise SystemExit("Validation failed: merged config has no processor using neuron operators.")

    return len(metadata), len(contexts), len(sigmoid_entries), neuron_processor_count


def load_json(path: pathlib.Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base",
        type=pathlib.Path,
        default=DEFAULT_BASE,
        help="Path to the base dataplane JSON produced by rp4_compiler "
             f"(default: {DEFAULT_BASE})",
    )
    parser.add_argument(
        "--overlay",
        action="append",
        type=pathlib.Path,
        default=None,
        help="Overlay JSON files to layer on top of the base config. "
             "Repeat for multiple overlays (default: metadata_context, "
             "sigmoid_table, processor).",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        required=True,
        help="Destination file for the merged JSON.",
    )
    return parser.parse_args()


def resolve_paths(paths: Iterable[pathlib.Path]) -> Iterable[pathlib.Path]:
    for path in paths:
        yield path if path.is_absolute() else (REPO_ROOT / path)


def main() -> None:
    args = parse_args()
    base_path = args.base if args.base.is_absolute() else REPO_ROOT / args.base
    overlay_list = args.overlay or DEFAULT_OVERLAY_REL
    overlay_paths = list(resolve_paths(overlay_list))

    config = load_json(base_path)
    for overlay in overlay_paths:
        config = deep_merge(config, load_json(overlay))

    ensure_gateway_defaults(config)
    metadata_count, context_count, sigmoid_count, neuron_processor_count = validate_neuron_overlay(config)

    output_path = args.output if args.output.is_absolute() else REPO_ROOT / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as handle:
        json.dump(config, handle, indent=2)
        handle.write("\n")

    rel_out = output_path.relative_to(REPO_ROOT)
    overlay_str = ", ".join(str(p.relative_to(REPO_ROOT)) for p in overlay_paths)
    print(f"Merged {base_path} with [{overlay_str}] -> {rel_out}")
    print(
        "Validated overlay: "
        f"metadata_headers={metadata_count}, "
        f"neuron_contexts={context_count}, "
        f"sigmoid_entries={sigmoid_count}, "
        f"neuron_processors={neuron_processor_count}"
    )


if __name__ == "__main__":
    main()
