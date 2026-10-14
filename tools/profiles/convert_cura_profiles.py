#!/usr/bin/env python3
"""Tisma Slicer: converts the printer definitions of UltiMaker Cura that Tisma does not have into vendor bundles.

Source: resources/definitions and resources/extruders of Cura (https://github.com/Ultimaker/Cura), LGPLv3. Only the
machine is taken (bed size and shape, height, nozzle, firmware flavour, start / end G-code, retraction, limits);
the print and material profiles of Cura are not converted: each bundle gets three generic print profiles per nozzle,
and the printers use the generic filaments of the OrcaSlicer library (1.75 mm) or generic 2.85 mm filaments of the
bundle.

- Every manufacturer becomes a bundle "Cura_<manufacturer>" (shown as "<manufacturer>", or "<manufacturer> (Cura)"
  when Tisma has a bundle of that name).
- Printers that Tisma already has (same model name, in any bundle) are skipped, as well as hidden and abstract
  definitions, printers with several extruders and printers whose firmware or G-code Tisma cannot produce (Griffin,
  UltiGCode, MakerBot, BFB, Mach3, volumetric E, conditional or unknown variables in the start / end G-code).

Usage: convert_cura_profiles.py <cura resources dir> <tisma resources/profiles dir> [--commit SHA]
"""

import argparse
import ast
import glob
import json
import math
import os
import re
import sys

FLAVORS = {
    'RepRap (Marlin/Sprinter)': 'marlin2',
    'RepRap (RepRap)': 'reprapfirmware',
    'Repetier': 'repetier',
}

# Cura variables of the start / end G-code -> Tisma placeholders (the others are replaced by the value of the printer
# definition if it has one, otherwise the printer is skipped).
VARIABLES = {
    'material_bed_temperature': '[bed_temperature]',
    'material_bed_temperature_layer_0': '[first_layer_bed_temperature]',
    'material_print_temperature': '[temperature]',
    'material_print_temperature_layer_0': '[first_layer_temperature]',
    'material_initial_print_temperature': '[first_layer_temperature]',
    'material_final_print_temperature': '[temperature]',
    'default_material_print_temperature': '[temperature]',
    'default_material_bed_temperature': '[bed_temperature]',
    'build_volume_temperature': '0',
    'layer_height': '[layer_height]',
    'layer_height_0': '[first_layer_height]',
    'initial_extruder_nr': '[initial_extruder]',
    'extruder_nr': '[current_extruder]',
    'material_type': '[filament_type]',
    'print_time': '',
    'filament_amount': '',
    'filament_weight': '',
    'filament_cost': '',
    'jobname': '[input_filename_base]',
    'date': '',
    'time': '',
    'day': '',
    'travel_speed': '[travel_speed]',
    'speed_travel': '[travel_speed]',
    'machine_nozzle_size': '[nozzle_diameter]',
    'material_diameter': '[filament_diameter]',
    'material_guid': '',
    'material_id': '',
    'material_name': '[filament_settings_id]',
    'material_brand': '',
    'prime_tower_enable': '[wipe_tower]',
    'adhesion_type': '',
    'print_bed_temperature': '[bed_temperature]',
    'print_temperature': '[temperature]',
}

VENDOR_SUFFIXES = re.compile(r'[,.]?\s*(B\.V\.|N\.V\.|LLC\.?|Ltd\.?|GmbH|Co\.?|Inc\.?|AB|Technologies|Technology)\s*$', re.I)


def sanitize(name):
    return re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_')


def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def fmt(v):
    if isinstance(v, bool):
        return '1' if v else '0'
    if isinstance(v, float):
        s = ('%.4f' % v).rstrip('0').rstrip('.')
        return s if s else '0'
    return str(v)


