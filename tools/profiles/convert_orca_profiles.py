#!/usr/bin/env python3
"""Tisma Slicer: converts the printer, process and filament profiles of OrcaSlicer (resources/profiles, AGPLv3, the same
license as Tisma) into vendor bundles of Tisma / PrusaSlicer (resources/profiles/Orca_<vendor>.ini + .idx + images).

Usage: convert_orca_profiles.py <OrcaSlicer>/resources/profiles <Tisma>/resources/profiles [--orca-commit HASH]

- Every Orca vendor becomes a bundle "Orca_<vendor>" (shown as "<vendor>", or "<vendor> (OrcaSlicer)" when Tisma has a
  bundle of that vendor already). The generic filament library of Orca becomes a templates bundle (compatible with all
  the printers, as the "Templates" bundle of PrusaSlicer).
- The inheritance of Orca is kept inside a bundle: a preset stores only the converted values which differ from its
  parent. Parents in other bundles (the filament library) are copied in.
- The option names, values and the placeholders of the custom G-code are translated with the tables below; the options
  of Orca without an equivalent are dropped (Tisma uses its defaults).
- Preset names must be unique among all the bundles: a duplicated name gets the suffix " @Orca <vendor>".

The tables were written by comparing the definitions of both programs (src/libslic3r/PrintConfig.cpp of each one).
"""

import argparse
import glob
import json
import os
import re
import shutil
import sys

# --------------------------------------------------------------------------------------------------------------------
# Option names: Orca -> Tisma. Options with the same name in both programs are copied as they are.
RENAMED = {
    # printer
    'printable_area': 'bed_shape',
    'printable_height': 'max_print_height',
    'machine_start_gcode': 'start_gcode',
    'machine_end_gcode': 'end_gcode',
    'before_layer_change_gcode': 'before_layer_gcode',
    'layer_change_gcode': 'layer_gcode',
    'change_filament_gcode': 'toolchange_gcode',
    'machine_pause_gcode': 'pause_print_gcode',
    'retraction_length': 'retract_length',
    'retraction_speed': 'retract_speed',
    'deretraction_speed': 'deretract_speed',
    'z_hop': 'retract_lift',
    'retraction_minimum_travel': 'retract_before_travel',
    'retract_when_changing_layer': 'retract_layer_change',
    'extruder_clearance_radius': 'extruder_clearance_radius',
    'extruder_clearance_height_to_rod': 'extruder_clearance_height',
    'machine_load_filament_time': 'filament_load_time',
    'machine_unload_filament_time': 'filament_unload_time',
    # print (process)
    'initial_layer_print_height': 'first_layer_height',
    'wall_loops': 'perimeters',
    'top_shell_layers': 'top_solid_layers',
    'bottom_shell_layers': 'bottom_solid_layers',
    'top_shell_thickness': 'top_solid_min_thickness',
    'bottom_shell_thickness': 'bottom_solid_min_thickness',
    'sparse_infill_density': 'fill_density',
    'sparse_infill_pattern': 'fill_pattern',
    'top_surface_pattern': 'top_fill_pattern',
    'bottom_surface_pattern': 'bottom_fill_pattern',
    'infill_direction': 'fill_angle',
    'line_width': 'extrusion_width',
    'outer_wall_line_width': 'external_perimeter_extrusion_width',
    'inner_wall_line_width': 'perimeter_extrusion_width',
    'sparse_infill_line_width': 'infill_extrusion_width',
    'internal_solid_infill_line_width': 'solid_infill_extrusion_width',
    'top_surface_line_width': 'top_infill_extrusion_width',
    'initial_layer_line_width': 'first_layer_extrusion_width',
    'support_line_width': 'support_material_extrusion_width',
    'outer_wall_speed': 'external_perimeter_speed',
    'inner_wall_speed': 'perimeter_speed',
    'sparse_infill_speed': 'infill_speed',
    'internal_solid_infill_speed': 'solid_infill_speed',
    'top_surface_speed': 'top_solid_infill_speed',
    'gap_infill_speed': 'gap_fill_speed',
    'support_speed': 'support_material_speed',
    'support_interface_speed': 'support_material_interface_speed',
    'initial_layer_speed': 'first_layer_speed',
    'initial_layer_infill_speed': 'first_layer_infill_speed',
    'initial_layer_travel_speed': 'first_layer_travel_speed',
    'outer_wall_acceleration': 'external_perimeter_acceleration',
    'inner_wall_acceleration': 'perimeter_acceleration',
    'sparse_infill_acceleration': 'infill_acceleration',
    'internal_solid_infill_acceleration': 'solid_infill_acceleration',
    'top_surface_acceleration': 'top_solid_infill_acceleration',
    'initial_layer_acceleration': 'first_layer_acceleration',
    'skirt_loops': 'skirts',
    'brim_object_gap': 'brim_separation',
    'enable_support': 'support_material',
    'support_threshold_angle': 'support_material_threshold',
    'support_on_build_plate_only': 'support_material_buildplate_only',
    'support_top_z_distance': 'support_material_contact_distance',
    'support_bottom_z_distance': 'support_material_bottom_contact_distance',
    'support_interface_top_layers': 'support_material_interface_layers',
    'support_interface_bottom_layers': 'support_material_bottom_interface_layers',
    'support_base_pattern_spacing': 'support_material_spacing',
    'support_interface_spacing': 'support_material_interface_spacing',
    'support_object_xy_distance': 'support_material_xy_spacing',
    'support_angle': 'support_material_angle',
    'support_filament': 'support_material_extruder',
    'support_interface_filament': 'support_material_interface_extruder',
    'detect_thin_wall': 'thin_walls',
    'detect_overhang_wall': 'overhangs',
    'xy_contour_compensation': 'xy_size_compensation',
    'spiral_mode': 'spiral_vase',
    'enable_prime_tower': 'wipe_tower',
    'prime_tower_width': 'wipe_tower_width',
    'prime_tower_brim_width': 'wipe_tower_brim_width',
    'infill_wall_overlap': 'infill_overlap',
    'bridge_flow': 'bridge_flow_ratio',
    'reduce_crossing_wall': 'avoid_crossing_perimeters',
    'max_travel_detour_distance': 'avoid_crossing_perimeters_max_detour',
    'minimum_sparse_infill_area': 'solid_infill_below_area',
    'filename_format': 'output_filename_format',
    'fuzzy_skin_point_distance': 'fuzzy_skin_point_dist',
    'wall_generator': 'perimeter_generator',
    'only_one_wall_first_layer': 'only_one_perimeter_first_layer',
    'seam_slope_start_height': 'scarf_seam_start_height',
    # filament
    'nozzle_temperature': 'temperature',
    'nozzle_temperature_initial_layer': 'first_layer_temperature',
    'filament_flow_ratio': 'extrusion_multiplier',
    'fan_min_speed': 'min_fan_speed',
    'fan_max_speed': 'max_fan_speed',
    'fan_cooling_layer_time': 'fan_below_layer_time',
    'slow_down_layer_time': 'slowdown_below_layer_time',
    'slow_down_min_speed': 'min_print_speed',
    'close_fan_the_first_x_layers': 'disable_fan_first_layers',
    'reduce_fan_stop_start_freq': 'fan_always_on',
    'slow_down_for_layer_cooling': 'cooling',
    'overhang_fan_speed': 'bridge_fan_speed',
    'filament_start_gcode': 'start_filament_gcode',
    'filament_end_gcode': 'end_filament_gcode',
    'filament_retraction_length': 'filament_retract_length',
    'filament_z_hop': 'filament_retract_lift',
    'filament_retraction_speed': 'filament_retract_speed',
    'filament_deretraction_speed': 'filament_deretract_speed',
    'filament_retraction_minimum_travel': 'filament_retract_before_travel',
    'filament_retract_when_changing_layer': 'filament_retract_layer_change',
    'chamber_temperatures': 'chamber_temperature',
}


