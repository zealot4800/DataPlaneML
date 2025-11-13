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

    output_path = args.output if args.output.is_absolute() else REPO_ROOT / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as handle:
        json.dump(config, handle, indent=2)
        handle.write("\n")

    rel_out = output_path.relative_to(REPO_ROOT)
    overlay_str = ", ".join(str(p.relative_to(REPO_ROOT)) for p in overlay_paths)
    print(f"Merged {base_path} with [{overlay_str}] -> {rel_out}")


if __name__ == "__main__":
    main()
