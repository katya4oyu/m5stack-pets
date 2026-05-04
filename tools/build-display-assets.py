#!/usr/bin/env python3
"""Build display-ready runtime assets from imported Codex pet source frames."""

from __future__ import annotations

import argparse
import json
import shutil
import re
from pathlib import Path

try:
    from PIL import Image
except ModuleNotFoundError as error:
    raise SystemExit(
        "Pillow is required. Install it for your Python environment, or run this "
        "with a Python runtime that already includes PIL."
    ) from error


RESAMPLERS = {
    "nearest": Image.Resampling.NEAREST,
    "box": Image.Resampling.BOX,
    "lanczos": Image.Resampling.LANCZOS,
}

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
        description="Convert assets/<pet-id>/source/frames into display-<width> runtime assets."
    )
    parser.add_argument(
        "--pet-dir",
        required=True,
        type=Path,
        help="Pet asset directory, for example assets/bitomos-umi.",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=96,
        help="Output frame width. Height defaults to preserving 192:208 aspect.",
    )
    parser.add_argument(
        "--height",
        type=int,
        help="Output frame height. Defaults to round(width * 208 / 192).",
    )
    parser.add_argument(
        "--resample",
        choices=sorted(RESAMPLERS),
        default="nearest",
        help="Resize filter. nearest preserves the pixel-pet feel.",
    )
    parser.add_argument(
        "--output-name",
        help="Output folder name under pet dir. Defaults to display-<width>-png.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace the output directory if it already exists.",
    )
    return parser.parse_args()


def load_manifest(pet_dir: Path) -> dict:
    manifest_path = pet_dir / "source" / "codex-pet.json"
    if not manifest_path.is_file():
        raise SystemExit(f"missing source manifest: {manifest_path}")
    with manifest_path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def identifier(value: str) -> str:
    cleaned = re.sub(r"[^0-9A-Za-z_]", "_", value).strip("_").lower()
    if not cleaned:
        return "pet"
    if cleaned[0].isdigit():
        return f"pet_{cleaned}"
    return cleaned


def durations_for(state: dict) -> list[int]:
    frame_count = int(state["frames"])
    durations = state.get("durationsMs") or DEFAULT_DURATIONS_MS.get(state["name"])
    if durations and len(durations) == frame_count:
        return [int(value) for value in durations]
    if durations:
        expanded = [int(value) for value in durations[:frame_count]]
        while len(expanded) < frame_count:
            expanded.append(expanded[-1])
        return expanded
    return [120 for _ in range(frame_count)]


def header_durations_for(state: dict) -> list[int]:
    if "durationsMs" in state:
        return [int(value) for value in state["durationsMs"]]
    return durations_for(state)


def header_frame_count_for(state: dict) -> int:
    if "frameCount" in state:
        return int(state["frameCount"])
    return int(state["frames"])