# Orca options which are not copied even if Tisma has an option with the same name (different meaning or handled
# below), and the bookkeeping keys of the JSON files.
SKIPPED = {
    'name', 'type', 'from', 'inherits', 'instantiation', 'setting_id', 'filament_id', 'version', 'is_custom_defined',
    'printer_settings_id', 'print_settings_id', 'filament_settings_id', 'printer_model', 'printer_variant',
    'compatible_printers', 'compatible_prints', 'compatible_printers_condition', 'compatible_prints_condition',
    'default_print_profile', 'default_filament_profile', 'upward_compatible_machine', 'thumbnails', 'thumbnails_format',
    'host_type', 'print_host', 'print_host_webui', 'printhost_apikey', 'printhost_cafile', 'printer_technology',
    'bed_custom_texture', 'bed_custom_model', 'inherits_group', 'renamed_from', 'description', '_comment',
    'wall_infill_order', 'brim_type', 'support_type', 'support_style', 'ironing_type', 'print_sequence',
    'exclude_object', 'gcode_label_objects', 'emit_machine_limits_to_gcode', 'only_one_wall_top', 'infill_combination',
    'ensure_vertical_shell_thickness', 'fuzzy_skin', 'seam_position', 'default_bed_type',
}

# Value translations of the enumerations.
INFILL = {'crosshatch': 'grid', 'zig-zag': 'rectilinear', 'tri-hexagon': 'stars', 'monotonicline': 'monotoniclines',
          'lateral-honeycomb': 'honeycomb', 'lateral-lattice': 'grid', 'cross-zag': 'rectilinear',
          'locked-zag': 'rectilinear', '2dhoneycomb': 'honeycomb', '2dlattice': 'grid', 'tpmsd': 'gyroid',
          'tpmsfk': 'gyroid', 'quartercubic': 'cubic'}
SEAM = {'back': 'rear', 'aligned_back': 'aligned'}
PLATE_TEMPS = {  # default_bed_type -> the bed temperature keys of Orca
    'Cool Plate': ('cool_plate_temp', 'cool_plate_temp_initial_layer'),
    'Engineering Plate': ('eng_plate_temp', 'eng_plate_temp_initial_layer'),
    'High Temp Plate': ('hot_plate_temp', 'hot_plate_temp_initial_layer'),
    'Textured PEI Plate': ('textured_plate_temp', 'textured_plate_temp_initial_layer'),
    'Textured Cool Plate': ('textured_cool_plate_temp', 'textured_cool_plate_temp_initial_layer'),
    'Supertack Plate': ('supertack_plate_temp', 'supertack_plate_temp_initial_layer'),
}
PLATE_ORDER = ['hot_plate_temp', 'textured_plate_temp', 'eng_plate_temp', 'cool_plate_temp',
               'textured_cool_plate_temp', 'supertack_plate_temp']

# Placeholders of the custom G-code: Orca -> Tisma.
PLACEHOLDERS = {
    'nozzle_temperature_initial_layer': 'first_layer_temperature',
    'nozzle_temperature': 'temperature',
    'bed_temperature_initial_layer_single': 'first_layer_bed_temperature[initial_extruder]',
    'bed_temperature_initial_layer_vector': 'first_layer_bed_temperature',
    'bed_temperature_initial_layer': 'first_layer_bed_temperature',
    'hot_plate_temp_initial_layer': 'first_layer_bed_temperature',
    'hot_plate_temp': 'bed_temperature',
    'textured_plate_temp_initial_layer': 'first_layer_bed_temperature',
    'textured_plate_temp': 'bed_temperature',
    'cool_plate_temp_initial_layer': 'first_layer_bed_temperature',
    'cool_plate_temp': 'bed_temperature',
    'eng_plate_temp_initial_layer': 'first_layer_bed_temperature',
    'eng_plate_temp': 'bed_temperature',
    'overall_chamber_temperature': 'chamber_temperature[initial_extruder]',
    'chamber_temperatures': 'chamber_temperature',
    'retraction_length': 'retract_length',
    'z_hop': 'retract_lift',
    'printable_height': 'max_print_height',
    'initial_layer_print_height': 'first_layer_height',
    'filament_flow_ratio': 'extrusion_multiplier',
    'outer_wall_volumetric_speed': 'filament_max_volumetric_speed[initial_extruder]',
    'bed_temperature_initial_layer_single_extruder': 'first_layer_bed_temperature[initial_extruder]',
    'spiral_mode': 'spiral_vase',
    'initial_no_support_extruder': 'initial_extruder',
    'adaptive_bed_mesh_min': 'first_layer_print_min',
    'adaptive_bed_mesh_max': 'first_layer_print_max',
    'nozzle_temperature_initial_layer_0': 'first_layer_temperature[0]',
}

# Variables of the placeholder parser of Tisma which are not options (set while exporting the G-code).
SPECIAL_VARIABLES = {
    'default_output_extension', 'e_position', 'e_restart_extra', 'e_retracted', 'extruded_volume', 'extruder',
    'filament_extruder_id', 'initial_extruder', 'initial_filament_type', 'initial_tool', 'input_filename',
    'input_filename_base', 'layer_num', 'layer_z', 'max_layer_z', 'next_extruder', 'normal_print_time',
    'num_extruders', 'num_instances', 'num_objects', 'num_printing_extruders', 'position', 'previous_extruder',
    'print_time', 'printing_extruders', 'printing_filament_types', 'scale', 'silent_print_time', 'toolchange_z',
    'total_cost', 'total_toolchanges', 'total_weight', 'total_wipe_tower_cost', 'total_wipe_tower_filament',
    'used_filament', 'current_extruder', 'current_object_idx', 'version', 'timestamp', 'year', 'month', 'day',
    'hour', 'minute', 'second', 'zhop', 'color_change_extruder', 'has_wipe_tower', 'filament_preset',
    'print_preset', 'printer_preset', 'physical_printer_preset', 'extruded_weight', 'extruded_volume_total',
    'extruded_weight_total', 'total_layer_count', 'is_extruder_used'}
