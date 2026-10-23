///|/ Tisma Slicer: tisma-fea, the structural analysis of an object of a project (3MF) from the command line, as the
///|/ Engineering panel runs it. Used by the computing servers ("Compute in the cloud"); the result file (.tfea) is
///|/ shown by the GUI.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <boost/nowide/args.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <libslic3r/Format/3mf.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/Utils.hpp>

#include "tisma_fea/Analysis.hpp"
#include "tisma_fea/ModelSetup.hpp"
#include "tisma_fea/Optimize.hpp"
#include "tisma_fea/Quality.hpp"
#include "tisma_fea/Serialize.hpp"

using namespace Slic3r;

namespace {

const char *USAGE =
    "Usage: tisma-fea --input project.3mf --output result.tfea [options]\n"
    "\n"
    "Runs the structural analysis of an object of a Tisma project (3MF) with the supports, loads and print settings\n"
    "saved in it, as the Engineering panel does, and writes the result for the GUI.\n"
    "\n"
    "  --input FILE      project (3MF) with the print configuration and the engineering setup of the object\n"
    "  --output FILE     result file (.tfea)\n"
    "  --summary FILE    short JSON summary (verdict, safety factor, mass...)\n"
    "  --mode MODE       analyze (default), lightest-infill or resin-structure\n"
    "  --object N        index of the object in the project (default 0)\n"
    "  --instance N      index of the instance of the object (default 0)\n"
    "  --quality Q       fast, normal (default), high or ultra\n"
    "  --solid           analyze: the part solid, without the walls and the infill of the print settings\n"
    "  --no-zones        lightest-infill: only the uniform infill\n"
    "  --ribs            lightest-infill: also ribs along the stresses\n"
    "  --no-gpu          run the multigrid on the CPU\n"
    "  --max-voxels N    at most N voxels: a coarser grid when the quality needs more (default: no limit)\n"
    "\n"
    "Prints \"progress N\" (0..100) lines on the standard output. Exit codes: 0 done (see the verdict), 1 bad\n"
    "arguments or project, 2 the analysis failed.\n";

struct Args
{
    std::string input, output, summary;
    std::string mode { "analyze" };
    int         object { 0 };
    int         instance { 0 };
    Fea::Quality quality { Fea::Quality::Normal };
    bool        solid { false };
    bool        zones { true };
    bool        ribs { false };
    bool        gpu { true };
    int         max_voxels { 0 };
};

bool parse_int(const char *s, int &out)
{
    char *end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > 1000000)
        return false;
    out = int(v);
    return true;
}

bool parse_args(int argc, char **argv, Args &args, std::string &error)
{
    for (int i = 1; i < argc; ++ i) {
        const std::string a = argv[i];
        auto value = [&](std::string &out) {
            if (i + 1 >= argc) { error = "Missing value of " + a; return false; }
            out = argv[++ i];
            return true;
        };
        std::string v;
        if (a == "--input")         { if (! value(args.input)) return false; }
        else if (a == "--output")   { if (! value(args.output)) return false; }
        else if (a == "--summary")  { if (! value(args.summary)) return false; }
        else if (a == "--mode")     { if (! value(args.mode)) return false; }
        else if (a == "--object" || a == "--instance") {
            if (! value(v)) return false;
            if (! parse_int(v.c_str(), a == "--object" ? args.object : args.instance)) { error = "Bad value of " + a; return false; }
        }
        else if (a == "--quality") {
            if (! value(v)) return false;
            if (! Fea::parse_quality(v, args.quality)) { error = "Unknown quality " + v; return false; }
        }
        else if (a == "--solid")    args.solid = true;
        else if (a == "--no-zones") args.zones = false;
        else if (a == "--ribs")     args.ribs = true;
        else if (a == "--no-gpu")   args.gpu = false;
        else if (a == "--max-voxels") {
            if (! value(v)) return false;
            if (! parse_int(v.c_str(), args.max_voxels)) { error = "Bad value of " + a; return false; }
        }
        else { error = "Unknown argument " + a; return false; }
    }
    if (args.input.empty() || args.output.empty()) {
        error = "--input and --output are required";
        return false;
    }
    if (args.mode != "analyze" && args.mode != "lightest-infill" && args.mode != "resin-structure") {
        error = "Unknown mode " + args.mode;
        return false;
    }
    return true;
}

