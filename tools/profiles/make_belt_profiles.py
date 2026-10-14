#!/usr/bin/env python3
"""Tisma Slicer: vendor bundles of belt printers (docs/PRINTER_PROFILES.md, "Impresoras de cinta").

Sources, each read from its own repository (the commit goes in the header of the bundle):
- BLACKBELT 3D Printer and varioDrive: BlackBelt Cura (https://github.com/BlackBelt3D/Cura, LGPLv3),
  resources/definitions/blackbelt*.def.json and resources/variants/blackbelt*_<angle>_<nozzle>.inst.cfg.
- IdeaFormer IR3 V2: VertexSlice (https://github.com/kdho77/VertexSlice, AGPLv3), resources/profiles/IdeaFormer.
- Creality CR-30 PrintMill: Creality Print (https://github.com/CrealityOfficial/CrealityPrint, AGPLv3),
  resources/profiles/Creality (machine, process and filament of the CR-30). The firmware of Creality
  (https://github.com/CrealityOfficial/cr-30, Marlin/Configuration.h: Z_MAX_POS 20000000) confirms that the belt is Z.

All of them move the belt with the Z axis and the inclined gantry with the Y axis (belt_axis_z = 1). The start G-code of
BlackBelt and IR3 V2 works with absolute E (M82, G92 E-4.5 to prime), so the bundles keep absolute E.

Usage: make_belt_profiles.py <BlackBelt Cura> <VertexSlice> <Creality Print> <tisma resources/profiles>
       [--blackbelt-commit <sha>] [--vertexslice-commit <sha>] [--creality-print-commit <sha>]
"""

import argparse
import configparser
import json
import math
import os
import re

TISMA_VERSION = '1.0.0'
GENERIC = 'Generic PLA @Orca @Template'
GENERIC_MATERIALS = 'Generic ABS @Orca @Template; Generic PETG @Orca @Template; Generic PLA @Orca @Template'
# The belt as a bed: Tisma needs a finite bed; the part is placed on it.
BELT_LENGTH = 1000


def ini_value(v):
    """Multi-line G-code in a bundle: "\\n" escapes."""
    return str(v).replace('\\', '\\\\').replace('\n', '\\n')


def fmt(x):
    return ('%.4f' % x).rstrip('0').rstrip('.')


def write_bundle(path, header, sections):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        for line in header:
            f.write('# ' + line + '\n')
        for title, values in sections:
            f.write('\n[%s]\n' % title)
            for k, v in values.items():
                f.write('%s = %s\n' % (k, ini_value(v)))
    with open(os.path.splitext(path)[0] + '.idx', 'w', encoding='utf-8', newline='\n') as f:
        f.write('min_slic3r_version = 2.9.0\n%s %s\n' % (TISMA_VERSION, 'Belt printers (Tisma).'))


def vendor(name):
    return ('vendor', {'repo_id': 'non-prusa-fff', 'name': name, 'config_version': TISMA_VERSION, 'config_update_url': ''})


def belt_printer_common(note, flavor, angle, bed_width, height):
    return {
        'printer_technology': 'FFF',
        'printer_notes': note,
        'gcode_flavor': flavor,
        'belt_printer': 1,
        'belt_angle': fmt(angle),
        'belt_axis_z': 1,
        'bed_shape': '0x0,%sx0,%sx%d,0x%d' % (fmt(bed_width), fmt(bed_width), BELT_LENGTH, BELT_LENGTH),
        'max_print_height': fmt(height),
        'use_relative_e_distances': 0,
        'machine_limits_usage': 'time_estimate_only',
        'single_extruder_multi_material': 0,
        'extruder_colour': '""',
        'wipe': 0,
    }


def belt_print_common(note):
    # Not available on a belt (docs/BELT_PRINTER.md): they are disabled when slicing anyway.
    return {
        'skirts': 0,
        'brim_width': 0,
        'support_material': 0,
        'raft_layers': 0,
        'seam_position': 'nearest',
        'gcode_label_objects': 'firmware',
        'output_filename_format': '{input_filename_base}.gcode',
        'compatible_printers_condition': 'printer_notes=~/.*%s.*/' % note,
    }