# Words of the macro language.
KEYWORDS = {'if', 'elsif', 'else', 'endif', 'true', 'false', 'and', 'or', 'not', 'min', 'max', 'int', 'round',
            'digits', 'zdigits', 'local', 'global', 'size', 'empty', 'one_of', 'interpolate_table', 'is_nil', 'eq',
            'ne', 'lt', 'gt', 'le', 'ge', 'filament_change'}
# Defaults of Orca options used in custom G-code which the parser of defaults can't read (enumerations).
ORCA_LITERAL_DEFAULTS = {
    'print_sequence': 'by layer', 'timelapse_type': '0', 'bed_mesh_algo': 'bicubic', 'bed_mesh_probe_count': '5,5',
    'bed_mesh_probe_distance': '50,50', 'adaptive_bed_mesh_margin': '0', 'plate_name': 'plate',
}
# Names shown in the configuration wizard for the vendor folders of Orca with a short name.
VENDOR_DISPLAY_NAMES = {'BBL': 'Bambu Lab'}
BED_TYPES = ['Default Plate', 'Cool Plate', 'Engineering Plate', 'High Temp Plate', 'Textured PEI Plate',
             'Textured Cool Plate', 'Supertack Plate']

# Defaults of Orca which differ from Tisma, applied when a profile does not set the option (the profiles rely on them).
ORCA_DEFAULTS = {
    'printer': {'use_relative_e_distances': '1'},
    'print': {},
    'filament': {},
}

TEMPLATE_PERCENT_OVER_NOZZLE = {  # line widths: % of the nozzle in Orca, % of the layer height in Tisma -> mm
    'extrusion_width', 'external_perimeter_extrusion_width', 'perimeter_extrusion_width', 'infill_extrusion_width',
    'solid_infill_extrusion_width', 'top_infill_extrusion_width', 'first_layer_extrusion_width',
    'support_material_extrusion_width'}
ACCELERATIONS = {'external_perimeter_acceleration', 'perimeter_acceleration', 'infill_acceleration',
                 'solid_infill_acceleration', 'top_solid_infill_acceleration', 'first_layer_acceleration',
                 'travel_acceleration', 'bridge_acceleration'}


def normalize_model(vendor, model):
    """Model name without the vendor and the punctuation, to find the same printer in two vendor bundles."""
    v = re.sub(r'[^a-z0-9]', '', vendor.lower())
    m = re.sub(r'[^a-z0-9]', '', model.lower())
    if v and m.startswith(v):
        m = m[len(v):]
    return v + '/' + m


def sanitize(name):
    return re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_')


def as_list(v):
    return v if isinstance(v, list) else [v]


def first(v):
    v = as_list(v)
    return v[0] if v else ''


def escape(s):
    # PrusaSlicer stores strings escaped as in C (new lines as \n).
    return s.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '').replace('"', '\\"') if s else s


def to_float(s, default=0.):
    try:
        return float(str(s).strip().rstrip('%'))
    except ValueError:
        return default


