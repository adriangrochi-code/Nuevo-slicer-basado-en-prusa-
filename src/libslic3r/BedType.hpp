///|/ Tisma Slicer: the bed (build plate) types of OrcaSlicer. A printer which supports them lets the user choose the
///|/ plate in the sidebar; a filament gives a bed temperature for each plate. The names of the plates and of the options
///|/ are those of OrcaSlicer (src/libslic3r/PrintConfig.cpp), so that its profiles and projects keep their values.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BedType_hpp_
#define slic3r_BedType_hpp_

#include <string>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;

struct BedType
{
    const char *key;                  // value of curr_bed_type and default_bed_type, as in OrcaSlicer
    const char *label;                // shown to the user (translatable)
    const char *temp_key;             // bed temperature of the filament, other layers
    const char *first_layer_temp_key; // bed temperature of the filament, first layer
};

// The plates in the order of OrcaSlicer (its enum BedType without btDefault).
const std::vector<BedType>& bed_types();
const BedType* find_bed_type(const std::string &key);
// A value of default_bed_type of OrcaSlicer, a name or the index of its enum ("4"), as a key; empty if unknown.
std::string normalize_bed_type(const std::string &value);

// Whether the printer of the configuration lets the user choose the plate.
bool supports_bed_types(const DynamicPrintConfig &config);

// Sets bed_temperature and first_layer_bed_temperature to the temperatures of the plate chosen (curr_bed_type) for the
// filaments which give them; 0 keeps the bed temperatures of the filament. Without a plate chosen, curr_bed_type is set
// to the default_bed_type of the printer. Returns true if a value changed.
bool apply_bed_type(DynamicPrintConfig &config);

} // namespace Slic3r

#endif // slic3r_BedType_hpp_
