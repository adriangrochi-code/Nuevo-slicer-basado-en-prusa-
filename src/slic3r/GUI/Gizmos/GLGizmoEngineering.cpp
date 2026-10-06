///|/ Tisma Slicer: Engineering workspace in the 3D view (phase 5).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoEngineering.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <queue>

#include <GL/glew.h>
#include <imgui/imgui.h>

#include <libslic3r/Model.hpp>
#include <libslic3r/PresetBundle.hpp>
#include <tisma_fea/Materials.hpp>
#include <tisma_fea/ModelSetup.hpp>
#include <tisma_fea/Optimize.hpp>

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiPureWrap.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

namespace Slic3r {
namespace GUI {

namespace {

const ColorRGBA FIXTURE_COLOR   { 0.20f, 0.55f, 1.00f, 0.85f };
const ColorRGBA FACE_LOAD_COLOR { 1.00f, 0.55f, 0.10f, 0.85f };
const ColorRGBA LOAD_COLOR      { 0.95f, 0.25f, 0.20f, 1.00f };
constexpr int   COLOR_STEPS = 16;

// Blue - cyan - green - yellow - red.
ColorRGBA scale_color(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    const float stops[5][3] = { { 0.10f, 0.25f, 0.90f }, { 0.10f, 0.80f, 0.90f }, { 0.20f, 0.85f, 0.25f },
                                { 0.95f, 0.85f, 0.15f }, { 0.90f, 0.15f, 0.10f } };
    const float x = t * 4.f;
    const int   i = std::min(3, int(x));
    const float f = x - i;
    return { stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f, stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f,
             stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f, 1.f };
}

ImVec4 to_imvec(const ColorRGBA &c) { return { c.r(), c.g(), c.b(), c.a() }; }

// The object changed (infill, modifiers, transformation): the scene, the object list and the slicing are updated.
void changed_object(ModelObject &object)
{
    Plater *plater = wxGetApp().plater();
    plater->changed_object(object);
    wxGetApp().obj_list()->update_after_undo_redo();
}

void notify(const std::string &text)
{
    if (NotificationManager *nm = wxGetApp().plater()->get_notification_manager())
        nm->push_notification(NotificationType::CustomNotification, NotificationManager::NotificationLevel::PrintInfoNotificationLevel, text);
}

std::string verdict_text(Fea::Verdict v)
{
    switch (v) {
    case Fea::Verdict::Holds:            return _u8L("The part holds");
    case Fea::Verdict::LowMargin:        return _u8L("It holds, but with less margin than the required safety factor");
    case Fea::Verdict::OutOfLoad:        return _u8L("Out of load: the stresses exceed the strength of the material");
    case Fea::Verdict::TooFlexible:      return _u8L("Too flexible: a load moves more than its allowed deformation");
    case Fea::Verdict::OutOfTemperature: return _u8L("Out of temperature: the material is not structural at this temperature");
    case Fea::Verdict::NotApplicable:    return _u8L("The linear analysis does not describe this material (elastomer)");
    }
    return {};
}

ColorRGBA verdict_color(Fea::Verdict v)
{
    switch (v) {
    case Fea::Verdict::Holds:     return { 0.30f, 0.85f, 0.35f, 1.f };
    case Fea::Verdict::LowMargin: return { 0.95f, 0.80f, 0.20f, 1.f };
    default:                      return { 0.95f, 0.30f, 0.25f, 1.f };
    }
}

} // namespace

GLGizmoEngineering::GLGizmoEngineering(GLCanvas3D &parent)
    : GLGizmoBase(parent, "", -1)
{}

GLGizmoEngineering::~GLGizmoEngineering()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
}

bool GLGizmoEngineering::on_init()
{
    m_sphere.init_from(smooth_sphere(16, 1.f));
    m_arrow.init_from(stilized_arrow(16, 1.6f, 4.f, 0.6f, 8.f));
    return true;
}

std::string GLGizmoEngineering::on_get_name() const
{
    return _u8L("Engineering");
}

bool GLGizmoEngineering::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF &&
           selection.is_single_full_instance();
}

CommonGizmosDataID GLGizmoEngineering::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::Raycaster));
}

void GLGizmoEngineering::on_set_state()
{
    if (m_state == On) {
        m_regions_dirty = true;
        m_result_models_dirty = true;
        if (const std::string q = wxGetApp().app_config->get("tisma_fea_quality"); ! q.empty())
            m_quality = Quality(std::clamp(std::atoi(q.c_str()), 0, 3));
        if (const ModelObject *mo = model_object()) {
            m_edit_temperature = float(mo->engineering.temperature);
            m_edit_safety      = float(mo->engineering.safety_factor);
        }
    } else if (m_state == Off) {
        cancel_analysis();
        show_object(true);
        m_tool = Tool::None;
    }
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->update_nav_rail();
}

void GLGizmoEngineering::data_changed(bool is_serializing)
{
    m_regions_dirty = true;
    const ModelObject *mo = model_object();
    if (m_result && (mo == nullptr || mo->id() != m_result_object)) {
        // Another object: its own analysis is not known.
        m_result.reset();
        show_object(true);
    }
    if (mo) {
        m_edit_temperature = float(mo->engineering.temperature);
        m_edit_safety      = float(mo->engineering.safety_factor);
    }
}

std::string GLGizmoEngineering::get_tooltip() const
{
    switch (m_tool) {
    case Tool::Fixture:   return _u8L("Click a face to hold the part there");
    case Tool::FaceLoad:  return _u8L("Click a face to apply the load on it");
    case Tool::PointLoad: return _u8L("Click a point of the part to apply the load there");
    default:              return {};
    }
}

ModelObject* GLGizmoEngineering::model_object() const
{
    return m_c && m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
}

int GLGizmoEngineering::instance_idx() const
{
    return m_c && m_c->selection_info() ? std::max(0, m_c->selection_info()->get_active_instance()) : 0;
}

bool GLGizmoEngineering::raycast(const Vec2d &mouse, int &volume_idx, int &facet, Vec3f &hit) const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || m_c->raycaster() == nullptr)
        return false;
    const std::vector<const MeshRaycaster*> casters = m_c->raycaster()->raycasters();
    const Transform3d instance_trafo = mo->instances[instance_idx()]->get_transformation().get_matrix();
    const Camera &camera = wxGetApp().plater()->get_camera();

    double best = std::numeric_limits<double>::max();
    volume_idx = -1;
    size_t mesh_id = 0;
    for (size_t v = 0; v < mo->volumes.size(); ++ v) {
        if (! mo->volumes[v]->is_model_part())
            continue;
        if (mesh_id >= casters.size())
            break;
        const Transform3d trafo = instance_trafo * mo->volumes[v]->get_matrix();
        Vec3f  pos, normal;
        size_t fidx = 0;
        if (casters[mesh_id]->unproject_on_mesh(mouse, trafo, camera, pos, normal, nullptr, &fidx, false)) {
            const double d = (camera.get_position() - trafo * pos.cast<double>()).squaredNorm();
            if (d < best) {
                best       = d;
                volume_idx = int(v);
                facet      = int(fidx);
                hit        = pos;
            }
        }
        ++ mesh_id;
    }
    return volume_idx >= 0;
}

