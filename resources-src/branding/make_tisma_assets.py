#!/usr/bin/env python3
"""Generates the Tisma branding assets (application icons, splash screen) into resources/icons.

The mark is a vector redrawing of the official logo (resources-src/branding/originals/tisma_isotipo.jpg),
traced in the coordinates of that 1024x1536 image. The wordmark and the tagline of the splash screen are
taken from the official horizontal logo (originals/tisma_logo_horizontal.jpg).

The file names of PrusaSlicer are kept (PrusaSlicer_128px.png ...) so that the application code
loading them does not change.

Requires rsvg-convert (librsvg2-bin) and ImageMagick (convert).
Run from the repository root:  python3 resources-src/branding/make_tisma_assets.py
"""

import math
import os
import random
import subprocess
import tempfile

ICONS     = os.path.join('resources', 'icons')
ORIGINALS = os.path.join('resources-src', 'branding', 'originals')

# Tisma palette, sampled from the official logo.
PURPLE       = '#7A24C9'   # brand color (middle of the pyramid gradient)
PURPLE_LIGHT = '#9D40E9'   # top of the pyramid
PURPLE_DARK  = '#5B0FA7'   # bottom of the pyramid
LAVENDER     = '#D9BDF5'
INK          = '#0B0B0B'
SURFACE      = '#F2F2F4'
FACET_TOP    = '#B2B2BC'   # side facets, near the apex
FACET_BOTTOM = '#DCDCE0'   # side facets, near the base
FACET_BASE   = '#D4D3D8'   # front facets below the pyramid


def logo_group(hex_fill='#FFFFFF', stroke=INK, stroke_width=22, badge=False, uid='t'):
    """The Tisma mark (pyramid inside a hexagon) in a 200x200 box.

    Drawn in the pixel coordinates of originals/tisma_isotipo.jpg (hexagon centred at 502,702)
    and mapped to the 200x200 box. The official stroke is ~11 px; small icons use a heavier one."""
    # Pyramid: apex, outer corners of the side facets, base corners, front corner, bottom.
    A, OL, OR = (502, 405), (240, 868), (764, 868)
    BL, BR, F, B = (342, 852), (662, 852), (502, 928), (502, 1030)
    seams = (
        (A, F), (F, B),        # vertical edge of the pyramid and of the base
        (A, BL), (A, BR),      # side facets / pyramid
        (BL, F), (BR, F),      # pyramid / base
        (OL, BL), (OR, BR),    # side facets / base
    )
    pts = lambda *p: ' '.join(f'{x},{y}' for x, y in p)
    seam_svg = ''.join(f'<line x1="{a[0]}" y1="{a[1]}" x2="{b[0]}" y2="{b[1]}"/>' for a, b in seams)

    badge_svg = ''
    if badge:
        # G-code viewer: small purple hexagon with a white "G" in the lower right corner (200x200 units).
        badge_svg = f'''
  <polygon points="{hex_points(165, 152, 34, 30)}" fill="{PURPLE}" stroke="#FFFFFF" stroke-width="5"/>
  <path d="M178,142 A17,17 0 1 0 181,160 L168,160" fill="none" stroke="#FFFFFF" stroke-width="7" stroke-linecap="round"/>'''

    s = 200 / 720
    return f'''
  <defs>
    <linearGradient id="{uid}p" gradientUnits="userSpaceOnUse" x1="0" y1="{A[1]}" x2="0" y2="{F[1]}">
      <stop offset="0" stop-color="{PURPLE_LIGHT}"/>
      <stop offset="1" stop-color="{PURPLE_DARK}"/>
    </linearGradient>
    <linearGradient id="{uid}f" gradientUnits="userSpaceOnUse" x1="0" y1="{A[1]}" x2="0" y2="{OL[1]}">
      <stop offset="0" stop-color="{FACET_TOP}"/>
      <stop offset="1" stop-color="{FACET_BOTTOM}"/>
    </linearGradient>
  </defs>
  <g transform="scale({s:.6f}) translate(-142,-342)">
    <polygon points="{pts((502, 358), (788, 526), (788, 880), (502, 1046), (216, 880), (216, 526))}" fill="{hex_fill}" stroke="{stroke}" stroke-width="{stroke_width / s:.1f}" stroke-linejoin="miter"/>
    <polygon points="{pts(A, OL, B, OR)}" fill="url(#{uid}f)"/>
    <polygon points="{pts(OL, BL, F, B)}" fill="{FACET_BASE}"/>
    <polygon points="{pts(OR, BR, F, B)}" fill="{FACET_BASE}"/>
    <polygon points="{pts(A, BL, F, BR)}" fill="url(#{uid}p)"/>
    <g stroke="#FFFFFF" stroke-width="{max(11, 2.2 / s):.1f}" stroke-linecap="round">{seam_svg}</g>
  </g>{badge_svg}'''


def logo_svg(stroke_width=22, **kwargs):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 200 200">'
            f'{logo_group(stroke_width=stroke_width, **kwargs)}\n</svg>\n')


def hex_points(cx, cy, r, angle=-30):
    return ' '.join(f'{cx + r * math.cos(math.radians(60 * i + angle)):.1f},{cy + r * math.sin(math.radians(60 * i + angle)):.1f}'
                    for i in range(6))


