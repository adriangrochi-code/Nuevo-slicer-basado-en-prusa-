// Tisma: validation of the structural analysis against analytical solutions (phase 5).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>

#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include "tisma_fea/Analysis.hpp"
#include "tisma_fea/Materials.hpp"
#include "tisma_fea/ModelSetup.hpp"
#include "tisma_fea/Optimize.hpp"
#include "tisma_fea/Structures.hpp"
#include "tisma_fea/Orientation.hpp"

#include <libslic3r/Format/3mf.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/Layer.hpp>

#include <boost/filesystem/operations.hpp>

using namespace Slic3r;
using namespace Slic3r::Fea;
using Catch::Approx;

// Triangles of the box whose normal is along the axis with the sign.
static std::vector<int> side(const indexed_triangle_set &its, int axis, bool positive)
{
    std::vector<int> out;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const Vec3f n = its_face_normal(its, int(i));
        if (std::abs(n[axis]) > 0.99f && (n[axis] > 0.f) == positive)
            out.push_back(int(i));
    }
    return out;
}

// Bar clamped at x = 0 and pulled or bent by a force on the face x = length.
static Result bar(double length, double width, double height, const Vec3d &force, const std::string &material,
                  double temperature, double voxel, int axis = 0)
{
    Vec3d dims(width, width, width);
    dims[axis] = length;
    if (axis != 2)
        dims.z() = height;
    const indexed_triangle_set its = its_make_cube(dims.x(), dims.y(), dims.z());
    Setup setup;
    setup.material    = material;
    setup.temperature = temperature;
    setup.voxel_size  = voxel;
    setup.tolerance   = 1e-9;
    setup.fixtures.push_back({ side(its, axis, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, axis, true);
    load.force     = force;
    setup.loads.push_back(load);
    return analyze(its, setup);
}

// Displacement along the axis of the voxels at the loaded end.
static double end_displacement(const Result &res, int axis, int component)
{
    double sum = 0.;
    int    n = 0;
    for (size_t e = 0; e < res.voxels.size(); ++ e) {
        const int idx = res.voxels[e];
        const int ijk[3] = { idx % res.size.x(), (idx / res.size.x()) % res.size.y(), idx / (res.size.x() * res.size.y()) };
        if (ijk[axis] == res.size[axis] - 1) {
            sum += res.displacement[e][component];
            ++ n;
        }
    }
    return n ? sum / n : 0.;
}

TEST_CASE("Material table", "[FEA]")
{
    REQUIRE(find_material("pla") != nullptr);
    CHECK(material_for_filament_type("PETG")->key == "PET");
    CHECK(material_for_filament_type("NYLON")->key == "PA");
    CHECK(material_for_filament_type("PA12-CF")->key == "PA-CF");
    CHECK(material_for_filament_type("SCAFF") == nullptr);
    const Material &pla = *find_material("PLA");
    CHECK(temperature_factor(pla, 20.) == Approx(1.));
    CHECK(temperature_factor(pla, pla.max_service_temperature) == Approx(0.5));
    CHECK(temperature_factor(pla, 130.) == Approx(0.02));
    CHECK(out_of_temperature(pla, 130.));
    CHECK(! out_of_temperature(*find_material("PEI"), 130.));
    for (const Material &m : materials()) {
        CHECK(m.E_xy > 0.);
        CHECK(m.E_z > 0.);
        CHECK(m.E_z <= m.E_xy);
        CHECK(m.strength_z <= m.strength_xy);
    }
}

TEST_CASE("Bar in tension", "[FEA]")
{
    // delta = F L / (E A): 1000 N on 10 x 10 mm, 50 mm long, PLA E_xy = 3000 MPa -> 0.1667 mm; stress 10 MPa.
    const Result res = bar(50., 10., 10., Vec3d(1000., 0., 0.), "PLA", 23., 1.);
    REQUIRE(res.ok);
    INFO("iterations " << res.iterations);
    const double expected = 1000. * 50. / (3000. * 100.);
    // The clamped end stops the lateral contraction, which stiffens the bar a little.
    CHECK(end_displacement(res, 0, 0) == Approx(expected).epsilon(0.05));
    // Stress far from the ends.
    double vm_mid = 0.;
    int    n = 0;
    for (size_t e = 0; e < res.voxels.size(); ++ e)
        if (res.voxels[e] % res.size.x() == res.size.x() / 2) {
            vm_mid += res.von_mises[e];
            ++ n;
        }
    CHECK(vm_mid / n == Approx(10.).epsilon(0.03));
    CHECK(res.verdict == Verdict::Holds);
}

TEST_CASE("Cantilever converges to the beam theory", "[FEA]")
{
    // delta = F L^3 / (3 E I) + shear: 10 N at the end of a 100 x 10 x 10 mm PLA beam -> 1.333 mm (bending)
    // + F L / (k G A) = 0.011 mm (shear, k = 5/6).
    const double L = 100., b = 10., F = 10., E = 3000., G = E / (2. * 1.35);
    const double I = b * b * b * b / 12.;
    const double expected = F * L * L * L / (3. * E * I) + F * L / (5. / 6. * G * b * b);
    const Result coarse = bar(L, b, b, Vec3d(0., 0., -F), "PLA", 23., 2.5, 0);
    const Result fine   = bar(L, b, b, Vec3d(0., 0., -F), "PLA", 23., 1.25, 0);
    REQUIRE(coarse.ok);
    REQUIRE(fine.ok);
    const double d_coarse = - end_displacement(coarse, 0, 2);
    const double d_fine   = - end_displacement(fine, 0, 2);
    INFO("expected " << expected << " coarse " << d_coarse << " fine " << d_fine);
    // Linear hexahedra are too stiff in bending; the error must shrink with the voxel size.
    CHECK(d_fine > d_coarse);
    CHECK(std::abs(d_fine - expected) < std::abs(d_coarse - expected));
    CHECK(d_fine == Approx(expected).epsilon(0.10));
}

TEST_CASE("Working temperature softens the material", "[FEA]")
{
    const Result cold = bar(40., 10., 10., Vec3d(500., 0., 0.), "PET", 23., 2.);
    const Result warm = bar(40., 10., 10., Vec3d(500., 0., 0.), "PET", 50., 2.);
    REQUIRE(cold.ok);
    REQUIRE(warm.ok);
    const Material &pet = *find_material("PET");
    CHECK(end_displacement(warm, 0, 0) / end_displacement(cold, 0, 0) ==
          Approx(temperature_factor(pet, 23.) / temperature_factor(pet, 50.)).epsilon(1e-3));
}

TEST_CASE("Layers are softer across than along", "[FEA]")
{
    // The same bar along X and along Z: the displacement ratio is E_xy / E_z.
    const Result along  = bar(40., 10., 10., Vec3d(500., 0., 0.), "PA-CF", 23., 2., 0);
    const Result across = bar(40., 10., 10., Vec3d(0., 0., 500.), "PA-CF", 23., 2., 2);
    REQUIRE(along.ok);
    REQUIRE(across.ok);
    const Material &m = *find_material("PA-CF");
    CHECK(end_displacement(across, 2, 2) / end_displacement(along, 0, 0) == Approx(m.E_xy / m.E_z).epsilon(0.05));
}

TEST_CASE("Verdicts", "[FEA]")
{
    // 130 °C: PLA is out of temperature and materials for high temperature are proposed.
    const Result hot = bar(40., 10., 10., Vec3d(500., 0., 0.), "PLA", 130., 2.);
    REQUIRE(hot.ok);
    CHECK(hot.verdict == Verdict::OutOfTemperature);
    bool pei = false;
    for (const auto &[key, sf] : hot.alternatives) {
        CHECK(! out_of_temperature(*find_material(key), 130.));
        pei |= key == "PEI";
    }
    CHECK(pei);

    // 6000 N on 100 mm² is 60 MPa: more than the strength of PLA.
    const Result broken = bar(40., 10., 10., Vec3d(6000., 0., 0.), "PLA", 23., 2.);
    REQUIRE(broken.ok);
    CHECK(broken.verdict == Verdict::OutOfLoad);
    CHECK(broken.safety_factor < 1.);

    // Pulling across the layers is limited by the layer adhesion.
    const Result layers = bar(40., 10., 10., Vec3d(0., 0., 3500.), "PLA", 23., 2., 2);
    REQUIRE(layers.ok);
    CHECK(layers.verdict == Verdict::OutOfLoad);

    const Result tpu = bar(40., 10., 10., Vec3d(10., 0., 0.), "FLEX", 23., 2.);
    REQUIRE(tpu.ok);
    CHECK(tpu.verdict == Verdict::NotApplicable);
}

TEST_CASE("Errors of the setup", "[FEA]")
{
    const indexed_triangle_set its = its_make_cube(10., 10., 10.);
    Setup setup;
    Load load;
    load.point = Vec3d(10., 5., 5.);
    load.force = Vec3d(0., 0., -10.);
    setup.loads.push_back(load);
    CHECK(! analyze(its, setup).ok);         // no fixtures
    setup.fixtures.push_back({ side(its, 0, false) });
    setup.material = "UNOBTAINIUM";
    CHECK(! analyze(its, setup).ok);
    setup.material = "PLA";
    setup.voxel_size = 1.;
    const Result res = analyze(its, setup);
    REQUIRE(res.ok);
    CHECK(res.voxels.size() == 1000);
    int calls = 0;
    const Result stopped = analyze(its, setup, [&]() { return ++ calls > 1; });
    CHECK(! stopped.ok);
}

TEST_CASE("Size of a real analysis", "[.][FEA_benchmark]")
{
    // About 60000 voxels, as the default of the GUI.
    const indexed_triangle_set its = its_make_cube(120., 30., 25.);
    Setup setup;
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.point = Vec3d(120., 15., 25.);
    load.force = Vec3d(0., 0., -50.);
    setup.loads.push_back(load);
    const auto t0 = std::chrono::steady_clock::now();
    const Result res = analyze(its, setup);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    REQUIRE(res.ok);
    WARN("voxels " << res.voxels.size() << " h " << res.h << " iterations " << res.iterations << " time " << seconds << " s"
         << " max displacement " << res.max_displacement << " safety " << res.safety_factor);
}

// Object with a 60 x 10 x 10 mm bar, held at x = 0, with a point load at the other end.
static Model bar_model()
{
    Model model;
    ModelObject *object = model.add_object();
    object->name = "bar";
    ModelVolume *volume = object->add_volume(TriangleMesh(its_make_cube(60., 10., 10.)));
    volume->name = "bar";
    object->add_instance();
    EngineeringRegion fixed;
    fixed.volume    = 0;
    fixed.triangles = side(volume->mesh().its, 0, false);
    object->engineering.fixtures.push_back(fixed);
    EngineeringLoad load;
    load.name   = "end";
    load.volume = 0;
    // The mesh of the volume is centered when it is added: the end is at x = 30 and the top at z = 5.
    load.point  = Vec3d(30., 0., 5.);
    load.force  = Vec3d(0., 0., -20.);
    object->engineering.loads.push_back(load);
    object->engineering.temperature = 40.;
    return model;
}

TEST_CASE("Engineering setup is kept in the 3MF", "[FEA]")
{
    Model model = bar_model();
    model.objects.front()->engineering.material = "PET";
    const std::string path = (boost::filesystem::temp_directory_path() / "tisma_engineering_test.3mf").string();
    REQUIRE(store_3mf(path.c_str(), &model, nullptr, false));
    Model                     loaded;
    DynamicPrintConfig        config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    boost::optional<Semver>   version;
    REQUIRE(load_3mf(path.c_str(), config, ctxt, &loaded, false, version));
    boost::filesystem::remove(path);
    REQUIRE(loaded.objects.size() == 1);
    const EngineeringSetup &a = model.objects.front()->engineering;
    const EngineeringSetup &b = loaded.objects.front()->engineering;
    CHECK(b.material == "PET");
    CHECK(b.temperature == Approx(40.));
    CHECK(b.fixtures == a.fixtures);
    REQUIRE(b.loads.size() == 1);
    CHECK(b.loads.front().name == "end");
    CHECK((b.loads.front().point - a.loads.front().point).norm() < 1e-6);
    CHECK((b.loads.front().force - a.loads.front().force).norm() < 1e-6);
}

TEST_CASE("Analysis of an object of the model", "[FEA]")
{
    Model model = bar_model();
    Fea::ModelAnalysisInput input;
    std::string error;
    // The material comes from the filament type.
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PETG", input, error));
    CHECK(input.material == "PET");
    input.setup.voxel_size = 2.;
    const Result flat = analyze(input.mesh, input.setup);
    INFO(flat.error);
    REQUIRE(flat.ok);
    CHECK(flat.max_displacement > 0.);

    // Standing the bar up (rotated 90 degrees about Y) bends it across the layers... no: the bending is now in the
    // plane of the layers, where the material is the same, but the load stays vertical (print coordinates), so it
    // pulls along the bar: much smaller displacement.
    model.objects.front()->instances.front()->set_rotation(Vec3d(0., -0.5 * M_PI, 0.));
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PETG", input, error));
    input.setup.voxel_size = 2.;
    const Result standing = analyze(input.mesh, input.setup);
    REQUIRE(standing.ok);
    CHECK(standing.max_displacement < 0.2 * flat.max_displacement);

    // Unknown filament types need a chosen material.
    CHECK(! build_analysis_input(*model.objects.front(), 0, "SCAFF", input, error));
    CHECK(! error.empty());
    // A region on a volume which does not exist.
    model.objects.front()->engineering.fixtures.front().volume = 3;
    CHECK(! build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
}

TEST_CASE("Deformation limit", "[FEA]")
{
    Model model = bar_model();
    model.objects.front()->engineering.temperature = 23.;
    Fea::ModelAnalysisInput input;
    std::string error;
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
    input.setup.voxel_size = 2.;
    const Result free = analyze(input.mesh, input.setup);
    REQUIRE(free.ok);
    REQUIRE(free.load_displacement.size() == 1);
    const double d = free.load_displacement.front();
    CHECK(d > 0.);
    CHECK(free.verdict == Verdict::Holds);

    // A limit above the displacement is met.
    model.objects.front()->engineering.loads.front().max_displacement = 2. * d;
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
    input.setup.voxel_size = 2.;
    CHECK(analyze(input.mesh, input.setup).verdict == Verdict::Holds);

    // Half the displacement, given as % of the length of the bar (60 mm): too flexible; only stiffer materials
    // are proposed.
    model.objects.front()->engineering.loads.front().max_displacement = 0.;
    model.objects.front()->engineering.loads.front().max_displacement_percent = 100. * 0.5 * d / 60.;
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
    CHECK(input.setup.loads.front().max_displacement == Approx(0.5 * d));
    input.setup.voxel_size = 2.;
    const Result stiff = analyze(input.mesh, input.setup);
    REQUIRE(stiff.ok);
    CHECK(stiff.verdict == Verdict::TooFlexible);
    REQUIRE(! stiff.alternatives.empty());
    for (const auto &[key, sf] : stiff.alternatives)
        CHECK(find_material(key)->E_xy >= 2. * find_material("PLA")->E_xy);
}

// Bar 80 x 10 x 10 mm clamped at x = 0 with a load on the end face, printed with walls and infill.
static Setup printed_cantilever(indexed_triangle_set &its, double force, double max_displacement)
{
    its = its_make_cube(80., 10., 10.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 1.;
    setup.tolerance  = 1e-8;
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.type             = Load::Type::Faces;
    load.triangles        = side(its, 0, true);
    load.force            = Vec3d(0., 0., -force);
    load.max_displacement = max_displacement;
    setup.loads.push_back(load);
    setup.infill.enabled              = true;
    setup.infill.wall_thickness       = 0.9;
    setup.infill.top_bottom_thickness = 0.8;
    setup.infill.density              = 0.2;
    std::tie(setup.infill.stiffness_exponent, setup.infill.strength_exponent) = infill_exponents("gyroid");
    return setup;
}

TEST_CASE("Walls and infill", "[FEA]")
{
    indexed_triangle_set its;
    Setup setup = printed_cantilever(its, 5., 0.);
    const Result r20 = analyze(its, setup);
    setup.infill.density = 0.6;
    const Result r60 = analyze(its, setup);
    setup.infill.enabled = false;
    const Result solid = analyze(its, setup);
    REQUIRE(r20.ok);
    REQUIRE(r60.ok);
    REQUIRE(solid.ok);
    INFO("mass 20% " << r20.mass << " 60% " << r60.mass << " solid " << solid.mass << " (" << solid.solid_mass << ")");
    // 0.9 mm walls on a 10 x 10 section are about 1/3 of it.
    CHECK(r20.mass > 0.35 * solid.mass);
    CHECK(r20.mass < 0.60 * solid.mass);
    CHECK(r20.mass < r60.mass);
    CHECK(r60.mass < solid.mass);
    // 80 mm x 100 mm² of PLA (1.24 g/cm³) = 9.92 g.
    CHECK(solid.mass == Approx(9.92).epsilon(0.02));
    // Less infill, more flexible and weaker.
    CHECK(r20.max_displacement > r60.max_displacement);
    CHECK(r60.max_displacement > solid.max_displacement);
    CHECK(r20.safety_factor < r60.safety_factor);
    // The shell carries most of the bending: the infill changes the stiffness much less than its density.
    CHECK(r20.max_displacement < 3. * solid.max_displacement);
    CHECK(infill_exponents("lightning").first > infill_exponents("rectilinear").first);
}

TEST_CASE("Lightest uniform infill", "[FEA]")
{
    indexed_triangle_set its;
    Setup setup = printed_cantilever(its, 5., 0.);
    // The limit is the displacement with 40 % infill: the answer must be close to 40 %.
    setup.infill.density = 0.4;
    const Result at40 = analyze(its, setup);
    REQUIRE(at40.ok);
    setup.loads.front().max_displacement = at40.load_displacement.front() * 1.0001;
    OptimizeOptions options;
    options.zones = false;
    const OptimizeResult opt = optimize_infill(its, setup, options);
    REQUIRE(opt.ok);
    REQUIRE(opt.feasible);
    INFO("density " << opt.uniform_density << " analyses " << opt.analyses);
    CHECK(opt.uniform_density == Approx(0.40).margin(0.02));
    CHECK(meets_requirements(opt.uniform));
    // 2 % less does not meet the limit.
    setup.infill.density = opt.uniform_density - 0.02;
    CHECK(! meets_requirements(analyze(its, setup)));

    // A limit that not even the solid part meets.
    setup.loads.front().max_displacement = 1e-4;
    const OptimizeResult impossible = optimize_infill(its, setup, options);
    REQUIRE(impossible.ok);
    CHECK(! impossible.feasible);
    CHECK(impossible.uniform.verdict == Verdict::TooFlexible);
}

TEST_CASE("Infill by zones follows the stresses", "[FEA]")
{
    indexed_triangle_set its;
    // Strength driven: high load, no deformation limit. The bending moment is highest at the clamped end.
    Setup setup = printed_cantilever(its, 40., 0.);
    setup.required_safety_factor = 1.5;
    const OptimizeResult opt = optimize_infill(its, setup);
    REQUIRE(opt.ok);
    REQUIRE(opt.feasible);
    INFO("uniform " << opt.uniform_density << " mass " << opt.uniform.mass << " zones " << opt.zones.size()
         << " base " << opt.base_density << " mass " << opt.zoned.mass << " analyses " << opt.analyses);
    REQUIRE(opt.zones_found);
    CHECK(meets_requirements(opt.zoned));
    CHECK(opt.zoned.mass < opt.uniform.mass);
    CHECK(opt.base_density < opt.uniform_density);
    // The densest zone is near the clamped end (x = 0).
    const InfillZone &densest = opt.zones.back();
    BoundingBoxf3 bb;
    for (const Vec3f &v : densest.mesh.vertices)
        bb.merge(v.cast<double>());
    CHECK(bb.min.x() < 10.);
    CHECK(bb.max.x() < 60.);
}

TEST_CASE("Infill of an object from its print settings, applied back", "[FEA]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its_make_cube(80., 10., 10.)));
    object->add_instance();
    EngineeringRegion fixed;
    fixed.volume    = 0;
    fixed.triangles = side(object->volumes.front()->mesh().its, 0, false);
    object->engineering.fixtures.push_back(fixed);
    EngineeringLoad load;
    load.type            = EngineeringLoad::Type::Faces;
    load.faces.volume    = 0;
    load.faces.triangles = side(object->volumes.front()->mesh().its, 0, true);
    load.force           = Vec3d(0., 0., -55.);
    object->engineering.loads.push_back(load);
    object->engineering.safety_factor = 1.5;

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("perimeters", new ConfigOptionInt(2));
    config.set_key_value("fill_density", new ConfigOptionPercent(20));
    config.set_deserialize_strict("fill_pattern", "gyroid");
    Fea::ModelAnalysisInput input;
    std::string error;
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    CHECK(input.setup.infill.enabled);
    CHECK(input.setup.infill.density == Approx(0.2));
    CHECK(input.setup.infill.wall_thickness > 0.5);
    CHECK(input.setup.infill.stiffness_exponent == Approx(infill_exponents("gyroid").first));
    // Without the print config the part is solid.
    Fea::ModelAnalysisInput solid;
    REQUIRE(build_analysis_input(*object, 0, "PLA", solid, error));
    CHECK(! solid.setup.infill.enabled);

    input.setup.voxel_size = 1.;
    const OptimizeResult opt = optimize_infill(input.mesh, input.setup);
    INFO("uniform " << opt.uniform_density << " feasible " << opt.feasible << " uniform mass " << opt.uniform.mass
         << " zoned mass " << opt.zoned.mass);
    REQUIRE(opt.ok);
    REQUIRE(opt.zones_found);

    // Applied twice: the second time replaces the zones of the first one.
    apply_infill(*object, 0, opt.base_density, opt.zones);
    const size_t added = apply_infill(*object, 0, opt.base_density, opt.zones);
    CHECK(added == opt.zones.size());
    CHECK(object->volumes.size() == 1 + opt.zones.size());
    CHECK(object->engineering.fixtures.front().volume == 0);
    CHECK(object->config.get().option<ConfigOptionPercent>("fill_density")->value == Approx(std::round(opt.base_density * 100.)));

    // The object, analyzed with its modifiers, is what the optimizer validated.
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    CHECK(input.setup.infill.zones.size() == opt.zones.size());
    input.setup.voxel_size = 1.;
    const Result applied = analyze(input.mesh, input.setup);
    REQUIRE(applied.ok);
    INFO("optimizer mass " << opt.zoned.mass << " applied " << applied.mass);
    CHECK(meets_requirements(applied));
    CHECK(applied.mass == Approx(opt.zoned.mass).epsilon(0.02));
}

TEST_CASE("Local reinforcement around supports and loads", "[FEA]")
{
    indexed_triangle_set its;
    Setup setup = printed_cantilever(its, 25., 0.);
    // A point load near the free end on the top face.
    setup.loads.front().type   = Load::Type::Point;
    setup.loads.front().point  = Vec3d(75., 5., 10.);
    setup.loads.front().radius = 3.;
    setup.infill.perimeters      = 2;
    setup.infill.perimeter_width = 0.45;
    ReinforcementOptions options;
    options.radius = 6.;
    const InfillZone zone = reinforcement_zone(its, setup, options);
    REQUIRE(! zone.mesh.indices.empty());
    CHECK(its_num_open_edges(zone.mesh) == 0);
    CHECK(zone.wall_thickness == Approx(setup.infill.wall_thickness + 2 * 0.45));
    BoundingBoxf3 bb;
    for (const Vec3f &v : zone.mesh.vertices)
        bb.merge(v.cast<double>());
    // Around the clamped face (x = 0) and the load (x = 75), nothing in the middle of the bar.
    CHECK(bb.min.x() < 0.);
    CHECK(bb.max.x() > 78.);

    // Same infill, with and without the reinforcement: stronger with it.
    const Result plain = analyze(its, setup);
    Setup reinforced = setup;
    InfillZone z = zone;
    z.density = std::max(z.density, setup.infill.density);
    reinforced.infill.zones = { z };
    const Result strong = analyze(its, reinforced);
    REQUIRE(plain.ok);
    REQUIRE(strong.ok);
    INFO("safety plain " << plain.safety_factor << " reinforced " << strong.safety_factor << " mass " << plain.mass << " / " << strong.mass);
    CHECK(strong.safety_factor > plain.safety_factor);
    CHECK(strong.mass > plain.mass);

    // Lightest infill with and without the reinforcement.
    setup.required_safety_factor = 2.;
    const ReinforcementResult res = optimize_reinforcement(its, setup, options);
    REQUIRE(res.ok);
    INFO("without " << res.without.uniform_density << " (" << res.without.uniform.mass << " g), with "
         << res.with.uniform_density << " (" << res.with.uniform.mass << " g)");
    REQUIRE(res.without.feasible);
    REQUIRE(res.with.feasible);
    CHECK(meets_requirements(res.with.uniform));
    CHECK(res.with.uniform_density <= res.without.uniform_density);
}

TEST_CASE("Lattice struts are printable and closed", "[FEA]")
{
    LatticeGrid grid;
    grid.origin = Vec3d::Zero();
    grid.cell   = 10.;
    grid.cells  = Vec3i(1, 1, 1);
    LatticeOptions options;
    options.segments = 6;
    const indexed_triangle_set its = lattice_mesh(grid, options, [](const Vec3d &) { return 1.2; });
    // 4 vertical struts and 2 crosses on each of the 4 side faces of the cell, 4 * 6 triangles each.
    CHECK(its.indices.size() == size_t(12 * 4 * 6));
    CHECK(its_num_open_edges(its) == 0);
    // No horizontal strut: every prism goes up at least 45 degrees (checked on the cap centers, the last 2 vertices
    // of each strut).
    const size_t per_strut = 2 * 6 + 2;
    for (size_t s = 0; s < its.vertices.size() / per_strut; ++ s) {
        const Vec3f a = its.vertices[s * per_strut + 12], b = its.vertices[s * per_strut + 13];
        const Vec3f d = b - a;
        CHECK(std::abs(d.z()) >= 0.99f * Vec2f(d.x(), d.y()).norm());
    }
}

TEST_CASE("Lattice instead of the infill", "[FEA]")
{
    // Block 30 x 30 x 30 standing on its bottom face, pressed on the top face.
    const indexed_triangle_set its = its_make_cube(30., 30., 30.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 1.;
    setup.required_safety_factor = 2.;
    setup.fixtures.push_back({ side(its, 2, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 2, true);
    load.force     = Vec3d(0., 0., -6000.);
    setup.loads.push_back(load);
    setup.infill.enabled              = true;
    setup.infill.wall_thickness       = 0.9;
    setup.infill.top_bottom_thickness = 0.9;
    LatticeOptions options;
    options.cell = 7.5;
    const LatticeResult res = optimize_lattice(its, setup, options);
    REQUIRE(res.ok);
    INFO("uniform " << res.uniform_diameter << " mm, mass " << res.uniform.mass << " g, safety " << res.uniform.safety_factor
         << "; variable " << res.variable_found << " " << res.min_used_diameter << "-" << res.max_used_diameter
         << " mm, mass " << res.variable.mass << "; analyses " << res.analyses);
    REQUIRE(res.feasible);
    CHECK(meets_requirements(res.uniform));
    CHECK(res.uniform_diameter >= options.min_diameter);
    CHECK(res.uniform_diameter <= options.max_diameter);
    CHECK(! res.mesh.indices.empty());
    // Lighter than the solid block (33.5 g of PLA).
    CHECK(res.uniform.mass < res.uniform.solid_mass);
    if (res.variable_found) {
        CHECK(meets_requirements(res.variable));
        CHECK(res.variable.mass < res.uniform.mass);
    }
}

TEST_CASE("Lattice with variable struts in bending", "[FEA]")
{
    // Cantilever 90 x 20 x 20 mm: the bending moment grows towards the clamped end.
    const indexed_triangle_set its = its_make_cube(90., 20., 20.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 1.;
    setup.required_safety_factor = 2.;
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 0, true);
    load.force     = Vec3d(0., 0., -200.);
    setup.loads.push_back(load);
    setup.infill.enabled              = true;
    setup.infill.wall_thickness       = 0.9;
    setup.infill.top_bottom_thickness = 0.9;
    LatticeOptions options;
    options.cell = 7.5;
    const LatticeResult res = optimize_lattice(its, setup, options);
    REQUIRE(res.ok);
    INFO("uniform " << res.uniform_diameter << " mm, mass " << res.uniform.mass << " g; variable " << res.variable_found << " "
         << res.min_used_diameter << "-" << res.max_used_diameter << " mm, mass " << res.variable.mass << "; analyses " << res.analyses);
    REQUIRE(res.feasible);
    REQUIRE(res.variable_found);
    CHECK(meets_requirements(res.variable));
    CHECK(res.variable.mass < res.uniform.mass);
    CHECK(res.max_used_diameter > res.min_used_diameter);
    // The thickest struts are near the clamped end.
    const Vec3i n = res.grid.cells;
    double near = 0., far = 0.;
    for (int k = 0; k < n.z(); ++ k)
        for (int j = 0; j < n.y(); ++ j) {
            near = std::max(near, double(res.cell_diameter[0 + size_t(n.x()) * (j + size_t(n.y()) * k)]));
            far  = std::max(far,  double(res.cell_diameter[n.x() - 1 + size_t(n.x()) * (j + size_t(n.y()) * k)]));
        }
    CHECK(near > far);
}

TEST_CASE("Lattice analysis cost", "[.][FEA_benchmark]")
{
    const indexed_triangle_set its = its_make_cube(90., 20., 20.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 1.;
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 0, true);
    load.force     = Vec3d(0., 0., -60.);
    setup.loads.push_back(load);
    setup.infill.enabled              = true;
    setup.infill.wall_thickness       = 0.9;
    setup.infill.top_bottom_thickness = 0.9;
    setup.infill.density              = 0.;
    setup.infill.stiffness_exponent   = 1.5;
    setup.infill.strength_exponent    = 1.5;
    for (double rho : { 0.075, 0.3, 0.84 }) {
        setup.infill.density_field = [rho](const Vec3d &) { return rho; };
        const auto t0 = std::chrono::steady_clock::now();
        const Result r = analyze(its, setup);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        WARN("rho " << rho << " ok " << r.ok << " '" << r.error << "' iterations " << r.iterations << " residual " << r.residual
             << " safety " << r.safety_factor << " time " << s << " s voxels " << r.voxels.size());
    }
}

TEST_CASE("Reinforcement and lattice applied to an object", "[FEA]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its_make_cube(60., 20., 20.)));
    object->add_instance();
    EngineeringRegion fixed;
    fixed.volume    = 0;
    fixed.triangles = side(object->volumes.front()->mesh().its, 0, false);
    object->engineering.fixtures.push_back(fixed);
    EngineeringLoad load;
    load.type            = EngineeringLoad::Type::Faces;
    load.faces.volume    = 0;
    load.faces.triangles = side(object->volumes.front()->mesh().its, 0, true);
    load.force           = Vec3d(0., 0., -150.);
    object->engineering.loads.push_back(load);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("perimeters", new ConfigOptionInt(2));
    Fea::ModelAnalysisInput input;
    std::string error;
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    input.setup.voxel_size = 1.;

    // Reinforcement: a modifier with 4 perimeters, read back as thicker walls.
    ReinforcementOptions ropt;
    const InfillZone zone = reinforcement_zone(input.mesh, input.setup, ropt);
    apply_reinforcement(*object, 0, 0.15, zone, input.setup.infill.perimeters + ropt.extra_perimeters);
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes.back()->name == REINFORCEMENT_NAME);
    Fea::ModelAnalysisInput rin;
    REQUIRE(build_analysis_input(*object, 0, "PLA", rin, error, &config));
    REQUIRE(rin.setup.infill.zones.size() == 1);
    CHECK(rin.setup.infill.zones.front().wall_thickness == Approx(4 * rin.setup.infill.perimeter_width));
    CHECK(rin.setup.infill.density == Approx(0.15));

    // Lattice: replaces the reinforcement and the infill.
    LatticeOptions lopt;
    lopt.cell = 7.5;
    const LatticeResult lat = optimize_lattice(input.mesh, input.setup, lopt);
    REQUIRE(lat.ok);
    REQUIRE(lat.feasible);
    apply_lattice(*object, 0, lat.mesh, lopt.cell);
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes.back()->name.rfind(LATTICE_NAME, 0) == 0);
    CHECK(object->config.get().option<ConfigOptionPercent>("fill_density")->value == Approx(0.));
    CHECK(object->engineering.fixtures.front().volume == 0);

    // The object analyzed with its lattice modifier (homogenized from the real struts).
    Fea::ModelAnalysisInput lin;
    REQUIRE(build_analysis_input(*object, 0, "PLA", lin, error, &config));
    REQUIRE(lin.setup.infill.zones.size() == 1);
    CHECK(lin.setup.infill.zones.front().homogenize_cell == Approx(7.5));
    lin.setup.voxel_size = 1.;
    const Result applied = analyze(lin.mesh, lin.setup);
    REQUIRE(applied.ok);
    const Result &expected = lat.variable_found ? lat.variable : lat.uniform;
    INFO("optimizer mass " << expected.mass << " safety " << expected.safety_factor << "; applied mass " << applied.mass
         << " safety " << applied.safety_factor);
    // The struts overlap at the nodes (less material than the formula) and the walls cut the outer struts.
    CHECK(applied.mass == Approx(expected.mass).epsilon(0.25));
    CHECK(applied.safety_factor > 0.7 * expected.safety_factor);
}

TEST_CASE("Applied infill reaches the slicing", "[FEA]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its_make_cube(30., 20., 10.)));
    object->add_instance();
    object->instances.front()->set_offset(Vec3d(100., 100., 0.));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("fill_density", new ConfigOptionPercent(20));

    // The same Print every time, as in the application (the changes are applied incrementally).
    Print print;
    auto sliced_density = [&]() {
        print.apply(model, config);
        print.process();
        REQUIRE(print.objects().size() == 1);
        double density = -1.;
        for (const LayerRegion *region : print.objects().front()->layers()[5]->regions())
            density = std::max(density, region->region().config().fill_density.value);
        return density;
    };
    CHECK(sliced_density() == Approx(20.));
    // The lightest uniform infill: the object setting wins over the print profile.
    apply_infill(*object, 0, 0.37, {});
    CHECK(sliced_density() == Approx(37.));
    // By zones: the zone is in print coordinates (the cube spans 100..130 x 100..120 on the bed); it becomes a
    // modifier of the object with its own region.
    InfillZone zone;
    zone.mesh = its_make_cube(10., 20., 10.);
    its_translate(zone.mesh, Vec3f(100.f, 100.f, 0.f));
    zone.density = 0.8;
    CHECK(apply_infill(*object, 0, 0.15, { zone }) == 1);
    CHECK(sliced_density() == Approx(80.));
    // Applying again replaces the modifiers.
    CHECK(apply_infill(*object, 0, 0.25, {}) == 0);
    CHECK(sliced_density() == Approx(25.));
}

TEST_CASE("Line width of the profile", "[FEA]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its_make_cube(10., 10., 10.)));
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({ 0.6 }));
    config.set_key_value("layer_height", new ConfigOptionFloat(0.3));
    config.set_key_value("extrusion_width", new ConfigOptionFloatOrPercent(0., false));
    config.set_key_value("perimeter_extrusion_width", new ConfigOptionFloatOrPercent(0.5, false));
    CHECK(line_width(*object, config) == Approx(0.5));
    // A percent is of the layer height, as in PrusaSlicer (Flow::extrusion_width).
    config.set_key_value("perimeter_extrusion_width", new ConfigOptionFloatOrPercent(150., true));
    CHECK(line_width(*object, config) == Approx(0.45));
    // Automatic.
    config.set_key_value("perimeter_extrusion_width", new ConfigOptionFloatOrPercent(0., false));
    CHECK(line_width(*object, config) == Approx(1.125 * 0.6));
    // The object overrides the profile.
    object->config.set_key_value("perimeter_extrusion_width", new ConfigOptionFloatOrPercent(0.8, false));
    CHECK(line_width(*object, config) == Approx(0.8));
}

TEST_CASE("Belt printer inclines the layers of the analysis", "[FEA][Belt]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its_make_cube(20., 20., 20.)));
    object->add_instance();
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    ModelAnalysisInput input;
    std::string error;
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    CHECK((input.setup.build_direction - Vec3d::UnitZ()).norm() < 1e-9);
    config.set_deserialize_strict({ { "belt_printer", 1 }, { "belt_angle", 30 } });
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    // The layers are stacked along the normal of the gantry: (0, sin 30°, cos 30°).
    CHECK((input.setup.build_direction - Vec3d(0., 0.5, std::sqrt(3.) / 2.)).norm() < 1e-9);
}

TEST_CASE("Build direction rotates the weak axis of the material", "[FEA][Orientation]")
{
    // A bar pulled along X: printed with the layers stacked along Z it is pulled along the layers; with the
    // layers stacked along X it is pulled across them (weaker and softer).
    const indexed_triangle_set its = its_make_cube(40., 10., 10.);
    Setup setup;
    setup.material   = "PA-CF";
    setup.voxel_size = 2.;
    setup.tolerance  = 1e-9;
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 0, true);
    load.force     = Vec3d(500., 0., 0.);
    setup.loads.push_back(load);
    const Result along = analyze(its, setup);
    setup.build_direction = Vec3d::UnitX();
    const Result across = analyze(its, setup);
    REQUIRE(along.ok);
    REQUIRE(across.ok);
    const Material &m = *find_material("PA-CF");
    // Same as the bar built vertically and pulled along Z (test "Layers are softer across than along").
    CHECK(end_displacement(across, 0, 0) / end_displacement(along, 0, 0) == Approx(m.E_xy / m.E_z).epsilon(0.05));
    // Tension of 5 MPa (higher at the clamped end): limited by the layer adhesion across, by the strength along the
    // layers. The clamp concentrates the stresses, so only the ratio is close to the ratio of the strengths.
    CHECK(along.safety_factor < m.strength_xy / 5.);
    CHECK(across.safety_factor < along.safety_factor);
    const double ratio = along.safety_factor / across.safety_factor;
    CHECK(ratio > 0.8 * m.strength_xy / m.strength_z);
    CHECK(ratio < 1.1 * m.strength_xy / m.strength_z);
}

TEST_CASE("Layer adhesion depends on the nozzle temperature", "[FEA][Orientation]")
{
    const Material &pla = *find_material("PLA");
    CHECK(layer_adhesion_factor(pla, 0.) == 1.);
    CHECK(layer_adhesion_factor(pla, 205.) == Approx(1.));
    CHECK(layer_adhesion_factor(pla, 190.) == Approx(0.8));
    CHECK(layer_adhesion_factor(pla, 230.) == Approx(1.15));
    CHECK(layer_adhesion_factor(pla, 150.) == Approx(0.5));
    CHECK(layer_adhesion_factor(pla, 197.5) < layer_adhesion_factor(pla, 212.5));

    // A bar pulled across the layers is stronger printed hotter.
    const indexed_triangle_set its = its_make_cube(10., 10., 40.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 2.;
    setup.fixtures.push_back({ side(its, 2, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 2, true);
    load.force     = Vec3d(0., 0., 500.);
    setup.loads.push_back(load);
    setup.print_temperature = 190.;
    const Result cold = analyze(its, setup);
    setup.print_temperature = 220.;
    const Result hot = analyze(its, setup);
    REQUIRE(cold.ok);
    REQUIRE(hot.ok);
    CHECK(hot.layer_adhesion_factor == Approx(1.15));
    CHECK(hot.safety_factor / cold.safety_factor == Approx(1.15 / 0.8).epsilon(0.02));
}

TEST_CASE("Recommended orientation puts the loads along the layers", "[FEA][Orientation]")
{
    // A vertical post pulled along Z: printed upright the load is across the layers. Laid on a side it is stronger.
    const indexed_triangle_set its = its_make_cube(10., 10., 40.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 2.;
    setup.fixtures.push_back({ side(its, 2, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 2, true);
    load.force     = Vec3d(0., 0., 600.);
    setup.loads.push_back(load);
    const OrientationResult res = recommend_orientation(its, setup);
    REQUIRE(res.ok);
    REQUIRE(res.candidates.size() == 3);
    CHECK(res.candidates[0].axis == "Z");
    CHECK(res.best != 0);
    const Material &m = *find_material("PLA");
    const double gain = res.candidates[res.best].result.safety_factor / res.candidates[0].result.safety_factor;
    CHECK(gain > 0.8 * m.strength_xy / m.strength_z);
    CHECK(gain < 1.1 * m.strength_xy / m.strength_z);
    // The rotation lays the chosen axis vertical.
    const Vec3d up = rotation_to_print(res.candidates[res.best].build_direction) * res.candidates[res.best].build_direction;
    CHECK(up.z() == Approx(1.).margin(1e-9));
}

// Time of the analysis with many voxels (quality Ultra), run with "[.FEA-bench]".
TEST_CASE("Analysis time with many voxels", "[.FEA-bench]")
{
    const indexed_triangle_set its = its_make_cube(100., 50., 50.);
    Setup setup;
    setup.material = "PLA";
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 0, true);
    load.force     = Vec3d(0., 0., -200.);
    setup.loads.push_back(load);
    for (double voxels : { 6e4, 2.5e5, 1.2e6 }) {
        setup.voxel_size = std::cbrt(100. * 50. * 50. / voxels);
        const auto start = std::chrono::steady_clock::now();
        const Result r = analyze(its, setup);
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << voxels << " voxels (h " << setup.voxel_size << " mm): " << t << " s, ok " << r.ok << ", sf " << r.safety_factor << std::endl;
        CHECK(r.ok);
    }
}

// Tisma phase 9b: the material follows the curved (non-planar) layers voxel by voxel.
TEST_CASE("Curved layers orient the material voxel by voxel", "[FEA][NonPlanar]")
{
    // A bar standing on the bed, pulled up: with flat layers the whole load crosses the layers.
    const indexed_triangle_set its = its_make_cube(10., 10., 40.);
    Setup setup;
    setup.material   = "PLA";
    setup.voxel_size = 2.;
    setup.tolerance  = 1e-9;
    setup.fixtures.push_back({ side(its, 2, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, 2, true);
    load.force     = Vec3d(0., 0., 400.);
    setup.loads.push_back(load);

    const Result flat = analyze(its, setup);
    REQUIRE(flat.ok);
    CHECK(flat.layer_orientations == 1);

    SECTION("A uniform field is the same as the build direction") {
        const Vec3d n = Vec3d(0., std::sin(0.4), std::cos(0.4));
        Setup by_field = setup;
        by_field.layer_normal = [n](const Vec3d &) { return n; };
        Setup by_direction = setup;
        by_direction.build_direction = n;
        const Result a = analyze(its, by_field);
        const Result b = analyze(its, by_direction);
        REQUIRE(a.ok);
        REQUIRE(b.ok);
        // The groups snap the normal to a 2° grid.
        CHECK(a.safety_factor == Approx(b.safety_factor).epsilon(0.03));
        CHECK(a.layer_orientations == 1);
    }
    SECTION("Inclined layers carry more of the pull along them") {
        // Layers tilted by ±25° (alternating along the bar, like waves): the pull is no longer only across them.
        Setup waves = setup;
        waves.layer_normal = [](const Vec3d &p) {
            const double t = 25. * M_PI / 180. * (std::fmod(std::floor(p.z() / 8.), 2.) == 0. ? 1. : -1.);
            return Vec3d(std::sin(t), 0., std::cos(t));
        };
        const Result curved = analyze(its, waves);
        REQUIRE(curved.ok);
        CHECK(curved.layer_orientations == 2);
        CHECK(curved.safety_factor > flat.safety_factor);
    }
}

TEST_CASE("Non-planar print settings reach the analysis", "[FEA][NonPlanar]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its_make_cube(30., 30., 20.)));
    object->add_instance();
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    ModelAnalysisInput input;
    std::string error;
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    CHECK(! input.setup.layer_normal);

    config.set_deserialize_strict({ { "nonplanar_mode", "wave" }, { "nonplanar_pattern", "ridges" },
                                    { "nonplanar_amplitude", 1 }, { "nonplanar_wavelength", 20 } });
    REQUIRE(build_analysis_input(*object, 0, "PLA", input, error, &config));
    REQUIRE(input.setup.layer_normal);
    // Flat at the bottom (first layers), tilted higher up where the ridges have a slope.
    const BoundingBoxf3 bb = bounding_box(input.mesh);
    const Vec3d c = 0.5 * (bb.min + bb.max);
    CHECK((input.setup.layer_normal(Vec3d(c.x(), c.y(), bb.min.z() + 0.1)) - Vec3d::UnitZ()).norm() < 1e-9);
    double max_tilt = 0.;
    for (double x = bb.min.x(); x <= bb.max.x(); x += 1.)
        for (double y = bb.min.y(); y <= bb.max.y(); y += 1.)
            max_tilt = std::max(max_tilt, std::acos(input.setup.layer_normal(Vec3d(x, y, bb.max.z() - 1.)).z()));
    // Ridges of amplitude 1 mm and wavelength 20 mm: slope up to atan(2π / 20) ≈ 17°.
    CHECK(max_tilt * 180. / M_PI > 10.);
    CHECK(max_tilt * 180. / M_PI < 20.);
}
