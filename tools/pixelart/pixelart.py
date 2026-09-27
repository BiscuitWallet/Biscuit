#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: The Biscuit developers

"""Developer tool: recovers the real pixel grid of an upscaled pixel-art image
(JPEG, screenshot...) and writes it back at 1:1 with a transparent background.

Usage: pixelart.py input.jpeg out.png [--cell 5.87] [--merge 30] [--preview 12]

1. Cell size: strongest period of the colour edges (unless --cell is given).
2. Grid lines: one global phase, then each line snapped within 1px to the
   nearest real edge (upscalers are rarely exact).
3. One colour per cell: median of the cell centre, borders excluded.
4. Palette: cell colours closer than --merge are merged (removes JPEG noise).
5. Background: flood fill from the borders, stopped by the outline.

Needs numpy and pillow.
"""

import argparse
from collections import deque

import numpy as np
from PIL import Image


def subject_box(img, bg, margin=12, tol=60):
    """Bounding box of everything that is not background, plus a margin."""
    mask = np.abs(img - bg).sum(2) > tol
    ys, xs = np.nonzero(mask)
    h, w = mask.shape
    return (max(0, xs.min() - margin), max(0, ys.min() - margin),
            min(w, xs.max() + margin + 1), min(h, ys.max() + margin + 1))


def edge_profiles(img):
    gx = np.abs(np.diff(img, axis=1)).sum(2).sum(0)
    gy = np.abs(np.diff(img, axis=0)).sum(2).sum(1)
    return gx, gy


def find_period(profiles, lo=3.0, hi=20.0):
    best = (0.0, lo)
    for s in np.arange(lo, hi, 0.01):
        score = 0.0
        for p in profiles:
            phase = np.exp(2j * np.pi * np.arange(len(p)) / s)
            score += abs((p * phase).sum()) / p.sum()
        best = max(best, (score, s))
    return best[1]


def grid_lines(profile, size, cell):
    def score(o):
        idx = [int(round(o + k * cell)) for k in range(int((size - o) / cell))]
        return sum(profile[i] for i in idx if i < len(profile))
    offset = max(np.arange(0, cell, 0.05), key=score)
    lines, k = [], 0
    while offset + k * cell < size - 1:
        i = int(round(offset + k * cell))
        lo, hi = max(0, i - 1), min(len(profile) - 1, i + 1)
        lines.append(lo + int(np.argmax(profile[lo:hi + 1])) + 1)
        k += 1
    return lines


def sample_cells(img, xs, ys):
    cells = np.zeros((len(ys) - 1, len(xs) - 1, 3))
    for r in range(len(ys) - 1):
        for c in range(len(xs) - 1):
            block = img[ys[r] + 1:ys[r + 1] - 1, xs[c] + 1:xs[c + 1] - 1].reshape(-1, 3)
            cells[r, c] = np.median(block, 0)
    return cells


def build_palette(cells, merge):
    px = cells.reshape(-1, 3)
    centres = []
    for p in px[np.argsort(px.sum(1))]:
        if not centres or min(np.linalg.norm(p - q) for q in centres) > merge:
            centres.append(p.copy())
    centres = np.array(centres)
    for _ in range(30):
        labels = np.argmin(((px[:, None] - centres[None]) ** 2).sum(2), 1)
        keep = [k for k in range(len(centres)) if (labels == k).sum() >= 2]
        centres = np.array([px[labels == k].mean(0) for k in keep])
    labels = np.argmin(((px[:, None] - centres[None]) ** 2).sum(2), 1)
    return centres[labels].reshape(cells.shape).round().astype(np.uint8)


def remove_background(rgb, bg, tol=45):
    rows, cols, _ = rgb.shape
    alpha = np.full((rows, cols), 255, np.uint8)
    seen = np.zeros((rows, cols), bool)
    queue = deque([(r, c) for r in range(rows) for c in (0, cols - 1)]
                  + [(r, c) for c in range(cols) for r in (0, rows - 1)])
    while queue:
        r, c = queue.popleft()
        if not (0 <= r < rows and 0 <= c < cols) or seen[r, c]:
            continue
        seen[r, c] = True
        if np.abs(rgb[r, c].astype(int) - bg).sum() >= tol:
            continue
        alpha[r, c] = 0
        queue.extend([(r + 1, c), (r - 1, c), (r, c + 1), (r, c - 1)])
    rgba = np.dstack([rgb, alpha])
    ys, xs = np.nonzero(alpha)
    return rgba[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--cell", type=float, help="cell size in source pixels (default: detected)")
    ap.add_argument("--merge", type=float, default=30, help="palette merge distance (RGB)")
    ap.add_argument("--preview", type=int, default=12, help="also write a preview scaled by N (0: none)")
    args = ap.parse_args()

    src = np.asarray(Image.open(args.input).convert("RGB")).astype(float)
    bg = np.median(np.concatenate([src[:8].reshape(-1, 3), src[-8:].reshape(-1, 3)]), 0)
    x0, y0, x1, y1 = subject_box(src, bg)
    img = src[y0:y1, x0:x1]

    gx, gy = edge_profiles(img)
    cell = args.cell or find_period([gx, gy])
    xs = grid_lines(gx, img.shape[1], cell)
    ys = grid_lines(gy, img.shape[0], cell)

    rgb = build_palette(sample_cells(img, xs, ys), args.merge)
    bg_q = rgb[0, 0].astype(int)  # the margin guarantees a background corner
    out = remove_background(rgb, bg_q)

    colours = len({tuple(p) for p in out.reshape(-1, 4) if p[3]})
    print(f"cell {cell:.2f}px, grid {out.shape[1]}x{out.shape[0]}, {colours} colours")
    sprite = Image.fromarray(out)
    sprite.save(args.output)
    if args.preview:
        big = sprite.resize((sprite.width * args.preview, sprite.height * args.preview), Image.NEAREST)
        big.save(args.output.rsplit(".", 1)[0] + f"_x{args.preview}.png")


if __name__ == "__main__":
    main()
