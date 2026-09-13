#!/usr/bin/env python3
"""Verify DGCURSOR.EXE's native cursor against its deterministic desktop.

Input must be an unscaled canonical desktop capture INCLUDING the cursor,
with no CRT shader. Ordinary QMP VRAM screenshots omit the native plane.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import json
from pathlib import Path
import struct
import zlib


def png_rows(png):
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("expected PNG")
    offset, data, header = 8, bytearray(), None
    while offset + 12 <= len(png):
        size = struct.unpack_from("!I", png, offset)[0]
        kind, payload = png[offset + 4:offset + 8], png[offset + 8:offset + 8 + size]
        if len(payload) != size:
            raise ValueError("truncated PNG chunk")
        if kind == b"IHDR":
            header = struct.unpack("!IIBBBBB", payload)
        elif kind == b"IDAT":
            data.extend(payload)
        offset += size + 12
    if not header:
        raise ValueError("missing PNG dimensions")
    width, height, depth, color, compression, filtering, interlace = header
    if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
        raise ValueError("expected non-interlaced RGB/RGBA8 PNG")
    if not 0 < width <= 8192 or not 0 < height <= 8192:
        raise ValueError("PNG dimensions exceed capture bounds")
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(data)
    if len(raw) != (stride + 1) * height:
        raise ValueError("incorrect PNG scanline length")
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, row = raw[start], bytearray(raw[start + 1:start + stride + 1])
        if kind > 4:
            raise ValueError("invalid PNG filter")
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            above = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = left
            elif kind == 2:
                predictor = above
            elif kind == 3:
                predictor = (left + above) // 2
            else:
                target = left + above - corner
                predictor = min((left, above, corner), key=lambda v: abs(target - v))
            row[x] = (row[x] + predictor) & 255
        yield y, row, channels
        previous = row


def background(x, y):
    return (192, 128, 64) if ((x // 16) ^ (y // 16)) & 1 else (32, 64, 96)


def compose(rgb, x, y, shape):
    if shape == "mono":
        if y < 16:
            return (0, 0, 0) if x < 16 else (255, 255, 255)
        return rgb if x < 16 else tuple(c ^ 255 for c in rgb)
    if shape == "masked":
        color = (0x12, 0x34, 0x56) if x < 16 else (0x65, 0x43, 0x21)
        return color if y < 16 else tuple(a ^ b for a, b in zip(rgb, color))
    if shape == "alpha":
        source, alpha = (((0, 0, 0), 0) if x < 16 else ((64, 0, 0), 128)) if y < 16 else (
            ((0, 255, 0), 255) if x < 16 else ((0, 64, 0), 128))
        # DGCURSOR uses the public CreateIconIndirect API. Actual NT5 cursor
        # DMA verifies that this API premultiplies each input channel using
        # C*A>>8 before DrvSetPointerShape(SPS_ALPHA), including 255->254.
        # The compositor receives that result and must not premultiply again.
        source = tuple((channel * alpha) >> 8 for channel in source)
        return tuple(s + (d * (255 - alpha) + 127) // 255 for s, d in zip(source, rgb))
    return rgb


def verify(png, shape="mono", left=320, top=256, origin=(64, 64)):
    count, bad_count, samples = 0, 0, []
    for y, row, channels in png_rows(png):
        for x in range(len(row) // channels):
            cx, cy = x - origin[0], y - origin[1]
            if not (0 <= cx < 512 and 32 <= cy < 384):
                continue  # caption and pixels outside the diagnostic window
            expected = background(cx, cy)
            inside = left <= x < left + 32 and top <= y < top + 32
            if inside:
                expected = compose(expected, x - left, y - top, shape)
            actual = tuple(row[x * channels:x * channels + 3])
            tolerance = 1 if inside and shape == "alpha" else 0
            count += 1
            if any(abs(a - b) > tolerance for a, b in zip(actual, expected)):
                bad_count += 1
                if len(samples) < 12:
                    samples.append({"x": x, "y": y, "actual": actual, "expected": expected})
    if count != 512 * 352:
        raise ValueError("capture does not contain the complete diagnostic window")
    return {"shape": shape, "cursor_top_left": [left, top], "origin": list(origin),
            "verified_pixels": count, "mismatches": bad_count, "samples": samples,
            "pass": not bad_count}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--shape", choices=["mono", "hidden", "alpha", "masked"], default="mono")
    parser.add_argument("--left", type=int, default=320)
    parser.add_argument("--top", type=int, default=256)
    parser.add_argument("--origin-x", type=int, default=64)
    parser.add_argument("--origin-y", type=int, default=64)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = verify(args.image.read_bytes(), args.shape, args.left, args.top, (args.origin_x, args.origin_y))
    result["image"] = str(args.image)
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    print(text, end="")
    raise SystemExit(0 if result["pass"] else 1)


if __name__ == "__main__":
    main()
