// Tisma Slicer: reference results of CalculiX for test_fea.cpp, written by calculix/run_calculix.py (This is Version 2.21).
// Displacement [mm] and stress [MPa] at the centers of the measured 1 mm voxels (see the script).
#pragma once

struct CalculixReference { const char *name; double displacement; double stress; };

static const CalculixReference CALCULIX_REFERENCE[] = {
    { "bar_tension", 0.196906, 9.9996 },
    { "cantilever_x", -2.87131, 15.9294 },
    { "cantilever_z", 1.65236, 7.9647 },
    { "l_bracket", 1.10208, 9.94309 },
};