# ---------------------------------------------------------------------------------------------------------------------
# BLACKBELT 3D (BlackBelt Cura)
# ---------------------------------------------------------------------------------------------------------------------
def read_variants(cura, definition):
    """{angle: [(nozzle, layer_height)]} from resources/variants/<definition>_<angle>_<nozzle>.inst.cfg."""
    out = {}
    folder = os.path.join(cura, 'resources', 'variants')
    for name in sorted(os.listdir(folder)):
        m = re.match(re.escape(definition) + r'_(\d+)_([\d.]+)\.inst\.cfg$', name)
        if not m:
            continue
        cfg = configparser.ConfigParser()
        cfg.read(os.path.join(folder, name), encoding='utf-8')
        values = cfg['values']
        angle = int(values['blackbelt_gantry_angle'])
        out.setdefault(angle, []).append((float(values['machine_nozzle_size']), float(values['layer_height'])))
    for v in out.values():
        v.sort()
    return out


def print_name(definition, layer, nozzle):
    return '%.2fmm Standard @BLACKBELT %s %s' % (layer, '3D' if definition == 'blackbelt' else 'varioDrive', fmt(nozzle))


def blackbelt(cura, commit, profiles):
    defs = os.path.join(cura, 'resources', 'definitions')
    base = json.load(open(os.path.join(defs, 'blackbelt.def.json'), encoding='utf-8'))
    belt = json.load(open(os.path.join(defs, 'beltprinter.def.json'), encoding='utf-8'))
    # The gap to the belt (Belt Offset) of the definition, used by the start G-code ({blackbelt_z_offset}).
    gap = base['settings']['blackbelt_settings']['children']['blackbelt_z_offset_gap']['default_value'] \
        if 'settings' in base and 'blackbelt_settings' in base['settings'] else 0.25
    sections = [vendor('BLACKBELT 3D')]
    printers, prints = [], []
    for definition, label, length in (('blackbelt', 'BLACKBELT 3D Printer', 455), ('blackbeltvd', 'BLACKBELT 3D varioDrive', 325)):
        d = json.load(open(os.path.join(defs, definition + '.def.json'), encoding='utf-8'))
        o = dict(base['overrides'])
        o.update(d.get('overrides', {}))
        start = o['machine_start_gcode']['default_value']
        end = o['machine_end_gcode']['default_value']
        retract = float(o['retraction_amount']['value'])
        width = float(o['machine_width']['default_value'])
        first_layer_speed = 5 if definition == 'blackbelt' else 3
        min_layer_time = float(o['cool_min_layer_time']['value'])
        density = o.get('infill_percentage', o.get('infill_sparse_density', {'value': '15'}))['value']
        for angle, variants in sorted(read_variants(cura, definition).items()):
            model = '%s %d°' % (label, angle)
            sections.append(('printer_model:' + model, {
                'name': model, 'variants': '; '.join(fmt(n) for n, _ in variants), 'technology': 'FFF',
                'family': label, 'default_materials': GENERIC_MATERIALS}))
            for nozzle, layer in variants:
                note = 'BELT_BLACKBELT_%s_%d' % (definition.upper(), angle)
                # round((wall_line_width_0 / 2) - (gap / sin(angle)) - xy_offset, 4), xy_offset = 0, line width = nozzle.
                z_offset = round(nozzle / 2 - gap / math.sin(math.radians(angle)), 4)
                name = '%s %s nozzle' % (model, fmt(nozzle))
                p = belt_printer_common('%s\nBELT_PRINTER' % note, 'marlin', angle, width, length * math.sin(math.radians(angle)))
                p.update({
                    'printer_model': model, 'printer_variant': fmt(nozzle), 'nozzle_diameter': fmt(nozzle),
                    'min_layer_height': fmt(min(0.05, layer)), 'max_layer_height': fmt(0.75 * nozzle),
                    'retract_length': fmt(retract), 'retract_speed': 65, 'retract_before_travel': 0.8, 'retract_lift': 0,
                    'machine_max_acceleration_x': 500, 'machine_max_acceleration_y': 500,
                    'start_gcode': start.replace('{blackbelt_z_offset}', fmt(z_offset)),
                    'end_gcode': end.replace('{blackbelt_z_offset}', fmt(z_offset)),
                    'default_print_profile': print_name(definition, layer, nozzle),
                    'default_filament_profile': GENERIC,
                })
                printers.append((name, p))
                # One process per nozzle, as the variants of BlackBelt Cura (its layer height).
                walls = 4 if nozzle <= 0.4 else 2 if nozzle >= 0.8 else 3
                pr = belt_print_common(note)
                pr['compatible_printers_condition'] = 'printer_notes=~/.*BELT_BLACKBELT_%s_.*/ and nozzle_diameter[0]==%s' % (
                    definition.upper(), fmt(nozzle))
                pr.update({
                    'layer_height': fmt(layer), 'first_layer_height': fmt(layer),
                    'perimeters': walls, 'top_solid_layers': 5, 'bottom_solid_layers': 3,
                    'fill_density': density + '%', 'fill_pattern': 'rectilinear', 'top_fill_pattern': 'concentric',
                    'bottom_fill_pattern': 'concentric',
                    'extrusion_width': fmt(nozzle), 'infill_extrusion_width': fmt(nozzle + 0.2),
                    'first_layer_extrusion_width': fmt(nozzle),
                    'perimeter_speed': 40, 'external_perimeter_speed': 30, 'infill_speed': 30, 'solid_infill_speed': 40,
                    'top_solid_infill_speed': 40, 'gap_fill_speed': 30, 'bridge_speed': 30, 'first_layer_speed': first_layer_speed,
                    'travel_speed': 150,
                })
                prints.append((print_name(definition, layer, nozzle), pr))
    # A process is shared by the angles of a nozzle: one per name.
    merged = {}
    for name, pr in prints:
        merged.setdefault(name, pr)
    sections += [('print:' + n, pr) for n, pr in merged.items()]
    sections += [('printer:' + n, p) for n, p in printers]
    header = ['Belt printers of BLACKBELT 3D B.V. converted from BlackBelt Cura (https://github.com/BlackBelt3D/Cura,',
              'resources/definitions/blackbelt*.def.json and resources/variants), LGPLv3.',
              'Generated by tools/profiles/make_belt_profiles.py from commit %s. Do not edit: run it again.' % commit]
    write_bundle(os.path.join(profiles, 'Belt_BLACKBELT_3D.ini'), header, sections)
    return len(printers)


