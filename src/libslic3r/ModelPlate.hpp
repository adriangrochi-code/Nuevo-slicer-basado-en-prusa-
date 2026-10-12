///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ModelPlate_hpp_
#define slic3r_ModelPlate_hpp_

#include <string>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;
class Model;

// Per plate (bed) settings, as in OrcaSlicer: a name, a lock that keeps arrange away from the plate,
// and a few print settings that override the presets for the G-code of that plate only.
struct ModelPlate
{
    enum class Sequence { Default, ByLayer, ByObject };
    enum class Toggle { Default, On, Off };

    std::string name;
    bool        locked{ false };
    Sequence    print_sequence{ Sequence::Default };
    Toggle      spiral_vase{ Toggle::Default };
    // Bed temperatures in °C for every extruder, 0 = from the filament presets.
    int         first_layer_bed_temperature{ 0 };
    int         bed_temperature{ 0 };
    // Instances (ObjectID values) that were on the plate when it was locked; only these stay fixed and
    // anything added later is moved off by arrange. Empty = everything on the plate (e.g. a loaded project).
    // Kept for undo / redo, not stored in projects and not compared.
    std::vector<size_t> locked_instances;

    bool has_overrides() const {
        return print_sequence != Sequence::Default || spiral_vase != Toggle::Default ||
               first_layer_bed_temperature > 0 || bed_temperature > 0;
    }
    bool is_default() const { return name.empty() && !locked && !has_overrides(); }

    // Writes the overrides of this plate into a full print config.
    void apply_to(DynamicPrintConfig &config) const;

    bool operator==(const ModelPlate &rhs) const {
        return name == rhs.name && locked == rhs.locked && print_sequence == rhs.print_sequence &&
               spiral_vase == rhs.spiral_vase && first_layer_bed_temperature == rhs.first_layer_bed_temperature &&
               bed_temperature == rhs.bed_temperature;
    }
    bool operator!=(const ModelPlate &rhs) const { return !(*this == rhs); }

    template<class Archive> void serialize(Archive &ar) {
        ar(name, locked, print_sequence, spiral_vase, first_layer_bed_temperature, bed_temperature, locked_instances);
    }
};

// Plate settings as stored in 3MF projects (Metadata/Tisma_plates.xml). Empty when all plates are default.
std::string plates_to_xml(const std::vector<ModelPlate> &plates, int number_of_beds);
// Reads plates_to_xml() output into plates (sized to the maximum number of beds). Returns false on a malformed file.
bool        plates_from_xml(const std::string &xml, std::vector<ModelPlate> &plates);

// File name suffix of the G-code of a plate: "_<name>" with characters unsafe in file names replaced,
// or "_bed<n>" for a plate without a name.
std::string plate_file_suffix(const std::vector<ModelPlate> &plates, int bed_index);

} // namespace Slic3r

#endif // slic3r_ModelPlate_hpp_
