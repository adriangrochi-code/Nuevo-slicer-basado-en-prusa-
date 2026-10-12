///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Original implementation of STEP format import created by Bambulab.
// https://github.com/bambulab/BambuStudio
// Forked off commit 1555904, modified by Prusa Research.

#ifndef slic3r_Format_STEP_hpp_
#define slic3r_Format_STEP_hpp_

#include <utility>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r {

class Model;
class TriangleMesh;

//typedef std::function<void(int load_stage, int current, int total, bool& cancel)> ImportStepProgressFn;

// Load a step file into a provided model.
// Inside deflections pair:
// * first value is linear deflection
// * second value is angle deflection
extern bool load_step(const char *path_str, Model *model /*LMBBS:, ImportStepProgressFn proFn = nullptr*/, std::optional<std::pair<double, double>> deflections = std::nullopt);

// Tisma: tessellates one solid of a STEP file (index as in OCCTVolume::solid_index). Returns the mesh and, for every
// triangle, the tag with the index of its B-Rep face (0xFFFF when unknown).
extern bool step_tessellate_solid(const char *path, int solid_index, double linear_deflection, double angular_deflection,
                                  TriangleMesh &mesh_out, std::vector<uint16_t> &face_tags_out, std::string &error);

}; // namespace Slic3r

#endif /* slic3r_Format_STEP_hpp_ */