class Definitions:
    def __init__(self, cura):
        self.defs = {}
        for path in glob.glob(os.path.join(cura, 'definitions', '*.def.json')):
            with open(path, encoding='utf-8') as f:
                self.defs[os.path.basename(path)[:-len('.def.json')]] = json.load(f)
        self.extruders = {}
        for path in glob.glob(os.path.join(cura, 'extruders', '*.def.json')):
            with open(path, encoding='utf-8') as f:
                self.extruders[os.path.basename(path)[:-len('.def.json')]] = json.load(f)
        # Defaults of the base definitions (the settings trees).
        self.base_defaults = {}
        for base in ('fdmprinter', 'fdmextruder'):
            self._collect(self.defs.get(base) or self.extruders.get(base) or {}, base)

    def _collect(self, d, base):
        def walk(settings):
            for key, s in settings.items():
                if 'default_value' in s:
                    self.base_defaults.setdefault(key, s['default_value'])
                if 'children' in s:
                    walk(s['children'])
        walk(d.get('settings', {}))

    def chain(self, table, name):
        out = []
        while name and name in table and len(out) < 30:
            out.append(table[name])
            name = table[name].get('inherits')
        return out

    def metadata(self, name):
        md = {}
        for d in reversed(self.chain(self.defs, name)):
            md.update(d.get('metadata', {}))
        return md

    @staticmethod
    def _literal(entry):
        if 'value' in entry:
            v = entry['value']
            if isinstance(v, str):
                try:
                    return True, ast.literal_eval(v)
                except Exception:
                    return False, None
            return True, v
        if 'default_value' in entry:
            return True, entry['default_value']
        return False, None

    def value(self, chain, key, default=None):
        for d in chain:
            entry = d.get('overrides', {}).get(key)
            if entry is not None:
                ok, v = self._literal(entry)
                if ok:
                    return v
        return self.base_defaults.get(key, default)


