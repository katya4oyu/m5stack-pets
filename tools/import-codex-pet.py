#!/usr/bin/env python3
"""Import Codex pet assets into this repository's asset layout."""

from __future__ import annotations

import argparse
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path


STATE_ORDER = [
    "idle",
    "running-right",
    "running-left",
    "waving",
    "jumping",
    "failed",
    "waiting",
    "running",
    "review",
]

DEFAULT_DURATIONS_MS = {
    "idle": [280, 110, 110, 140, 140, 320],
    "running-right": [120, 120, 120, 120, 120, 120, 120, 220],
    "running-left": [120, 120, 120, 120, 120, 120, 120, 220],
    "waving": [140, 140, 140, 280],
    "jumping": [140, 140, 140, 140, 280],
    "failed": [140, 140, 140, 140, 140, 140, 140, 240],
    "waiting": [150, 150, 150, 150, 150, 260],
    "running": [120, 120, 120, 120, 120, 220],
    "review": [150, 150, 150, 150, 150, 280],
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Copy Codex pet assets into assets/<pet-id>/source."
    )
    parser.add_argument(
        "--run-dir",
        required=True,
        type=Path,
        help="Codex pet run directory, for example .../bitomos-umi-run.",
    )
    parser.add_argument(
        "--pet-id",
        required=True,
        help="Asset id under assets/, for example bitomos-umi.",
    )
    parser.add_argument(
        "--display-name",
        help="Human-readable pet name. Defaults to --pet-id.",
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=Path("assets"),
        help="Destination asset root. Defaults to ./assets.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace assets/<pet-id>/source if it already exists.",
    )
    return parser.parse_args()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise SystemExit(f"missing required file: {path}")


def require_dir(path: Path) -> None:
    if not path.is_dir():
        raise SystemExit(f"missing required directory: {path}")


def copy_tree(src: Path, dst: Path) -> None:
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".DS_Store", "frames-manifest.json"))


def durations_for(state: str, frame_count: int) -> list[int]:
    durations = DEFAULT_DURATIONS_MS.get(state)
    if not durations:
        return [120 for _ in range(frame_count)]
    expanded = durations[:frame_count]
    while len(expanded) < frame_count:
        expanded.append(expanded[-1])
    return expanded


def validate_frames(frames_dir: Path) -> dict[str, list[str]]:
    require_dir(frames_dir)
    copied: dict[str, list[str]] = {}

    state_dirs = [path for path in frames_dir.iterdir() if path.is_dir()]
    known_order = {state: index for index, state in enumerate(STATE_ORDER)}
    state_dirs.sort(key=lambda path: (known_order.get(path.name, len(STATE_ORDER)), path.name))

    if not state_dirs:
        raise SystemExit(f"no state frame directories found under {frames_dir}")

    for state_dir in state_dirs:
        state = state_dir.name
        state_dir = frames_dir / state
        files = sorted(state_dir.glob("*.png"))
        if not files:
            raise SystemExit(f"{state}: no PNG frames found")

        expected_names = [f"{index:02d}.png" for index in range(len(files))]
        actual_names = [path.name for path in files]
        if actual_names != expected_names:
            raise SystemExit(
                f"{state}: expected frame names {expected_names}, found {actual_names}"
            )

        copied[state] = actual_names

    return copied


def main() -> None:
    args = parse_args()
    run_dir = args.run_dir.expanduser().resolve()
    assets_dir = args.assets_dir
    pet_id = args.pet_id
    display_name = args.display_name or pet_id

    frames_dir = run_dir / "frames"
    final_dir = run_dir / "final"
    source_dir = assets_dir / pet_id / "source"

    frame_manifest = validate_frames(frames_dir)
    require_file(final_dir / "spritesheet.webp")

    if source_dir.exists():
        if not args.force:
            raise SystemExit(f"destination exists; use --force to replace: {source_dir}")
        shutil.rmtree(source_dir)

    (source_dir / "atlas").mkdir(parents=True)

    copy_tree(frames_dir, source_dir / "frames")
    shutil.copy2(final_dir / "spritesheet.webp", source_dir / "atlas" / "spritesheet.webp")

    manifest = {
        "petId": pet_id,
        "displayName": display_name,
        "format": "codex-pet",
        "importedAt": datetime.now(timezone.utc).isoformat(),
        "sourceRun": run_dir.name,
        "cell": {"width": 192, "height": 208},
        "atlas": {"columns": 8, "rows": 9},
        "states": [
            {
                "name": state,
                "frames": len(frames),
                "durationsMs": durations_for(state, len(frames)),
            }
            for state, frames in frame_manifest.items()
        ],
        "files": {
            "frames": "frames/<state>/<nn>.png",
            "spritesheetWebp": "atlas/spritesheet.webp",
        },
    }

    with (source_dir / "codex-pet.json").open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    print(json.dumps({"ok": True, "sourceDir": str(source_dir)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
