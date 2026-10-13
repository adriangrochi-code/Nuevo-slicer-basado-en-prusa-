///|/ Tisma Slicer: projects of Bambu Studio and OrcaSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Format_BambuProject_hpp_
#define slic3r_Format_BambuProject_hpp_

#include <map>
#include <string>
#include <vector>

namespace Slic3r {

class Model;
class DynamicPrintConfig;
class ModelConfig;

// Bambu Studio and OrcaSlicer (its fork) save their projects as 3MF files with their own layout, known from the 3MF
// exporter of OrcaSlicer (src/libslic3r/Format/bbs_3mf.cpp, AGPLv3):
// - the meshes of the parts in 3D/Objects/*.model, one object per part;
// - in 3D/3dmodel.model one object per model object, with a component per part (its transform is the matrix of the
//   part), and one build item per instance (its transform is the instance);
// - Metadata/model_settings.config: names, extruders, types and settings of the objects and parts, and the plates with
//   their instances;
// - Metadata/project_settings.config: the settings of the project (print, filaments and printer) as JSON;
// - painting in the triangles: paint_color (colors), paint_supports and paint_seam.
// The plates are laid out on a grid of ceil(sqrt(n)) columns, 1.2 plate widths / depths apart, rows towards -Y
// (PartPlateList::compute_origin() of OrcaSlicer).
// Tisma's own reader: nothing of OrcaSlicer's code is used.

// The 3MF is a project of Bambu Studio or OrcaSlicer (and not one of PrusaSlicer / Tisma).
bool is_bambu_project_3mf(const std::string &path);

struct BambuProjectInfo
{
    // Settings of the project that have no equivalent in Tisma (not loaded).
    std::vector<std::string> skipped_settings;
    // Number of plates of the project.
    int plates { 0 };
    // Instances whose plate did not fit in the beds of Tisma (left on the last bed).
    int instances_off_beds { 0 };
};

// Loads a project of Bambu Studio / OrcaSlicer: the objects with their parts (types, names, extruders, settings,
// painting), the instances on their plates (moved to the beds of Tisma when their size is known) and, in config, the
// print and filament settings translated to Tisma. The printer settings of the project are not loaded: the project is
// printed on the printer selected in Tisma. Returns false and sets error when the file cannot be read.
bool load_bambu_project_3mf(const std::string &path, Model &model, DynamicPrintConfig &config, BambuProjectInfo &info,
                            std::string &error);

// Settings of Bambu Studio / OrcaSlicer (key -> values, one per extruder for the vectors) translated to Tisma: the print
// and filament settings when printer is false, all of them when true. The keys that could not be translated go to
// skipped (if given).
void translate_bambu_settings(const std::map<std::string, std::vector<std::string>> &settings, DynamicPrintConfig &out,
                              bool printer, std::vector<std::string> *skipped = nullptr);

// The settings of an object or a part of a Bambu Studio / OrcaSlicer project (model_settings.config) in Tisma.
void translate_bambu_object_settings(const std::map<std::string, std::string> &settings, ModelConfig &out);

} // namespace Slic3r

#endif // slic3r_Format_BambuProject_hpp_
