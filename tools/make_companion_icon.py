#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw


def main() -> None:
    size = 256
    image = Image.new("RGB", (size, size), "#126b5b")
    draw = ImageDraw.Draw(image)
    draw.ellipse((42, 24, 202, 184), fill="#ffffff")
    draw.line((122, 74, 122, 130, 174, 160), fill="#126b5b", width=18, joint="curve")
    draw.ellipse((113, 121, 131, 139), fill="#126b5b")
    draw.ellipse((148, 148, 244, 244), fill="#e5b94d")
    draw.line((196, 172, 196, 220), fill="#ffffff", width=16)
    draw.line((172, 196, 220, 196), fill="#ffffff", width=16)
    image.save(Path(__file__).resolve().parents[1] / "companion" / "nro" / "icon.jpg", format="JPEG", quality=95, subsampling=0)


if __name__ == "__main__":
    main()