# ---------------------------------------------------------------------------------------------------------------------
# IdeaFormer IR3 V2 (VertexSlice)
# ---------------------------------------------------------------------------------------------------------------------
ORCA_VARIABLES = {
    'hot_plate_temp_initial_layer': 'first_layer_bed_temperature',
    'nozzle_temperature_initial_layer': 'first_layer_temperature',
    'hot_plate_temp': 'bed_temperature',
    'nozzle_temperature': 'temperature',
}


def orca_gcode(text):
    for orca, tisma in ORCA_VARIABLES.items():
        text = text.replace('[%s]' % orca, '[%s]' % tisma)
    return text


def load_orca(folder, sub, name, seen=None):
    """A profile of the VertexSlice bundle with its parents (inherits) resolved."""
    path = os.path.join(folder, sub, name + '.json')
    d = json.load(open(path, encoding='utf-8'))
    out = {}
    if 'inherits' in d:
        out.update(load_orca(folder, sub, d['inherits']))
    out.update(d)
    return out


def first(v):
    return v[0] if isinstance(v, list) else v


def ideaformer(vs, commit, profiles):
    folder = os.path.join(vs, 'resources', 'profiles', 'IdeaFormer')
    m = load_orca(folder, 'machine', 'IdeaFormer IR3 V2 0.4 nozzle')
    proc = load_orca(folder, 'process', '0.20mm Standard @IdeaFormer IR3 V2')
    note = 'BELT_IDEAFORMER_IR3V2'
    area = [tuple(float(c) for c in pt.split('x')) for pt in m['printable_area']]
    width = max(x for x, _ in area)
    model = 'IR3 V2'
    nozzle = float(first(m['nozzle_diameter']))
    p = belt_printer_common('%s\nBELT_PRINTER' % note, 'klipper', float(m['belt_angle']), width, float(m['printable_height']))
    p.update({
        'printer_model': model, 'printer_variant': fmt(nozzle), 'nozzle_diameter': fmt(nozzle),
        'min_layer_height': 0.08, 'max_layer_height': 0.3,
        'retract_length': first(m['retraction_length']), 'retract_speed': first(m['retraction_speed']),
        'deretract_speed': first(m['deretraction_speed']), 'retract_lift': first(m['z_hop']),
        'machine_max_acceleration_x': first(m['machine_max_acceleration_x']),
        'machine_max_acceleration_y': first(m['machine_max_acceleration_y']),
        'machine_max_acceleration_z': first(m['machine_max_acceleration_z']),
        'machine_max_acceleration_e': first(m['machine_max_acceleration_e']),
        'machine_max_acceleration_extruding': first(m['machine_max_acceleration_extruding']),
        'machine_max_acceleration_retracting': first(m['machine_max_acceleration_retracting']),
        'machine_max_acceleration_travel': first(m['machine_max_acceleration_travel']),
        'machine_max_feedrate_x': first(m['machine_max_speed_x']), 'machine_max_feedrate_y': first(m['machine_max_speed_y']),
        'machine_max_feedrate_z': first(m['machine_max_speed_z']), 'machine_max_feedrate_e': first(m['machine_max_speed_e']),
        'machine_max_jerk_x': first(m['machine_max_jerk_x']), 'machine_max_jerk_y': first(m['machine_max_jerk_y']),
        'machine_max_jerk_z': first(m['machine_max_jerk_z']), 'machine_max_jerk_e': first(m['machine_max_jerk_e']),
        'start_gcode': orca_gcode(m['machine_start_gcode']),
        'end_gcode': orca_gcode(m['machine_end_gcode']),
        'default_print_profile': proc['name'],
        'default_filament_profile': 'Generic PLA @IdeaFormer IR3 V2',
    })
    line = float(proc['line_width'])
    pr = belt_print_common(note)
    pr.update({
        'layer_height': proc['layer_height'], 'first_layer_height': proc['first_layer_height'],
        'perimeters': proc['wall_loops'], 'fill_density': proc['sparse_infill_density'], 'fill_pattern': 'grid',
        'extrusion_width': fmt(line), 'external_perimeter_extrusion_width': proc['outer_wall_line_width'],
        'first_layer_extrusion_width': proc['initial_layer_line_width'],
        'top_infill_extrusion_width': proc['top_surface_line_width'],
        'external_perimeter_speed': proc['outer_wall_speed'], 'perimeter_speed': proc['inner_wall_speed'],
        'infill_speed': proc['sparse_infill_speed'], 'solid_infill_speed': proc['internal_solid_infill_speed'],
        'top_solid_infill_speed': proc['top_surface_speed'], 'gap_fill_speed': proc['gap_infill_speed'],
        'bridge_speed': proc['bridge_speed'], 'first_layer_speed': proc['initial_layer_speed'],
        'travel_speed': proc['travel_speed'], 'default_acceleration': proc['default_acceleration'],
        'elefant_foot_compensation': proc['elefant_foot_compensation'], 'bridge_flow_ratio': proc['bridge_flow'],
    })
    sections = [vendor('IdeaFormer'),
                ('printer_model:' + model, {'name': model, 'variants': fmt(nozzle), 'technology': 'FFF', 'family': 'IR3',
                                            'default_materials': 'Generic PLA @IdeaFormer IR3 V2; Generic PLA High Speed @IdeaFormer IR3 V2'}),
                ('print:' + proc['name'], pr)]
    for fname in ('Generic PLA @IdeaFormer IR3 V2', 'Generic PLA High Speed @IdeaFormer IR3 V2'):
        f = load_orca(folder, 'filament', fname)
        sections.append(('filament:' + fname, {
            'filament_type': first(f['filament_type']), 'filament_vendor': first(f['filament_vendor']),
            'filament_diameter': first(f['filament_diameter']), 'filament_density': first(f['filament_density']),
            'filament_cost': first(f['filament_cost']), 'extrusion_multiplier': first(f.get('filament_flow_ratio', ['1'])),
            'filament_max_volumetric_speed': first(f['filament_max_volumetric_speed']),
            'temperature': first(f['nozzle_temperature']), 'first_layer_temperature': first(f['nozzle_temperature_initial_layer']),
            'bed_temperature': first(f['hot_plate_temp']), 'first_layer_bed_temperature': first(f['hot_plate_temp_initial_layer']),
            'min_fan_speed': first(f['fan_min_speed']), 'max_fan_speed': first(f.get('fan_max_speed', f['fan_min_speed'])),
            'disable_fan_first_layers': first(f['close_fan_the_first_x_layers']),
            'full_fan_speed_layer': first(f['full_fan_speed_layer']),
            'slowdown_below_layer_time': first(f['slow_down_layer_time']), 'min_print_speed': first(f['slow_down_min_speed']),
            'fan_below_layer_time': first(f['fan_cooling_layer_time']),
            'compatible_printers_condition': 'printer_notes=~/.*%s.*/' % note,
        }))
    sections.append(('printer:IdeaFormer IR3 V2 0.4 nozzle', p))
    header = ['IdeaFormer IR3 V2 belt printer converted from VertexSlice (https://github.com/kdho77/VertexSlice,',
              'resources/profiles/IdeaFormer), AGPLv3.',
              'Generated by tools/profiles/make_belt_profiles.py from commit %s. Do not edit: run it again.' % commit]
    write_bundle(os.path.join(profiles, 'Belt_IdeaFormer.ini'), header, sections)
    return 1


