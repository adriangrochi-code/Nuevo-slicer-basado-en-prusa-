// Tisma Slicer: the vendor bundles converted from OrcaSlicer (resources/profiles/Orca_*.ini) load in Tisma without
// substitutions, and their printers, processes and filaments resolve their parents.
#include <catch2/catch_test_macros.hpp>

#include <boost/filesystem.hpp>

#include <libslic3r/PresetBundle.hpp>
#include <libslic3r/Utils.hpp>

#include <string>
#include <vector>

using namespace Slic3r;
namespace fs = boost::filesystem;

TEST_CASE("Vendor bundles converted from OrcaSlicer load", "[VendorBundles]")
{
    const fs::path profiles = fs::path(TEST_DATA_DIR) / ".." / ".." / "resources" / "profiles";
    std::vector<fs::path> bundles;
    for (const fs::directory_entry &entry : fs::directory_iterator(profiles))
        if (entry.path().extension() == ".ini" && entry.path().filename().string().rfind("Orca_", 0) == 0)
            bundles.push_back(entry.path());
    REQUIRE(bundles.size() > 30);

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
    CHECK(presets > 5000);
    boost::system::error_code ec;
    fs::remove_all(data, ec);
}