void GLGizmoEngineering::pick_face(const ModelVolume &volume, int volume_idx, int facet, EngineeringRegion &region) const
{
    region.volume = volume_idx;
    region.triangles.clear();
    region.cad_faces.clear();
    if (volume.has_cad_source()) {
        const int face = volume.cad_source->face_ids[facet];
        if (face >= 0) {
            region.cad_faces.push_back(face);
            for (size_t i = 0; i < volume.cad_source->face_ids.size(); ++ i)
                if (volume.cad_source->face_ids[i] == face)
                    region.triangles.push_back(int(i));
            return;
        }
    }
    // Meshes without CAD faces: the flat region around the triangle.
    const indexed_triangle_set &its = volume.mesh().its;
    const std::vector<Vec3i>  neighbors = its_face_neighbors(its);
    const Vec3f               normal = its_face_normal(its, facet);
    const float               limit = std::cos(float(M_PI) * 3.f / 180.f);
    std::vector<char>         visited(its.indices.size(), 0);
    std::queue<int>           queue;
    queue.push(facet);
    visited[facet] = 1;
    while (! queue.empty()) {
        const int f = queue.front();
        queue.pop();
        region.triangles.push_back(f);
        for (int k = 0; k < 3; ++ k) {
            const int n = neighbors[f][k];
            if (n >= 0 && ! visited[n] && its_face_normal(its, n).dot(normal) > limit) {
                visited[n] = 1;
                queue.push(n);
            }
        }
    }
    std::sort(region.triangles.begin(), region.triangles.end());
}

void GLGizmoEngineering::add_at_mouse(const Vec2d &mouse)
{
    ModelObject *mo = model_object();
    int volume_idx = -1, facet = 0;
    Vec3f hit;
    if (mo == nullptr || ! raycast(mouse, volume_idx, facet, hit))
        return;
    const ModelVolume &volume = *mo->volumes[volume_idx];
    EngineeringSetup &eng = mo->engineering;

    if (m_tool == Tool::Fixture) {
        EngineeringRegion region;
        pick_face(volume, volume_idx, facet, region);
        // Clicking an already held face releases it.
        for (auto it = eng.fixtures.begin(); it != eng.fixtures.end(); ++ it)
            if (it->volume == region.volume && it->triangles == region.triangles) {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove fixed face"));
                eng.fixtures.erase(it);
                m_regions_dirty = m_result_stale = true;
                return;
            }
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Add fixed face"));
        eng.fixtures.emplace_back(std::move(region));
    } else {
        EngineeringLoad load;
        load.force = Vec3d(m_new_force[0], m_new_force[1], m_new_force[2]);
        load.max_displacement         = std::max(0.f, m_new_limit_mm);
        load.max_displacement_percent = std::max(0.f, m_new_limit_percent);
        load.name = GUI::format(_u8L("Load %1%"), eng.loads.size() + 1);
        if (m_tool == Tool::FaceLoad) {
            load.type = EngineeringLoad::Type::Faces;
            pick_face(volume, volume_idx, facet, load.faces);
        } else {
            load.type   = EngineeringLoad::Type::Point;
            load.volume = volume_idx;
            load.point  = hit.cast<double>();
            load.radius = std::max(0.f, m_new_radius);
        }
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Add load"));
        eng.loads.emplace_back(std::move(load));
    }
    m_regions_dirty = m_result_stale = true;
}

bool GLGizmoEngineering::on_mouse(const wxMouseEvent &mouse_event)
{
    if (m_tool == Tool::None || ! mouse_event.LeftDown() || mouse_event.ShiftDown() || mouse_event.AltDown())
        return false;
    const Vec2d mouse(mouse_event.GetX(), mouse_event.GetY());
    int volume_idx, facet;
    Vec3f hit;
    if (! raycast(mouse, volume_idx, facet, hit))
        return false;
    add_at_mouse(mouse);
    m_parent.set_as_dirty();
    return true;
}

std::string GLGizmoEngineering::filament_type(const ModelObject &object) const
{
    int extruder = 1;
    for (const ModelVolume *v : object.volumes)
        if (v->is_model_part()) {
            extruder = std::max(1, v->extruder_id());
            break;
        }
    const DynamicPrintConfig &config = wxGetApp().preset_bundle->full_config();
    if (const ConfigOptionStrings *types = config.option<ConfigOptionStrings>("filament_type"); types && ! types->values.empty())
        return types->get_at(size_t(extruder - 1));
    return "PLA";
}

void GLGizmoEngineering::start_analysis()
{
    Fea::ModelAnalysisInput input;
    if (! prepare_input(input, JobSize::Single))
        return;
    // Solid part: the walls and the infill of the profile are not used (its nozzle temperature still is).
    if (! m_as_printed)
        input.setup.infill = Fea::InfillModel();
    m_job_optimize = false;
    m_cancel   = false;
    m_running  = true;
    m_progress = 0;
    m_thread = std::thread([this, input = std::move(input)]() {
        Fea::Result res = Fea::analyze(input.mesh, input.setup, [this]() { return m_cancel.load(); },
                                       [this](int p) { m_progress = p; });
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending = std::move(res);
        }
        m_running = false;
        wxGetApp().CallAfter([]() {
            if (GLCanvas3D *canvas = wxGetApp().plater() ? wxGetApp().plater()->get_current_canvas3D() : nullptr) {
                canvas->set_as_dirty();
                canvas->request_extra_frame();
            }
        });
    });
}

void GLGizmoEngineering::start_optimization()
{
    Fea::ModelAnalysisInput input;
    if (! prepare_input(input))
        return;
    // The current infill of the object, to compare.
    m_current_infill_mass = 0.;
    m_job_optimize = true;
    m_cancel   = false;
    m_running  = true;
    m_progress = 0;
    Fea::OptimizeOptions options;
    options.zones = m_opt_zones;
    m_thread = std::thread([this, input = std::move(input), options]() {
        Fea::OptimizeResult res;
        // The current infill first: the reference of the comparison.
        Fea::Result current = Fea::analyze(input.mesh, input.setup, [this]() { return m_cancel.load(); });
        if (current.ok)
            m_current_infill_mass = current.mass;
        if (! m_cancel)
            res = Fea::optimize_infill(input.mesh, input.setup, options, [this]() { return m_cancel.load(); },
                                       [this](int p) { m_progress = p; });
        else
            res.error = "Cancelled";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_opt = std::move(res);
        }
        m_running = false;
        wxGetApp().CallAfter([]() {
            if (GLCanvas3D *canvas = wxGetApp().plater() ? wxGetApp().plater()->get_current_canvas3D() : nullptr) {
                canvas->set_as_dirty();
                canvas->request_extra_frame();
            }
        });
    });
}

void GLGizmoEngineering::apply_optimization(bool zones)
{
    ModelObject *mo = model_object();
    if (mo == nullptr || ! m_opt || mo->id() != m_opt_object)
        return;
    const Fea::OptimizeResult &opt = *m_opt;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Apply lightest infill"));
    const bool   by_zones = zones && opt.zones_found;
    const double density  = by_zones ? opt.base_density : opt.uniform_density;
    const size_t added    = Fea::apply_infill(*mo, size_t(instance_idx()), density, by_zones ? opt.zones : std::vector<Fea::InfillZone>());
    changed_object(*mo);
    // The infill is a setting of the object and modifiers: it is seen in the object list and after slicing again.
    m_applied_note = added > 0 ?
        GUI::format(_u8L("Applied: infill %1% %% in \"%2%\" and %3% modifiers \"%4%\" with more infill. Slice again to see it in the preview."),
                    int(std::round(density * 100.)), mo->name, added, Fea::INFILL_ZONE_NAME) :
        GUI::format(_u8L("Applied: infill %1% %% in \"%2%\". Slice again to see it in the preview."),
                    int(std::round(density * 100.)), mo->name);
    m_applied_object = mo->id();
    notify(m_applied_note);
    m_regions_dirty = true;
    m_opt_object = mo->id();
}

