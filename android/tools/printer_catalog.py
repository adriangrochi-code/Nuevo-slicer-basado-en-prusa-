#!/usr/bin/env python3
"""Tisma for Android: the catalog of FFF printers of Tisma's vendor bundles (resources/profiles), for the printer
picker of the app.

Every instantiable printer preset of the bundles is flattened (inheritance resolved as in PrusaSlicer, with
tools/profiles/validate_bundles.py) together with its default print profile, so that the app slices with the bed,
firmware, custom G-code, machine limits, speeds and accelerations the vendor chose for it. The settings of the app
(filament, layer height, infill, walls, supports, brim) are written after these and override them.

Output: a JSON document (gzip when the name ends with .gz; the app stores it plain, the APK compresses it)
    {"keys": [option names], "values": [option values],
     "vendors": [{"name": vendor, "models": [{"name": model, "presets": [
         {"name": preset, "nozzle": "0.4", "config": [key index, value index, ...],
          "filament_diameter": "2.85" (only when not 1.75)}]}]}]}
Options and values are stored once (most presets of a vendor share their G-code and limits).

Usage: printer_catalog.py <resources/profiles> <output .json or .json.gz>
"""

import argparse
import gzip
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'profiles'))
from validate_bundles import Bundles, parse_ini  # noqa: E402

# Options of the presets which only make sense inside the desktop program (preset links, GUI, notes).
DROPPED = {
    'inherits', 'compatible_printers', 'compatible_printers_condition', 'compatible_prints',
    'compatible_prints_condition', 'printer_model', 'printer_variant', 'default_print_profile',
    'default_filament_profile', 'renamed_from', 'printer_settings_id', 'print_settings_id', 'printer_notes', 'notes',
    'bed_custom_texture', 'bed_custom_model', 'thumbnails_format', 'host_type', 'print_host', 'printhost_apikey',
    'printhost_cafile', 'printhost_port', 'printhost_authorization_type', 'printhost_user', 'printhost_password',
    'post_process', 'output_filename_format',
}


def unquote(s):
    return s.strip().strip('"')


def catalog(profiles):
    keys, values = [], []
    key_index, value_index = {}, {}

    def index(table, lookup, s):
        i = lookup.get(s)
        if i is None:
            i = lookup[s] = len(table)
            table.append(s)
        return i

    vendors = {}
    missing_print = []
    # The filaments of the templates bundle, which the printers of other bundles may name as their default.
    library = [p for p in [os.path.join(profiles, 'Templates.ini')] if os.path.exists(p)]
    for path in sorted(os.listdir(profiles)):
        if not path.endswith('.ini'):
            continue
        bundle = os.path.join(profiles, path)
        sections = parse_ini(bundle)
        vendor_name = sections.get(('vendor', ''), {}).get('name', path[:-4])
        bundles = Bundles([bundle] + library)
        for (kind, name) in sections:
            if kind != 'printer' or name.startswith('*'):
                continue
            try:
                printer = bundles.flat('printer', name)
            except KeyError as e:
                print('%s: printer "%s": %s' % (path, name, e), file=sys.stderr)
                continue
            if printer.get('printer_technology', 'FFF') != 'FFF':
                continue
            cfg = {}
            print_name = unquote(printer.get('default_print_profile', ''))
            if print_name:
                try:
                    cfg.update(bundles.flat('print', print_name))
                except KeyError:
                    # Some bundles name a print profile they do not have: the defaults of the core, as for a
                    # custom printer.
                    missing_print.append('%s: %s' % (path, name))
            cfg.update(printer)
            # The filament diameter of the printer (2.85 mm for some), from its default filament.
            diameter = ''
            filament_name = unquote(printer.get('default_filament_profile', '').split(';')[0])
            if not filament_name:
                filament_name = sections.get(('printer_model', printer.get('printer_model', '')), {}).get(
                    'default_materials', '').split(';')[0].strip()
            if filament_name:
                try:
                    diameter = bundles.flat('filament', filament_name).get('filament_diameter', '').split(',')[0]
                except KeyError:
                    pass
            model_id = printer.get('printer_model', '')
            model = sections.get(('printer_model', model_id), {})
            model_name = model.get('name', '') or name
            nozzle = printer.get('printer_variant', '') or printer.get('nozzle_diameter', '').split(',')[0]
            pairs = []
            for k, v in cfg.items():
                if k in DROPPED:
                    continue
                pairs += [index(keys, key_index, k), index(values, value_index, v)]
            models = vendors.setdefault(vendor_name, {})
            preset = {'name': name, 'nozzle': nozzle, 'config': pairs}
            if diameter and diameter != '1.75':
                preset['filament_diameter'] = diameter
            models.setdefault(model_name, []).append(preset)

    def nozzle_order(preset):
        try:
            return (float(preset['nozzle']), preset['name'])
        except ValueError:
            return (99., preset['name'])

    out_vendors = []
    for vendor_name in sorted(vendors, key=str.casefold):
        models = vendors[vendor_name]
        out_vendors.append({'name': vendor_name, 'models': [
            {'name': m, 'presets': sorted(models[m], key=nozzle_order)} for m in sorted(models, key=str.casefold)]})
    if missing_print:
        print('%d printers without their default print profile (first: %s)' % (len(missing_print), missing_print[0]),
              file=sys.stderr)
    return {'keys': keys, 'values': values, 'vendors': out_vendors}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('profiles')
    ap.add_argument('output')
    args = ap.parse_args()
    data = catalog(args.profiles)
    raw = json.dumps(data, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, 'wb') as f:
        if args.output.endswith('.gz'):
            # mtime 0: the same profiles give the same file.
            with gzip.GzipFile(fileobj=f, mode='wb', compresslevel=9, mtime=0) as gz:
                gz.write(raw)
        else:
            f.write(raw)
    presets = sum(len(p) for v in data['vendors'] for p in (m['presets'] for m in v['models']))
    models = sum(len(v['models']) for v in data['vendors'])
    print('%d vendors, %d models, %d printer presets, %d bytes (%d uncompressed)' % (
        len(data['vendors']), models, presets, os.path.getsize(args.output), len(raw)), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
