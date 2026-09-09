"""Build the compact LVGL Xiaolan frames from a 3x3 pixel-art sheet."""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "firmware" / "main" / "app_xiaolan" / "assets"
SHEET_PATH = ASSET_DIR / "xiaolan_prototype_sheet.png"
FRAME_WIDTH = 96
FRAME_HEIGHT = 104
ROWS = 3
COLS = 5
FRAME_MARGIN = 2


def remove_checkerboard(image: Image.Image) -> Image.Image:
    """Make the neutral checkerboard transparent without erasing dark outlines."""
    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    background = bytearray(width * height)

    def is_background(x: int, y: int) -> bool:
        red, green, blue = pixels[x, y]
        # The image generator's checkerboard is slightly compressed, so its
        # gray squares are not perfectly neutral. Only edge-connected gray
        # pixels are removed; enclosed white highlights remain part of Xiaolan.
        return max(red, green, blue) - min(red, green, blue) <= 20 and min(red, green, blue) >= 50

    queue: deque[tuple[int, int]] = deque()
    for x in range(width):
        for y in (0, height - 1):
            if is_background(x, y) and not background[y * width + x]:
                background[y * width + x] = 1
                queue.append((x, y))
    for y in range(height):
        for x in (0, width - 1):
            if is_background(x, y) and not background[y * width + x]:
                background[y * width + x] = 1
                queue.append((x, y))

    while queue:
        x, y = queue.popleft()
        for next_x, next_y in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= next_x < width and 0 <= next_y < height:
                index = next_y * width + next_x
                if not background[index] and is_background(next_x, next_y):
                    background[index] = 1
                    queue.append((next_x, next_y))

    output = Image.new("RGBA", (width, height))
    output_pixels = output.load()
    for y in range(height):
        for x in range(width):
            red, green, blue = pixels[x, y]
            output_pixels[x, y] = (0, 0, 0, 0) if background[y * width + x] else (red, green, blue, 255)
    return output


def make_compact_sheet(source: Path) -> None:
    source_image = remove_checkerboard(Image.open(source))
    source_width, source_height = source_image.size
    compact = Image.new("RGBA", (COLS * FRAME_WIDTH, ROWS * FRAME_HEIGHT), (0, 0, 0, 0))
    for row in range(ROWS):
        for col in range(COLS):
            box = (
                col * source_width // COLS,
                row * source_height // ROWS,
                (col + 1) * source_width // COLS,
                (row + 1) * source_height // ROWS,
            )
            frame = normalize_frame(source_image.crop(box))
            compact.alpha_composite(frame, (col * FRAME_WIDTH, row * FRAME_HEIGHT))
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    compact.save(SHEET_PATH)