bool GLGizmoEngineering::prepare_input(Fea::ModelAnalysisInput &input, JobSize size)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || m_running)
        return false;
    if (m_thread.joinable())
        m_thread.join();
    std::string error;
    const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    if (! Fea::build_analysis_input(*mo, size_t(instance_idx()), filament_type(*mo), input, error, &config)) {
        m_error = error;
        return false;
    }
    m_error.clear();
    apply_quality(input, size);
    m_result_material    = input.material;
    m_result_temperature = input.setup.temperature;
    m_result_safety      = input.setup.required_safety_factor;
    m_result_print_temperature = input.setup.print_temperature;
    m_result_object      = mo->id();
    return true;
}

namespace {
// Voxels of the qualities Fast and Normal.
constexpr size_t FAST_VOXELS   = 20000;
constexpr size_t NORMAL_VOXELS = 60000;
// Limits of the number of voxels: one analysis (about a minute with a million voxels on 8 cores) and the jobs of many
// analyses (the lightest infill runs about 20).
constexpr double MAX_VOXELS_SINGLE = 1.2e6;
constexpr double MAX_VOXELS_MANY   = 2.5e5;
// Voxel of the qualities High and Ultra in line widths.
double quality_line_widths(int quality) { return quality == 3 ? 1. : 2.; }
}

void GLGizmoEngineering::estimate_quality(const ModelObject &object, double &h, double &voxels) const
{
    const int idx = std::clamp(instance_idx(), 0, int(object.instances.size()) - 1);
    double volume = 0.;
    for (const ModelVolume *v : object.volumes)
        if (v->is_model_part())
            volume += std::abs((v->mesh().stats().volume >= 0. ? double(v->mesh().stats().volume) : double(its_volume(v->mesh().its))) *
                               (object.instances[idx]->get_matrix() * v->get_matrix()).matrix().block<3, 3>(0, 0).determinant());
    volume = std::max(volume, 1e-9);
    if (m_quality == Quality::Fast || m_quality == Quality::Normal) {
        voxels = double(m_quality == Quality::Fast ? FAST_VOXELS : NORMAL_VOXELS);
        h = std::cbrt(volume / voxels);
    } else {
        h = quality_line_widths(int(m_quality)) * Fea::line_width(object, wxGetApp().preset_bundle->full_config());
        voxels = volume / (h * h * h);
    }
}

void GLGizmoEngineering::apply_quality(Fea::ModelAnalysisInput &input, JobSize size)
{
    m_quality_note.clear();
    const double max_voxels = size == JobSize::Single ? MAX_VOXELS_SINGLE : MAX_VOXELS_MANY;
    const double volume = std::max(std::abs(double(its_volume(input.mesh))), 1e-9);
    double h = 0.;
    switch (m_quality) {
    case Quality::Fast:   h = std::cbrt(volume / double(FAST_VOXELS)); break;
    case Quality::Normal: h = std::cbrt(volume / double(NORMAL_VOXELS)); break;
    default:              h = quality_line_widths(int(m_quality)) * input.line_width; break;
    }
    if (volume / (h * h * h) > max_voxels) {
        h = std::cbrt(volume / max_voxels);
        m_quality_note = GUI::format(_u8L("Limited to %1% voxels of %2$.2f mm (%3$.1f line widths)."),
                                     size_t(max_voxels), h, h / std::max(input.line_width, 1e-3));
    }
    input.setup.voxel_size = h;
}

template<class T> void GLGizmoEngineering::launch(std::function<T()> job, std::optional<T> *pending)
{
    m_cancel   = false;
    m_running  = true;
    m_progress = 0;
    m_thread = std::thread([this, job = std::move(job), pending]() {
        T res = job();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            *pending = std::move(res);
        }
        m_running = false;
        wxGetApp().CallAfter([]() {
            if (GLCanvas3D *canvas = wxGetApp().plater() ? wxGetApp().plater()->get_current_canvas3D() : nullptr) {
                canvas->set_as_dirty();
                canvas->request_extra_frame();
            }
        });
    });
}

void GLGizmoEngineering::start_reinforcement()
{
    Fea::ModelAnalysisInput input;
    if (! prepare_input(input))
        return;
    m_reinf_base_perimeters = input.setup.infill.perimeters;
    Fea::ReinforcementOptions options;
    options.radius           = std::max(0.f, m_reinf_radius);
    options.extra_perimeters = std::clamp(m_reinf_perimeters, 0, 10);
    options.density          = std::clamp(m_reinf_density, 0.f, 100.f) * 0.01;
    launch<Fea::ReinforcementResult>([this, input = std::move(input), options]() {
        return Fea::optimize_reinforcement(input.mesh, input.setup, options, [this]() { return m_cancel.load(); },
                                           [this](int p) { m_progress = p; });
    }, &m_pending_reinf);
}

void GLGizmoEngineering::start_lattice()
{
    Fea::ModelAnalysisInput input;
    if (! prepare_input(input))
        return;
    Fea::LatticeOptions options;
    options.cell         = std::max(2.f, m_lattice_cell);
    options.min_diameter = std::max(0.2f, m_lattice_min_d);
    options.max_diameter = std::max(options.min_diameter, double(m_lattice_max_d));
    launch<Fea::LatticeResult>([this, input = std::move(input), options]() {
        return Fea::optimize_lattice(input.mesh, input.setup, options, [this]() { return m_cancel.load(); },
                                     [this](int p) { m_progress = p; });
    }, &m_pending_lattice);
}

void GLGizmoEngineering::apply_reinforcement()
{
    ModelObject *mo = model_object();
    if (mo == nullptr || ! m_reinf || mo->id() != m_reinf_object || ! m_reinf->with.feasible)
        return;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Apply local reinforcement"));
    Fea::apply_reinforcement(*mo, size_t(instance_idx()), m_reinf->with.uniform_density, m_reinf->zone,
                             m_reinf_base_perimeters + std::clamp(m_reinf_perimeters, 0, 10));
    changed_object(*mo);
    m_regions_dirty = true;
    m_reinf_object = mo->id();
}

void GLGizmoEngineering::apply_lattice()
{
    ModelObject *mo = model_object();
    if (mo == nullptr || ! m_lattice || mo->id() != m_lattice_object || ! m_lattice->feasible)
        return;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Apply lattice"));
    Fea::apply_lattice(*mo, size_t(instance_idx()), m_lattice->mesh, m_lattice->grid.cell);
    changed_object(*mo);
    m_regions_dirty = true;
    m_lattice_object = mo->id();
}

void GLGizmoEngineering::start_orientation()
{
    Fea::ModelAnalysisInput input;
    if (! prepare_input(input))
        return;
    if (! m_as_printed)
        input.setup.infill = Fea::InfillModel();
    launch<Fea::OrientationResult>([this, input = std::move(input)]() {
        return Fea::recommend_orientation(input.mesh, input.setup, [this]() { return m_cancel.load(); },
                                          [this](int p) { m_progress = p; });
    }, &m_pending_orient);
}

void GLGizmoEngineering::rotate_to_recommended()
{
    ModelObject *mo = model_object();
    if (mo == nullptr || ! m_orient || mo->id() != m_orient_object || m_orient->best == 0 ||
        m_orient->best >= m_orient->candidates.size())
        return;
    // Rotation in print coordinates (as on the bed), about the position of the instance.
    const Transform3d rotation = Fea::rotation_to_print(m_orient->candidates[m_orient->best].build_direction);
    ModelInstance *instance = mo->instances[std::clamp(instance_idx(), 0, int(mo->instances.size()) - 1)];
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Rotate to the recommended orientation"));
    const Transform3d old_matrix = instance->get_matrix();
    Transform3d new_matrix = rotation * old_matrix;
    new_matrix.translation() = old_matrix.translation();
    instance->set_transformation(Geometry::Transformation(new_matrix));
    // The forces are given in print coordinates: they turn with the part. The faces and points of the supports
    // and loads are in the coordinates of the object and follow it.
    for (EngineeringLoad &load : mo->engineering.loads)
        load.force = rotation.linear() * load.force;
    mo->invalidate_bounding_box();
    changed_object(*mo);
    m_orient.reset();
    m_regions_dirty = m_result_stale = true;
    notify(_u8L("Part rotated to print the loads along the layers: analyze again and check the supports of the print."));
}

