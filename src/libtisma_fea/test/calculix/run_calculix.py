#!/usr/bin/env python3
# Tisma Slicer: reference results of CalculiX (ccx, http://www.calculix.de, GPL-2) for the validation of the structural
# analysis of libtisma_fea (test "Agrees with CalculiX" in test_fea.cpp). CalculiX is only run here, as an external
# program; nothing of it is linked or distributed with Tisma.
#
# Every case is a part made of 1 mm cubes (the same part the voxel analysis sees with 1 mm voxels), meshed for CalculiX
# with quadratic 20-node hexahedra (C3D20R) on the same cubes, the same transversely isotropic material (PLA of the table
# of Materials.cpp) and the same supports and loads: the nodes of the fixed face held, the force spread as a uniform
# traction over the loaded face (consistent nodal loads). The values compared are those at the centers of 1 mm voxels:
# the mean of the 8 corner nodes of the cube, as the voxel analysis reports them.
#
# Usage: run_calculix.py [output header]   (needs ccx in the PATH; writes calculix_reference.hpp next to this script's
# parent directory by default).
import os, subprocess, sys, tempfile, itertools

# PLA of Materials.cpp: E in the layers, E across them (Z), Poisson ratio.
EXY, EZ, NU = 3000., 2600., 0.35

# Cases: boxes (x0, y0, z0, x1, y1, z1) in mm (integers), fixed face (axis, value), loaded face (axis, value, force),
# measured displacement (component, plane axis, plane value of the voxel centers) and stress (component, then fixed
# coordinates of the voxel centers {axis: value}).
CASES = [
    dict(name="bar_tension", boxes=[(0, 0, 0, 60, 10, 10)], fixed=(0, 0.), load=(0, 60., (1000., 0., 0.)),
         disp=(0, 0, 59.5), stress=(0, {0: 30.5})),
    dict(name="cantilever_x", boxes=[(0, 0, 0, 60, 10, 10)], fixed=(0, 0.), load=(0, 60., (0., 0., -100.)),
         disp=(2, 0, 59.5), stress=(0, {0: 30.5, 2: 9.5})),
    dict(name="cantilever_z", boxes=[(0, 0, 0, 10, 10, 60)], fixed=(2, 0.), load=(2, 60., (50., 0., 0.)),
         disp=(0, 2, 59.5), stress=(2, {2: 30.5, 0: 0.5})),
    dict(name="l_bracket", boxes=[(0, 0, 0, 40, 10, 10), (30, 0, 10, 40, 10, 40)], fixed=(0, 0.),
         load=(2, 40., (50., 0., 0.)), disp=(0, 2, 39.5), stress=(0, {0: 15.5, 2: 9.5})),
]

def cubes(case):
    out = set()
    for (x0, y0, z0, x1, y1, z1) in case["boxes"]:
        for i in range(x0, x1):
            for j in range(y0, y1):
                for k in range(z0, z1):
                    out.add((i, j, k))
    return sorted(out)

