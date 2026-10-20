#!/usr/bin/env python3
"""Tisma Slicer: sanity check of the filament presets of vendor bundles.

Every instantiable filament preset (inheritance resolved as in PrusaSlicer, with the templates bundle as library) is
checked for values a print cannot work with: missing type, nozzle and bed temperatures outside the range of its
material family, a diameter that is not 1.75 / 2.85 / 3.0 mm, a density outside 0.6-4.5 g/cm3 (metal filled filaments
reach 4), a zero or negative
maximum volumetric speed. The ranges are wide on purpose (they catch broken values, not tuning choices); the material
ranges come from the manufacturers' data sheets of common filaments (e.g. PLA 190-230 °C, PETG 220-250 °C).

Usage: check_filaments.py <bundle.ini>... [--library Templates.ini] [--quiet]
Exit status 1 when a preset has a problem.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_bundles import Bundles, parse_ini  # noqa: E402

# Nozzle temperature range [°C] by material family (start of filament_type); bed range is 0-160 for all (PEEK, PEI).
NOZZLE = {
    'PLA': (170, 260), 'PETG': (200, 290), 'PET': (200, 330), 'PCTG': (210, 290), 'ABS': (200, 300),
    'ASA': (200, 300), 'HIPS': (200, 280), 'TPU': (180, 270), 'FLEX': (180, 270), 'TPE': (180, 270),
    'PA': (220, 320), 'NYLON': (220, 320), 'PC': (230, 320), 'PP': (180, 310), 'PVA': (170, 240), 'BVOH': (180, 240),
    'PVB': (180, 240), 'PEEK': (340, 450), 'PEI': (330, 420), 'PEKK': (330, 420), 'PPS': (280, 350), 'PPA': (260, 330),
    'CPE': (220, 290), 'PEBA': (200, 260), 'EVA': (150, 230), 'PHA': (170, 230),
}
DEFAULT_NOZZLE = (150, 450)


def numbers(value):
    out = []
    for part in str(value).replace(';', ',').split(','):
        part = part.strip().rstrip('%')
        try:
            out.append(float(part))
        except ValueError:
            pass
    return out


def nozzle_range(filament_type):
    t = filament_type.upper().replace('_', '-')
    for key in sorted(NOZZLE, key=len, reverse=True):
        if t == key or t.startswith(key + '-') or t.startswith(key + '+') or t.startswith(key + ' ') or t.startswith(key):
            return NOZZLE[key]
    return DEFAULT_NOZZLE


def check(cfg):
    problems = []
    ftype = cfg.get('filament_type', '').strip().strip('"')
    if not ftype:
        problems.append('no filament_type')
    lo, hi = nozzle_range(ftype)
    for key in ('temperature', 'first_layer_temperature'):
        for v in numbers(cfg.get(key, '')):
            if v != 0 and not lo <= v <= hi:
                problems.append('%s %g outside %d-%d for %s' % (key, v, lo, hi, ftype or '?'))
    if not numbers(cfg.get('temperature', '')) or all(v == 0 for v in numbers(cfg.get('temperature', ''))):
        problems.append('no nozzle temperature')
    for key in ('bed_temperature', 'first_layer_bed_temperature'):
        for v in numbers(cfg.get(key, '')):
            if not 0 <= v <= 160:
                problems.append('%s %g outside 0-160' % (key, v))
    for v in numbers(cfg.get('filament_diameter', '')):
        if not any(abs(v - d) < 0.11 for d in (1.75, 2.85, 3.0)):
            problems.append('filament_diameter %g' % v)
    for v in numbers(cfg.get('filament_density', '')):
        if v != 0 and not 0.6 <= v <= 4.5:
            problems.append('filament_density %g' % v)
    for v in numbers(cfg.get('filament_max_volumetric_speed', '')):
        if v < 0:
            problems.append('filament_max_volumetric_speed %g' % v)
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bundles', nargs='+')
    ap.add_argument('--library', action='append', default=[])
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()

    checked = bad = 0
    for bundle in args.bundles:
        b = Bundles([bundle] + args.library)
        for (kind, name), _ in parse_ini(bundle).items():
            if kind != 'filament' or name.startswith('*'):
                continue
            checked += 1
            try:
                problems = check(b.flat('filament', name))
            except KeyError as e:
                problems = ['preset: %s' % e]
            if problems:
                bad += 1
                print('%s | %s | %s' % (os.path.basename(bundle), name, '; '.join(problems)))
    if not args.quiet or bad:
        print('%d filament presets checked, %d with problems' % (checked, bad), file=sys.stderr)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