# ---------------------------------------------------------------------------------------------------------------------
# Creality CR-30 PrintMill (Creality Print)
# ---------------------------------------------------------------------------------------------------------------------
def orca_process(proc, note):
    """The main settings of a process of an Orca based slicer."""
    pr = belt_print_common(note)
    get = lambda k, d=None: first(proc.get(k, d))
    pr.update({
        'layer_height': get('layer_height'), 'first_layer_height': get('initial_layer_print_height', get('layer_height')),
        'perimeters': get('wall_loops'), 'top_solid_layers': get('top_shell_layers', 4),
        'bottom_solid_layers': get('bottom_shell_layers', 3), 'fill_density': get('sparse_infill_density', '15%'),
        'fill_pattern': 'grid' if get('sparse_infill_pattern', 'grid') in ('grid', 'crosshatch') else 'rectilinear',
        'external_perimeter_speed': get('outer_wall_speed'), 'perimeter_speed': get('inner_wall_speed'),
        'infill_speed': get('sparse_infill_speed'), 'solid_infill_speed': get('internal_solid_infill_speed'),
        'top_solid_infill_speed': get('top_surface_speed'), 'gap_fill_speed': get('gap_infill_speed'),
        'bridge_speed': get('bridge_speed'), 'first_layer_speed': get('initial_layer_speed'),
        'travel_speed': get('travel_speed'), 'default_acceleration': get('default_acceleration'),
        'elefant_foot_compensation': get('elefant_foot_compensation', 0),
    })
    return {k: v for k, v in pr.items() if v is not None}


