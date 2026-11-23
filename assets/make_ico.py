"""Build a multi-resolution RegionMirror.ico from the brand PNG."""

from __future__ import annotations

import struct
from io import BytesIO
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parent.parent / "brand-assets" / "Region mirror dual-monitor icon.png"
DST = Path(__file__).resolve().parent / "RegionMirror.ico"
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def png_bytes(image: Image.Image) -> bytes:
    buf = BytesIO()
    image.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def write_ico(path: Path, images: list[tuple[int, bytes]]) -> None:
    # ICONDIR + ICONDIRENTRY array, then image payloads (PNG-compressed).
    count = len(images)
    header = struct.pack("<HHH", 0, 1, count)
    entries = bytearray()
    payloads = bytearray()
    offset = 6 + 16 * count
    for size, blob in images:
        w = 0 if size >= 256 else size
        h = 0 if size >= 256 else size
        entries += struct.pack(
            "<BBBBHHII",
            w,
            h,
            0,  # color count
            0,  # reserved
            1,  # planes
            32,  # bit count
            len(blob),
            offset,
        )
        payloads += blob
        offset += len(blob)
    path.write_bytes(header + entries + payloads)


def main() -> None:
    img = Image.open(SRC).convert("RGBA")
    print(f"source {img.size} mode={img.mode}")

    images: list[tuple[int, bytes]] = []
    for s in SIZES:
        resized = img.resize((s, s), Image.Resampling.LANCZOS)
        corners = [
            resized.getpixel((0, 0))[3],
            resized.getpixel((s - 1, 0))[3],
            resized.getpixel((0, s - 1))[3],
            resized.getpixel((s - 1, s - 1))[3],
        ]
        print(f"  {s}x{s}: corner alpha={corners}")
        if any(a != 0 for a in corners):
            print(f"  WARNING: non-zero corner alpha at {s}px")
        images.append((s, png_bytes(resized)))

    write_ico(DST, images)
    print(f"wrote {DST} ({DST.stat().st_size} bytes)")

    data = DST.read_bytes()
    assert data[0:4] == b"\x00\x00\x01\x00"
    count = struct.unpack_from("<H", data, 4)[0]
    print(f"ICO image count={count}")
    found_sizes: list[int] = []
    for i in range(count):
        off = 6 + i * 16
        w, h, _c, _r, _p, bitcount, nbytes, offset = struct.unpack_from("<BBBBHHII", data, off)
        aw = 256 if w == 0 else w
        found_sizes.append(aw)
        blob = data[offset : offset + nbytes]
        assert blob[:8] == b"\x89PNG\r\n\x1a\n", f"entry {i} is not PNG"
        frame = Image.open(BytesIO(blob)).convert("RGBA")
        extrema = frame.getchannel("A").getextrema()
        corner = frame.getpixel((0, 0))[3]
        print(f"  entry {i}: {aw}x{aw} bits={bitcount} bytes={nbytes} alpha={extrema} corner_a={corner}")
        if aw in (16, 32) and corner != 0:
            raise SystemExit(f"{aw}px lost transparency")

    missing = [s for s in SIZES if s not in found_sizes]
    if missing:
        raise SystemExit(f"MISSING sizes: {missing}")
    print("OK")


if __name__ == "__main__":
    main()