void GLGizmoEngineering::cancel_analysis()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
    m_running = false;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.reset();
    m_pending_opt.reset();
    m_pending_reinf.reset();
    m_pending_lattice.reset();
    m_pending_orient.reset();
}

void GLGizmoEngineering::fetch_result()
{
    std::optional<Fea::Result> res;
    std::optional<Fea::OptimizeResult> opt;
    std::optional<Fea::ReinforcementResult> reinf;
    std::optional<Fea::LatticeResult> lattice;
    std::optional<Fea::OrientationResult> orient;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        orient.swap(m_pending_orient);
        res.swap(m_pending);
        opt.swap(m_pending_opt);
        reinf.swap(m_pending_reinf);
        lattice.swap(m_pending_lattice);
    }
    if (orient) {
        if (m_thread.joinable())
            m_thread.join();
        if (! orient->ok) {
            m_error = orient->error == "Cancelled" ? std::string() : orient->error;
            return;
        }
        m_orient        = std::move(*orient);
        m_orient_object = m_result_object;
        // Show the analysis of the current orientation.
        if (! m_orient->candidates.empty()) {
            m_result = m_orient->candidates.front().result;
            m_result_is_lattice = false;
            m_result_stale = false;
            m_result_models_dirty = true;
        }
        return;
    }
    if (reinf || lattice) {
        if (m_thread.joinable())
            m_thread.join();
        const std::string error = reinf ? reinf->error : lattice->error;
        if (reinf ? ! reinf->ok : ! lattice->ok) {
            m_error = error == "Cancelled" ? std::string() : error;
            return;
        }
        m_result_is_lattice = ! reinf;
        if (reinf) {
            m_reinf        = std::move(*reinf);
            m_reinf_object = m_result_object;
            m_result       = m_reinf->with.uniform;
        } else {
            m_lattice        = std::move(*lattice);
            m_lattice_object = m_result_object;
            m_result         = m_lattice->variable_found ? m_lattice->variable : m_lattice->uniform;
            m_lattice_model_dirty = true;
        }
        m_result_stale = false;
        m_field = Field::Density;
        m_result_models_dirty = true;
        return;
    }
    if (opt) {
        if (m_thread.joinable())
            m_thread.join();
        if (! opt->ok) {
            m_error = opt->error == "Cancelled" ? std::string() : opt->error;
            return;
        }
        m_opt        = std::move(*opt);
        m_opt_object = m_result_object;
        m_result_is_lattice = false;
        // Show the infill found on the part.
        m_result = m_opt->zones_found ? m_opt->zoned : m_opt->uniform;
        m_result_stale = false;
        m_field = Field::Density;
        m_result_models_dirty = true;
        return;
    }
    if (! res)
        return;
    if (m_thread.joinable())
        m_thread.join();
    if (! res->ok) {
        m_error = res->error == "Cancelled" ? std::string() : res->error;
        return;
    }
    m_result = std::move(*res);
    m_result_is_lattice = false;
    m_result_stale = false;
    m_result_models_dirty = true;
}

void GLGizmoEngineering::show_object(bool show)
{
    if (show) {
        if (m_object_hidden) {
            m_parent.toggle_model_objects_visibility(true);
            m_object_hidden = false;
        }
    } else if (const ModelObject *mo = model_object()) {
        m_parent.toggle_model_objects_visibility(false, mo, instance_idx());
        m_object_hidden = true;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Rendering.

void GLGizmoEngineering::update_region_models()
{
    m_region_models.clear();
    m_regions_dirty = false;
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;
    const Transform3d instance_trafo = mo->instances[instance_idx()]->get_transformation().get_matrix();
    auto add = [&](const EngineeringRegion &region, const ColorRGBA &color) {
        if (region.volume < 0 || region.volume >= int(mo->volumes.size()))
            return;
        const ModelVolume &volume = *mo->volumes[region.volume];
        const indexed_triangle_set &its = volume.mesh().its;
        indexed_triangle_set sub;
        for (int t : region.triangles)
            if (t >= 0 && t < int(its.indices.size())) {
                const int base = int(sub.vertices.size());
                for (int k = 0; k < 3; ++ k)
                    sub.vertices.emplace_back(its.vertices[its.indices[t][k]]);
                sub.indices.emplace_back(base, base + 1, base + 2);
            }
        if (sub.indices.empty())
            return;
        RegionModel rm;
        rm.model.init_from(sub);
        rm.trafo = instance_trafo * volume.get_matrix();
        rm.color = color;
        m_region_models.emplace_back(std::move(rm));
    };
    for (const EngineeringRegion &r : mo->engineering.fixtures)
        add(r, FIXTURE_COLOR);
    for (const EngineeringLoad &l : mo->engineering.loads)
        if (l.type == EngineeringLoad::Type::Faces)
            add(l.faces, FACE_LOAD_COLOR);
}

void GLGizmoEngineering::update_result_models()
{
    m_result_models.clear();
    m_result_models_dirty = false;
    if (! m_result)
        return;
    const Fea::Result &r = *m_result;
    const Vec3i size = r.size;
    // The infill view hides the solid shell, to show the infill inside and its zones.
    const bool only_interior = m_field == Field::Density && r.interior.size() == r.voxels.size();
    auto shown = [&](size_t e) { return ! only_interior || r.interior[e]; };
    std::vector<int> voxel_of(size_t(size.x()) * size_t(size.y()) * size_t(size.z()), -1);
    for (size_t e = 0; e < r.voxels.size(); ++ e)
        if (shown(e))
            voxel_of[r.voxels[e]] = int(e);
    auto solid = [&](int i, int j, int k) {
        return i >= 0 && j >= 0 && k >= 0 && i < size.x() && j < size.y() && k < size.z() &&
               voxel_of[i + size_t(size.x()) * (j + size_t(size.y()) * k)] >= 0;
    };
    const double max_vm = std::max(r.max_von_mises, 1e-9);
    const double max_d  = std::max(r.max_displacement, 1e-12);
    auto value = [&](size_t e) -> float {
        switch (m_field) {
        case Field::Safety:       return r.failure_index[e];    // 1 = breaks
        case Field::Stress:       return float(r.von_mises[e] / max_vm);
        case Field::Displacement: return float(r.displacement[e].norm() / max_d);
        case Field::Density:      return e < r.density.size() ? r.density[e] : 1.f;
        }
        return 0.f;
    };

    std::vector<GLModel::Geometry> geos(COLOR_STEPS);
    for (GLModel::Geometry &g : geos)
        g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    static const int dirs[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    const float h = float(r.h);
    for (size_t e = 0; e < r.voxels.size(); ++ e) {
        if (! shown(e))
            continue;
        const int idx = r.voxels[e];
        const int i = idx % size.x(), j = (idx / size.x()) % size.y(), k = idx / (size.x() * size.y());
        const int step = std::clamp(int(value(e) * (COLOR_STEPS - 1) + 0.5f), 0, COLOR_STEPS - 1);
        GLModel::Geometry &g = geos[step];
        const Vec3f c = (r.origin + r.h * Vec3d(i + 0.5, j + 0.5, k + 0.5)).cast<float>();
        for (const auto &d : dirs) {
            if (solid(i + d[0], j + d[1], k + d[2]))
                continue;
            const Vec3f n { float(d[0]), float(d[1]), float(d[2]) };
            // Two axes of the face.
            const int ax = d[0] != 0 ? 0 : (d[1] != 0 ? 1 : 2);
            Vec3f u = Vec3f::Zero(), v = Vec3f::Zero();
            u[(ax + 1) % 3] = 0.5f * h;
            v[(ax + 2) % 3] = 0.5f * h;
            const Vec3f fc = c + 0.5f * h * n;
            const unsigned int base = unsigned(g.vertices_count());
            g.add_vertex(Vec3f(fc - u - v), n);
            g.add_vertex(Vec3f(fc + u - v), n);
            g.add_vertex(Vec3f(fc + u + v), n);
            g.add_vertex(Vec3f(fc - u + v), n);
            // Counter clockwise seen from outside.
            if (n.dot(u.cross(v)) > 0.f) {
                g.add_triangle(base, base + 1, base + 2);
                g.add_triangle(base, base + 2, base + 3);
            } else {
                g.add_triangle(base, base + 2, base + 1);
                g.add_triangle(base, base + 3, base + 2);
            }
        }
    }
    m_result_models.resize(COLOR_STEPS);
    for (int s = 0; s < COLOR_STEPS; ++ s) {
        if (geos[s].is_empty())
            continue;
        m_result_models[s].init_from(std::move(geos[s]));
        m_result_models[s].set_color(scale_color(float(s) / float(COLOR_STEPS - 1)));
    }
}

void GLGizmoEngineering::on_render()
{
    fetch_result();
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    const bool results = m_show_results && m_result && m_result_object == mo->id();
    show_object(! results);
    const bool lattice_view = results && m_result_is_lattice && m_field == Field::Density && m_lattice && m_lattice_object == mo->id() &&
                              m_lattice->feasible && ! m_lattice->mesh.indices.empty();
    if (lattice_view)
        render_lattice();
    else if (results)
        render_results();
    else
        render_regions();
    render_loads();
}

void GLGizmoEngineering::render_regions()
{
    if (m_regions_dirty)
        update_region_models();
    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr || m_region_models.empty())
        return;
    shader->start_using();
    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-2.f, -2.f));
    for (RegionModel &rm : m_region_models) {
        const Transform3d view_model = camera.get_view_matrix() * rm.trafo;
        shader->set_uniform("view_model_matrix", view_model);
        const Matrix3d view_normal = view_model.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal);
        rm.model.set_color(rm.color);
        rm.model.render();
    }
    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    shader->stop_using();
}

