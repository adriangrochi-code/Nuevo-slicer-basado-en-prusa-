///|/ Tisma Slicer: the bed (build plate) types of OrcaSlicer.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BedType.hpp"

#include <cstdlib>

#include "I18N.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

const std::vector<BedType>& bed_types()
{
    static const std::vector<BedType> types {
        { "Cool Plate",          L("Smooth Cool Plate"),      "cool_plate_temp",          "cool_plate_temp_initial_layer" },
        { "Engineering Plate",   L("Engineering Plate"),      "eng_plate_temp",           "eng_plate_temp_initial_layer" },
        { "High Temp Plate",     L("Smooth High Temp Plate"), "hot_plate_temp",           "hot_plate_temp_initial_layer" },
        { "Textured PEI Plate",  L("Textured PEI Plate"),     "textured_plate_temp",      "textured_plate_temp_initial_layer" },
        { "Textured Cool Plate", L("Textured Cool Plate"),    "textured_cool_plate_temp", "textured_cool_plate_temp_initial_layer" },
        { "Supertack Plate",     L("Cool Plate (SuperTack)"), "supertack_plate_temp",     "supertack_plate_temp_initial_layer" },
    };
    return types;
}

const BedType* find_bed_type(const std::string &key)
{
    for (const BedType &type : bed_types())
        if (key == type.key)
            return &type;
    return nullptr;
}

std::string normalize_bed_type(const std::string &value)
{
    if (find_bed_type(value))
        return value;
    if (value == "SuperTack Plate")
        return "Supertack Plate";
    // The index of the enum of OrcaSlicer: 0 is its "Default Plate", then the plates in order.
    char *end = nullptr;
    const long index = std::strtol(value.c_str(), &end, 10);
    if (! value.empty() && *end == '\0' && index >= 1 && index <= long(bed_types().size()))
        return bed_types()[size_t(index - 1)].key;
    return std::string();
}

bool supports_bed_types(const DynamicPrintConfig &config)
{
    const ConfigOptionBool *opt = config.option<ConfigOptionBool>("support_multi_bed_types");
    return opt != nullptr && opt->value;
}

bool apply_bed_type(DynamicPrintConfig &config)
{
    bool changed = false;
    // No plate chosen: the default one of the printer (its custom G-code of OrcaSlicer may test curr_bed_type).
    ConfigOptionString *curr = config.option<ConfigOptionString>("curr_bed_type", true);
    if (! find_bed_type(curr->value))
        if (const ConfigOptionString *def = config.option<ConfigOptionString>("default_bed_type");
            def != nullptr && find_bed_type(def->value) && curr->value != def->value) {
            curr->value = def->value;
            changed = true;
        }
    const BedType *type = find_bed_type(curr->value);
    if (type == nullptr || ! supports_bed_types(config))
        return changed;
    auto apply = [&config, &changed](const char *plate_key, const char *key) {
        const ConfigOptionInts *plate = config.option<ConfigOptionInts>(plate_key);
        ConfigOptionInts       *temps = config.option<ConfigOptionInts>(key);
        if (plate == nullptr || plate->values.empty() || temps == nullptr)
            return;
        for (size_t i = 0; i < temps->values.size(); ++ i)
            if (const int t = plate->get_at(i); t > 0 && temps->values[i] != t) {
                temps->values[i] = t;
                changed = true;
            }
    };
    apply(type->temp_key, "bed_temperature");
    apply(type->first_layer_temp_key, "first_layer_bed_temperature");
    return changed;
}

} // namespace Slic3r
