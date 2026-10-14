#!/usr/bin/env python3
"""Writes bambu_project.3mf: a small project laid out as Bambu Studio / OrcaSlicer write it, for the tests of the import
of their projects. The layout follows the exporter of OrcaSlicer (src/libslic3r/Format/bbs_3mf.cpp, AGPLv3): the meshes of
the parts in 3D/Objects/*.model, one object with components (one per part, transform = matrix of the part) in
3D/3dmodel.model, one build item per instance, Metadata/model_settings.config (names, extruders, part types and
settings, plates) and Metadata/project_settings.config (JSON settings). The meshes are boxes; nothing is copied from
Orca. Run it from this directory: python3 make_bambu_project.py"""
import json
import zipfile


def box(x0, y0, z0, dx, dy, dz):
    v = [(x0 + a * dx, y0 + b * dy, z0 + c * dz) for a in (0, 1) for b in (0, 1) for c in (0, 1)]
    t = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4),
         (1, 5, 7), (1, 7, 3)]
    return v, t


def mesh_object(oid, mesh, paint=None):
    v, t = mesh
    s = '  <object id="%d" type="model">\n   <mesh>\n    <vertices>\n' % oid
    s += ''.join('     <vertex x="%g" y="%g" z="%g"/>\n' % p for p in v)
    s += '    </vertices>\n    <triangles>\n'
    for i, tri in enumerate(t):
        extra = ' paint_color="%s"' % paint if paint and i < 2 else ''
        s += '     <triangle v1="%d" v2="%d" v3="%d"%s/>\n' % (tri + (extra,))
    s += '    </triangles>\n   </mesh>\n  </object>\n'
    return s


MODEL_HEAD = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" '
              'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" '
              'xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p">\n'
              ' <metadata name="Application">BambuStudio-01.10.02.76</metadata>\n'
              ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n')

# Part meshes: the box of object 1 (20 mm), its modifier (10 mm, moved by the component transform), object 2 (15 mm).
objects_1 = MODEL_HEAD + ' <resources>\n' + mesh_object(1, box(-10, -10, -10, 20, 20, 20), paint='4') + \
    mesh_object(2, box(-5, -5, -5, 10, 10, 10)) + ' </resources>\n <build/>\n</model>\n'
objects_2 = MODEL_HEAD + ' <resources>\n' + mesh_object(3, box(-7.5, -7.5, -7.5, 15, 15, 15)) + \
    ' </resources>\n <build/>\n</model>\n'

# Plates of 256 x 256 mm: 2 plates -> 2 columns, the second plate at x = 256 * 1.2 = 307.2.
main = MODEL_HEAD + (
    ' <resources>\n'
    '  <object id="4" type="model">\n   <components>\n'
    '    <component p:path="/3D/Objects/Box_1.model" objectid="1" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>\n'
    '    <component p:path="/3D/Objects/Box_1.model" objectid="2" transform="1 0 0 0 1 0 0 0 1 3 0 5"/>\n'
    '   </components>\n  </object>\n'
    '  <object id="5" type="model">\n   <components>\n'
    '    <component p:path="/3D/Objects/Small_2.model" objectid="3" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>\n'
    '   </components>\n  </object>\n'
    ' </resources>\n'
    ' <build>\n'
    '  <item objectid="4" transform="1 0 0 0 1 0 0 0 1 100 120 10" printable="1"/>\n'
    '  <item objectid="5" transform="1 0 0 0 1 0 0 0 1 160 120 7.5" printable="1"/>\n'
    '  <item objectid="5" transform="1 0 0 0 1 0 0 0 1 437.2 140 7.5" printable="1"/>\n'
    ' </build>\n</model>\n')

