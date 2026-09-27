# pixelart

Tools used to make the application icon from a pixel-art drawing.

## 1. Recover the pixels

`pixelart.py` takes an upscaled pixel-art image (JPEG, screenshot...) and
writes the drawing back at one pixel per cell, background removed. It detects
the cell size from the edges, snaps each grid line to the real edges, takes
the median colour of each cell centre and merges near-identical colours
(JPEG noise).

```sh
python3 -m venv /tmp/pixelart && /tmp/pixelart/bin/pip install numpy pillow
/tmp/pixelart/bin/python tools/pixelart/pixelart.py drawing.jpeg sprite.png
```

It prints the detected cell size, grid and number of colours, and writes
`sprite_x12.png` to check the result. Use `--cell` to force the cell size and
`--merge` to change how aggressively colours are merged.

## 2. Build the icons

```sh
cmake -S tools/pixelart/appicon -B build-appicon -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build-appicon
./build-appicon/appicon sprite.png icons
iconutil -c icns icons/appicon.iconset -o icons/appicon.icns
cp icons/*.png icons/appicon.ico icons/appicon.icns src/assets/images/appicons/
```
