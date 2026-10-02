#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: The Biscuit developers
"""Biscuit's 16x16 pixel-art icons for the status bar and the settings.

    python3 icons16.py preview out.png   # a sheet of every icon, enlarged (needs Pillow)
    python3 icons16.py cpp               # the C++ for src/widgets/PixelIcons16.cpp

Same grid, outline and palette as the tab icons in src/widgets/PixelIcons.cpp.
'k' is the outline, '.' is transparent, other letters come from each icon's colours.
"""
import sys

K = (40, 40, 40)
WHITE = (255, 255, 255)
CREAM = (255, 251, 232)
BEIGE = (222, 216, 200)
BEIGE_DARK = (190, 182, 162)
GREY_LIGHT = (205, 205, 205)
GREY = (160, 160, 160)
GREY_DARK = (110, 110, 110)
YELLOW = (242, 211, 60)
YELLOW_DARK = (200, 160, 30)
GREEN = (63, 174, 74)
GREEN_LIGHT = (130, 210, 110)
GREEN_DARK = (40, 120, 50)
BLUE = (47, 123, 214)
RED = (200, 50, 40)
RED_DARK = (150, 35, 30)
RED_LIGHT = (236, 90, 70)
ORANGE = (240, 150, 40)
ORANGE_DARK = (190, 110, 20)
PURPLE = (125, 70, 152)
PURPLE_LIGHT = (185, 140, 210)
BROWN = (150, 100, 60)
BROWN_DARK = (110, 70, 40)

ICONS = {}


def icon(name, colors, rows):
    assert len(rows) == 16, (name, len(rows))
    for r in rows:
        assert len(r) == 16, (name, r, len(r))
        for ch in r:
            assert ch in ".k" or ch in colors, (name, ch)
    ICONS[name] = (rows, colors)


# --- Status bar ---------------------------------------------------------------

# Connection: a round lamp, one colour per state.
LAMP = [
    "................",
    "................",
    ".....kkkkkk.....",
    "....kcccccck....",
    "...kcwwcccccк...".replace("к", "k"),
    "..kcwwccccccdk..",
    "..kcwcccccccdk..",
    "..kcccccccccdk..",
    "..kccccccccddk..",
    "..kccccccccddk..",
    "..kcccccccdddk..",
    "...kcccccdddk...",
    "....kddddddk....",
    ".....kkkkkk.....",
    "................",
    "................",
]
for state, (c, d) in {
    "status_connected": (GREEN, GREEN_DARK),
    "status_synchronizing": (YELLOW, YELLOW_DARK),
    "status_connecting": (ORANGE, ORANGE_DARK),
    "status_disconnected": (RED_LIGHT, RED),
    "status_offline": (GREY_LIGHT, GREY),
}.items():
    icon(state, {"c": c, "d": d, "w": WHITE}, LAMP)

# Password: a padlock.
icon("lock", {"s": GREY, "y": YELLOW, "Y": YELLOW_DARK}, [
    "................",
    ".....kkkkkk.....",
    "....kssssssk....",
    "...ksskkkkssk...",
    "...ksk....ksk...",
    "...ksk....ksk...",
    "..kkkkkkkkkkkk..",
    "..kyyyyyyyyyyk..",
    "..kyyyykkyyyyk..",
    "..kyyyykkyyyyk..",
    "..kyyyyykyyyyk..",
    "..kYYYYYYYYYYk..",
    "..kYYYYYYYYYYk..",
    "..kkkkkkkkkkkk..",
    "................",
    "................",
])

# No password: the padlock open.
icon("unlock", {"s": GREY, "y": YELLOW, "Y": YELLOW_DARK}, [
    "................",
    ".....kkkkkk.....",
    "....kssssssk....",
    "...ksskkkkssk...",
    "...ksk....kkk...",
    "...ksk..........",
    "..kkkkkkkkkkkk..",
    "..kyyyyyyyyyyk..",
    "..kyyyykkyyyyk..",
    "..kyyyykkyyyyk..",
    "..kyyyyykyyyyk..",
    "..kYYYYYYYYYYk..",
    "..kYYYYYYYYYYk..",
    "..kkkkkkkkkkkk..",
    "................",
    "................",
])