# C3D20 nodes of the unit cube in half steps: corners then the midside nodes (CalculiX / Abaqus order).
CORNERS = [(0, 0, 0), (2, 0, 0), (2, 2, 0), (0, 2, 0), (0, 0, 2), (2, 0, 2), (2, 2, 2), (0, 2, 2)]
EDGES = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4), (0, 4), (1, 5), (2, 6), (3, 7)]
LOCAL = CORNERS + [tuple((CORNERS[a][c] + CORNERS[b][c]) // 2 for c in range(3)) for a, b in EDGES]
# Faces of the C3D20 (corner indices, midside indices) for the loads: x-, x+, y-, y+, z-, z+.
FACE_OF = {(0, 0): ([0, 3, 7, 4], [11, 19, 15, 16]), (0, 1): ([1, 2, 6, 5], [9, 18, 13, 17]),
           (1, 0): ([0, 1, 5, 4], [8, 17, 12, 16]), (1, 1): ([3, 2, 6, 7], [10, 18, 14, 19]),
           (2, 0): ([0, 1, 2, 3], [8, 9, 10, 11]), (2, 1): ([4, 5, 6, 7], [12, 13, 14, 15])}

def run_case(case, workdir):
    cs = cubes(case)
    node_id = {}
    def nid(p):
        if p not in node_id:
            node_id[p] = len(node_id) + 1
        return node_id[p]
    elements = []
    for (i, j, k) in cs:
        elements.append([nid((2 * i + a, 2 * j + b, 2 * k + c)) for (a, b, c) in LOCAL])
    pos = {v: k for k, v in node_id.items()}
    fax, fval = case["fixed"]
    fixed = [n for p, n in node_id.items() if abs(p[fax] / 2. - fval) < 1e-9]
    lax, lval, force = case["load"]
    # Consistent loads of a uniform traction on the loaded faces: -1/12 of the face force at the corners, 1/3 at the midside nodes.
    side = 1 if any(abs(c[lax] + 1 - lval) < 1e-9 for c in cs) else 0
    faces = [c for c in cs if abs(c[lax] + side - lval) < 1e-9]
    area = len(faces)
    loads = {}
    for f in faces:
        corners, mids = FACE_OF[(lax, side)]
        el = elements[cs.index(f)]
        for idx, w in [(c, -1. / 12.) for c in corners] + [(m, 1. / 3.) for m in mids]:
            loads[el[idx]] = loads.get(el[idx], 0.) + w / area
    nu13 = NU * (EXY / EZ) ** 0.5
    gxy, gz = EXY / (2. * (1. + NU)), EZ / (2. * (1. + NU))
    deck = ["*NODE"]
    deck += ["%d, %g, %g, %g" % (n, p[0] / 2., p[1] / 2., p[2] / 2.) for p, n in node_id.items()]
    deck.append("*ELEMENT, TYPE=C3D20R, ELSET=PART")
    for e, nodes in enumerate(elements):
        deck.append("%d, " % (e + 1) + ", ".join(str(n) for n in nodes[:15]) + ",")
        deck.append(", ".join(str(n) for n in nodes[15:]))
    deck += ["*NSET, NSET=FIXED"] + ["%d," % n for n in fixed]
    deck += ["*MATERIAL, NAME=PLA", "*ELASTIC, TYPE=ENGINEERING CONSTANTS",
             "%g, %g, %g, %g, %g, %g, %g, %g," % (EXY, EXY, EZ, NU, nu13, nu13, gxy, gz), "%g" % gz,
             "*SOLID SECTION, ELSET=PART, MATERIAL=PLA", "*STEP", "*STATIC", "*BOUNDARY", "FIXED, 1, 3, 0.", "*CLOAD"]
    for n, w in sorted(loads.items()):
        for c in range(3):
            if force[c] != 0.:
                deck.append("%d, %d, %.10g" % (n, c + 1, w * force[c]))
    deck += ["*NODE FILE", "U, S", "*END STEP"]
    base = os.path.join(workdir, case["name"])
    with open(base + ".inp", "w") as f:
        f.write("\n".join(deck) + "\n")
    subprocess.run(["ccx", "-i", base], cwd=workdir, check=True, stdout=subprocess.DEVNULL)
    disp, stress = read_frd(base + ".frd")
    # Values at the centers of the 1 mm cubes: mean of their 8 corners.
    def at_center(field, comp, c):
        return sum(field[node_id[(2 * c[0] + a, 2 * c[1] + b, 2 * c[2] + cc)]][comp] for (a, b, cc) in CORNERS) / 8.
    dcomp, dax, dval = case["disp"]
    sel = [c for c in cs if abs(c[dax] + 0.5 - dval) < 1e-9]
    d = sum(at_center(disp, dcomp, c) for c in sel) / len(sel)
    scomp, fixed_coords = case["stress"]
    sel = [c for c in cs if all(abs(c[a] + 0.5 - v) < 1e-9 for a, v in fixed_coords.items())]
    s = sum(at_center(stress, scomp, c) for c in sel) / len(sel)
    return d, s, len(cs), len(node_id)

def read_frd(path):
    disp, stress, block = {}, {}, None
    with open(path) as f:
        for line in f:
            if line.startswith(" -4"):
                block = line[5:13].strip()
            elif line.startswith(" -1") and block in ("DISP", "STRESS"):
                n = int(line[3:13])
                vals = [float(line[13 + 12 * i:25 + 12 * i]) for i in range(6 if block == "STRESS" else 3)]
                (disp if block == "DISP" else stress)[n] = vals
            elif line.startswith(" -3"):
                block = None
    return disp, stress

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "calculix_reference.hpp")
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for case in CASES:
            d, s, n_el, n_nodes = run_case(case, tmp)
            print("%-14s %6d elements %7d nodes  displacement %.6g mm  stress %.6g MPa" % (case["name"], n_el, n_nodes, d, s))
            rows.append((case["name"], d, s))
    ver = subprocess.run(["ccx", "-v"], capture_output=True, text=True).stdout.strip().splitlines()
    with open(out, "w") as f:
        f.write("// Tisma Slicer: reference results of CalculiX for test_fea.cpp, written by calculix/run_calculix.py (%s).\n"
                % (ver[-1].strip() if ver else "ccx"))
        f.write("// Displacement [mm] and stress [MPa] at the centers of the measured 1 mm voxels (see the script).\n")
        f.write("#pragma once\n\nstruct CalculixReference { const char *name; double displacement; double stress; };\n\n")
        f.write("static const CalculixReference CALCULIX_REFERENCE[] = {\n")
        for name, d, s in rows:
            f.write('    { "%s", %.6g, %.6g },\n' % (name, d, s))
        f.write("};\n")
    print("Written", out)

if __name__ == "__main__":
    main()
