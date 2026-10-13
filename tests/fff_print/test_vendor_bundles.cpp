// Tisma Slicer: the vendor bundles converted from OrcaSlicer (resources/profiles/Orca_*.ini) and from UltiMaker Cura
// (Cura_*.ini) load in Tisma without substitutions, and their printers, processes and filaments resolve their parents.
#include <catch2/catch_test_macros.hpp>

#include <boost/filesystem.hpp>

#include <libslic3r/PresetBundle.hpp>
#include <libslic3r/Utils.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace Slic3r;
namespace fs = boost::filesystem;

static size_t load_bundles(const std::string &prefix, size_t min_bundles)
{
    const fs::path profiles = fs::path(TEST_DATA_DIR) / ".." / ".." / "resources" / "profiles";
    std::vector<fs::path> bundles;
    for (const fs::directory_entry &entry : fs::directory_iterator(profiles))
        if (entry.path().extension() == ".ini" && entry.path().filename().string().rfind(prefix, 0) == 0)
            bundles.push_back(entry.path());
    REQUIRE(bundles.size() > min_bundles);

    // A data directory for the bundles (nothing is saved there).
    const fs::path data = fs::temp_directory_path() / fs::unique_path("tisma-bundles-%%%%-%%%%");
    fs::create_directories(data);
    set_data_dir(data.string());

    size_t presets = 0;
    for (const fs::path &path : bundles) {
        INFO(path.filename().string());
        PresetBundle bundle;
        size_t loaded = 0;
        // System profiles: an unknown option value throws instead of being substituted.
        CHECK_NOTHROW(loaded = bundle.load_configbundle(path.string(), PresetBundle::LoadConfigBundleAttribute::LoadSystem,
                                                        ForwardCompatibilitySubstitutionRule::Disable).second);
        CHECK(loaded > 0);
        presets += loaded;
    }
    boost::system::error_code ec;
    fs::remove_all(data, ec);
    return presets;
}

TEST_CASE("Vendor bundles converted from OrcaSlicer load", "[VendorBundles]")
{
    CHECK(load_bundles("Orca_", 30) > 5000);
}

TEST_CASE("Vendor bundles converted from UltiMaker Cura load", "[VendorBundles]")
{
    CHECK(load_bundles("Cura_", 100) > 600);
}

// Names of the filaments of a bundle as they are loaded (the filaments of a templates bundle get " @Template").
static std::set<std::string> loaded_filaments(const fs::path &path, PresetBundle &bundle)
{
    bundle.load_configbundle(path.string(), PresetBundle::LoadConfigBundleAttribute::LoadSystem,
                             ForwardCompatibilitySubstitutionRule::Disable);
    std::set<std::string> out;
    for (const Preset &preset : bundle.filaments)
        if (! preset.is_default)
            out.insert(preset.name);
    return out;
}

TEST_CASE("Default filaments of the converted printers exist as loaded", "[VendorBundles]")
{
    // The configuration wizard asks for the default filaments of the printer models again and again (and cannot
    // finish) when none of them exists under its name; the printers start with the filament named by
    // default_filament_profile.
    const fs::path profiles = fs::path(TEST_DATA_DIR) / ".." / ".." / "resources" / "profiles";
    const fs::path data = fs::temp_directory_path() / fs::unique_path("tisma-bundles-%%%%-%%%%");
    fs::create_directories(data);
    set_data_dir(data.string());

    std::set<std::string> library;
    for (const char *name : { "Orca_OrcaFilamentLibrary.ini", "Templates.ini" }) {
        PresetBundle bundle;
        const std::set<std::string> names = loaded_filaments(profiles / name, bundle);
        library.insert(names.begin(), names.end());
    }
    REQUIRE(library.count("Generic PLA @Orca @Template") == 1);

    size_t models = 0, printers = 0;
    for (const fs::directory_entry &entry : fs::directory_iterator(profiles)) {
        const std::string file = entry.path().filename().string();
        if (entry.path().extension() != ".ini" || file == "Orca_OrcaFilamentLibrary.ini" ||
            (file.rfind("Orca_", 0) != 0 && file.rfind("Cura_", 0) != 0))
            continue;
        INFO(file);
        PresetBundle bundle;
        const std::set<std::string> own = loaded_filaments(entry.path(), bundle);
        auto exists = [&](const std::string &name) { return own.count(name) > 0 || library.count(name) > 0; };
        REQUIRE(bundle.vendors.size() == 1);
        for (const VendorProfile::PrinterModel &model : bundle.vendors.begin()->second.models) {
            INFO(model.id);
            // At least one of them, as the wizard checks.
            CHECK((model.default_materials.empty() ||
                   std::any_of(model.default_materials.begin(), model.default_materials.end(), exists)));
            for (const std::string &material : model.default_materials) {
                INFO(material);
                CHECK(exists(material));
            }
            ++ models;
        }
        for (const Preset &printer : bundle.printers)
            if (! printer.is_default && printer.vendor != nullptr)
                if (const auto *opt = printer.config.option<ConfigOptionStrings>("default_filament_profile"))
                    for (const std::string &name : opt->values) {
                        INFO(printer.name << ": " << name);
                        CHECK(exists(name));
                        ++ printers;
                    }
    }
    CHECK(models > 400);
    CHECK(printers > 400);
    boost::system::error_code ec;
    fs::remove_all(data, ec);
}

// The printers have no alias: the plater shows and selects them by name. The printers converted from OrcaSlicer are
// named "<name> @Orca <vendor>"; with an alias "<name>" derived from the '@', selecting one in the plater selected
// another printer (of another technology too) and the program crashed.
TEST_CASE("Printers converted from OrcaSlicer are selected by their name", "[VendorBundles]")
{
    const fs::path profiles = fs::path(TEST_DATA_DIR) / ".." / ".." / "resources" / "profiles";
    const fs::path data = fs::temp_directory_path() / fs::unique_path("tisma-bundles-%%%%-%%%%");
    fs::create_directories(data);
    set_data_dir(data.string());

    PresetBundle bundle;
    bundle.load_configbundle((profiles / "Orca_Prusa.ini").string(), PresetBundle::LoadConfigBundleAttribute::LoadSystem,
                             ForwardCompatibilitySubstitutionRule::Disable);
    size_t with_at = 0;
    for (const Preset &printer : bundle.printers) {
        if (printer.is_default)
            continue;
        INFO(printer.name);
        CHECK(printer.alias == printer.name);
        if (printer.name.find('@') != std::string::npos) {
            ++ with_at;
            CHECK(bundle.get_preset_name_by_alias(Preset::TYPE_PRINTER, printer.alias) == printer.name);
        }
    }
    CHECK(with_at > 0);
    // The prints and filaments keep their aliases.
    const Preset *print = bundle.prints.find_preset("0.05mm DETAIL @CORE One 0.25");
    REQUIRE(print != nullptr);
    CHECK(print->alias == "0.05mm DETAIL");
    boost::system::error_code ec;
    fs::remove_all(data, ec);
}
