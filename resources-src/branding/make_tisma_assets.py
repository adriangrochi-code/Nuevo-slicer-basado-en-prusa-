#!/usr/bin/env python3
"""Generates the Tisma branding assets (application icons, splash screen) into resources/icons.

The file names of PrusaSlicer are kept (PrusaSlicer_128px.png ...) so that the application code
loading them does not change.

Requires rsvg-convert (librsvg2-bin), ImageMagick (convert) and the Montserrat font.
Run from the repository root:  python3 resources-src/branding/make_tisma_assets.py
"""

import os
import random
import subprocess
import tempfile

ICONS = os.path.join('resources', 'icons')

# Tisma palette.
PURPLE       = '#5B3CC4'
PURPLE_LIGHT = '#8466E0'
PURPLE_DARK  = '#4527A8'
LAVENDER     = '#C9BCF2'
INK          = '#141414'
SURFACE      = '#F2F2F4'
GREY_LIGHT   = '#E4E4E9'
GREY         = '#CFCFD7'


def logo_group(hex_fill='#FFFFFF', stroke=INK, badge=False):
    """The Tisma mark (pyramid inside a hexagon) in a 200x200 box."""
    badge_svg = ''
    if badge:
        # G-code viewer: small purple hexagon with a white "G" in the lower right corner.
        badge_svg = f'''
    <polygon points="165,118 194,135 194,169 165,186 136,169 136,135" fill="{PURPLE}" stroke="#FFFFFF" stroke-width="5"/>
    <path d="M178,142 A17,17 0 1 0 181,160 L168,160" fill="none" stroke="#FFFFFF" stroke-width="7" stroke-linecap="round"/>'''
    return f'''
  <defs>
    <linearGradient id="pl" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{PURPLE_LIGHT}"/>
      <stop offset="1" stop-color="{PURPLE_DARK}"/>
    </linearGradient>
    <linearGradient id="pr" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#9579EA"/>
      <stop offset="1" stop-color="{PURPLE}"/>
    </linearGradient>
  </defs>
  <g>
    <polygon points="100,8 180,54 180,146 100,192 20,146 20,54" fill="{hex_fill}" stroke="{stroke}" stroke-width="9" stroke-linejoin="miter"/>
    <polygon points="100,26 36,150 164,150" fill="{GREY_LIGHT}"/>
    <polygon points="36,150 164,150 151,166 49,166" fill="{GREY}"/>
    <polygon points="100,38 60,142 100,158" fill="url(#pl)"/>
    <polygon points="100,38 140,142 100,158" fill="url(#pr)"/>
    <line x1="100" y1="38" x2="100" y2="158" stroke="#FFFFFF" stroke-opacity="0.55" stroke-width="2"/>{badge_svg}
  </g>'''


def logo_svg(**kwargs):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 200 200">{logo_group(**kwargs)}\n</svg>\n'


def hex_points(cx, cy, r):
    import math
    return ' '.join(f'{cx + r * math.cos(math.radians(60 * i - 30)):.1f},{cy + r * math.sin(math.radians(60 * i - 30)):.1f}'
                    for i in range(6))


def hex_pattern(width, height, corner, seed):
    """Scattered purple hexagons growing from a corner, as on the Tisma artwork."""
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
            if d > 0.45:
                continue
            r = rnd.uniform(5, 11)
            color = rnd.choice([PURPLE, PURPLE_LIGHT, '#7353D8', '#9C85E6', PURPLE_DARK])
            opacity = rnd.uniform(0.75, 1.0)
            out.append(f'<polygon points="{hex_points(x, y, r)}" fill="{color}" fill-opacity="{opacity:.2f}"/>')
    return '\n'.join(out)


def splash_svg(size=600, viewer=False):
    """Right part of the splash screen (the left part is drawn by the application)."""
    s = size
    pattern = hex_pattern(s, s, (s, s), seed=7) + hex_pattern(s, s, (0, s), seed=11)
    letters = ((185, 'T'), (242, 'I'), (300, 'S'), (365, 'M'), (430, '\u039b'))
    wordmark = ''.join(
        '<text x="%d" y="380" text-anchor="middle" font-family="Montserrat" font-weight="500" font-size="58" fill="%s">%s</text>'
        % (x, PURPLE if c == 'I' else INK, c) for x, c in letters)
    subtitle = 'G-code Viewer' if viewer else 'Fabricación · Innovación · Flexibilidad'
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{s}" height="{s}" viewBox="0 0 {s} {s}">
  <rect width="{s}" height="{s}" fill="{SURFACE}"/>
  <polygon points="{hex_points(300, 150, 175)}" fill="none" stroke="{GREY}" stroke-width="10" opacity="0.6"/>
  <polygon points="300,-40 230,260 370,260" fill="{LAVENDER}" opacity="0.35"/>
  {pattern}
  <g transform="translate(195,95) scale(1.05)">{logo_group(badge=viewer)}</g>
  {wordmark}
  <line x1="245" y1="405" x2="355" y2="405" stroke="{PURPLE}" stroke-width="3"/>
  <rect x="105" y="418" width="390" height="34" fill="{SURFACE}" opacity="0.85"/>
  <text x="300" y="441" text-anchor="middle" font-family="Montserrat" font-size="19" fill="{INK}">{subtitle}</text>
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


def main():
    for name, kwargs in (('PrusaSlicer', {}), ('PrusaSlicer-gcodeviewer', {'badge': True})):
        svg = logo_svg(**kwargs)
        with open(os.path.join(ICONS, name + '.svg'), 'w') as f:
            f.write(svg)
        sizes = [32, 128, 192] if name == 'PrusaSlicer' else [32, 128, 192]
        for px in sizes:
            render(svg, os.path.join(ICONS, f'{name}_{px}px.png'), px)
        render(svg, os.path.join(ICONS, f'{name}-mac_128px.png'), 128)
        # Windows icon with all the usual sizes.
        tmp = []
        for px in (16, 24, 32, 48, 64, 128, 256):
            p = os.path.join(tempfile.gettempdir(), f'tisma_{name}_{px}.png')
            render(svg, p, px)
            tmp.append(p)
        run('convert', *tmp, os.path.join(ICONS, name + '.ico'))
        for p in tmp:
            os.unlink(p)

    render(logo_svg(), os.path.join(ICONS, 'PrusaSlicer.png'), 512)
    render(logo_svg(hex_fill='none'), os.path.join(ICONS, 'PrusaSlicer_192px_transparent.png'), 192)
    gray = os.path.join(ICONS, 'PrusaSlicer_192px_grayscale.png')
    render(logo_svg(), gray, 192)
    run('convert', gray, '-colorspace', 'Gray', gray)

    for viewer, out in ((False, 'splashscreen.jpg'), (True, 'splashscreen-gcodepreview.jpg')):
        png = os.path.join(tempfile.gettempdir(), 'tisma_splash.png')
        render(splash_svg(viewer=viewer), png, 600, 600)
        run('convert', png, '-quality', '92', os.path.join(ICONS, out))
        os.unlink(png)


if __name__ == '__main__':
    main()