void GLGizmoEngineering::render_results()
{
    if (m_result_models_dirty)
        update_result_models();
    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Transform3d view_model = camera.get_view_matrix();
    shader->set_uniform("view_model_matrix", view_model);
    shader->set_uniform("view_normal_matrix", Matrix3d(view_model.matrix().block(0, 0, 3, 3).inverse().transpose()));
    glsafe(::glEnable(GL_DEPTH_TEST));
    for (GLModel &m : m_result_models)
        if (m.is_initialized())
            m.render();
    shader->stop_using();
}

void GLGizmoEngineering::render_lattice()
{
    if (m_lattice_model_dirty) {
        m_lattice_model.reset();
        m_lattice_model.init_from(m_lattice->mesh);
        m_lattice_model_dirty = false;
    }
    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Transform3d view_model = camera.get_view_matrix();
    shader->set_uniform("view_model_matrix", view_model);
    shader->set_uniform("view_normal_matrix", Matrix3d(view_model.matrix().block(0, 0, 3, 3).inverse().transpose()));
    glsafe(::glEnable(GL_DEPTH_TEST));
    m_lattice_model.set_color({ 0.62f, 0.35f, 0.95f, 1.f });
    m_lattice_model.render();
    shader->stop_using();
}

void GLGizmoEngineering::render_loads()
{
    const ModelObject *mo = model_object();
    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
    if (mo == nullptr || shader == nullptr || mo->engineering.loads.empty())
        return;
    const Transform3d instance_trafo = mo->instances[instance_idx()]->get_transformation().get_matrix();
    const BoundingBoxf3 bbox = mo->instance_bounding_box(size_t(instance_idx()));
    const double scale = std::max(0.5, 0.012 * bbox.size().maxCoeff());

    shader->start_using();
    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    glsafe(::glEnable(GL_DEPTH_TEST));
    auto render_model = [&](GLModel &model, const Transform3d &trafo) {
        const Transform3d view_model = camera.get_view_matrix() * trafo;
        shader->set_uniform("view_model_matrix", view_model);
        shader->set_uniform("view_normal_matrix", Matrix3d(view_model.matrix().block(0, 0, 3, 3).inverse().transpose()));
        model.render();
    };
    for (const EngineeringLoad &load : mo->engineering.loads) {
        Vec3d point;
        if (load.type == EngineeringLoad::Type::Point) {
            if (load.volume < 0 || load.volume >= int(mo->volumes.size()))
                continue;
            point = instance_trafo * mo->volumes[load.volume]->get_matrix() * load.point;
        } else {
            // Centroid of the faces.
            if (load.faces.volume < 0 || load.faces.volume >= int(mo->volumes.size()) || load.faces.triangles.empty())
                continue;
            const ModelVolume &v = *mo->volumes[load.faces.volume];
            const indexed_triangle_set &its = v.mesh().its;
            Vec3d sum = Vec3d::Zero();
            int n = 0;
            for (int t : load.faces.triangles)
                if (t >= 0 && t < int(its.indices.size())) {
                    for (int k = 0; k < 3; ++ k)
                        sum += its.vertices[its.indices[t][k]].cast<double>();
                    n += 3;
                }
            if (n == 0)
                continue;
            point = instance_trafo * v.get_matrix() * (sum / n);
        }
        m_sphere.set_color(LOAD_COLOR);
        render_model(m_sphere, Geometry::translation_transform(point) * Geometry::scale_transform(scale * 0.8));
        if (load.force.norm() > 0.) {
            // The arrow ends at the point and pushes in the direction of the force.
            Eigen::Quaterniond q;
            q.setFromTwoVectors(Vec3d::UnitZ(), load.force.normalized());
            const Transform3d trafo = Geometry::translation_transform(point) * Transform3d(q.toRotationMatrix()) *
                                      Geometry::scale_transform(scale) * Geometry::translation_transform(-12. * Vec3d::UnitZ());
            m_arrow.set_color(LOAD_COLOR);
            render_model(m_arrow, trafo);
        }
    }
    shader->stop_using();
}

// ---------------------------------------------------------------------------------------------------------------------
// Panel.

void GLGizmoEngineering::render_legend(float width)
{
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  hh = ImGui::GetFrameHeight() * 0.6f;
    const int    n = 32;
    for (int i = 0; i < n; ++ i) {
        const ColorRGBA c = scale_color(float(i) / float(n - 1));
        draw->AddRectFilled(ImVec2(p.x + width * i / n, p.y), ImVec2(p.x + width * (i + 1) / n + 1.f, p.y + hh),
                            ImGui::GetColorU32(to_imvec(c)));
    }
    ImGui::Dummy(ImVec2(width, hh));
    const Fea::Result &r = *m_result;
    std::string lo, hi;
    switch (m_field) {
    case Field::Safety:       lo = "0"; hi = _u8L("1 = breaks"); break;
    case Field::Stress:       lo = "0 MPa"; hi = GUI::format("%.2f MPa", r.max_von_mises); break;
    case Field::Displacement: lo = "0 mm"; hi = GUI::format("%.3f mm", r.max_displacement); break;
    case Field::Density:      lo = _u8L("0 % (empty)"); hi = _u8L("100 % (solid)"); break;
    }
    if (m_field == Field::Density) {
        ImGuiPureWrap::text(lo);
        ImGui::SameLine(std::max(0.f, width - ImGuiPureWrap::calc_text_size(hi).x + ImGui::GetStyle().WindowPadding.x));
        ImGuiPureWrap::text(hi);
        ImGuiPureWrap::text_wrapped(_u8L("The solid walls are hidden to show the infill inside."), width);
        return;
    }
    ImGuiPureWrap::text(lo);
    ImGui::SameLine(std::max(0.f, width - ImGuiPureWrap::calc_text_size(hi).x + ImGui::GetStyle().WindowPadding.x));
    ImGuiPureWrap::text(hi);
}