# Settings: a cog.
icon("settings", {"g": GREY_LIGHT, "d": GREY}, [
    "................",
    "......kkkk......",
    "..kk..kggk..kk..",
    ".kggkkkggkkkggk.",
    ".kggggggggggggk.",
    "..kgggkkkkgddk..",
    "kkkggk....kddkkk",
    "kggggk....kddddk",
    "kggggk....kddddk",
    "kkkggk....kddkkk",
    "..kggdkkkkdddk..",
    ".kgdddddddddddk.",
    ".kddkkkddkkkddk.",
    "..kk..kddk..kk..",
    "......kkkk......",
    "................",
])

# Seed: a sprout in a pot.
icon("seed", {"l": GREEN_LIGHT, "g": GREEN, "b": BROWN, "B": BROWN_DARK}, [
    "................",
    ".kkkk......kkkk.",
    "kllllk....kllllk",
    "kllllgk..kgllllk",
    ".kgllggkkgglllk.",
    "..kkggggggggkk..",
    "....kkkggkkk....",
    "......kggk......",
    "......kggk......",
    "..kkkkkggkkkkk..",
    "..kbbbbbbbbbbk..",
    "...kBBBBBBBBk...",
    "...kbbbbbbbbk...",
    "....kBBBBBBk....",
    "....kkkkkkkk....",
    "................",
])

# Tor: an onion, purple when on, grey when off.
ONION = [
    "................",
    ".......kk.......",
    "......kggk......",
    ".......kk.......",
    ".....kkppkk.....",
    "....kpPppPpk....",
    "...kpPppppPpk...",
    "..kpPpppppPppk..",
    "..kpPpppppPppk..",
    "..kpPpppppPppk..",
    "..kppPppppPppk..",
    "...kpPpppPppk...",
    "....kpPppPpk....",
    ".....kkkkkk.....",
    "................",
    "................",
]
icon("tor_on", {"p": PURPLE, "P": PURPLE_LIGHT, "g": GREEN}, ONION)
icon("tor_off", {"p": GREY, "P": GREY_LIGHT, "g": GREY_DARK}, ONION)

# Warning: a yellow triangle.
icon("warning", {"y": YELLOW}, [
    "................",
    ".......kk.......",
    "......kyyk......",
    "......kyyk......",
    ".....kyyyyk.....",
    ".....kykkyk.....",
    "....kyykkyyk....",
    "....kyykkyyk....",
    "...kyyykkyyyk...",
    "...kyyyyyyyyk...",
    "..kyyyykkyyyyk..",
    "..kyyyykkyyyyk..",
    ".kyyyyyyyyyyyyk.",
    ".kkkkkkkkkkkkkk.",
    "................",
    "................",
])

# Update available: a floppy disk with a green arrow.
icon("update", {"b": BLUE, "w": CREAM, "s": GREY_LIGHT, "a": GREEN}, [
    "................",
    ".kkkkkkkkkkkkk..",
    ".kbbksssskbbbbk.",
    ".kbbksskskbbbbk.",
    ".kbbksskskbbbbk.",
    ".kbbkkkkkkbbbbk.",
    ".kbbbbbbbbbbbbk.",
    ".kbwwwwwwwwwwbk.",
    ".kbwwwwaawwwwbk.",
    ".kbwwwwaawwwwbk.",
    ".kbwwaaaaaawwbk.",
    ".kbwwwaaaawwwbk.",
    ".kbwwwwaawwwwbk.",
    ".kbwwwwwwwwwwbk.",
    ".kkkkkkkkkkkkkk.",
    "................",
])

# Account switcher: an ID card.
icon("account", {"c": CREAM, "b": BLUE, "s": BEIGE_DARK}, [
    "................",
    "................",
    "kkkkkkkkkkkkkkkk",
    "kbbbbbbbbbbbbbbk",
    "kcccccccccccccck",
    "kcckkkccccccccck",
    "kckbbbkcsssssssk",
    "kckbbbkcccccccck",
    "kcckkkccssssscck",
    "kckbbbkcccccccck",
    "kkbbbbbkssssccck",
    "kcccccccccccccck",
    "kkkkkkkkkkkkkkkk",
    "................",
    "................",
    "................",
])