def normalize_frame(frame: Image.Image) -> Image.Image:
    """Keep the art's aspect ratio while giving every pose the same visible height."""
    bbox = frame.getbbox()
    output = Image.new("RGBA", (FRAME_WIDTH, FRAME_HEIGHT), (0, 0, 0, 0))
    if bbox is None:
        return output

    art = frame.crop(bbox)
    max_width = FRAME_WIDTH - FRAME_MARGIN * 2
    max_height = FRAME_HEIGHT - FRAME_MARGIN * 2
    scale = min(max_width / art.width, max_height / art.height)
    target_size = (max(1, round(art.width * scale)), max(1, round(art.height * scale)))
    art = art.resize(target_size, Image.Resampling.NEAREST)
    output.alpha_composite(
        art,
        ((FRAME_WIDTH - art.width) // 2, FRAME_HEIGHT - art.height - FRAME_MARGIN),
    )
    return output


def frame_bytes(sheet: Image.Image, row: int, col: int) -> bytes:
    frame = sheet.crop((col * FRAME_WIDTH, row * FRAME_HEIGHT, (col + 1) * FRAME_WIDTH, (row + 1) * FRAME_HEIGHT))
    data = bytearray()
    for red, green, blue, alpha in frame.getdata():
        data.extend((blue, green, red, alpha) if alpha else (0, 0, 0, 0))
    return bytes(data)


def emit_assets() -> None:
    sheet = Image.open(SHEET_PATH).convert("RGBA")
    # The simulator's still preview showcases the newly designed floral outfit.
    sheet.crop((0, 2 * FRAME_HEIGHT, FRAME_WIDTH, 3 * FRAME_HEIGHT)).resize((288, 312), Image.Resampling.NEAREST).save(
        ASSET_DIR / "xiaolan_idle_default.png"
    )
    frames = {(row, col): frame_bytes(sheet, row, col) for row in range(ROWS) for col in range(COLS)}
    names = {
        "idle": 0,
        "active": 0,
        "working": 2,
        "success": 2,
        "failed": 1,
        "rest": 1,
    }

    header = [
        "#pragma once",
        "#include \"lvgl.h\"",
        "",
        "#define XIAOLAN_IDLE_FRAMES 5",
        "#define XIAOLAN_ACTIVE_FRAMES 5",
        "#define XIAOLAN_WORKING_FRAMES 5",
        "#define XIAOLAN_SUCCESS_FRAMES 5",
        "#define XIAOLAN_FAILED_FRAMES 5",
        "#define XIAOLAN_REST_FRAMES 5",
        "#define XIAOLAN_OUTFITS 3",
        "#define XIAOLAN_POSES 5",
        "#define XIAOLAN_DEFAULT_OUTFIT 2",
        "",
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
    ]
    for name in names:
        header.append(f"extern const lv_image_dsc_t xiaolan_{name}[XIAOLAN_{name.upper()}_FRAMES];")
    header.append("extern const lv_image_dsc_t xiaolan_outfits[XIAOLAN_OUTFITS][XIAOLAN_POSES];")
    header.extend(["#ifdef __cplusplus", "}", "#endif", ""])

    source = ['#include "xiaolan_assets.hpp"', ""]
    for row in range(ROWS):
        for col in range(COLS):
            data = frames[(row, col)]
            source.append(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t xiaolan_frame_{row}_{col}_map[] = {{")
            for offset in range(0, len(data), 24):
                source.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 24]) + ",")
            source.extend(["};", ""])

    source.append("const lv_image_dsc_t xiaolan_outfits[XIAOLAN_OUTFITS][XIAOLAN_POSES] = {")
    for row in range(ROWS):
        source.append("    {")
        for col in range(COLS):
            source.extend([
                "        {",
                "            .header.cf = LV_COLOR_FORMAT_ARGB8888,",
                "            .header.magic = LV_IMAGE_HEADER_MAGIC,",
                f"            .header.w = {FRAME_WIDTH},",
                f"            .header.h = {FRAME_HEIGHT},",
                f"            .data_size = sizeof(xiaolan_frame_{row}_{col}_map),",
                f"            .data = xiaolan_frame_{row}_{col}_map,",
                "        },",
            ])
        source.append("    },")
    source.extend(["};", ""])

    for name, row in names.items():
        source.append(f"const lv_image_dsc_t xiaolan_{name}[{COLS}] = {{")
        for col in range(COLS):
            source.extend([
                "    {",
                "        .header.cf = LV_COLOR_FORMAT_ARGB8888,",
                "        .header.magic = LV_IMAGE_HEADER_MAGIC,",
                f"        .header.w = {FRAME_WIDTH},",
                f"        .header.h = {FRAME_HEIGHT},",
                f"        .data_size = sizeof(xiaolan_frame_{row}_{col}_map),",
                f"        .data = xiaolan_frame_{row}_{col}_map,",
                "    },",
            ])
        source.extend(["};", ""])

    (ASSET_DIR / "xiaolan_assets.hpp").write_text("\n".join(header), encoding="utf-8")
    (ASSET_DIR / "xiaolan_assets.c").write_text("\n".join(source), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, help="high-resolution reference sheet from the image generator")
    args = parser.parse_args()
    if args.source:
        make_compact_sheet(args.source)
    elif not SHEET_PATH.exists():
        raise SystemExit(f"missing {SHEET_PATH}; pass --source on the first run")
    emit_assets()
    print(f"built {ROWS * COLS} Xiaolan frames at {ASSET_DIR}")


if __name__ == "__main__":
    main()