def orca_filament(f, note):
    get = lambda k, d=None: first(f.get(k, d))
    out = {
        'filament_type': get('filament_type'), 'filament_vendor': get('filament_vendor'),
        'filament_diameter': get('filament_diameter'), 'filament_density': get('filament_density'),
        'filament_cost': get('filament_cost'), 'extrusion_multiplier': get('filament_flow_ratio', '1'),
        'filament_max_volumetric_speed': get('filament_max_volumetric_speed'),
        'temperature': get('nozzle_temperature'), 'first_layer_temperature': get('nozzle_temperature_initial_layer'),
        'bed_temperature': get('hot_plate_temp'), 'first_layer_bed_temperature': get('hot_plate_temp_initial_layer'),
        'min_fan_speed': get('fan_min_speed'), 'max_fan_speed': get('fan_max_speed', get('fan_min_speed')),
        'disable_fan_first_layers': get('close_fan_the_first_x_layers'), 'full_fan_speed_layer': get('full_fan_speed_layer'),
        'slowdown_below_layer_time': get('slow_down_layer_time'), 'min_print_speed': get('slow_down_min_speed'),
        'fan_below_layer_time': get('fan_cooling_layer_time'),
        'compatible_printers_condition': 'printer_notes=~/.*%s.*/' % note,
    }
    return {k: v for k, v in out.items() if v is not None}