class Converter:
    def __init__(self, tisma_types, orca_defaults=None):
        self.limits = tisma_types.get('limits', {})
        self.kinds = tisma_types.get('kinds', {})
        orca_defaults = orca_defaults or {'values': {}, 'types': {}}
        self.orca_defaults = orca_defaults['values']
        self.orca_types = orca_defaults['types']
        self.unknown = set()
        self.types = tisma_types['types']
        self.enum_values = tisma_types['enum_values']
        self.enum_of = tisma_types['enums']
        self.warnings = []

    # ---- G-code ------------------------------------------------------------------------------------------------
    def known(self, name):
        return name in self.types or name in SPECIAL_VARIABLES or name in KEYWORDS

    def literal(self, name, index, ctx):
        """The value of an Orca option without equivalent, as a literal of the macro language (None = unknown)."""
        if name == 'curr_bed_type':
            bt = str(first(ctx.get('default_bed_type', ''))) or 'Textured PEI Plate'
            if bt.isdigit():
                bt = BED_TYPES[int(bt)] if int(bt) < len(BED_TYPES) else 'Textured PEI Plate'
            return '"%s"' % bt
        m = re.match(r'^(.*)_(\d+)$', name)
        if m and index is None and (m.group(1) in PLACEHOLDERS or m.group(1) in self.types):
            base = PLACEHOLDERS.get(m.group(1), m.group(1)).split('[')[0]
            return '%s[%s]' % (base, m.group(2))
        if name in ctx:
            v = ctx[name]
        elif name in ORCA_LITERAL_DEFAULTS:
            v = ORCA_LITERAL_DEFAULTS[name]
        elif name in self.orca_defaults:
            v = self.orca_defaults[name]
        else:
            return None
        vals = as_list(v)
        if len(vals) == 1 and isinstance(vals[0], str) and ',' in vals[0] and not isinstance(v, list):
            vals = vals[0].split(',')
        x = vals[min(int(index), len(vals) - 1)] if vals and index is not None and str(index).isdigit() else (vals[0] if vals else '')
        x = str(x).strip()
        if self.orca_types.get(name, '') in ('coBool', 'coBools'):
            return 'true' if x.lower() in ('1', 'true') else 'false'
        if x.lower() in ('true', 'false'):
            return x.lower()
        try:
            float(x)
            return x
        except ValueError:
            return '"%s"' % x.replace('"', '')

    def gcode(self, text, ctx=None):
        if not text:
            return text
        ctx = ctx or {}
        names = '|'.join(sorted(map(re.escape, PLACEHOLDERS), key=len, reverse=True))
        # Legacy syntax [name]: no index allowed (a vector gives the value of the current extruder).
        text = re.sub(r'\[(' + names + r')\]', lambda m: '[' + PLACEHOLDERS[m.group(1)].split('[')[0] + ']', text)
        # Expressions {...}: whole identifiers. A mapping with an index is used only when the name is not indexed already.
        def repl(m):
            target = PLACEHOLDERS[m.group(2)]
            if m.group(3) == '[':
                target = target.split('[')[0]
            return m.group(1) + target + m.group(3)
        text = re.sub(r'(^|[^A-Za-z0-9_])(' + names + r')(?![A-Za-z0-9_])(\[?)', repl, text)

        # Options of Orca without equivalent: their value (of the profile, or the default of Orca) as a literal.
        def legacy(m):
            name, index = m.group(1), m.group(3)
            if self.known(name):
                return m.group(0)
            lit = self.literal(name, index, ctx)
            if lit is None:
                self.unknown.add(name)
                return m.group(0)
            if re.match(r'^[a-z_][a-z0-9_]*\[', lit):
                return '{' + lit + '}'  # an indexed variable: only in an expression
            return lit.strip('"')
        text = re.sub(r'\[([a-z_][a-z0-9_]*)(\[(\d+|[a-z_]+)\])?\]', legacy, text)

        def expression(block):
            body = block.group(0)
            out, i = [], 0
            # Skip the string literals.
            for part in re.split(r'("(?:[^"\\]|\\.)*")', body):
                if part.startswith('"'):
                    out.append(part)
                    continue
                def ident(m):
                    name, index = m.group(1), m.group(3)
                    if self.known(name) or m.group(4) == '(':
                        return m.group(0)
                    lit = self.literal(name, index, ctx)
                    if lit is None:
                        self.unknown.add(name)
                        return m.group(0)
                    return lit + (m.group(4) or '')
                out.append(re.sub(r'(?<![A-Za-z0-9_.])([a-z_][a-z0-9_]*)(\[([^\]]*)\])?(\(?)', ident, part))
            return ''.join(out)
        return re.sub(r'\{[^{}]*\}', expression, text)

    # ---- one value -----------------------------------------------------------------------------------------------
    def value(self, key, v, ctx=None):
        """Serializes an Orca value for the Tisma option key (None = not representable)."""
        t = self.types.get(key)
        if t is None:
            return None
        vec = t.endswith('s') and t not in ('coPoints',) and t != 'coString'
        if key.endswith('_gcode') or key in ('start_gcode', 'end_gcode', 'template_custom_gcode'):
            if t == 'coStrings':
                return ';'.join('"%s"' % escape(self.gcode(x, ctx)) for x in as_list(v))
            return escape(self.gcode(first(v), ctx))
        if t == 'coPoints':
            pts = as_list(v)
            if len(pts) == 1 and ',' in pts[0]:
                pts = pts[0].split(',')
            return ','.join(p.strip() for p in pts)
        if t == 'coEnum':
            val = str(first(v))
            if key in ('fill_pattern', 'top_fill_pattern', 'bottom_fill_pattern'):
                val = INFILL.get(val, val)
            allowed = self.enum_values.get(self.enum_of.get(key, ''), None)
            if allowed is not None and val not in allowed:
                return None
            return val
        if t in ('coBool', 'coBools'):
            vals = ['1' if str(x).lower() in ('1', 'true') else '0' for x in as_list(v)]
            return ','.join(vals) if t == 'coBools' else vals[0]
        if t == 'coStrings':
            items = [str(x) for x in as_list(v)]
            if key == 'post_process':
                items = [x for x in items if x.strip()]
            return ';'.join('"%s"' % escape(x) for x in items)
        if t == 'coString':
            return escape(str(first(v)))
        if vec:
            vals = [str(x).strip() for x in as_list(v)]
            if len(vals) == 1 and ',' in vals[0]:
                vals = [x.strip() for x in vals[0].split(',')]
            if any(not self.in_range(key, x) for x in vals):
                return None
            return ','.join(vals)
        val = str(first(v)).strip()
        if t == 'coPercent' and val and not val.endswith('%'):
            val += '%'
        if not self.in_range(key, val):
            return None
        return val

    def in_range(self, key, val):
        lo, hi = self.limits.get(key, (None, None))
        if val in ('nil', '') or (lo is None and hi is None):
            return True
        x = to_float(val, None)
        if x is None:
            return True
        return (lo is None or x >= lo) and (hi is None or x <= hi)

    # ---- one preset (all its values, inheritance resolved) -----------------------------------------------------------
    def convert(self, kind, flat, nozzle):
        flat = dict(ORCA_DEFAULTS.get(kind, {}), **flat)
        out = {}
        for k, v in flat.items():
            if k in SKIPPED:
                continue
            key = RENAMED.get(k, k)
            if key not in self.types:
                continue
            if key in out and k == key:
                continue  # a renamed key wins over an option of Tisma with the same name
            s = self.value(key, v, flat)
            if s is not None:
                out[key] = s
        # Line widths in % of the nozzle -> mm.
        for key in TEMPLATE_PERCENT_OVER_NOZZLE & set(out):
            val = out[key]
            if val.endswith('%'):
                out[key] = '%.3g' % (to_float(val) * 0.01 * nozzle)
        # Accelerations in % of the default acceleration.
        base = to_float(flat.get('default_acceleration', 0))
        for key in ACCELERATIONS & set(out):
            if out[key].endswith('%'):
                out[key] = '%.0f' % (to_float(out[key]) * 0.01 * base)
        if kind == 'print':
            order = str(first(flat.get('wall_infill_order', '')))
            if order:
                out['external_perimeters_first'] = '1' if order.startswith('outer wall') or order.startswith('infill/outer') else '0'
            brim = str(first(flat.get('brim_type', '')))
            if brim == 'no_brim':
                out['brim_width'] = '0'
            elif brim in ('outer_only', 'inner_only', 'outer_and_inner'):
                out['brim_type'] = brim
            elif brim in ('auto_brim', 'brim_ears'):
                out['brim_type'] = 'outer_only'
            st = str(first(flat.get('support_type', '')))
            if st:
                out['support_material_auto'] = '0' if 'manual' in st else '1'
                if st.startswith('tree'):
                    out['support_material_style'] = 'organic'
            style = str(first(flat.get('support_style', '')))
            if style in ('grid', 'snug'):
                out['support_material_style'] = style
            elif style.startswith('tree') or style == 'organic':
                out['support_material_style'] = 'organic'
            it = str(first(flat.get('ironing_type', '')))
            if it:
                out['ironing'] = '0' if it == 'no ironing' else '1'
                if it in ('top', 'topmost', 'solid'):
                    out['ironing_type'] = it
            if out.get('support_material_style') == 'organic':
                width = to_float(out.get('support_material_extrusion_width', '0'))
                if width > 0 and to_float(out.get('support_tree_tip_diameter', '0.8'), 0.8) < width:
                    out['support_tree_tip_diameter'] = '%.3g' % width
                if width > 0 and to_float(out.get('support_tree_branch_diameter', '2'), 2.) < 2. * width:
                    out['support_tree_branch_diameter'] = '%.3g' % (2. * width)
            if str(first(flat.get('print_sequence', ''))) == 'by object':
                out['complete_objects'] = '1'
            if 'only_one_wall_top' in flat:
                out['top_one_perimeter_type'] = 'top' if str(first(flat['only_one_wall_top'])) == '1' else 'none'
            if str(first(flat.get('infill_combination', '0'))) == '1':
                out['infill_every_layers'] = '2'
            if str(first(flat.get('exclude_object', '0'))) == '1':
                out['gcode_label_objects'] = 'firmware'
            elif 'gcode_label_objects' in flat:
                out['gcode_label_objects'] = 'octoprint' if str(first(flat['gcode_label_objects'])) == '1' else 'disabled'
            fz = str(first(flat.get('fuzzy_skin', '')))
            if fz:
                out['fuzzy_skin'] = 'all' if fz.startswith('all') else ('external' if fz in ('external', 'outer') else 'none')
            seam = str(first(flat.get('seam_position', '')))
            if seam:
                out['seam_position'] = SEAM.get(seam, seam) if SEAM.get(seam, seam) in ('random', 'nearest', 'aligned', 'rear') else 'aligned'
        if kind == 'printer':
            flavor = out.get('gcode_flavor', 'marlin')
            relative = out.get('use_relative_e_distances', '0') == '1'
            # Rules of Tisma (PrusaSlicer) which Orca does not enforce.
            reset = re.compile(r'(^|\\n)\s*G92 E0(\.0*)?(?=\s|;|\\n|$)')
            if relative and not reset.search(out.get('layer_gcode', '')) and not reset.search(out.get('before_layer_gcode', '')):
                out['layer_gcode'] = (out.get('layer_gcode', '') + '\\nG92 E0').lstrip('\\n') if out.get('layer_gcode') else 'G92 E0'
            if not relative:
                for k in ('before_layer_gcode', 'layer_gcode'):
                    if k in out:
                        out[k] = re.sub(r'(^|\\n)G92 E0(?=\\n|$| *;)', r'\1', out[k])
            if out.get('use_firmware_retraction') == '1':
                out['wipe'] = ','.join('0' for _ in out.get('wipe', '0').split(','))
            if 'emit_machine_limits_to_gcode' in flat:
                out['machine_limits_usage'] = 'emit_to_gcode' if str(first(flat['emit_machine_limits_to_gcode'])) == '1' else 'time_estimate_only'
            if flavor == 'klipper' and out.get('machine_limits_usage', 'emit_to_gcode') == 'emit_to_gcode':
                out['machine_limits_usage'] = 'time_estimate_only'
            thumbs = flat.get('thumbnails')
            if thumbs not in (None, '', []):
                fmt = str(first(flat.get('thumbnails_format', 'PNG'))) or 'PNG'
                if fmt not in ('PNG', 'JPG', 'QOI'):
                    fmt = 'PNG'
                items = []
                for x in as_list(thumbs):
                    items += [y.strip() for y in str(x).split(',') if y.strip()]
                good = []
                for i in items:
                    parts = i.split('/')
                    f2 = parts[1] if len(parts) > 1 and parts[1] in ('PNG', 'JPG', 'QOI') else fmt
                    if re.match(r'^\d+x\d+$', parts[0]):
                        good.append(parts[0] + '/' + f2)
                if good:
                    out['thumbnails'] = ', '.join(good)
        if kind == 'filament':
            # Shrinkage: Orca gives the size of the printed part (100 % = none), Tisma the shrinkage (0 % = none).
            for ok, tk in (('filament_shrink', 'filament_shrinkage_compensation_xy'),
                           ('filament_shrinkage_compensation_z', 'filament_shrinkage_compensation_z')):
                if ok in flat and tk in self.types:
                    out[tk] = '%.4g%%' % (100. - to_float(first(flat[ok]), 100.))
            # Bed temperature: the one of the plate of the printer (Orca has one per plate type).
            for plate in PLATE_ORDER:
                if plate in flat:
                    out['bed_temperature'] = self.value('bed_temperature', flat[plate])
                    if plate + '_initial_layer' in flat:
                        out['first_layer_bed_temperature'] = self.value('first_layer_bed_temperature', flat[plate + '_initial_layer'])
                    break
            for key in [k for k, v in out.items() if v is None]:
                del out[key]
            # Per filament options: one value (the first one; Orca has one per extruder type for some printers).
            for key in list(out):
                t = self.types.get(key, '')
                if t.endswith('s') and t != 'coStrings' and ',' in out[key]:
                    out[key] = out[key].split(',')[0]
                elif t == 'coStrings' and '";"' in out[key]:
                    out[key] = out[key].split('";"')[0] + '"'
        allowed = self.kinds.get(kind)
        if allowed:
            out = {k: v for k, v in out.items() if k in allowed}
        return out