def hex_pattern(width, height, corner, seed):
    """Scattered purple hexagons growing from a corner."""
    rnd = random.Random(seed)
    out = []
    step = 26
    for row in range(int(height / step) + 2):
        for col in range(int(width / step) + 2):
            x = col * step + (step / 2 if row % 2 else 0)
            y = row * step * 0.9
            # Distance from the corner (0..1): density falls off from the corner.
            cx, cy = corner
            d = (((x - cx) / width) ** 2 + ((y - cy) / height) ** 2) ** 0.5
            if rnd.random() > 1.15 - 2.6 * d:
                continue
            if d > 0.28:
                continue
            r = rnd.uniform(5, 10)
            color = rnd.choice([PURPLE, PURPLE_LIGHT, '#8A30DA', '#B07AEE', PURPLE_DARK])
            opacity = rnd.uniform(0.7, 1.0)
            out.append(f'<polygon points="{hex_points(x, y, r, 30)}" fill="{color}" fill-opacity="{opacity:.2f}"/>')
    return '\n'.join(out)


def splash_svg(size=600, viewer=False):
    """Right part of the splash screen (the left part is drawn by the application).
    The wordmark is composited afterwards from the official logo."""
    s = size
    pattern = hex_pattern(s, s, (s, s), seed=7) + hex_pattern(s, s, (0, s), seed=11)
    caption = ''
    if viewer:
        caption = f'<text x="300" y="560" text-anchor="middle" font-family="sans-serif" font-size="20" fill="{PURPLE_DARK}">G-code Viewer</text>'
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{s}" height="{s}" viewBox="0 0 {s} {s}">
  <rect width="{s}" height="{s}" fill="{SURFACE}"/>
  {pattern}
  <g transform="translate(170,30) scale(1.3)">{logo_group(hex_fill='none', stroke_width=4, badge=viewer)}</g>
  {caption}
</svg>
'''


def run(*cmd):
    subprocess.run(cmd, check=True)


def render(svg, png, width, height=None):
    with tempfile.NamedTemporaryFile('w', suffix='.svg', delete=False) as f:
        f.write(svg)
        path = f.name
    try:
        args = ['rsvg-convert', '-w', str(width)]
        if height:
            args += ['-h', str(height)]
        run(*args, '-o', png, path)
    finally:
        os.unlink(path)


def toolbar_background_svg():
    """Background of the floating toolbars of the 3D scene (9-slice with 16 px borders): dark rounded panel."""
    return '''<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <rect x="1" y="1" width="126" height="126" rx="14" ry="14" fill="#1C1C21" fill-opacity="0.94" stroke="#34343C" stroke-width="2"/>
</svg>
'''


def main():
    render(toolbar_background_svg(), os.path.join(ICONS, 'toolbar_background.png'), 128, 128)
    for name, kwargs in (('PrusaSlicer', {}), ('PrusaSlicer-gcodeviewer', {'badge': True})):
        svg = logo_svg(**kwargs)
        with open(os.path.join(ICONS, name + '.svg'), 'w') as f:
            f.write(svg)
        for px in (32, 128, 192):
            render(svg, os.path.join(ICONS, f'{name}_{px}px.png'), px)
        render(svg, os.path.join(ICONS, f'{name}-mac_128px.png'), 128)
        # Windows icon with all the usual sizes; the smallest ones get a heavier outline.
        tmp = []
        for px in (16, 24, 32, 48, 64, 128, 256):
            p = os.path.join(tempfile.gettempdir(), f'tisma_{name}_{px}.png')
            render(logo_svg(stroke_width=26 if px <= 32 else 22, **kwargs), p, px)
            tmp.append(p)
        run('convert', *tmp, os.path.join(ICONS, name + '.ico'))
        for p in tmp:
            os.unlink(p)

    # Large logo (README, about dialog) with the official outline weight.
    render(logo_svg(stroke_width=6), os.path.join(ICONS, 'PrusaSlicer.png'), 512)
    render(logo_svg(hex_fill='none'), os.path.join(ICONS, 'PrusaSlicer_192px_transparent.png'), 192)
    gray = os.path.join(ICONS, 'PrusaSlicer_192px_grayscale.png')
    render(logo_svg(), gray, 192)
    run('convert', gray, '-colorspace', 'Gray', gray)

    # Wordmark "TISMΛ" with the tagline, cut from the official logo; JPEG noise around it is whitened
    # so that it can be multiplied onto the background.
    wordmark = os.path.join(tempfile.gettempdir(), 'tisma_wordmark.png')
    run('convert', os.path.join(ORIGINALS, 'tisma_logo_horizontal.jpg'), '-crop', '440x150+578+542', '+repage',
        '-channel', 'RGB', '-white-threshold', '86%', '+channel', wordmark)
    for viewer, out in ((False, 'splashscreen.jpg'), (True, 'splashscreen-gcodepreview.jpg')):
        png = os.path.join(tempfile.gettempdir(), 'tisma_splash.png')
        render(splash_svg(viewer=viewer), png, 600, 600)
        run('convert', png, wordmark, '-geometry', '+80+315', '-compose', 'Multiply', '-composite',
            '-quality', '92', os.path.join(ICONS, out))
        os.unlink(png)
    os.unlink(wordmark)


if __name__ == '__main__':
    main()