void GLGizmoEngineering::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = model_object();
    if (mo == nullptr)
        return;
    EngineeringSetup &eng = mo->engineering;
    if (m_running)
        m_imgui->set_requires_extra_frame();

    // The panel never grows beyond the canvas: it scrolls.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, std::max(200.f, bottom_limit - 10.f)));
    ImGuiPureWrap::begin(get_name(false), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    const float win_h = ImGui::GetWindowHeight();
    ImGui::SetWindowPos(ImVec2(x, std::min(y, bottom_limit - win_h)), ImGuiCond_Always);
    const float width = 22.f * ImGui::GetFontSize();
    ImGui::PushItemWidth(10.f * ImGui::GetFontSize());

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, mo->name);

    // Material.
    const std::string filament = filament_type(*mo);
    const Fea::Material *from_filament = Fea::material_for_filament_type(filament);
    std::vector<std::string> options;
    options.push_back(GUI::format(_u8L("Filament (%1%)"), from_filament ? from_filament->name : filament));
    int selection = 0;
    for (size_t i = 0; i < Fea::materials().size(); ++ i) {
        options.push_back(Fea::materials()[i].name);
        if (Fea::materials()[i].key == eng.material)
            selection = int(i + 1);
    }
    if (ImGuiPureWrap::combo(_u8L("Material"), options, selection, 0, 9.f * ImGui::GetFontSize(), 10.f * ImGui::GetFontSize())) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Change material"));
        eng.material = selection == 0 ? std::string() : Fea::materials()[selection - 1].key;
        m_result_stale = true;
    }
    const Fea::Material *material = eng.material.empty() ? from_filament : Fea::find_material(eng.material);
    if (material == nullptr)
        ImGuiPureWrap::text_wrapped(_u8L("The filament type has no material in the table: choose one."), width);
    else if (! material->note.empty())
        ImGuiPureWrap::text_wrapped(material->note, width);

    // Working conditions.
    auto edit_value = [&](const std::string &label, float &buffer, double &target, float lo, float hi, const std::string &snapshot_name) {
        ImGuiPureWrap::text(label);
        ImGui::SameLine(9.f * ImGui::GetFontSize());
        ImGui::InputFloat(("##" + label).c_str(), &buffer, 0.f, 0.f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            buffer = std::clamp(buffer, lo, hi);
            if (double(buffer) != target) {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), snapshot_name);
                target = buffer;
                m_result_stale = true;
            }
        } else if (! ImGui::IsItemActive())
            buffer = float(target);
    };
    edit_value(_u8L("Temperature [°C]"), m_edit_temperature, eng.temperature, -40.f, 400.f, _u8L("Change working temperature"));
    if (material && Fea::out_of_temperature(*material, eng.temperature))
        ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::OutOfTemperature)),
            GUI::format(_u8L("%1% is not structural above %2% °C"), material->name, material->max_service_temperature));
    edit_value(_u8L("Safety factor"), m_edit_safety, eng.safety_factor, 1.f, 20.f, _u8L("Change safety factor"));
    if (ImGuiPureWrap::checkbox(_u8L("Printed part (walls and infill of the profile)"), m_as_printed))
        m_result_stale = true;
    {
        std::vector<std::string> qualities = { _u8L("Fast"), _u8L("Normal"), _u8L("High (voxel 2 line widths)"),
                                               _u8L("Ultra (voxel 1 line width)") };
        int q = int(m_quality);
        if (ImGuiPureWrap::combo(_u8L("Resolution"), qualities, q, 0, 9.f * ImGui::GetFontSize(), 12.f * ImGui::GetFontSize())) {
            m_quality = Quality(q);
            wxGetApp().app_config->set("tisma_fea_quality", std::to_string(q));
            m_result_stale = true;
        }
        double h = 0., voxels = 0.;
        estimate_quality(*mo, h, voxels);
        if (voxels > MAX_VOXELS_SINGLE)
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT,
                GUI::format(_u8L("About %1% voxels of %2$.2f mm: too many, limited to %3%."), size_t(voxels), h, size_t(MAX_VOXELS_SINGLE)));
        else
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_GREY_LIGHT,
                GUI::format(_u8L("About %1% voxels of %2$.2f mm."), size_t(voxels), h));
        if (voxels > MAX_VOXELS_MANY)
            ImGuiPureWrap::text_wrapped(GUI::format(_u8L("The infill search, the reinforcement, the lattice and the orientation use at most %1% voxels."),
                                                    size_t(MAX_VOXELS_MANY)), width);
    }

    // Tools.
    ImGui::Separator();
    ImGuiPureWrap::text(_u8L("Click on the part:"));
    if (ImGuiPureWrap::radio_button(_u8L("Rotate the view"), m_tool == Tool::None))       m_tool = Tool::None;
    if (ImGuiPureWrap::radio_button(_u8L("Fixed face"), m_tool == Tool::Fixture))         m_tool = Tool::Fixture;
    if (ImGuiPureWrap::radio_button(_u8L("Load on a face"), m_tool == Tool::FaceLoad))    m_tool = Tool::FaceLoad;
    if (ImGuiPureWrap::radio_button(_u8L("Load on a point"), m_tool == Tool::PointLoad))  m_tool = Tool::PointLoad;
    if (m_tool == Tool::FaceLoad || m_tool == Tool::PointLoad) {
        ImGuiPureWrap::text(_u8L("Force X, Y, Z [N]"));
        ImGui::InputFloat3("##force", m_new_force, "%.1f");
        ImGuiPureWrap::text(_u8L("Max. deformation [mm] / [%]"));
        ImGui::PushItemWidth(4.8f * ImGui::GetFontSize());
        ImGui::InputFloat("##limit_mm", &m_new_limit_mm, 0.f, 0.f, "%.2f");
        ImGui::SameLine();
        ImGui::InputFloat("##limit_pct", &m_new_limit_percent, 0.f, 0.f, "%.2f");
        ImGui::PopItemWidth();
        if (m_tool == Tool::PointLoad) {
            ImGuiPureWrap::text(_u8L("Load radius [mm]"));
            ImGui::SameLine(9.f * ImGui::GetFontSize());
            ImGui::InputFloat("##radius", &m_new_radius, 0.f, 0.f, "%.1f");
        }
        ImGuiPureWrap::text_wrapped(_u8L("0 = no limit. Z is up, as on the bed: -100 in Z pushes down with 100 N (about 10 kg)."), width);
    }

    // Supports and loads.
    ImGui::Separator();
    ImGuiPureWrap::text(GUI::format(_u8L("Fixed faces: %1%"), eng.fixtures.size()));
    for (size_t i = 0; i < eng.fixtures.size(); ++ i) {
        ImGui::PushID(int(i));
        ImGuiPureWrap::text(GUI::format("  %1% (%2%)", GUI::format(_u8L("Face %1%"), i + 1), eng.fixtures[i].triangles.size()));
        ImGui::SameLine();
        if (ImGuiPureWrap::button(_u8L("Remove"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove fixed face"));
            eng.fixtures.erase(eng.fixtures.begin() + i);
            m_regions_dirty = m_result_stale = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGuiPureWrap::text(GUI::format(_u8L("Loads: %1%"), eng.loads.size()));
    for (size_t i = 0; i < eng.loads.size(); ++ i) {
        const EngineeringLoad &l = eng.loads[i];
        ImGui::PushID(int(1000 + i));
        std::string text = GUI::format("  %1%: (%2%, %3%, %4%) N", l.name, l.force.x(), l.force.y(), l.force.z());
        if (l.type == EngineeringLoad::Type::Point && l.radius > 0.)
            text += GUI::format(_u8L(", r %1% mm"), l.radius);
        if (l.max_displacement > 0.)
            text += GUI::format(_u8L(", max %1% mm"), l.max_displacement);
        if (l.max_displacement_percent > 0.)
            text += GUI::format(_u8L(", max %1% %%"), l.max_displacement_percent);
        if (m_result && ! m_result_stale && i < m_result->load_displacement.size())
            text += GUI::format(_u8L(" -> moves %1$.3f mm"), m_result->load_displacement[i]);
        ImGuiPureWrap::text(text);
        ImGui::SameLine();
        if (ImGuiPureWrap::button(_u8L("Remove"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove load"));
            eng.loads.erase(eng.loads.begin() + i);
            m_regions_dirty = m_result_stale = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    // Quick cleanup: the last load, or everything to start again.
    if (! eng.loads.empty()) {
        if (ImGuiPureWrap::button(_u8L("Remove last load"), _u8L("Removes the last load added (Ctrl+Z restores it)"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove load"));
            eng.loads.pop_back();
            m_regions_dirty = m_result_stale = true;
        }
        ImGui::SameLine();
    }
    if (! eng.loads.empty() || ! eng.fixtures.empty()) {
        if (ImGuiPureWrap::button(_u8L("Clear all"), _u8L("Removes all the fixed faces and loads of the part (Ctrl+Z restores them)"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Clear supports and loads"));
            eng.loads.clear();
            eng.fixtures.clear();
            m_regions_dirty = m_result_stale = true;
            m_result.reset();
            m_result_models_dirty = true;
            m_orient.reset();
        }
    }

    // Analysis.
    ImGui::Separator();
    if (m_running) {
        ImGui::ProgressBar(float(m_progress.load()) / 100.f, ImVec2(width * 0.7f, 0.f));
        ImGui::SameLine();
        if (ImGuiPureWrap::button(_u8L("Cancel")))
            cancel_analysis();
    } else {
        const bool can_run = ! eng.fixtures.empty() && ! eng.loads.empty();
        if (ImGuiPureWrap::button(_u8L("Analyze"), _u8L("Run the structural analysis")) && can_run)
            start_analysis();
        if (! can_run) {
            ImGui::SameLine();
            ImGuiPureWrap::text(_u8L("Add a fixed face and a load."));
        }
    }
    if (! m_error.empty())
        ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::OutOfLoad)), m_error);

    // Results.
    if (m_result && m_result_object == mo->id()) {
        const Fea::Result &r = *m_result;
        ImGui::Separator();
        if (m_result_stale)
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_GREY_LIGHT, _u8L("The setup changed: analyze again."));
        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(verdict_color(r.verdict)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(verdict_text(r.verdict).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGuiPureWrap::text(GUI::format(_u8L("Material: %1% at %2% °C (stiffness and strength x %3$.2f)"),
                                   m_result_material, m_result_temperature, r.temperature_factor));
        if (m_result_print_temperature > 0.)
            ImGuiPureWrap::text(GUI::format(_u8L("Layer adhesion with the nozzle at %1% °C: x %2$.2f"),
                                            int(std::round(m_result_print_temperature)), r.layer_adhesion_factor));
        else
            ImGuiPureWrap::text(_u8L("Nozzle temperature unknown: nominal layer adhesion."));
        ImGuiPureWrap::text(GUI::format(_u8L("Safety factor: %1$.2f (required %2$.1f)"), r.safety_factor, m_result_safety));
        ImGuiPureWrap::text(GUI::format(_u8L("Max. displacement: %1$.3f mm"), r.max_displacement));
        ImGuiPureWrap::text(GUI::format(_u8L("Max. stress (von Mises): %1$.2f MPa"), r.max_von_mises));
        ImGuiPureWrap::text(GUI::format(_u8L("Estimated mass: %1$.1f g (solid part: %2$.1f g)"), r.mass, r.solid_mass));
        if (! r.alternatives.empty()) {
            std::string alt;
            for (size_t i = 0; i < r.alternatives.size() && i < 5; ++ i)
                alt += (i ? ", " : "") + GUI::format("%1% (%2$.1f)", r.alternatives[i].first, r.alternatives[i].second);
            ImGuiPureWrap::text_wrapped(GUI::format(_u8L("Materials that would hold (safety factor): %1%"), alt), width);
        }
        ImGuiPureWrap::checkbox(_u8L("Show results on the part"), m_show_results);
        if (m_show_results) {
            std::vector<std::string> fields = { _u8L("Safety (stress / strength)"), _u8L("Stress (von Mises)"), _u8L("Displacement"), _u8L("Infill") };
            int f = int(m_field);
            if (ImGuiPureWrap::combo(_u8L("Show"), fields, f, 0, 4.f * ImGui::GetFontSize(), 14.f * ImGui::GetFontSize())) {
                m_field = Field(f);
                m_result_models_dirty = true;
            }
            render_legend(width);
        }
        ImGuiPureWrap::text_wrapped(r.density.empty() || r.mass >= r.solid_mass * 0.999 ?
            GUI::format(_u8L("Solid part, %1$.2f mm voxels."), r.h) :
            GUI::format(_u8L("Walls and homogenized infill, %1$.2f mm voxels."), r.h), width);
        if (! m_quality_note.empty())
            ImGuiPureWrap::text_wrapped(m_quality_note, width);
    }

    // Lightest infill (phase 6).
    ImGui::Separator();
    if (m_open_infill_section) {
        ImGui::SetNextItemOpen(true);
        m_open_infill_section = false;
    }
    if (ImGui::CollapsingHeader(_u8L("Lightest infill").c_str())) {
        ImGuiPureWrap::text_wrapped(_u8L("Searches the lowest infill which meets the safety factor and the deformation limits of the loads, "
                                         "with the walls and the infill pattern of the profile."), width);
        ImGuiPureWrap::checkbox(_u8L("Also by zones (more infill where the stresses are high)"), m_opt_zones);
        const bool can_run = ! m_running && ! eng.fixtures.empty() && ! eng.loads.empty();
        if (ImGuiPureWrap::button(_u8L("Find lightest infill")) && can_run)
            start_optimization();
        if (m_opt && m_opt_object == mo->id()) {
            const Fea::OptimizeResult &o = *m_opt;
            if (! o.feasible) {
                ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::OutOfLoad)),
                    _u8L("Not even 100 % infill meets the requirements:"));
                ImGuiPureWrap::text_wrapped(verdict_text(o.uniform.verdict), width);
                ImGuiPureWrap::text_wrapped(_u8L("Add perimeters, choose another material or reinforce the shape."), width);
            } else {
                if (m_current_infill_mass > 0.)
                    ImGuiPureWrap::text(GUI::format(_u8L("Current infill: %1$.1f g"), m_current_infill_mass.load()));
                ImGuiPureWrap::text(GUI::format(_u8L("Uniform: %1% %% -> %2$.1f g, safety factor %3$.2f"),
                    int(std::round(o.uniform_density * 100.)), o.uniform.mass, o.uniform.safety_factor));
                if (ImGuiPureWrap::button(_u8L("Apply uniform")))
                    apply_optimization(false);
                if (o.zones_found) {
                    std::string zones;
                    for (const Fea::InfillZone &z : o.zones)
                        zones += (zones.empty() ? "" : ", ") + std::to_string(int(std::round(z.density * 100.))) + " %";
                    ImGuiPureWrap::text_wrapped(GUI::format(_u8L("By zones: %1% %% + zones of %2% -> %3$.1f g (%4$.0f %% lighter), safety factor %5$.2f"),
                        int(std::round(o.base_density * 100.)), zones, o.zoned.mass,
                        100. * (1. - o.zoned.mass / std::max(o.uniform.mass, 1e-9)), o.zoned.safety_factor), width);
                    if (ImGuiPureWrap::button(_u8L("Apply by zones")))
                        apply_optimization(true);
                } else if (m_opt_zones)
                    ImGuiPureWrap::text_wrapped(_u8L("The zones would not save material: the uniform infill is the lightest."), width);
                ImGuiPureWrap::text_wrapped(GUI::format(_u8L("%1% analyses. Homogenized infill model: check the result with a printed test part."), o.analyses), width);
            }
        }
        if (! m_applied_note.empty() && m_applied_object == mo->id())
            ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::Holds)), m_applied_note);
    }

    auto input_float = [&](const std::string &label, float &value, const char *fmt) {
        ImGuiPureWrap::text(label);
        ImGui::SameLine(14.f * ImGui::GetFontSize());
        ImGui::PushItemWidth(5.f * ImGui::GetFontSize());
        ImGui::InputFloat(("##" + label).c_str(), &value, 0.f, 0.f, fmt);
        ImGui::PopItemWidth();
    };
    const bool can_run = ! m_running && ! eng.fixtures.empty() && ! eng.loads.empty();

    // Local reinforcement (phase 6).
    if (ImGui::CollapsingHeader(_u8L("Local reinforcement").c_str())) {
        ImGuiPureWrap::text_wrapped(_u8L("More perimeters and infill around the fixed faces and the loads, and the rest of the part as light as possible."), width);
        input_float(_u8L("Radius [mm] (0 = automatic)"), m_reinf_radius, "%.1f");
        ImGuiPureWrap::text(_u8L("Extra perimeters"));
        ImGui::SameLine(14.f * ImGui::GetFontSize());
        ImGui::PushItemWidth(5.f * ImGui::GetFontSize());
        ImGui::InputInt("##extra_perimeters", &m_reinf_perimeters, 0, 0);
        ImGui::PopItemWidth();
        input_float(_u8L("Infill of the reinforcement [%]"), m_reinf_density, "%.0f");
        if (ImGuiPureWrap::button(_u8L("Calculate reinforcement")) && can_run)
            start_reinforcement();
        if (m_reinf && m_reinf_object == mo->id()) {
            const Fea::ReinforcementResult &r = *m_reinf;
            if (r.without.feasible)
                ImGuiPureWrap::text(GUI::format(_u8L("Without reinforcement: %1% %% -> %2$.1f g"),
                    int(std::round(r.without.uniform_density * 100.)), r.without.uniform.mass));
            else
                ImGuiPureWrap::text_wrapped(_u8L("Without reinforcement: not even 100 % infill meets the requirements."), width);
            if (r.with.feasible) {
                ImGuiPureWrap::text_wrapped(GUI::format(_u8L("With reinforcement: %1% %% + reinforcement -> %2$.1f g, safety factor %3$.2f"),
                    int(std::round(r.with.uniform_density * 100.)), r.with.uniform.mass, r.with.uniform.safety_factor), width);
                if (r.without.feasible && r.with.uniform.mass >= r.without.uniform.mass * 0.99)
                    ImGuiPureWrap::text_wrapped(_u8L("The reinforcement does not save material here: the stresses are not concentrated at the supports and loads."), width);
                if (ImGuiPureWrap::button(_u8L("Apply reinforcement")))
                    apply_reinforcement();
            } else
                ImGuiPureWrap::text_wrapped(_u8L("With reinforcement: not even 100 % infill meets the requirements."), width);
        }
    }

    // 3D lattice (phase 6).
    if (ImGui::CollapsingHeader(_u8L("3D lattice").c_str())) {
        ImGuiPureWrap::text_wrapped(_u8L("Struts instead of the infill, joined to the walls: vertical and at 45 degrees, printable without supports. "
                                         "Their thickness follows the stresses."), width);
        input_float(_u8L("Cell [mm]"), m_lattice_cell, "%.1f");
        input_float(_u8L("Min. strut diameter [mm]"), m_lattice_min_d, "%.2f");
        input_float(_u8L("Max. strut diameter [mm]"), m_lattice_max_d, "%.2f");
        if (ImGuiPureWrap::button(_u8L("Generate lattice")) && can_run)
            start_lattice();
        if (m_lattice && m_lattice_object == mo->id()) {
            const Fea::LatticeResult &l = *m_lattice;
            if (! l.feasible)
                ImGuiPureWrap::text_wrapped(_u8L("Not even the thickest struts meet the requirements: a smaller cell, more walls or another material."), width);
            else {
                ImGuiPureWrap::text(GUI::format(_u8L("Uniform struts: %1$.2f mm -> %2$.1f g, safety factor %3$.2f"),
                    l.uniform_diameter, l.uniform.mass, l.uniform.safety_factor));
                if (l.variable_found)
                    ImGuiPureWrap::text_wrapped(GUI::format(_u8L("Variable struts: %1$.2f-%2$.2f mm -> %3$.1f g (%4$.0f %% lighter), safety factor %5$.2f"),
                        l.min_used_diameter, l.max_used_diameter, l.variable.mass,
                        100. * (1. - l.variable.mass / std::max(l.uniform.mass, 1e-9)), l.variable.safety_factor), width);
                if (ImGuiPureWrap::button(_u8L("Apply lattice")))
                    apply_lattice();
                ImGuiPureWrap::text_wrapped(GUI::format(_u8L("The infill of the part becomes 0 %%. %1% analyses; homogenized lattice model: check the first print."), l.analyses), width);
            }
        }
    }

    // Orientation of the print: the layers hold less across than along.
    if (ImGui::CollapsingHeader(_u8L("Print orientation").c_str())) {
        ImGuiPureWrap::text_wrapped(_u8L("The layers are weaker across than along: compares the part printed as it is placed with the part "
                                         "laid on its X and Y axes, with the same loads, and recommends the strongest."), width);
        if (ImGuiPureWrap::button(_u8L("Recommend orientation")) && can_run)
            start_orientation();
        if (m_orient && m_orient_object == mo->id()) {
            const Fea::OrientationResult &o = *m_orient;
            for (size_t i = 0; i < o.candidates.size(); ++ i) {
                const Fea::OrientationCandidate &c = o.candidates[i];
                const std::string name = i == 0 ? _u8L("As placed (layers along Z)") :
                                         GUI::format(_u8L("%1% axis vertical"), c.axis);
                const std::string text = GUI::format(_u8L("%1%: safety factor %2$.2f, displacement %3$.3f mm"),
                                                     name, c.result.safety_factor, c.result.max_displacement);
                if (i == o.best)
                    ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::Holds)), "> " + text);
                else
                    ImGuiPureWrap::text("  " + text);
            }
            if (o.best == 0 || o.best >= o.candidates.size())
                ImGuiPureWrap::text_wrapped(_u8L("The current orientation is already the strongest for these loads."), width);
            else {
                const double gain = o.candidates[o.best].result.safety_factor / std::max(o.candidates.front().result.safety_factor, 1e-9);
                ImGuiPureWrap::text_wrapped(GUI::format(_u8L("Recommended: print with the %1% axis vertical (safety factor x %2$.2f)."),
                                                        o.candidates[o.best].axis, gain), width);
                if (ImGuiPureWrap::button(_u8L("Rotate the part")))
                    rotate_to_recommended();
            }
            ImGuiPureWrap::text_wrapped(_u8L("The overhangs and the supports of the new orientation are not checked."), width);
        }
    }

    ImGui::PopItemWidth();
    ImGuiPureWrap::end();
}

} // namespace GUI
} // namespace Slic3r