def tisma_option_types(tisma_root):
    """Option types, enumerations and their values of Tisma, parsed from src/libslic3r/PrintConfig.cpp."""
    src = open(os.path.join(tisma_root, 'src', 'libslic3r', 'PrintConfig.cpp'), encoding='utf-8').read()
    types = {}
    for m in re.finditer(r'this->add(?:_nullable)?\("([a-z0-9_]+)",\s*(co\w+)\)', src):
        types.setdefault(m.group(1), m.group(2))
    enums = {}
    for m in re.finditer(r'this->add\("([a-z0-9_]+)",\s*coEnum\);(.{0,3000}?)(?=this->add\(|\Z)', src, re.S):
        t = re.search(r'set_enum<(\w+)>', m.group(2))
        if t:
            enums[m.group(1)] = t.group(1)
    values = {}
    for m in re.finditer(r'static const t_config_enum_values s_keys_map_(\w+)\s*=?\s*\{(.*?)\};', src, re.S):
        values[m.group(1)] = re.findall(r'\{\s*"([^"]+)"', m.group(2))
    limits = {}
    for m in re.finditer(r'this->add(?:_nullable)?\("([a-z0-9_]+)",\s*co\w+\);(.{0,2500}?)(?=this->add|\Z)', src, re.S):
        lo = re.search(r'def->min\s*=\s*(-?[0-9.]+)', m.group(2))
        hi = re.search(r'def->max\s*=\s*(-?[0-9.]+)', m.group(2))
        if lo or hi:
            limits[m.group(1)] = (float(lo.group(1)) if lo else None, float(hi.group(1)) if hi else None)
    # Options of each preset type (src/libslic3r/Preset.cpp, and the per extruder options of the printers).
    preset_src = open(os.path.join(tisma_root, 'src', 'libslic3r', 'Preset.cpp'), encoding='utf-8').read()
    def keys_of(var):
        m = re.search(r'static std::vector<std::string> ' + var + r'\s*\{(.*?)\};', preset_src, re.S)
        return set(re.findall(r'"([a-z0-9_]+)"', m.group(1))) if m else set()
    m = re.search(r'm_extruder_option_keys = \{(.*?)\};', src, re.S)
    extruder = set(re.findall(r'"([a-z0-9_]+)"', m.group(1))) if m else set()
    kinds = {'print': keys_of('s_Preset_print_options'), 'filament': keys_of('s_Preset_filament_options'),
             'printer': keys_of('s_Preset_printer_options') | keys_of('s_Preset_machine_limits_options') | extruder}
    return {'types': types, 'enums': enums, 'enum_values': values, 'limits': limits, 'kinds': kinds}