def bed_shape(width, depth, center_zero, shape):
    x0, y0 = (-width / 2., -depth / 2.) if center_zero else (0., 0.)
    if shape in ('elliptic',):
        cx, cy = x0 + width / 2., y0 + depth / 2.
        pts = [(cx + width / 2. * math.cos(2 * math.pi * i / 64), cy + depth / 2. * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    else:
        pts = [(x0, y0), (x0 + width, y0), (x0 + width, y0 + depth), (x0, y0 + depth)]
    return ','.join('%sx%s' % (fmt(round(x, 3)), fmt(round(y, 3))) for x, y in pts)


def translate_gcode(text, lookup):
    """Cura G-code -> Tisma, or None if it cannot be translated."""
    if text is None:
        return ''
    if '{if' in text or '{else' in text or '{endif' in text or '{%' in text:
        return None
    # Square brackets are placeholders in Tisma: the literal ones of Cura (comments) become parentheses.
    text = text.replace('[', '(').replace(']', ')')
    unknown = []

    def repl(m):
        name = m.group(1).strip()
        if name in VARIABLES:
            return VARIABLES[name]
        v = lookup(name)
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            return fmt(v)
        if isinstance(v, str) and v and '\n' not in v and '{' not in v:
            return v
        unknown.append(name)
        return ''
    out = re.sub(r'\{([A-Za-z_][A-Za-z0-9_]*)(?:\s*,\s*-?\d+)?\}', repl, text)
    if unknown or '{' in out or '}' in out:
        return None
    lines = []
    for line in out.split('\n'):
        # Tisma prints with relative E (G92 E0 at every layer): no switch to absolute E.
        if re.match(r'\s*M82\b', line):
            line = 'M83 ; relative extrusion (Tisma)'
        lines.append(line)
    return '\\n'.join(lines).replace('\r', '')


def existing_printers(profiles_dir):
    """Normalized model names of the printers of Tisma (any bundle), with and without the vendor in front."""
    names, vendors = set(), {}
    for ini in glob.glob(os.path.join(profiles_dir, '*.ini')):
        stem = os.path.basename(ini)[:-4]
        if stem.startswith('Cura_'):
            continue
        section, vendor = '', stem
        with open(ini, encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = re.match(r'\[([^\]]+)\]\s*$', line)
                if m:
                    section = m.group(1)
                    continue
                m = re.match(r'name\s*=\s*(.+)$', line)
                if m and section == 'vendor':
                    vendor = m.group(1).strip()
                    # Without the suffix of the converted bundles: "Anycubic (OrcaSlicer)" is the brand Anycubic.
                    vendors[re.sub(r'\s*\(OrcaSlicer\)$', '', vendor).lower()] = re.sub(r'\s*\(OrcaSlicer\)$', '', vendor)
                if m and section.startswith('printer_model:'):
                    model = m.group(1).strip()
                    names.add(norm(model))
                    for v in (vendor, stem, vendor.split()[0] if vendor else ''):
                        if v and norm(model).startswith(norm(v)):
                            names.add(norm(model)[len(norm(v)):])
    names.discard('')
    return names, vendors


def model_names(defs, def_id, chain, manufacturer):
    """Candidate names of the printer: its definition name (split at '/') and machine_name, without the vendor."""
    out = set()
    for raw in [defs.defs[def_id].get('name', '')] + [str(defs.value(chain, 'machine_name', ''))]:
        for part in re.split(r'\s*/\s*', raw):
            n = norm(part)
            if not n:
                continue
            out.add(n)
            for v in (manufacturer, manufacturer.split()[0] if manufacturer else '', raw.split()[0] if raw else ''):
                if v and n.startswith(norm(v)) and len(n) > len(norm(v)):
                    out.add(n[len(norm(v)):])
    return out


PRINT_COMMON = {
    'perimeters': 2, 'top_solid_layers': 4, 'bottom_solid_layers': 3, 'fill_density': '15%', 'fill_pattern': 'grid',
    'infill_speed': 60, 'perimeter_speed': 45, 'external_perimeter_speed': 30, 'solid_infill_speed': 45,
    'top_solid_infill_speed': 30, 'gap_fill_speed': 30, 'support_material_speed': 45, 'bridge_speed': 25,
    'first_layer_speed': 20, 'travel_speed': 150, 'skirts': 1, 'skirt_distance': 3, 'brim_width': 0,
    'support_material': 0, 'support_material_threshold': 45, 'support_material_contact_distance': 0.2,
    'extrusion_width': 0, 'first_layer_extrusion_width': 0, 'seam_position': 'aligned', 'gcode_label_objects': 'firmware',
    'output_filename_format': '{input_filename_base}.gcode',
}

FILAMENTS_285 = [
    # name, type, temperature, bed, density
    ('PLA', 'PLA', 205, 60, 1.24),
    ('PETG', 'PETG', 235, 80, 1.27),
    ('ABS', 'ABS', 245, 100, 1.04),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cura_resources')
    ap.add_argument('tisma_profiles')
    ap.add_argument('--commit', default='')
    args = ap.parse_args()

    defs = Definitions(args.cura_resources)
    if 'fdmprinter' not in defs.defs:
        sys.exit('error: %s is not the resources directory of Cura' % args.cura_resources)
    existing, existing_display = existing_printers(args.tisma_profiles)
    existing_vendors = set(existing_display)

    stats = {'converted': 0, 'existing': 0, 'hidden': 0, 'extruders': 0, 'flavor': 0, 'gcode': 0}
    bundles = {}
    for def_id in sorted(defs.defs):
        if def_id in ('fdmprinter', 'fdmextruder'):
            continue
        md = defs.metadata(def_id)
        if not defs.defs[def_id].get('metadata', {}).get('visible', False):
            stats['hidden'] += 1
            continue
        chain = defs.chain(defs.defs, def_id)
        manufacturer = VENDOR_SUFFIXES.sub('', str(md.get('manufacturer', '')).strip()).strip()
        if not manufacturer or manufacturer.lower() in ('unknown', 'custom', 'other'):
            stats['hidden'] += 1
            continue
        if model_names(defs, def_id, chain, manufacturer) & existing:
            stats['existing'] += 1
            continue
        if int(defs.value(chain, 'machine_extruder_count', 1) or 1) != 1:
            stats['extruders'] += 1
            continue
        flavor = FLAVORS.get(defs.value(chain, 'machine_gcode_flavor', 'RepRap (Marlin/Sprinter)'))
        if flavor is None:
            stats['flavor'] += 1
            continue
        # The extruder of the printer (nozzle and filament diameter are settings of the extruder).
        trains = md.get('machine_extruder_trains', {})
        ext_chain = defs.chain(defs.extruders, trains.get('0', trains.get(0, ''))) if trains else []

        def value(key, default=None):
            for d in ext_chain:
                entry = d.get('overrides', {}).get(key)
                if entry is not None:
                    ok, v = Definitions._literal(entry)
                    if ok:
                        return v
            return defs.value(chain, key, default)

        start = translate_gcode(value('machine_start_gcode', ''), value)
        end = translate_gcode(value('machine_end_gcode', ''), value)
        if start is None or end is None:
            stats['gcode'] += 1
            continue
        try:
            nozzle = float(value('machine_nozzle_size', 0.4))
            # Not a filament printer (the Peopoly Moai is a resin printer with a "0.07 mm nozzle" in Cura).
            if nozzle < 0.15:
                stats['flavor'] += 1
                continue
            filament = float(value('material_diameter', 2.85))
            width, depth, height = (float(value(k)) for k in ('machine_width', 'machine_depth', 'machine_height'))
        except (TypeError, ValueError):
            stats['gcode'] += 1
            continue
        model = defs.defs[def_id].get('name', def_id).strip()
        bundle = bundles.setdefault(manufacturer, {'printers': [], 'nozzles': set(), 'f285': False})
        variant = fmt(round(nozzle, 2))
        bundle['nozzles'].add(variant)
        bundle['f285'] |= abs(filament - 2.85) < 0.2
        bundle['printers'].append({
            'id': def_id, 'model': model, 'variant': variant, 'nozzle': nozzle, 'filament': filament,
            'bed_shape': bed_shape(width, depth, bool(value('machine_center_is_zero', False)), value('machine_shape', 'rectangular')),
            'height': height, 'flavor': flavor, 'start': start, 'end': end,
            'heated_bed': bool(value('machine_heated_bed', True)),
            'retract_length': float(value('retraction_amount', 1) or 1),
            'retract_speed': float(value('retraction_speed', 35) or 35),
        })
        existing |= model_names(defs, def_id, chain, manufacturer)
        stats['converted'] += 1

    written = []
    for manufacturer, bundle in sorted(bundles.items()):
        stem = 'Cura_' + sanitize(manufacturer)
        tag = 'PRINTER_VENDOR_CURA_' + sanitize(manufacturer).upper()
        # Same brand as a bundle of Tisma (ignoring case, punctuation and a trailing "3D": Creality3D = Creality).
        def brand_key(name):
            k = norm(name)
            return k[:-2] if k.endswith('3d') and len(k) > 2 else k
        same = [v for v in existing_vendors if brand_key(v) == brand_key(manufacturer)]
        display = existing_display.get(same[0], manufacturer) + ' (Cura)' if same else manufacturer
        out = []
        out.append('# Printers converted from UltiMaker Cura (https://github.com/Ultimaker/Cura, resources/definitions), LGPLv3.')
        out.append('# Generated by tools/profiles/convert_cura_profiles.py%s. Do not edit: run the converter again.' %
                   (' from commit ' + args.commit if args.commit else ''))
        out.append('')
        out.append('[vendor]')
        out.append('repo_id = non-prusa-fff')
        out.append('name = ' + display)
        out.append('config_version = 1.0.1')
        out.append('config_update_url = ')
        out.append('')
        models = {}
        for p in bundle['printers']:
            models.setdefault(p['model'], []).append(p)
        for model, printers in sorted(models.items()):
            out.append('[printer_model:%s]' % model)
            out.append('name = ' + model)
            out.append('variants = ' + '; '.join(sorted({p['variant'] for p in printers})))
            out.append('technology = FFF')
            out.append('family = ' + display)
            if any(abs(p['filament'] - 2.85) < 0.2 for p in printers):
                out.append('default_materials = ' + '; '.join('Generic %s 2.85 @%s' % (n, stem) for n, *_ in FILAMENTS_285))
            else:
                # Filaments of the OrcaSlicer library: a templates bundle, whose filaments are loaded with " @Template".
                out.append('default_materials = Generic ABS @Orca @Template; Generic PETG @Orca @Template; Generic PLA @Orca @Template')
            out.append('')
        # Print profiles: three layer heights for each nozzle.
        out.append('[print:*common @%s*]' % stem)
        for k, v in PRINT_COMMON.items():
            out.append('%s = %s' % (k, fmt(v)))
        out.append('')
        for nozzle in sorted(bundle['nozzles'], key=float):
            n = float(nozzle)
            for label, ratio in (('Fine', 0.3), ('Standard', 0.5), ('Draft', 0.7)):
                h = round(n * ratio, 2)
                out.append('[print:%.2fmm %s @%s %s]' % (h, label, stem.replace('_', ' '), nozzle))
                out.append('inherits = *common @%s*' % stem)
                out.append('layer_height = ' + fmt(h))
                out.append('first_layer_height = ' + fmt(round(min(max(n * 0.5, 0.2), n * 0.75), 2)))
                out.append('compatible_printers_condition = printer_notes=~/.*%s.*/ and nozzle_diameter[0]==%s' % (tag, nozzle))
                out.append('')
        # Generic 2.85 mm filaments (the generic library has 1.75 mm ones).
        if bundle['f285']:
            for name, ftype, temp, bed, density in FILAMENTS_285:
                out.append('[filament:Generic %s 2.85 @%s]' % (name, stem))
                out.append('filament_type = ' + ftype)
                out.append('filament_diameter = 2.85')
                out.append('filament_density = ' + fmt(density))
                out.append('temperature = %d' % temp)
                out.append('first_layer_temperature = %d' % (temp + 5))
                out.append('bed_temperature = %d' % bed)
                out.append('first_layer_bed_temperature = %d' % bed)
                out.append('filament_max_volumetric_speed = 12')
                out.append('cooling = %d' % (0 if ftype == 'ABS' else 1))
                out.append('fan_always_on = %d' % (0 if ftype == 'ABS' else 1))
                out.append('min_fan_speed = %d' % (15 if ftype == 'ABS' else 100))
                out.append('max_fan_speed = %d' % (30 if ftype == 'ABS' else 100))
                out.append('compatible_printers_condition = printer_notes=~/.*%s_285.*/' % tag)
                out.append('')
        # Printers.
        out.append('[printer:*common @%s*]' % stem)
        out.append('printer_technology = FFF')
        out.append('use_relative_e_distances = 1')
        out.append('before_layer_gcode = ;BEFORE_LAYER_CHANGE\\nG92 E0\\n;[layer_z]\\n')
        out.append('layer_gcode = ;AFTER_LAYER_CHANGE\\n;[layer_z]')
        out.append('machine_limits_usage = time_estimate_only')
        out.append('retract_lift = 0')
        out.append('retract_before_travel = 2')
        out.append('wipe = 0')
        out.append('extruder_colour = ""')
        out.append('single_extruder_multi_material = 0')
        out.append('')
        for p in sorted(bundle['printers'], key=lambda p: (p['model'], float(p['variant']))):
            name = '%s %s nozzle' % (p['model'], p['variant'])
            n = p['nozzle']
            out.append('[printer:%s]' % name)
            out.append('inherits = *common @%s*' % stem)
            out.append('printer_model = ' + p['model'])
            out.append('printer_variant = ' + p['variant'])
            out.append('printer_notes = %s%s\\nCURA_DEFINITION_%s' % (tag, ('\\n' + tag + '_285') if abs(p['filament'] - 2.85) < 0.2 else '', p['id']))
            out.append('bed_shape = ' + p['bed_shape'])
            out.append('max_print_height = ' + fmt(p['height']))
            out.append('nozzle_diameter = ' + fmt(n))
            out.append('min_layer_height = ' + fmt(round(n * 0.2, 2)))
            out.append('max_layer_height = ' + fmt(round(n * 0.75, 2)))
            out.append('gcode_flavor = ' + p['flavor'])
            out.append('retract_length = ' + fmt(min(p['retract_length'], 10.)))
            out.append('retract_speed = ' + fmt(min(p['retract_speed'], 100.)))
            out.append('start_gcode = ' + p['start'])
            out.append('end_gcode = ' + p['end'])
            out.append('default_print_profile = %.2fmm Standard @%s %s' % (round(n * 0.5, 2), stem.replace('_', ' '), p['variant']))
            out.append('default_filament_profile = ' + ('Generic PLA 2.85 @' + stem if abs(p['filament'] - 2.85) < 0.2 else 'Generic PLA @Orca @Template'))
            out.append('')
        path = os.path.join(args.tisma_profiles, stem + '.ini')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(out))
        with open(os.path.join(args.tisma_profiles, stem + '.idx'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('min_slic3r_version = 2.9.0\n1.0.1 Default filaments of the OrcaSlicer library named as loaded (@Template).\n'
                    '1.0.0 Converted from UltiMaker Cura %s.\n' % args.commit[:10])
        written.append(stem)

    print('printers converted: %(converted)d, already in Tisma: %(existing)d, hidden or abstract: %(hidden)d, '
          'several extruders: %(extruders)d, unsupported firmware or not FFF: %(flavor)d, G-code not translatable: %(gcode)d' % stats)
    print('bundles: %d' % len(written))


if __name__ == '__main__':
    main()