// The material of the object as the GUI chooses it (GLGizmoEngineering::filament_type()): the filament type of the
// extruder of its first part, or the name of the resin profile.
std::string filament_type(const ModelObject &object, const DynamicPrintConfig &config)
{
    if (Fea::is_resin(config)) {
        const std::string name = config.has("sla_material_settings_id") ? config.opt_string("sla_material_settings_id") : std::string();
        return name.empty() ? std::string("Resin") : name;
    }
    int extruder = 1;
    for (const ModelVolume *v : object.volumes)
        if (v->is_model_part()) {
            extruder = std::max(1, v->extruder_id());
            break;
        }
    if (const ConfigOptionStrings *types = config.option<ConfigOptionStrings>("filament_type"); types && ! types->values.empty())
        return types->get_at(size_t(extruder - 1));
    return "PLA";
}

void progress(int p)
{
    static int last = -1;
    if (p == last)
        return;
    last = p;
    std::printf("progress %d\n", p);
    std::fflush(stdout);
}

void add_result(boost::property_tree::ptree &tree, const std::string &prefix, const Fea::Result &r)
{
    tree.put(prefix + "ok", r.ok);
    if (! r.ok)
        return;
    tree.put(prefix + "verdict", Fea::verdict_name(r.verdict));
    tree.put(prefix + "safety_factor", r.safety_factor);
    tree.put(prefix + "max_displacement", r.max_displacement);
    tree.put(prefix + "max_von_mises", r.max_von_mises);
    tree.put(prefix + "mass", r.mass);
    tree.put(prefix + "voxels", r.voxels.size());
    tree.put(prefix + "voxel_size", r.h);
    tree.put(prefix + "iterations", r.iterations);
    tree.put(prefix + "gpu_device", r.gpu_device);
}

} // namespace

