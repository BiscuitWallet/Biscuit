#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: The Biscuit developers
"""Windows and Linux application icons: the bear alone on a transparent
background, as large as the icon allows. (macOS keeps the ivory tile made by
appicon/, as macOS icons are expected to be.)

    python3 plainicon.py bear.png outdir     # needs Pillow

Writes outdir/{32..512}x{32..512}.png and outdir/appicon.ico (16 to 256 px).
Where a whole multiple of the sprite fills the icon well, the pixels are
simply enlarged and stay sharp; smaller sizes are reduced from a large copy.
"""
import sys

from PIL import Image

FILL = 0.94          # share of the icon the bear may take
WHOLE_MULTIPLE = 0.84  # below this fill, reduce from a large copy instead


def render(sprite, size):
    w, h = sprite.size
    longest = max(w, h)
    scale = int(size * FILL / longest)
    if scale >= 1 and scale * longest >= size * WHOLE_MULTIPLE:
        art = sprite.resize((w * scale, h * scale), Image.NEAREST)
    else:
        big = int(1024 * FILL / longest)
        art = sprite.resize((w * big, h * big), Image.NEAREST)
        factor = size * FILL / max(art.size)
        art = art.resize((round(art.width * factor), round(art.height * factor)), Image.LANCZOS)
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    icon.paste(art, ((size - art.width) // 2, (size - art.height) // 2), art)
    return icon


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: plainicon.py bear.png outdir")
    sprite = Image.open(sys.argv[1]).convert("RGBA")
    out = sys.argv[2]
    for size in (32, 48, 64, 96, 128, 256, 512):
        render(sprite, size).save(f"{out}/{size}x{size}.png")
    sizes = (16, 24, 32, 48, 64, 128, 256)
    images = [render(sprite, s) for s in sizes]
    images[-1].save(f"{out}/appicon.ico", sizes=[(s, s) for s in sizes], append_images=images[:-1])


if __name__ == "__main__":
    main()