# --- Settings pages -------------------------------------------------------------

# Appearance: a painter's palette.
icon("appearance", {"w": BEIGE, "d": BEIGE_DARK, "r": RED, "b": BLUE, "y": YELLOW, "g": GREEN}, [
    "................",
    "....kkkkkkk.....",
    "..kkwwwwwwwkk...",
    ".kwwrrwwbbwwwk..",
    ".kwwrrwwbbwwwwk.",
    "kwwwwwwwwwwwwwk.",
    "kwyywwwwwwkkwwk.",
    "kwyywwwwwk..kwk.",
    "kwwwwwwwwk..kwk.",
    "kwggwwwwwwkkwwk.",
    ".kggwwwwwwwwwdk.",
    ".kwwwwwwwwwwddk.",
    "..kkddddddddkk..",
    "....kkkkkkkk....",
    "................",
    "................",
])

# Network: a radio mast sending waves.
icon("network", {"g": GREY, "d": GREY_DARK, "r": RED_LIGHT}, [
    "................",
    ".k............k.",
    "k..k...kk...k..k",
    "k.k...krrk...k.k",
    "k.k...krrk...k.k",
    "k..k...kk...k..k",
    ".k....kggk....k.",
    "......kdgk......",
    ".....kddggk.....",
    ".....kdkkgk.....",
    "....kddkkggk....",
    "....kdk..kgk....",
    "...kddk..kggk...",
    "...kdk....kgk...",
    "..kkkk....kkkk..",
    "................",
])

# Storage: a filing cabinet.
icon("storage", {"c": BEIGE, "d": BEIGE_DARK}, [
    "...kkkkkkkkkk...",
    "...kcccccccck...",
    "...kccckkccck...",
    "...kcccccccck...",
    "...kddddddddk...",
    "...kkkkkkkkkk...",
    "...kcccccccck...",
    "...kccckkccck...",
    "...kcccccccck...",
    "...kddddddddk...",
    "...kkkkkkkkkk...",
    "...kcccccccck...",
    "...kccckkccck...",
    "...kcccccccck...",
    "...kddddddddk...",
    "...kkkkkkkkkk...",
])

# Display: a window with its title bar.
icon("display", {"b": BLUE, "w": CREAM, "W": WHITE, "l": GREY}, [
    "................",
    "................",
    "kkkkkkkkkkkkkkkk",
    "kbbbbbbbbbWbWbWk",
    "kkkkkkkkkkkkkkkk",
    "kwwwwwwwwwwwwwwk",
    "kwlllllwwwwwwwwk",
    "kwwwwwwwwwwwwwwk",
    "kwllllllllwwwwwk",
    "kwwwwwwwwwwwwwwk",
    "kwlllllllwwwwwwk",
    "kwwwwwwwwwwwwwwk",
    "kkkkkkkkkkkkkkkk",
    "................",
    "................",
    "................",
])

# Transactions: a receipt, amounts in and out.
icon("transactions", {"w": CREAM, "l": GREY, "g": GREEN, "r": RED}, [
    "................",
    "..kkkkkkkkkkk...",
    "..kwwwwwwwwwk...",
    "..kwlllllwwwk...",
    "..kwwwwwwwwwk...",
    "..kwllllwwggk...",
    "..kwwwwwwwwwk...",
    "..kwlllllwrrk...",
    "..kwwwwwwwwwk...",
    "..kwllllwwggk...",
    "..kwwwwwwwwwk...",
    "..kwlllwwwrrk...",
    "..kwwwwwwwwwk...",
    "..kwkwkwkwkwk...",
    "..kk.k.k.k.kk...",
    "................",
])