int main(int argc, char **argv)
{
    boost::nowide::args nowide_args(argc, argv);
    set_logging_level(1);

    Args args;
    std::string error;
    if (argc <= 1 || (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0))) {
        std::cout << USAGE;
        return argc <= 1 ? 1 : 0;
    }
    if (! parse_args(argc, argv, args, error)) {
        std::cerr << "tisma-fea: " << error << "\n\n" << USAGE;
        return 1;
    }

    // The project: its print configuration and the engineering setup of the objects.
    Model model;
    DynamicPrintConfig config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::EnableSilent);
    boost::optional<Semver> generator;
    try {
        if (! load_3mf(args.input.c_str(), config, substitutions, &model, false, generator)) {
            std::cerr << "tisma-fea: cannot read the project " << args.input << "\n";
            return 1;
        }
    } catch (const std::exception &ex) {
        std::cerr << "tisma-fea: cannot read the project " << args.input << ": " << ex.what() << "\n";
        return 1;
    }
    if (args.object >= int(model.objects.size())) {
        std::cerr << "tisma-fea: the project has " << model.objects.size() << " objects\n";
        return 1;
    }
    const ModelObject &object = *model.objects[size_t(args.object)];
    if (args.instance >= int(object.instances.size())) {
        std::cerr << "tisma-fea: the object has " << object.instances.size() << " instances\n";
        return 1;
    }
    // The full print configuration of a project; without it (a bare model) the defaults of the print settings.
    DynamicPrintConfig full = DynamicPrintConfig::full_print_config();
    full.apply(config, true);

    if (args.mode == "resin-structure" && ! Fea::is_resin(full)) {
        std::cerr << "tisma-fea: resin-structure needs a project of a resin printer\n";
        return 1;
    }
    Fea::set_gpu_enabled(args.gpu);
    Fea::ModelAnalysisInput input;
    if (! Fea::build_analysis_input(object, size_t(args.instance), filament_type(object, full), input, error, &full)) {
        std::cerr << "tisma-fea: " << error << "\n";
        return 1;
    }
    bool limited = false;
    input.setup.voxel_size = Fea::quality_voxel_size(args.quality, its_volume(input.mesh), input.line_width,
                                                     double(args.max_voxels), &limited);

    Fea::ResultFile file;
    file.info["material"]       = input.material;
    file.info["quality"]        = Fea::quality_name(args.quality);
    file.info["voxel_size"]     = std::to_string(input.setup.voxel_size);
    file.info["quality_limited"] = limited ? "1" : "0";
    boost::property_tree::ptree summary;
    summary.put("mode", args.mode);
    summary.put("material", input.material);
    summary.put("quality", Fea::quality_name(args.quality));

    bool ok = false;
    progress(0);
    if (args.mode == "analyze") {
        if (args.solid)
            input.setup.infill = Fea::InfillModel();
        file.kind     = Fea::ResultFile::Kind::Analysis;
        file.analysis = Fea::analyze(input.mesh, input.setup, nullptr, progress);
        ok    = file.analysis.ok;
        error = file.analysis.error;
        add_result(summary, "result.", file.analysis);
    } else if (args.mode == "lightest-infill") {
        // The current infill of the object first, the reference of the comparison (as the GUI).
        const Fea::Result current = Fea::analyze(input.mesh, input.setup);
        if (current.ok)
            file.info["current_mass"] = std::to_string(current.mass);
        Fea::OptimizeOptions options;
        options.zones = args.zones;
        options.ribs  = args.ribs;
        file.kind   = Fea::ResultFile::Kind::LightestInfill;
        file.infill = Fea::optimize_infill(input.mesh, input.setup, options, nullptr, progress);
        ok    = file.infill.ok;
        error = file.infill.error;
        summary.put("feasible", file.infill.feasible);
        summary.put("uniform_density", file.infill.uniform_density);
        summary.put("zones", file.infill.zones.size());
        summary.put("analyses", file.infill.analyses);
        if (current.ok)
            summary.put("current_mass", current.mass);
        add_result(summary, "uniform.", file.infill.uniform);
    } else {
        const double min_density = Fea::resin_min_infill(Fea::resin_infill_cell(object, full));
        file.kind  = Fea::ResultFile::Kind::ResinStructure;
        file.resin = Fea::optimize_resin(input.mesh, input.setup, Fea::RESIN_WALLS, min_density, Fea::RESIN_MAX_INFILL,
                                         nullptr, progress);
        ok    = file.resin.ok;
        error = file.resin.error;
        summary.put("best", file.resin.best);
        summary.put("analyses", file.resin.analyses);
        if (file.resin.best >= 0) {
            const Fea::ResinOption &best = file.resin.options[size_t(file.resin.best)];
            summary.put("wall_thickness", best.wall_thickness);
            summary.put("infill_density", best.infill_density);
        }
    }
    progress(100);
    summary.put("ok", ok);
    if (! ok)
        summary.put("error", error);

    if (! Fea::save_result_file(args.output, file)) {
        std::cerr << "tisma-fea: cannot write " << args.output << "\n";
        return 1;
    }
    if (! args.summary.empty()) {
        try {
            boost::nowide::ofstream out(args.summary);
            boost::property_tree::write_json(out, summary);
        } catch (const std::exception &ex) {
            std::cerr << "tisma-fea: cannot write " << args.summary << ": " << ex.what() << "\n";
            return 1;
        }
    }
    if (! ok) {
        std::cerr << "tisma-fea: " << error << "\n";
        return 2;
    }
    return 0;
}
