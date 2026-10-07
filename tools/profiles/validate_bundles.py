#!/usr/bin/env python3
"""Tisma Slicer: validates vendor bundles by slicing a test model with every printer preset.

For each instantiable printer preset of the bundles given, the printer, its default print profile and its default
filament (or the first default material of its model) are flattened (inheritance resolved as in PrusaSlicer) into one
configuration, which is loaded by the Tisma command line to slice the model.

Usage: validate_bundles.py <tisma-slicer binary> <model.stl> <out dir> <bundle.ini>... [--jobs N]
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys


def parse_ini(path):
    """{(type, name): {key: value}} and the [printer_model:...] / [vendor] sections."""
    sections = {}
    current = None
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            m = re.match(r'\[([^:\]]+)(?::(.*))?\]\s*$', line)
            if m:
                current = (m.group(1), m.group(2) or '')
                sections.setdefault(current, {})
                continue
            if current is None or '=' not in line:
                continue
            k, v = line.split('=', 1)
            sections[current][k.strip()] = v.strip()
    return sections


class Bundles:
    def __init__(self, paths):
        self.sections = {}
        self.origin = {}
        for p in paths:
            for key, values in parse_ini(p).items():
                self.sections.setdefault(key, values)
                self.origin.setdefault(key, p)

    def flat(self, kind, name, depth=0):
        values = self.sections.get((kind, name))
        if values is None:
            raise KeyError('%s "%s" not found' % (kind, name))
        out = {}
        parents = values.get('inherits', '')
        if parents and depth < 30:
            for parent in [x.strip().strip('"') for x in parents.split(';') if x.strip()]:
                out.update(self.flat(kind, parent, depth + 1))
        out.update(values)
        out.pop('inherits', None)
        return out


def run(binary, model, cfg_path, out_path):
    p = subprocess.run([binary, '--load', cfg_path, '--export-gcode', model, '-o', out_path],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
    ok = p.returncode == 0 and os.path.exists(out_path) and os.path.getsize(out_path) > 1000
    msg = ''
    if not ok:
        lines = [l for l in p.stdout.splitlines() if 'error' in l.lower() or 'invalid' in l.lower() or 'failed' in l.lower()]
        msg = (lines[-1] if lines else p.stdout.strip().splitlines()[-1] if p.stdout.strip() else 'exit %d' % p.returncode)[:300]
    if os.path.exists(out_path):
        os.remove(out_path)
    return ok, msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('binary')
    ap.add_argument('model')
    ap.add_argument('out_dir')
    ap.add_argument('bundles', nargs='+')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--library', action='append', default=[], help='bundles of filaments only (templates)')
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    results = []
    tasks = []
    for bundle in args.bundles:
        b = Bundles([bundle] + args.library)
        for (kind, name), values in parse_ini(bundle).items():
            if kind != 'printer' or name.startswith('*'):
                continue
            try:
                printer = b.flat('printer', name)
                print_name = printer.get('default_print_profile', '')
                filament_name = printer.get('default_filament_profile', '').split(';')[0].strip().strip('"')
                if not filament_name:
                    model = b.sections.get(('printer_model', printer.get('printer_model', '')), {})
                    filament_name = model.get('default_materials', '').split(';')[0].strip()
                cfg = {}
                cfg.update(b.flat('print', print_name) if print_name else {})
                cfg.update(b.flat('filament', filament_name) if filament_name else {})
                cfg.update(printer)
            except KeyError as e:
                results.append((bundle, name, False, 'preset: %s' % e))
                continue
            for k in ('compatible_printers', 'compatible_printers_condition', 'compatible_prints',
                      'compatible_prints_condition', 'printer_model', 'printer_variant', 'default_print_profile',
                      'default_filament_profile', 'renamed_from'):
                cfg.pop(k, None)
            safe = re.sub(r'[^A-Za-z0-9]+', '_', os.path.basename(bundle)[:-4] + '_' + name)
            cfg_path = os.path.join(args.out_dir, safe + '.ini')
            with open(cfg_path, 'w', encoding='utf-8') as f:
                for k, v in cfg.items():
                    f.write('%s = %s\n' % (k, v))
            tasks.append((bundle, name, cfg_path, os.path.join(args.out_dir, safe + '.gcode')))

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futures = {ex.submit(run, args.binary, args.model, t[2], t[3]): t for t in tasks}
        for fut in concurrent.futures.as_completed(futures):
            t = futures[fut]
            try:
                ok, msg = fut.result()
            except Exception as e:
                ok, msg = False, str(e)
            results.append((t[0], t[1], ok, msg))

    failed = [r for r in results if not r[2]]
    print('printers: %d, sliced: %d, failed: %d' % (len(results), len(results) - len(failed), len(failed)))
    for bundle, name, ok, msg in sorted(failed):
        print('FAILED %s / %s: %s' % (os.path.basename(bundle), name, msg))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
