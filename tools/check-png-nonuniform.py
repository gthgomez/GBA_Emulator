#!/usr/bin/env python3
"""Verify a PNG contains genuinely nonuniform pixel content.

Used by the interactive desktop smoke to prove the SDL host presented real
game graphics rather than a blank/black window. Parses the PNG with only the
standard library (zlib) so it needs no Pillow install.

Exits non-zero when the image is uniform (a single color), too small, or
unreadable.
"""

import argparse
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_chunks(data):
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")
    offset = len(PNG_SIGNATURE)
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        start = offset + 8
        end = start + length
        if end + 4 > len(data):
            raise ValueError("truncated PNG chunk")
        yield chunk_type, data[start:end]
        offset = end + 4


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(path):
    with open(path, "rb") as handle:
        data = handle.read()

    width = height = bit_depth = color_type = None
    idat = bytearray()
    for chunk_type, payload in read_chunks(data):
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
                raise ValueError(
                    f"unsupported PNG format (bit depth {bit_depth}, "
                    f"color type {color_type}, interlace {interlace})"
                )
        elif chunk_type == b"IDAT":
            idat += payload
        elif chunk_type == b"IEND":
            break

    if width is None or height is None:
        raise ValueError("missing IHDR")

    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    expected = height * (stride + 1)
    if len(raw) < expected:
        raise ValueError("short IDAT payload")

    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    offset = 0
    for row in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset : offset + stride])
        offset += stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + a) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + b) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif filter_type == 4:
                line[i] = (line[i] + paeth(a, b, c)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unknown PNG filter {filter_type}")
        pixels[row * stride : (row + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("png")
    parser.add_argument("--min-unique", type=int, default=2)
    parser.add_argument(
        "--min-second-share",
        type=float,
        default=0.0,
        help="require the second most common color to cover at least this "
        "fraction of pixels (rejects a single stray pixel)",
    )
    parser.add_argument("--min-width", type=int, default=1)
    parser.add_argument("--min-height", type=int, default=1)
    args = parser.parse_args()

    try:
        width, height, channels, pixels = decode_png(args.png)
    except Exception as exc:  # noqa: BLE001 - report any parse failure clearly
        print(f"check-png-nonuniform: FAIL (cannot parse {args.png}: {exc})")
        return 1

    if width < args.min_width or height < args.min_height:
        print(
            f"check-png-nonuniform: FAIL ({width}x{height} smaller than required "
            f"{args.min_width}x{args.min_height})"
        )
        return 1

    total = width * height
    counts = {}
    for i in range(0, len(pixels), channels):
        key = bytes(pixels[i : i + channels])
        counts[key] = counts.get(key, 0) + 1
    ranked = sorted(counts.values(), reverse=True)

    if len(ranked) < args.min_unique:
        print(
            f"check-png-nonuniform: FAIL ({args.png} has {len(ranked)} distinct "
            f"color(s), need {args.min_unique})"
        )
        return 1

    second_share = (ranked[1] / total) if len(ranked) > 1 else 0.0
    if second_share < args.min_second_share:
        print(
            f"check-png-nonuniform: FAIL ({args.png}: second color covers only "
            f"{second_share:.3f} of pixels, need {args.min_second_share:.3f})"
        )
        return 1

    print(
        f"check-png-nonuniform: PASS ({width}x{height}, {len(ranked)} distinct colors, "
        f"second share {second_share:.3f})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