def orca_option_defaults(orca_profiles):
    """Types and default values of the options of Orca (src/libslic3r/PrintConfig.cpp next to resources/profiles)."""
    path = os.path.join(orca_profiles, '..', '..', 'src', 'libslic3r', 'PrintConfig.cpp')
    if not os.path.exists(path):
        return {'values': {}, 'types': {}}
    src = open(path, encoding='utf-8', errors='ignore').read()
    values, types = {}, {}
    for m in re.finditer(r'this->add(?:_nullable)?\("([a-z0-9_]+)",\s*(co\w+)\);(.{0,3000}?)(?=this->add|\Z)', src, re.S):
        types[m.group(1)] = m.group(2)
        d = re.search(r'set_default_value\(new ConfigOption\w+\s*[({]\s*\{?\s*([^)}]*)', m.group(3))
        if not d:
            continue
        v = d.group(1).strip()
        sv = re.match(r'^(?:L\()?"([^"]*)"', v)
        if sv:
            values[m.group(1)] = sv.group(1)
            continue
        v = v.split(',')[0].strip()
        if '::' in v:
            continue  # enumeration
        if re.match(r'^-?[0-9.]+f?$', v):
            v = v.rstrip('f')
        values[m.group(1)] = v
    return {'values': values, 'types': types}