def resize_alpha_aware(image: Image.Image, size: tuple[int, int], resample: int) -> Image.Image:
    """Resize RGBA with premultiplied alpha, then return straight-alpha RGBA."""
    rgba = image.convert("RGBA")
    pixels = rgba.tobytes()
    premultiplied = bytearray()
    for offset in range(0, len(pixels), 4):
        red = pixels[offset]
        green = pixels[offset + 1]
        blue = pixels[offset + 2]
        alpha = pixels[offset + 3]
        premultiplied.extend(
            (
                (red * alpha + 127) // 255,
                (green * alpha + 127) // 255,
                (blue * alpha + 127) // 255,
                alpha,
            )
        )

    resized = Image.frombytes("RGBA", rgba.size, bytes(premultiplied)).resize(size, resample)
    pixels = resized.tobytes()
    straight = bytearray()
    for offset in range(0, len(pixels), 4):
        red = pixels[offset]
        green = pixels[offset + 1]
        blue = pixels[offset + 2]
        alpha = pixels[offset + 3]
        if alpha == 0:
            straight.extend((0, 0, 0, 0))
        else:
            straight.extend(
                (
                    min(255, (red * 255 + alpha // 2) // alpha),
                    min(255, (green * 255 + alpha // 2) // alpha),
                    min(255, (blue * 255 + alpha // 2) // alpha),
                    alpha,
                )
            )
    return Image.frombytes("RGBA", resized.size, bytes(straight))


def write_cpp_header(
    output_dir: Path,
    manifest: dict,
    display_width: int,
    display_height: int,
) -> Path:
    pet_id = manifest["petId"]
    ns = identifier(pet_id)
    header_path = output_dir / f"{identifier(pet_id)}_anim.h"
    states = manifest["states"]

    lines: list[str] = [
        "#pragma once",
        "",
        '#include "codex_pet_anim.h"',
        "",
        f"namespace {ns} {{",
        "",
    ]

    for state in states:
        state_id = identifier(state["name"])
        durations = ", ".join(str(value) for value in header_durations_for(state))
        lines.append(f"static constexpr uint16_t k_{state_id}_durations_ms[] = {{{durations}}};")

    lines.extend(["", "static constexpr codex_pet::StateInfo k_states[] = {"])
    for state in states:
        state_id = identifier(state["name"])
        frame_count = header_frame_count_for(state)
        lines.append(
            f'    {{"{state["name"]}", {frame_count}, '
            f"k_{state_id}_durations_ms}},"
        )
    lines.extend(
        [
            "};",
            "",
            "static constexpr codex_pet::PetSpec k_pet = {",
            f'    "{pet_id}",',
            f'    "{manifest.get("displayName", pet_id)}",',
            f'    "/{pet_id}/{output_dir.name}",',
            f"    {display_width},",
            f"    {display_height},",
            "    k_states,",
            "    static_cast<uint8_t>(sizeof(k_states) / sizeof(k_states[0])),",
            "};",
            "",
            f"}}  // namespace {ns}",
            "",
        ]
    )

    header_path.write_text("\n".join(lines), encoding="utf-8")
    return header_path


def build_png_assets(
    pet_dir: Path,
    frames_dir: Path,
    source_manifest: dict,
    output_width: int,
    output_height: int,
    output_name: str,
    resample_name: str,
) -> dict:
    output_dir = pet_dir / output_name
    output_dir.mkdir(parents=True)

    frame_count = 0
    output_states = []
    for state in source_manifest["states"]:
        state_name = state["name"]
        source_state_dir = frames_dir / state_name
        output_state_dir = output_dir / state_name
        output_state_dir.mkdir(parents=True)
        output_frames = []

        for index in range(int(state["frames"])):
            name = f"{index:02d}.png"
            source_path = source_state_dir / name
            if not source_path.is_file():
                raise SystemExit(f"missing frame: {source_path}")

            output_path = output_state_dir / name
            with Image.open(source_path) as image:
                resized = resize_alpha_aware(
                    image,
                    (output_width, output_height),
                    RESAMPLERS[resample_name],
                )
                resized.save(output_path, format="PNG", optimize=True)

            output_frames.append(
                {
                    "index": index,
                    "file": str(output_path.relative_to(output_dir)),
                    "durationMs": durations_for(state)[index],
                }
            )
            frame_count += 1

        output_states.append(
            {
                "name": state_name,
                "frames": output_frames,
                "frameCount": int(state["frames"]),
                "durationsMs": durations_for(state),
            }
        )

    source_width = int(source_manifest["cell"]["width"])
    source_height = int(source_manifest["cell"]["height"])
    display_manifest = {
        "format": "m5stack-display-png",
        "sourceFormat": source_manifest.get("format", "codex-pet"),
        "petId": source_manifest["petId"],
        "displayName": source_manifest.get("displayName", source_manifest["petId"]),
        "sourceFrame": {"width": source_width, "height": source_height},
        "displayFrame": {"width": output_width, "height": output_height},
        "resample": resample_name,
        "alpha": "straight RGBA PNG; resized with premultiplied-alpha sampling",
        "frameCount": frame_count,
        "states": output_states,
        "runtimeNote": (
            "Runtime can decode PNG frames when entering a state or pet, cache "
            "them as RGB565 sprites/buffers, then draw cached frames during playback."
        ),
    }
    with (output_dir / "manifest.json").open("w", encoding="utf-8") as handle:
        json.dump(display_manifest, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    header_path = write_cpp_header(
        output_dir,
        display_manifest,
        output_width,
        output_height,
    )

    return {
        "ok": True,
        "outputDir": str(output_dir),
        "width": output_width,
        "height": output_height,
        "frames": frame_count,
        "manifest": str(output_dir / "manifest.json"),
        "header": str(header_path),
    }


def main() -> None:
    args = parse_args()
    pet_dir = args.pet_dir
    source_dir = pet_dir / "source"
    frames_dir = source_dir / "frames"
    if not frames_dir.is_dir():
        raise SystemExit(f"missing source frames directory: {frames_dir}")

    manifest = load_manifest(pet_dir)
    source_width = int(manifest["cell"]["width"])
    source_height = int(manifest["cell"]["height"])
    output_width = args.width
    output_height = args.height or round(output_width * source_height / source_width)
    default_output_name = f"display-{output_width}-png"
    output_name = args.output_name or default_output_name
    output_dir = pet_dir / output_name

    if output_dir.exists():
        if not args.force:
            raise SystemExit(f"destination exists; use --force to replace: {output_dir}")
        shutil.rmtree(output_dir)

    result = build_png_assets(
        pet_dir,
        frames_dir,
        manifest,
        output_width,
        output_height,
        output_name,
        args.resample,
    )

    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
