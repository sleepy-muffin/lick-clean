#!/usr/bin/env python3
"""Render app.svg to a PNG used by the Qt window icon and the AppImage."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import cairosvg
except ImportError:
    sys.exit("cairosvg is required: python3 -m pip install cairosvg")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--size",
        type=int,
        default=256,
        help="square output size in pixels (default: 256)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="output PNG path (default: app.png next to app.svg)",
    )
    args = parser.parse_args()

    if args.size <= 0:
        parser.error("--size must be positive")

    root = Path(__file__).resolve().parent
    source = root / "app.svg"
    output = args.output or root / "app.png"

    png = cairosvg.svg2png(
        url=str(source),
        output_width=args.size,
        output_height=args.size,
    )

    output.write_bytes(png)
    print(f"Wrote {output} ({args.size}x{args.size})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