model_settings = '''<?xml version="1.0" encoding="UTF-8"?>
<config>
  <object id="4">
    <metadata key="name" value="Big box"/>
    <metadata key="extruder" value="1"/>
    <metadata key="wall_loops" value="4"/>
    <part id="1" subtype="normal_part">
      <metadata key="name" value="Box body"/>
      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>
    </part>
    <part id="2" subtype="modifier_part">
      <metadata key="name" value="Dense core"/>
      <metadata key="matrix" value="1 0 0 3 0 1 0 0 0 0 1 5 0 0 0 1"/>
      <metadata key="extruder" value="2"/>
      <metadata key="sparse_infill_density" value="60%"/>
    </part>
  </object>
  <object id="5">
    <metadata key="name" value="Small box"/>
    <metadata key="extruder" value="2"/>
    <part id="3" subtype="normal_part">
      <metadata key="name" value="Small box"/>
      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>
    </part>
  </object>
  <plate>
    <metadata key="plater_id" value="1"/>
    <metadata key="plater_name" value="Main"/>
    <metadata key="locked" value="false"/>
    <model_instance>
      <metadata key="object_id" value="4"/>
      <metadata key="instance_id" value="0"/>
    </model_instance>
    <model_instance>
      <metadata key="object_id" value="5"/>
      <metadata key="instance_id" value="0"/>
    </model_instance>
  </plate>
  <plate>
    <metadata key="plater_id" value="2"/>
    <metadata key="plater_name" value="Spare"/>
    <metadata key="locked" value="false"/>
    <model_instance>
      <metadata key="object_id" value="5"/>
      <metadata key="instance_id" value="1"/>
    </model_instance>
  </plate>
</config>
'''

project_settings = {
    'printable_area': ['0x0', '256x0', '256x256', '0x256'],
    'printable_height': '250',
    'nozzle_diameter': ['0.4'],
    'layer_height': '0.16',
    'initial_layer_print_height': '0.2',
    'wall_loops': '3',
    'top_shell_layers': '5',
    'bottom_shell_layers': '4',
    'sparse_infill_density': '25%',
    'sparse_infill_pattern': 'gyroid',
    'top_surface_pattern': 'monotonicline',
    'seam_position': 'back',
    'enable_support': '1',
    'support_type': 'tree(auto)',
    'brim_type': 'no_brim',
    'outer_wall_speed': '120',
    'inner_wall_speed': '200',
    'filament_colour': ['#FF0000', '#00AE42'],
    'filament_type': ['PLA', 'PETG'],
    'nozzle_temperature': ['220', '245'],
    'nozzle_temperature_initial_layer': ['225', '250'],
    'hot_plate_temp': ['55', '70'],
    'hot_plate_temp_initial_layer': ['60', '75'],
    'curr_bed_type': 'High Temp Plate',
    'filament_settings_id': ['Bambu PLA Basic @BBL X1C', 'Bambu PETG HF @BBL X1C'],
    'print_settings_id': '0.16mm Optimal @BBL X1C',
    'printer_settings_id': 'Bambu Lab X1 Carbon 0.4 nozzle',
    'printer_model': 'Bambu Lab X1 Carbon',
    'machine_start_gcode': 'M104 S[nozzle_temperature_initial_layer]',
}

content_types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                 '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
                 ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
                 ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
                 '</Types>\n')
rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
        ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" '
        'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')
model_rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
              '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
              ' <Relationship Target="/3D/Objects/Box_1.model" Id="rel-1" '
              'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
              ' <Relationship Target="/3D/Objects/Small_2.model" Id="rel-2" '
              'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')

with zipfile.ZipFile('bambu_project.3mf', 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml', content_types)
    z.writestr('_rels/.rels', rels)
    z.writestr('3D/3dmodel.model', main)
    z.writestr('3D/_rels/3dmodel.model.rels', model_rels)
    z.writestr('3D/Objects/Box_1.model', objects_1)
    z.writestr('3D/Objects/Small_2.model', objects_2)
    z.writestr('Metadata/model_settings.config', model_settings)
    z.writestr('Metadata/project_settings.config', json.dumps(project_settings, indent=4))