# Plugins: a puzzle piece.
icon("plugins", {"g": GREEN, "G": GREEN_DARK}, [
    "................",
    "......kkkk......",
    ".....kggggk.....",
    ".....kggggk.....",
    "..kkkkggggkkkk..",
    "..kggggggggggk..",
    "..kggggggggggkk.",
    "..kggggggggggggk",
    "..kggggggggggggk",
    "..kggggggggggkk.",
    "..kggggggggggk..",
    "..kggggggggggk..",
    "..kGGGGGGGGGGk..",
    "..kkkkkkkkkkkk..",
    "................",
    "................",
])

# Converter: a calculator.
icon("converter", {"g": BEIGE, "b": GREY_DARK, "r": ORANGE, "L": GREEN_LIGHT}, [
    "................",
    "..kkkkkkkkkkkk..",
    "..kggggggggggk..",
    "..kgkkkkkkkkgk..",
    "..kgkLLLLLLkgk..",
    "..kgkkkkkkkkgk..",
    "..kggggggggggk..",
    "..kgbbgbbgrrgk..",
    "..kggggggggggk..",
    "..kgbbgbbgrrgk..",
    "..kggggggggggk..",
    "..kgbbgbbgrrgk..",
    "..kggggggggggk..",
    "..kkkkkkkkkkkk..",
    "................",
    "................",
])

# Misc: a red toolbox.
icon("misc", {"r": RED, "R": RED_DARK, "y": YELLOW}, [
    "................",
    "................",
    ".....kkkkkk.....",
    ".....k....k.....",
    ".kkkkkkkkkkkkkk.",
    ".krrrrrrrrrrrrk.",
    ".krrrrrrrrrrrrk.",
    ".kkkkkkkkkkkkkk.",
    ".krrrrrkkrrrrrk.",
    ".krrrrrkykrrrrk.",
    ".krrrrrkkrrrrrk.",
    ".kRRRRRRRRRRRRk.",
    ".kRRRRRRRRRRRRk.",
    ".kkkkkkkkkkkkkk.",
    "................",
    "................",
])


def preview(path):
    from PIL import Image, ImageDraw
    names = list(ICONS)
    scale, pad, cols = 6, 16, 6
    cell_w, cell_h = 16 * scale + 16 + pad * 2, 16 * scale + pad * 2 + 12
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (245, 243, 236))
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        grid, colors = ICONS[name]
        img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        for y, r in enumerate(grid):
            for x, ch in enumerate(r):
                if ch == "k":
                    img.putpixel((x, y), K + (255,))
                elif ch != ".":
                    img.putpixel((x, y), tuple(colors[ch]) + (255,))
        big = img.resize((16 * scale, 16 * scale), Image.NEAREST)
        x0, y0 = (i % cols) * cell_w + pad, (i // cols) * cell_h + pad
        sheet.paste(big, (x0, y0), big)
        sheet.paste(img, (x0 + 16 * scale + 6, y0), img)
        draw.text((x0, y0 + 16 * scale + 3), name, fill=(40, 40, 40))
    sheet.save(path)


def cpp():
    out = ["// SPDX-License-Identifier: BSD-3-Clause",
           "// SPDX-FileCopyrightText: The Biscuit developers",
           "",
           "// Generated by tools/pixelart/icons16.py: edit the script, not this file.",
           "",
           "struct Icon16 {",
           "    const char *name;",
           "    const char *rows[16];",
           "    struct { char key; unsigned rgb; } colors[8];",
           "};",
           "",
           "static const Icon16 icons16[] = {"]
    for name, (rows, colors) in ICONS.items():
        assert len(colors) < 8, name
        out.append("    {\"%s\", {" % name)
        out += ["        \"%s\"," % r for r in rows]
        pairs = ", ".join("{'%s', 0x%02x%02x%02x}" % ((k,) + tuple(v)) for k, v in colors.items())
        out.append("    }, {%s, {0, 0}}}," % pairs)
    out.append("};")
    print("\n".join(out))


if __name__ == "__main__":
    if sys.argv[1:2] == ["preview"]:
        preview(sys.argv[2])
    elif sys.argv[1:2] == ["cpp"]:
        cpp()
    else:
        print(__doc__)