def cr30(creality_print, commit, profiles):
    folder = os.path.join(creality_print, 'resources', 'profiles', 'Creality')
    m = load_orca(folder, 'machine', 'Creality CR-30 0.4 nozzle')
    proc = load_orca(folder, 'process', '0.20mm Standard @Creality CR-30 0.4 nozzle')
    fil = load_orca(folder, 'filament', 'Hyper PLA @Creality CR-30 0.4 nozzle')
    note = 'BELT_CREALITY_CR30'
    area = [tuple(float(c) for c in pt.split('x')) for pt in (m['printable_area'].split(',') if isinstance(m['printable_area'], str) else m['printable_area'])]
    width = max(x for x, _ in area)
    nozzle = float(first(m['nozzle_diameter']))
    # The profile sets no angle: the CR-30 has a 45 degree gantry (VertexSlice, "45° inclined-belt machines (CR-30 /
    # IdeaFormer IR3 V2 family)").
    p = belt_printer_common('%s\nBELT_PRINTER' % note, 'marlin', 45., width, float(m['printable_height']))
    p.update({
        'printer_model': 'CR-30 PrintMill', 'printer_variant': fmt(nozzle), 'nozzle_diameter': fmt(nozzle),
        'min_layer_height': first(m['min_layer_height']), 'max_layer_height': first(m['max_layer_height']),
        'retract_length': first(m['retraction_length']), 'retract_speed': first(m['retraction_speed']),
        'deretract_speed': first(m['deretraction_speed']), 'retract_lift': first(m['z_hop']),
        'retract_before_travel': first(m['retraction_minimum_travel']),
        'machine_max_acceleration_x': first(m['machine_max_acceleration_x']),
        'machine_max_acceleration_y': first(m['machine_max_acceleration_y']),
        'machine_max_acceleration_z': first(m['machine_max_acceleration_z']),
        'machine_max_acceleration_e': first(m['machine_max_acceleration_e']),
        'machine_max_acceleration_extruding': first(m['machine_max_acceleration_extruding']),
        'machine_max_acceleration_retracting': first(m['machine_max_acceleration_retracting']),
        'machine_max_acceleration_travel': first(m['machine_max_acceleration_travel']),
        'machine_max_feedrate_x': first(m['machine_max_speed_x']), 'machine_max_feedrate_y': first(m['machine_max_speed_y']),
        'machine_max_feedrate_z': first(m['machine_max_speed_z']), 'machine_max_feedrate_e': first(m['machine_max_speed_e']),
        'machine_max_jerk_x': first(m['machine_max_jerk_x']), 'machine_max_jerk_y': first(m['machine_max_jerk_y']),
        'machine_max_jerk_z': first(m['machine_max_jerk_z']), 'machine_max_jerk_e': first(m['machine_max_jerk_e']),
        'start_gcode': orca_gcode(m['machine_start_gcode']), 'end_gcode': orca_gcode(m['machine_end_gcode']),
        'default_print_profile': proc['name'],
        'default_filament_profile': fil['name'],
    })
    sections = [vendor('Creality (Belt)'),
                ('printer_model:CR-30 PrintMill', {'name': 'CR-30 PrintMill', 'variants': fmt(nozzle), 'technology': 'FFF',
                                                   'family': 'CR-30', 'default_materials': fil['name']}),
                ('print:' + proc['name'], orca_process(proc, note)),
                ('filament:' + fil['name'], orca_filament(fil, note)),
                ('printer:Creality CR-30 PrintMill 0.4 nozzle', p)]
    header = ['Creality CR-30 PrintMill belt printer converted from Creality Print (https://github.com/CrealityOfficial/CrealityPrint,',
              'resources/profiles/Creality), AGPLv3.',
              'Generated by tools/profiles/make_belt_profiles.py from commit %s. Do not edit: run it again.' % commit]
    write_bundle(os.path.join(profiles, 'Belt_Creality.ini'), header, sections)
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('blackbelt_cura')
    ap.add_argument('vertexslice')
    ap.add_argument('creality_print')
    ap.add_argument('profiles')
    ap.add_argument('--blackbelt-commit', default='unknown')
    ap.add_argument('--vertexslice-commit', default='unknown')
    ap.add_argument('--creality-print-commit', default='unknown')
    a = ap.parse_args()
    n = blackbelt(a.blackbelt_cura, a.blackbelt_commit, a.profiles)
    n += ideaformer(a.vertexslice, a.vertexslice_commit, a.profiles)
    n += cr30(a.creality_print, a.creality_print_commit, a.profiles)
    print('%d belt printers' % n)


if __name__ == '__main__':
    main()