def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('orca_profiles')
    ap.add_argument('tisma_profiles')
    ap.add_argument('--tisma-root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'),
                    help='root of the Tisma sources (to read the option types)')
    ap.add_argument('--orca-commit', default='')
    ap.add_argument('--vendors', default='', help='comma separated subset of vendors (for testing)')
    args = ap.parse_args()

    conv = Converter(tisma_option_types(args.tisma_root), orca_option_defaults(args.orca_profiles))
    orca = args.orca_profiles
    out_dir = args.tisma_profiles

    # Existing bundles and preset names of Tisma (not converted from Orca).
    existing_vendors = set()
    existing_models = set()   # normalized "vendor model" of the printers of Tisma
    used_names = set()
    for ini in glob.glob(os.path.join(out_dir, '*.ini')):
        stem = os.path.basename(ini)[:-4]
        if stem.startswith('Orca_'):
            continue
        existing_vendors.add(stem.lower())
        section = ''
        vendor_name = stem
        with open(ini, encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = re.match(r'\[([^\]]+)\]\s*$', line)
                if m:
                    section = m.group(1)
                    m2 = re.match(r'(print|filament|printer):(.+)$', section)
                    if m2:
                        used_names.add((m2.group(1), m2.group(2)))
                    continue
                m = re.match(r'name\s*=\s*(.+)$', line)
                if m and section == 'vendor':
                    existing_vendors.add(m.group(1).strip().lower())
                    vendor_name = m.group(1).strip()
                if m and section.startswith('printer_model:'):
                    existing_models.add(normalize_model(vendor_name, m.group(1)))
                    existing_models.add(normalize_model(stem, m.group(1)))

    # All the presets of Orca, by vendor.
    vendors = {}
    for index_path in sorted(glob.glob(os.path.join(orca, '*.json'))):
        vendor = os.path.basename(index_path)[:-5]
        if vendor in ('blacklist', 'Custom', 'OrcaArena'):
            continue
        if args.vendors and vendor not in args.vendors.split(','):
            if vendor != 'OrcaFilamentLibrary':
                continue
        index = load_json(index_path)
        presets = {}
        for list_key, kind in (('machine_model_list', 'machine_model'), ('machine_list', 'printer'),
                               ('process_list', 'print'), ('filament_list', 'filament')):
            for item in index.get(list_key, []):
                path = os.path.join(orca, vendor, item['sub_path'])
                if not os.path.exists(path):
                    continue
                try:
                    data = load_json(path)
                except Exception as e:
                    conv.warnings.append('%s: %s' % (path, e))
                    continue
                presets[(kind, data.get('name', item['name']))] = data
        vendors[vendor] = (index, presets)

    library = vendors.get('OrcaFilamentLibrary', ({}, {}))[1]

    def lookup(vendor, kind, name):
        p = vendors[vendor][1].get((kind, name))
        if p is not None:
            return vendor, p
        p = library.get((kind, name))
        if p is not None:
            return 'OrcaFilamentLibrary', p
        return None, None

    def flatten(vendor, kind, data, depth=0):
        parent = data.get('inherits', '')
        flat = {}
        if parent and depth < 20:
            if parent == data.get('name'):
                # A preset of the vendor named as its parent in the library.
                pv, pd = 'OrcaFilamentLibrary', library.get((kind, parent))
            else:
                pv, pd = lookup(vendor, kind, parent)
            if pd is not None:
                flat.update(flatten(pv, kind, pd, depth + 1))
            else:
                conv.warnings.append('%s: parent "%s" of "%s" not found' % (vendor, parent, data.get('name')))
        flat.update(data)
        return flat

    total = {'printer': 0, 'print': 0, 'filament': 0, 'models': 0}
    order = sorted(vendors, key=lambda v: (v != 'OrcaFilamentLibrary', v.lower()))
    for vendor in order:
        index, presets = vendors[vendor]
        is_library = vendor == 'OrcaFilamentLibrary'
        vid = 'Orca_' + sanitize(vendor)
        display = VENDOR_DISPLAY_NAMES.get(vendor, vendor)
        shown = 'OrcaSlicer filaments' if is_library else (
            display + ' (OrcaSlicer)' if (vendor.lower() in existing_vendors or sanitize(vendor).lower() in existing_vendors or
                                          display.lower() in existing_vendors) else display)

        # Printer models already in Tisma (from the PrusaSlicer repository): not converted again.
        duplicated_models = set()
        for (kind, name), data in presets.items():
            if kind == 'machine_model' and normalize_model(vendor, name) in existing_models:
                duplicated_models.add(name)
        if duplicated_models:
            for key in [k for k, d in presets.items() if k[0] == 'printer' and
                        str(d.get('instantiation', 'true')).lower() != 'false' and
                        flatten(vendor, 'printer', d).get('printer_model', '') in duplicated_models]:
                del presets[key]
            total['skipped_models'] = total.get('skipped_models', 0) + len(duplicated_models)
            # Processes and filaments only for the removed printers.
            remaining = {n for (k, n) in presets if k == 'printer'}
            for key in [k for k, d in presets.items() if k[0] in ('print', 'filament') and
                        str(d.get('instantiation', 'true')).lower() != 'false']:
                cps = [c for c in as_list(flatten(vendor, key[0], presets[key]).get('compatible_printers', [])) if c]
                if cps and not any(c in remaining for c in cps):
                    del presets[key]

        # Names of the presets in Tisma.
        rename = {}
        def tisma_name(kind, name, data):
            if (kind, name) in rename:
                return rename[(kind, name)]
            abstract = str(data.get('instantiation', 'true')).lower() == 'false'
            if abstract:
                new = '*%s @%s*' % (name, vid)
            else:
                new = name.replace('@System', '@Orca') if is_library else name
                if (kind, new) in used_names:
                    new = '%s @Orca %s' % (name, vendor)
            used_names.add((kind, new))
            rename[(kind, name)] = new
            return new
        for (kind, name), data in presets.items():
            if kind != 'machine_model':
                tisma_name(kind, name, data)

        # Nozzle diameter of each printer preset (for the line widths of the processes).
        nozzle_of = {}
        for (kind, name), data in presets.items():
            if kind == 'printer':
                nozzle_of[name] = to_float(first(flatten(vendor, kind, data).get('nozzle_diameter', ['0.4'])), 0.4)

        lines = ['# Profiles converted from OrcaSlicer (https://github.com/SoftFever/OrcaSlicer, resources/profiles/%s), AGPLv3.' % vendor,
                 '# Generated by tools/profiles/convert_orca_profiles.py%s. Do not edit: run the converter again.' %
                 (' from commit ' + args.orca_commit if args.orca_commit else ''),
                 '', '[vendor]', 'repo_id = non-prusa-fff', 'name = ' + shown, 'config_version = 1.0.0',
                 'config_update_url = ', '']
        if is_library:
            lines.insert(-1, 'templates_profile = 1')

        # Presets: parents before children (same vendor), values which differ from the parent.
        converted = {}
        def get_converted(kind, name, data):
            if (kind, name) in converted:
                return converted[(kind, name)]
            flat = flatten(vendor, kind, data)
            nozzle = 0.4
            if kind == 'print':
                cps = as_list(flat.get('compatible_printers', []))
                nz = [nozzle_of[c] for c in cps if c in nozzle_of]
                if nz:
                    nozzle = nz[0]
            elif kind == 'printer':
                nozzle = to_float(first(flat.get('nozzle_diameter', ['0.4'])), 0.4)
            out = conv.convert(kind, flat, nozzle)
            converted[(kind, name)] = out
            return out

        # Filaments of the vendor identical to a filament of the library (Orca copies the generic filaments for every
        # printer): the printers use the one of the library, which is compatible with all of them.
        if not is_library:
            for (kind, name), data in list(presets.items()):
                if kind != 'filament' or str(data.get('instantiation', 'true')).lower() == 'false':
                    continue
                chain_vendor, chain = vendor, data
                parent = data.get('inherits', '')
                pv, pd = lookup(vendor, 'filament', parent) if parent else (None, None)
                # The nearest instantiable ancestor in the library.
                while pd is not None and pv != 'OrcaFilamentLibrary':
                    parent = pd.get('inherits', '')
                    pv, pd = lookup(pv, 'filament', parent) if parent else (None, None)
                if pd is None or str(pd.get('instantiation', 'true')).lower() == 'false':
                    continue
                mine = get_converted('filament', name, data)
                theirs = conv.convert('filament', flatten('OrcaFilamentLibrary', 'filament', pd), 0.4)
                if mine == theirs:
                    rename[('filament', name)] = parent.replace('@System', '@Orca')
                    del presets[(kind, name)]
                    total['aliased_filaments'] = total.get('aliased_filaments', 0) + 1
        # Identical processes and filaments (Orca repeats them for every printer of a family): one preset with the
        # same base name, compatible with all those printers.
        merged_compat = {}
        for kind in ('print', 'filament'):
            groups = {}
            for (k, name), data in presets.items():
                if k != kind or str(data.get('instantiation', 'true')).lower() == 'false':
                    continue
                values = get_converted(kind, name, data)
                cps = [c for c in as_list(flatten(vendor, kind, data).get('compatible_printers', [])) if c]
                if not cps:
                    continue
                base = name.split(' @')[0].strip()
                groups.setdefault((base, tuple(sorted(values.items()))), []).append((name, cps))
            for (base, _), members in groups.items():
                if len(members) < 2:
                    continue
                keep = members[0][0]
                union = []
                for n, cps in members:
                    union += [c for c in cps if c not in union]
                merged_compat[(kind, keep)] = union
                for n, cps in members[1:]:
                    rename[(kind, n)] = keep  # resolved to the final name of "keep" below
                    del presets[(kind, n)]
                    total['merged_' + kind] = total.get('merged_' + kind, 0) + 1

        # Intermediate presets without a descendant.
        needed = set()
        for (kind, name), data in presets.items():
            if str(data.get('instantiation', 'true')).lower() == 'false' or kind == 'machine_model':
                continue
            parent = data.get('inherits', '') if data.get('inherits', '') != name else ''
            while parent and (kind, parent) in presets and (kind, parent) not in needed:
                needed.add((kind, parent))
                parent = presets[(kind, parent)].get('inherits', '')
        for key in [k for k, d in presets.items() if k[0] != 'machine_model' and
                    str(d.get('instantiation', 'true')).lower() == 'false' and k not in needed]:
            del presets[key]

        # Aliases (merged or replaced presets) point to the final name of the preset kept.
        for key, target in list(rename.items()):
            if key not in presets and (key[0], target) in rename and (key[0], target) in presets:
                rename[key] = rename[(key[0], target)]

        # Printer models.
        vendor_dir = os.path.join(out_dir, vid)
        files_to_copy = set()
        models = [d for (k, n), d in presets.items() if k == 'machine_model']
        model_variants = {}
        for (kind, name), data in presets.items():
            if kind == 'printer' and str(data.get('instantiation', 'true')).lower() != 'false':
                flat = flatten(vendor, kind, data)
                model = flat.get('printer_model', '')
                variant = str(flat.get('printer_variant', '') or first(flat.get('nozzle_diameter', ['0.4'])))
                model_variants.setdefault(model, [])
                if variant not in model_variants[model]:
                    model_variants[model].append(variant)
        model_count = 0
        for m in models:
            mname = m.get('name', '')
            if mname not in model_variants:
                continue
            lines.append('[printer_model:%s]' % mname)
            lines.append('name = ' + mname)
            lines.append('variants = ' + '; '.join(model_variants[mname]))
            lines.append('technology = FFF')
            lines.append('family = ' + (m.get('family') or vendor))
            for key in ('bed_model', 'bed_texture'):
                f = m.get(key, '')
                if f and os.path.exists(os.path.join(orca, vendor, f)):
                    lines.append('%s = %s' % (key, f))
                    files_to_copy.add(f)
            cover = mname + '_cover.png'
            if os.path.exists(os.path.join(orca, vendor, cover)):
                lines.append('thumbnail = ' + cover)
                files_to_copy.add(cover)
            mats = []
            for x in str(m.get('default_materials', '')).split(';'):
                x = x.strip()
                if not x:
                    continue
                v2, d2 = lookup(vendor, 'filament', x)
                if d2 is None and ('filament', x) not in rename:
                    continue
                n2 = rename.get(('filament', x)) if ('filament', x) in rename else (x.replace('@System', '@Orca') if v2 == 'OrcaFilamentLibrary' else None)
                if n2 and n2 not in mats:
                    mats.append(n2)
            if mats:
                lines.append('default_materials = ' + '; '.join(mats))
            lines.append('')
            total['models'] += 1
            model_count += 1

        emitted = set()
        section_lines = []
        def emit(kind, name, data):
            if (kind, name) in emitted:
                return
            parent = data.get('inherits', '')
            parent_in_bundle = parent and parent != name and (kind, parent) in presets
            if parent_in_bundle:
                emit(kind, parent, presets[(kind, parent)])
            emitted.add((kind, name))
            values = get_converted(kind, name, data)
            if parent_in_bundle:
                pvals = get_converted(kind, parent, presets[(kind, parent)])
                own = {k: v for k, v in values.items() if pvals.get(k) != v}
            else:
                own = dict(values)
            section_lines.append('[%s:%s]' % (kind, rename[(kind, name)]))
            if parent_in_bundle:
                section_lines.append('inherits = ' + rename[(kind, parent)])
            elif not own:
                section_lines.append('inherits = ')  # an empty section is dropped by the loader of Tisma
            abstract = rename[(kind, name)].startswith('*')
            if kind == 'printer' and not abstract:
                flat = flatten(vendor, kind, data)
                section_lines.append('printer_model = ' + str(flat.get('printer_model', '')))
                section_lines.append('printer_variant = ' + str(flat.get('printer_variant', '') or first(flat.get('nozzle_diameter', ['0.4']))))
                nozzle = to_float(first(flat.get('nozzle_diameter', ['0.4'])), 0.4)
                def first_layer_ok(print_name):
                    pd = presets.get(('print', print_name))
                    if pd is None:
                        return True
                    return to_float(get_converted('print', print_name, pd).get('first_layer_height', '0.2'), 0.2) <= nozzle + 1e-6
                def usable(print_name):
                    pd = presets.get(('print', print_name))
                    if pd is None:
                        pd = presets.get(('print', next((n for (k, n), t in rename.items() if k == 'print' and n == print_name and False), '')))
                    target = rename.get(('print', print_name), '')
                    return bool(target) and not target.startswith('*') and first_layer_ok(print_name)
                if not usable(str(first(flat.get('default_print_profile', '')))):
                    for (k2, n2), d2 in sorted(presets.items(), key=lambda x: x[0][1]):
                        if k2 == 'print' and str(d2.get('instantiation', 'true')).lower() != 'false' and \
                                name in as_list(flatten(vendor, 'print', d2).get('compatible_printers', [])) and first_layer_ok(n2):
                            flat['default_print_profile'] = n2
                            break
                for key, target_kind in (('default_print_profile', 'print'), ('default_filament_profile', 'filament')):
                    ref = str(first(flat.get(key, '')))
                    if ref:
                        v2, d2 = lookup(vendor, target_kind, ref)
                        if (target_kind, ref) in rename:
                            section_lines.append('%s = %s' % (key, rename[(target_kind, ref)]))
                        elif v2 == 'OrcaFilamentLibrary':
                            section_lines.append('%s = %s' % (key, ref.replace('@System', '@Orca')))
                tag = 'PRINTER_VENDOR_ORCA_%s' % sanitize(vendor).upper()
                notes = values.get('printer_notes', '')
                if tag not in notes:
                    own['printer_notes'] = (notes + '\\n' if notes else '') + tag
            if kind in ('print', 'filament') and not abstract:
                flat = flatten(vendor, kind, data)
                cps = [rename.get(('printer', c)) for c in merged_compat.get((kind, name), as_list(flat.get('compatible_printers', []))) if c]
                cps = [c for c in cps if c and not c.startswith('*')]
                section_lines.append('compatible_printers = ' + ';'.join('"%s"' % escape(c) for c in cps))
                cond = str(flat.get('compatible_printers_condition', '') or '')
                section_lines.append('compatible_printers_condition = ' + (escape(conv.gcode(cond)) if not cps else ''))
                if kind == 'filament':
                    section_lines.append('compatible_prints = ')
                    section_lines.append('compatible_prints_condition = ')
            for k in sorted(own):
                section_lines.append('%s = %s' % (k, own[k]))
            section_lines.append('')
            total[kind] += 0 if abstract else 1

        for kind in ('print', 'filament', 'printer'):
            for (k, name), data in sorted(presets.items(), key=lambda x: x[0][1]):
                if k == kind:
                    emit(kind, name, data)
        if model_count == 0 and not is_library:
            continue
        lines += section_lines

        with open(os.path.join(out_dir, vid + '.ini'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        with open(os.path.join(out_dir, vid + '.idx'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('min_slic3r_version = 2.9.0\n1.0.0 Converted from OrcaSlicer%s.\n' %
                    (' ' + args.orca_commit[:10] if args.orca_commit else ''))
        if files_to_copy:
            os.makedirs(vendor_dir, exist_ok=True)
            for f in files_to_copy:
                dst = os.path.join(vendor_dir, f)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(orca, vendor, f), dst)

    print('printer models: %(models)d, printers: %(printer)d, processes: %(print)d, filaments: %(filament)d' % total +
          ', models already in Tisma: %d, filaments replaced by the library: %d, merged identical processes: %d, filaments: %d' %
          (total.get('skipped_models', 0), total.get('aliased_filaments', 0), total.get('merged_print', 0),
           total.get('merged_filament', 0)))
    if conv.unknown:
        print('placeholders without equivalent: ' + ', '.join(sorted(conv.unknown)), file=sys.stderr)
    for w in conv.warnings[:40]:
        print('warning:', w, file=sys.stderr)
    if len(conv.warnings) > 40:
        print('... %d warnings' % len(conv.warnings), file=sys.stderr)


if __name__ == '__main__':
    main()
