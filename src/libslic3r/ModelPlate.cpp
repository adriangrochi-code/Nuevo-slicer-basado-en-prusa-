///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "ModelPlate.hpp"

#include "PrintConfig.hpp"

#include <algorithm>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

namespace Slic3r {

namespace pt = boost::property_tree;

void ModelPlate::apply_to(DynamicPrintConfig &config) const
{
    if (print_sequence != Sequence::Default && config.has("complete_objects"))
        config.set_key_value("complete_objects", new ConfigOptionBool(print_sequence == Sequence::ByObject));
    if (spiral_vase != Toggle::Default && config.has("spiral_vase"))
        config.set_key_value("spiral_vase", new ConfigOptionBool(spiral_vase == Toggle::On));
    auto set_temperature = [&config](const char *key, int value) {
        if (value <= 0)
            return;
        if (auto *opt = config.option<ConfigOptionInts>(key)) {
            for (int &v : opt->values)
                v = value;
        }
    };
    set_temperature("first_layer_bed_temperature", first_layer_bed_temperature);
    set_temperature("bed_temperature", bed_temperature);
}

std::string plates_to_xml(const std::vector<ModelPlate> &plates, int number_of_beds)
{
    pt::ptree tree;
    bool any = false;
    for (int i = 0; i < int(plates.size()) && i < number_of_beds; ++i) {
        const ModelPlate &p = plates[i];
        if (p.is_default())
            continue;
        any = true;
        pt::ptree &node = tree.add("plate", "");
        node.put("<xmlattr>.bed_idx", i);
        node.put("<xmlattr>.name", p.name);
        node.put("<xmlattr>.locked", p.locked ? 1 : 0);
        node.put("<xmlattr>.print_sequence", int(p.print_sequence));
        node.put("<xmlattr>.spiral_vase", int(p.spiral_vase));
        node.put("<xmlattr>.first_layer_bed_temperature", p.first_layer_bed_temperature);
        node.put("<xmlattr>.bed_temperature", p.bed_temperature);
    }
    if (!any)
        return {};
    std::ostringstream oss;
    pt::write_xml(oss, tree);
    return oss.str();
}

bool plates_from_xml(const std::string &xml, std::vector<ModelPlate> &plates)
{
    try {
        std::istringstream iss(xml);
        pt::ptree tree;
        pt::read_xml(iss, tree);
        for (const auto &node : tree) {
            if (node.first != "plate")
                continue;
            const pt::ptree &a = node.second;
            const int idx = a.get<int>("<xmlattr>.bed_idx", -1);
            if (idx < 0 || idx >= int(plates.size()))
                continue;
            ModelPlate p;
            p.name = a.get<std::string>("<xmlattr>.name", "");
            p.locked = a.get<int>("<xmlattr>.locked", 0) != 0;
            const int seq = a.get<int>("<xmlattr>.print_sequence", 0);
            p.print_sequence = seq >= 0 && seq <= 2 ? ModelPlate::Sequence(seq) : ModelPlate::Sequence::Default;
            const int vase = a.get<int>("<xmlattr>.spiral_vase", 0);
            p.spiral_vase = vase >= 0 && vase <= 2 ? ModelPlate::Toggle(vase) : ModelPlate::Toggle::Default;
            p.first_layer_bed_temperature = std::max(0, a.get<int>("<xmlattr>.first_layer_bed_temperature", 0));
            p.bed_temperature = std::max(0, a.get<int>("<xmlattr>.bed_temperature", 0));
            plates[idx] = std::move(p);
        }
    } catch (const std::exception &) {
        return false;
    }
    return true;
}

std::string plate_file_suffix(const std::vector<ModelPlate> &plates, int bed_index)
{
    std::string name = bed_index >= 0 && bed_index < int(plates.size()) ? plates[bed_index].name : std::string();
    std::string safe;
    for (char c : name) {
        const bool unsafe = std::string("<>:\"/\\|?*").find(c) != std::string::npos || (unsigned char)c < 32;
        safe += unsafe ? '_' : c;
    }
    // Trailing dots and spaces are not allowed in Windows file names.
    while (!safe.empty() && (safe.back() == '.' || safe.back() == ' '))
        safe.pop_back();
    return safe.empty() ? "_bed" + std::to_string(bed_index + 1) : "_" + safe;
}

} // namespace Slic3r
