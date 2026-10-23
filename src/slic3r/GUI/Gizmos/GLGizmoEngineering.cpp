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
#include <libslic3r/BeltPrinter.hpp>
#include <libslic3r/SLA/InteriorInfill.hpp>
#include <tisma_fea/Materials.hpp>
#include <tisma_fea/ModelSetup.hpp>
#include <tisma_fea/Optimize.hpp>
#include <tisma_fea/Ribs.hpp>
#include <tisma_fea/Serialize.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/FeaMaterialsDialog.hpp"
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
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/TismaCloudDialog.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

namespace Slic3r {
namespace GUI {

namespace {

const ColorRGBA FIXTURE_COLOR   { 0.20f, 0.55f, 1.00f, 0.85f };
const ColorRGBA FACE_LOAD_COLOR { 1.00f, 0.55f, 0.10f, 0.85f };
const ColorRGBA LOAD_COLOR      { 0.95f, 0.25f, 0.20f, 1.00f };
const ColorRGBA LIMIT_COLOR     { 0.20f, 0.85f, 0.70f, 0.85f };
constexpr int   COLOR_STEPS = 16;
// Where the material breaks (stress above the strength): black, after the red of the scale, which only means that
// a limit is reached (the safety factor 1, or the allowed displacement).
const ColorRGBA BROKEN_COLOR    { 0.06f, 0.06f, 0.07f, 1.00f };

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

// Building blocks of the panels (Órbita Pro): sections, rows of values, big figures, segmented buttons.
namespace ui {
const ImVec4 MUTED        { 0.541f, 0.541f, 0.588f, 1.f };
const ImVec4 CARD         { 0.153f, 0.153f, 0.176f, 1.f };
const ImVec4 CARD_HOVER   { 0.200f, 0.200f, 0.227f, 1.f };
const ImVec4 LINE         { 0.204f, 0.204f, 0.235f, 1.f };
const ImVec4 ACCENT       { 0.478f, 0.141f, 0.788f, 1.f };
const ImVec4 ACCENT_HOVER { 0.557f, 0.204f, 0.875f, 1.f };
const ImVec4 WHITE        { 1.f, 1.f, 1.f, 1.f };

void muted(const std::string &text, float width = 0.f)
{
    ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
    if (width > 0.f) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
    } else
        ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

// Title of a section with a line above it, and an optional note at the right.
void section(const std::string &title, float width, const std::string &right = {}, const ImVec4 &right_color = MUTED)
{
    const float fs = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0.f, 0.15f * fs));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), ImGui::GetColorU32(LINE));
    ImGui::Dummy(ImVec2(0.f, 0.2f * fs));
    const float x0 = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(title.c_str());
    if (! right.empty()) {
        ImGui::SameLine(std::max(x0 + ImGui::CalcTextSize(title.c_str()).x + fs, x0 + width - ImGui::CalcTextSize(right.c_str()).x));
        ImGui::PushStyleColor(ImGuiCol_Text, right_color);
        ImGui::TextUnformatted(right.c_str());
        ImGui::PopStyleColor();
    }
}

// Label at the left (muted) and value at the right.
void row(const std::string &label, const std::string &value, float width, const ImVec4 *color = nullptr)
{
    const float x0 = ImGui::GetCursorPosX();
    muted(label);
    ImGui::SameLine(std::max(x0 + ImGui::CalcTextSize(label.c_str()).x + ImGui::GetFontSize(), x0 + width - ImGui::CalcTextSize(value.c_str()).x));
    if (color)
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    ImGui::TextUnformatted(value.c_str());
    if (color)
        ImGui::PopStyleColor();
}

struct Metric {
    std::string label;
    std::string value;
    ImVec4      color { 0.925f, 0.925f, 0.941f, 1.f };
};

// The main figures of a result in cards, side by side, in a larger font.
void metrics(const std::vector<Metric> &items, float width)
{
    if (items.empty())
        return;
    ImDrawList  *dl  = ImGui::GetWindowDrawList();
    const ImVec2 p0  = ImGui::GetCursorScreenPos();
    const float  fs  = ImGui::GetFontSize();
    ImFont      *big = ImGuiWrapper::large_font();
    const float  big_size = big ? big->FontSize : fs;
    if (big == nullptr)
        big = ImGui::GetFont();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w   = (width - gap * float(items.size() - 1)) / float(items.size());
    const float pad = 0.55f * fs;
    const float h   = 2.f * pad + 1.2f * fs + big_size;
    for (size_t i = 0; i < items.size(); ++ i) {
        const ImVec2 a(p0.x + float(i) * (w + gap), p0.y);
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), ImGui::GetColorU32(CARD), 4.f);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + pad, a.y + pad), ImGui::GetColorU32(MUTED), items[i].label.c_str());
        dl->AddText(big, big_size, ImVec2(a.x + pad, a.y + pad + 1.2f * fs), ImGui::GetColorU32(items[i].color), items[i].value.c_str());
    }
    ImGui::Dummy(ImVec2(width, h));
}

// Buttons side by side filling the width, the selected one in the brand color. selected = -1: none.
bool segmented(const char *id, const std::vector<std::string> &options, int &selected, float width)
{
    bool changed = false;
    const float gap = 2.f;
    const float w   = (width - gap * float(options.size() - 1)) / float(options.size());
    ImGui::PushID(id);
    for (size_t i = 0; i < options.size(); ++ i) {
        if (i > 0)
            ImGui::SameLine(0.f, gap);
        const bool on = int(i) == selected;
        ImGui::PushStyleColor(ImGuiCol_Button,        on ? ACCENT : CARD);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ACCENT_HOVER : CARD_HOVER);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ACCENT_HOVER);
        ImGui::PushStyleColor(ImGuiCol_Text,          on ? WHITE : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::PushID(int(i));
        if (ImGui::Button(options[i].c_str(), ImVec2(w, 0.f)) && ! on) {
            selected = int(i);
            changed  = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(4);
    }
    ImGui::PopID();
    return changed;
}

// The main action of a panel: a full width button in the brand color.
bool primary_button(const std::string &label, float width, bool enabled = true)
{
    if (! enabled)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, enabled ? ACCENT_HOVER : ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  enabled ? ACCENT_HOVER : ACCENT);
    ImGui::PushStyleColor(ImGuiCol_Text,          WHITE);
    const bool clicked = ImGui::Button(label.c_str(), ImVec2(width, 1.5f * ImGui::GetFrameHeight()));
    ImGui::PopStyleColor(4);
    if (! enabled)
        ImGui::PopStyleVar();
    return clicked && enabled;
}

// Small "x" at the right end of the line, to remove an item of a list.
bool remove_button(const char *id, float width)
{
    const float bw = ImGui::GetFrameHeight();
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMin().x + width - bw));
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, CARD);
    const bool clicked = ImGui::Button("x", ImVec2(bw, 0.f));
    ImGui::PopStyleColor();
    ImGui::PopID();
    return clicked;
}
} // namespace ui

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
    case Fea::Verdict::TooFlexible:      return _u8L("Too flexible: a load or a limited zone moves more than its allowed displacement");
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
    std::string error;
    if (! load_fea_materials(error))
        BOOST_LOG_TRIVIAL(error) << "Materials of the structural analysis: " << error;
    return true;
}

std::string GLGizmoEngineering::on_get_name() const
{
    return _u8L("Engineering");
}

bool GLGizmoEngineering::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    const PrinterTechnology pt = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
    return (pt == ptFFF || pt == ptSLA) && selection.is_single_full_instance();
}

bool GLGizmoEngineering::resin()
{
    return wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA;
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
        m_cloud = wxGetApp().app_config->get("tisma_fea_cloud") == "1";
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
    } else if (m_tool == Tool::Limit) {
        EngineeringLimit limit;
        pick_face(volume, volume_idx, facet, limit.faces);
        // Clicking a limited face again removes its limit.
        for (auto it = eng.limits.begin(); it != eng.limits.end(); ++ it)
            if (it->faces.volume == limit.faces.volume && it->faces.triangles == limit.faces.triangles) {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove displacement limit"));
                eng.limits.erase(it);
                m_regions_dirty = m_result_stale = true;
                return;
            }
        limit.max_displacement         = std::max(0.f, m_new_zone_mm);
        limit.max_displacement_percent = std::max(0.f, m_new_zone_percent);
        int n = 1;
        for (const EngineeringLimit &l : eng.limits)
            n = std::max(n, std::atoi(l.name.substr(l.name.find_last_of(' ') + 1).c_str()) + 1);
        limit.name = GUI::format(_u8L("Zone %1%"), n);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Add displacement limit"));
        eng.limits.emplace_back(std::move(limit));
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
    if (resin()) {
        // The name of the resin profile (Fea::material_for_resin() picks the tough resin for tough and ABS-like ones).
        const std::string name = wxGetApp().preset_bundle->sla_materials.get_selected_preset_name();
        return name.empty() ? std::string("Resin") : name;
    }
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
    if (m_cloud) {
        start_cloud("analyze", ! m_as_printed, true, false);
        return;
    }
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
    if (m_cloud) {
        m_opt_ribs_tried = m_opt_ribs;
        start_cloud("lightest-infill", false, m_opt_zones, m_opt_ribs);
        return;
    }
    m_cancel   = false;
    m_running  = true;
    m_progress = 0;
    Fea::OptimizeOptions options;
    options.zones = m_opt_zones;
    options.ribs  = m_opt_ribs;
    m_opt_ribs_tried = m_opt_ribs;
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
    // The ribs the search used (the densities count on them) first: the infill zones are added after them and keep
    // their density, the ribs only set the direction.
    const size_t ribs     = Fea::apply_ribs(*mo, size_t(instance_idx()), opt.ribs);
    const size_t added    = Fea::apply_infill(*mo, size_t(instance_idx()), density, by_zones ? opt.zones : std::vector<Fea::InfillZone>());
    changed_object(*mo);
    // The infill is a setting of the object and modifiers: it is seen in the object list and after slicing again.
    m_applied_note = added > 0 ?
        GUI::format(_u8L("Applied: infill %1% %% in \"%2%\" and %3% modifiers \"%4%\" with more infill. Slice again to see it in the preview."),
                    int(std::round(density * 100.)), mo->name, added, Fea::INFILL_ZONE_NAME) :
        GUI::format(_u8L("Applied: infill %1% %% in \"%2%\". Slice again to see it in the preview."),
                    int(std::round(density * 100.)), mo->name);
    if (ribs > 0)
        m_applied_note += "\n" + GUI::format(_u8L("%1% modifiers \"%2%\" lay the infill lines along the stresses."), ribs, Fea::RIB_ZONE_NAME);
    m_applied_object = mo->id();
    notify(m_applied_note);
    m_regions_dirty = true;
    m_opt_object = mo->id();
}

void GLGizmoEngineering::start_skin_core()
{
    ModelObject *mo = model_object();
    if (mo == nullptr || m_running)
        return;
    // Skin: automatic non-planar layers (top faces that follow the surface, fewer supports). The analysis of the core
    // reads the print settings, so it is done with these layers.
    bool skin_changed = false;
    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
        DynamicPrintConfig new_conf = *tab->get_config();
        if (new_conf.option<ConfigOptionEnum<NonPlanarMode>>("nonplanar_mode")->value != NonPlanarMode::Automatic) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Skin + core"));
            new_conf.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonPlanarMode>(NonPlanarMode::Automatic));
            tab->load_config(new_conf);
            skin_changed = true;
        }
    }
    const std::string skin = skin_changed ?
        _u8L("Skin: Print Settings > Non-planar is now Automatic (top faces that follow the surface, fewer supports).") :
        _u8L("Skin: Print Settings > Non-planar was already Automatic.");
    // Core: the lightest infill by zones with ribs along the stresses, which needs the supports and the loads.
    if (mo->engineering.fixtures.empty() || mo->engineering.loads.empty()) {
        m_applied_note   = skin + "\n" + _u8L("Core: add the fixed faces and the loads of the part to find the lightest infill with ribs.");
        m_applied_object = mo->id();
        notify(m_applied_note);
        return;
    }
    m_opt_zones = true;
    m_opt_ribs  = true;
    m_skin_core_pending = true;
    m_skin_core_note    = skin;
    m_open_infill_section = true;
    m_applied_note   = skin + "\n" + _u8L("Core: searching the lightest infill with ribs along the stresses...");
    m_applied_object = mo->id();
    start_optimization();
    if (! m_running)
        m_skin_core_pending = false;
}

bool GLGizmoEngineering::prepare_input(Fea::ModelAnalysisInput &input, JobSize size)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || m_running)
        return false;
    if (m_thread.joinable())
        m_thread.join();
    std::string error;
    // The graphics card for the multigrid (Preferences > General).
    Fea::set_gpu_enabled(wxGetApp().app_config->get("tisma_fea_gpu") != "0");
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
    m_result_disp_limit  = 0.;
    auto add_limit = [this](double l) { if (l > 0.) m_result_disp_limit = m_result_disp_limit > 0. ? std::min(m_result_disp_limit, l) : l; };
    for (const Fea::Load &l : input.setup.loads)
        add_limit(l.max_displacement);
    m_result_zone_limits.clear();
    for (const Fea::DisplacementLimit &l : input.setup.limits) {
        add_limit(l.max_displacement);
        m_result_zone_limits.push_back(l.max_displacement);
    }
    m_result_object      = mo->id();
    return true;
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
        voxels = double(m_quality == Quality::Fast ? Fea::FAST_VOXELS : Fea::NORMAL_VOXELS);
        h = std::cbrt(volume / voxels);
    } else {
        h = Fea::quality_line_widths(m_quality) * Fea::line_width(object, wxGetApp().preset_bundle->full_config());
        voxels = volume / (h * h * h);
    }
}

void GLGizmoEngineering::apply_quality(Fea::ModelAnalysisInput &input, JobSize size)
{
    // No limit: the resolution chosen is the one computed (the panel warns about large analyses).
    m_quality_note.clear();
    input.setup.voxel_size = Fea::quality_voxel_size(m_quality, its_volume(input.mesh), input.line_width);
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

bool GLGizmoEngineering::start_cloud(const std::string &mode, bool solid, bool zones, bool ribs)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return false;
    const CloudAccess access = cloud_access();
    if (! access.logged_in()) {
        m_error = _u8L("Log in to Tisma Cloud first (Configuration > Tisma Cloud account), or compute on this computer.");
        wxGetApp().CallAfter([]() { show_cloud_account_dialog(wxGetApp().mainframe); });
        return false;
    }
    // The materials of the user are not on the servers.
    if (const std::optional<Fea::Material> material = Fea::material_by_key(m_result_material); material && material->user) {
        m_error = GUI::format(_u8L("%1% is one of your own materials: it is computed on this computer."), material->name);
        return false;
    }
    // The object alone, with its supports and loads and the print settings, as a project for the servers.
    const std::string project = cloud_temp_path(".3mf");
    std::string error;
    {
        Model model;
        model.add_object(*mo);
        if (! export_cloud_project(model, wxGetApp().preset_bundle->full_config(), project, error)) {
            m_error = error;
            return false;
        }
    }
    const nlohmann::json params = { { "mode", mode }, { "quality", Fea::quality_name(m_quality) }, { "object", 0 },
                                    { "instance", std::max(0, instance_idx()) }, { "solid", solid }, { "zones", zones },
                                    { "ribs", ribs } };
    m_cancel   = false;
    m_running  = true;
    m_progress = 0;
    m_thread = std::thread([this, access, mode, project, params = params.dump()]() {
        const std::string result = cloud_temp_path(".tfea");
        TismaCloud::Job job;
        std::string error;
        Fea::ResultFile file;
        bool ok = access.client().run("fea", params, project, result, job, error,
                                      [this](const TismaCloud::Job &j) { m_progress = j.status == "running" ? j.progress : 0; }, m_cancel);
        if (ok)
            ok = Fea::load_result_file(result, file, error);
        boost::system::error_code ec;
        boost::filesystem::remove(project, ec);
        boost::filesystem::remove(result, ec);
        if (! ok)
            error = error == "Cancelled" ? error : into_u8(GUI::format_wxstr(_L("Tisma Cloud: %1%"), cloud_error_text(error)));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (mode == "lightest-infill") {
                Fea::OptimizeResult res = ok ? std::move(file.infill) : Fea::OptimizeResult();
                if (! ok)
                    res.error = error;
                if (auto it = file.info.find("current_mass"); it != file.info.end())
                    m_current_infill_mass = std::atof(it->second.c_str());
                m_pending_opt = std::move(res);
            } else if (mode == "resin-structure") {
                Fea::ResinOptimizeResult res = ok ? std::move(file.resin) : Fea::ResinOptimizeResult();
                if (! ok)
                    res.error = error;
                m_pending_resin = std::move(res);
            } else {
                Fea::Result res = ok ? std::move(file.analysis) : Fea::Result();
                if (! ok)
                    res.error = error;
                m_pending = std::move(res);
            }
        }
        m_running = false;
        wxGetApp().CallAfter([]() {
            if (GLCanvas3D *canvas = wxGetApp().plater() ? wxGetApp().plater()->get_current_canvas3D() : nullptr) {
                canvas->set_as_dirty();
                canvas->request_extra_frame();
            }
        });
    });
    return true;
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

namespace {
// Cell of the interior infill of the object [mm] (the print settings with the overrides of the object).
double resin_infill_cell(const ModelObject &object)
{
    return Fea::resin_infill_cell(object, wxGetApp().preset_bundle->full_config());
}
}

void GLGizmoEngineering::start_resin_structure()
{
    const ModelObject *mo = model_object();
    Fea::ModelAnalysisInput input;
    if (mo == nullptr || ! prepare_input(input))
        return;
    if (m_cloud) {
        start_cloud("resin-structure", false, true, false);
        return;
    }
    const double min_density = Fea::resin_min_infill(resin_infill_cell(*mo));
    launch<Fea::ResinOptimizeResult>([this, input = std::move(input), min_density]() {
        return Fea::optimize_resin(input.mesh, input.setup, Fea::RESIN_WALLS, min_density, Fea::RESIN_MAX_INFILL,
                                   [this]() { return m_cancel.load(); }, [this](int p) { m_progress = p; });
    }, &m_pending_resin);
}

void GLGizmoEngineering::apply_resin_structure(bool solid)
{
    ModelObject *mo = model_object();
    if (mo == nullptr || ! m_resin || mo->id() != m_resin_object || (! solid && m_resin->best < 0))
        return;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Apply walls and interior infill"));
    if (solid) {
        Fea::apply_resin_structure(*mo, 0., 0.);
        m_applied_note = GUI::format(_u8L("Applied: \"%1%\" is printed solid. Slice again to see it in the preview."), mo->name);
    } else {
        const Fea::ResinOption &o = m_resin->options[size_t(m_resin->best)];
        Fea::apply_resin_structure(*mo, o.wall_thickness, o.infill_density);
        m_applied_note = GUI::format(_u8L("Applied: \"%1%\" hollowed with walls of %2$.1f mm and %3% %% interior infill. "
                                          "Slice again to see it in the preview."),
                                     mo->name, o.wall_thickness, int(std::round(o.infill_density * 100.)));
    }
    changed_object(*mo);
    m_applied_object = mo->id();
    notify(m_applied_note);
    m_resin_object = mo->id();
}

void GLGizmoEngineering::render_resin_optimize(const ModelObject &object, float width)
{
    const EngineeringSetup &eng = object.engineering;
    ui::section(_u8L("Walls and interior infill"), width);
    ImGuiPureWrap::text_wrapped(_u8L("Searches the thinnest walls and the lowest interior infill of the hollowed part which meet the "
                                     "safety factor and the deformation limits, and compares them with the solid part."), width);
    const double cell = resin_infill_cell(object);
    ui::muted(GUI::format(_u8L("Interior infill: self-supporting struts in cells of %1$.1f mm (Print Settings > Hollowing)."), cell), width);
    const bool can_run = ! m_running && ! eng.fixtures.empty() && ! eng.loads.empty();
    if (ImGuiPureWrap::button(_u8L("Find walls and infill")) && can_run)
        start_resin_structure();
    if (! can_run && ! m_running)
        ui::muted(_u8L("Add the fixed faces and the loads of the part first."), width);
    if (m_resin && m_resin_object == object.id()) {
        const Fea::ResinOptimizeResult &r = *m_resin;
        const ImVec4 green = to_imvec(verdict_color(Fea::Verdict::Holds));
        const bool solid_holds = Fea::meets_requirements(r.solid);
        const std::string solid = GUI::format(_u8L("Solid: %1$.1f g, safety factor %2$.2f"), r.solid.mass, r.solid.safety_factor);
        if (solid_holds && r.best < 0)
            ImGuiPureWrap::text_colored(green, "> " + solid);
        else
            ImGuiPureWrap::text("  " + solid);
        for (size_t i = 0; i < r.options.size(); ++ i) {
            const Fea::ResinOption &o = r.options[i];
            // Walls as thick as the part leave no cavity: the part stays solid.
            const bool no_cavity = o.result.mass >= 0.995 * r.solid.mass;
            const std::string text = ! o.feasible ?
                GUI::format(_u8L("Walls %1$.1f mm: does not hold"), o.wall_thickness) :
                no_cavity ?
                GUI::format(_u8L("Walls %1$.1f mm: no room for a cavity (solid)"), o.wall_thickness) :
                o.infill_density <= 0. ?
                GUI::format(_u8L("Walls %1$.1f mm, empty: %2$.1f g, safety factor %3$.2f"), o.wall_thickness, o.result.mass, o.result.safety_factor) :
                GUI::format(_u8L("Walls %1$.1f mm, infill %2% %%: %3$.1f g, safety factor %4$.2f"), o.wall_thickness,
                            int(std::round(o.infill_density * 100.)), o.result.mass, o.result.safety_factor);
            if (int(i) == r.best)
                ImGuiPureWrap::text_colored(green, "> " + text);
            else
                ImGuiPureWrap::text("  " + text);
        }
        if (r.best >= 0) {
            const Fea::ResinOption &o = r.options[size_t(r.best)];
            ImGuiPureWrap::text_wrapped(GUI::format(_u8L("Lightest: %1$.0f %% of the resin of the solid part."),
                                                    100. * o.result.mass / std::max(r.solid.mass, 1e-9)), width);
            if (ImGuiPureWrap::button(_u8L("Apply walls and infill")))
                apply_resin_structure(false);
            ui::muted(_u8L("A hollowed part needs drain holes (Hollow and drill tool) to let the resin out of the cavity."), width);
        } else if (solid_holds) {
            ImGuiPureWrap::text_wrapped(_u8L("No hollowed part meets the requirements: print it solid."), width);
            if (ImGuiPureWrap::button(_u8L("Apply solid")))
                apply_resin_structure(true);
        } else
            ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::OutOfLoad)),
                _u8L("Not even the solid part meets the requirements: choose another resin or reinforce the shape."));
        ui::muted(GUI::format(_u8L("%1% analyses. Homogenized infill model: check the result with a printed test part."), r.analyses), width);
    }
    if (! m_applied_note.empty() && m_applied_object == object.id()) {
        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(verdict_color(Fea::Verdict::Holds)));
        ImGuiPureWrap::text_wrapped(m_applied_note, width);
        ImGui::PopStyleColor();
    }
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

void GLGizmoEngineering::start_curved_layers()
{
    Fea::ModelAnalysisInput input;
    if (! prepare_input(input))
        return;
    if (! m_as_printed)
        input.setup.infill = Fea::InfillModel();
    const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    launch<Fea::CurvedLayersResult>([this, input = std::move(input), config]() {
        return Fea::curved_layers_for_strength(input, config, [this]() { return m_cancel.load(); },
                                               [this](int p) { m_progress = p; });
    }, &m_pending_layers);
}

void GLGizmoEngineering::apply_curved_layers()
{
    ModelObject *mo = model_object();
    if (mo == nullptr || ! m_layers || ! m_layers->ok || mo->id() != m_layers_object)
        return;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Curved layers for strength"));
    mo->engineering.layer_field = m_layers->field;
    // The non-planar layers of the print settings follow the stresses.
    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
        DynamicPrintConfig new_conf = *tab->get_config();
        if (new_conf.option<ConfigOptionEnum<NonPlanarMode>>("nonplanar_mode")->value != NonPlanarMode::Stress) {
            new_conf.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonPlanarMode>(NonPlanarMode::Stress));
            tab->load_config(new_conf);
        }
    }
    changed_object(*mo);
    notify(_u8L("Curved layers applied: Print Settings > Non-planar follows the stresses. Slice to see the layers."));
}

void GLGizmoEngineering::remove_curved_layers()
{
    ModelObject *mo = model_object();
    if (mo == nullptr || mo->engineering.layer_field.empty())
        return;
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove the curved layers"));
    mo->engineering.layer_field = EngineeringLayerField();
    if (Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
        DynamicPrintConfig new_conf = *tab->get_config();
        if (new_conf.option<ConfigOptionEnum<NonPlanarMode>>("nonplanar_mode")->value == NonPlanarMode::Stress) {
            new_conf.set_key_value("nonplanar_mode", new ConfigOptionEnum<NonPlanarMode>(NonPlanarMode::Disabled));
            tab->load_config(new_conf);
        }
    }
    changed_object(*mo);
}

void GLGizmoEngineering::cancel_analysis()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
    m_running = false;
    m_skin_core_pending = false;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.reset();
    m_pending_opt.reset();
    m_pending_reinf.reset();
    m_pending_lattice.reset();
    m_pending_orient.reset();
    m_pending_layers.reset();
    m_pending_resin.reset();
    m_pending_aero.reset();
}

void GLGizmoEngineering::fetch_result()
{
    std::optional<Fea::Result> res;
    std::optional<Fea::OptimizeResult> opt;
    std::optional<Fea::ReinforcementResult> reinf;
    std::optional<Fea::LatticeResult> lattice;
    std::optional<Fea::OrientationResult> orient;
    std::optional<Fea::CurvedLayersResult> layers;
    std::optional<Fea::ResinOptimizeResult> resin_opt;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        resin_opt.swap(m_pending_resin);
        orient.swap(m_pending_orient);
        layers.swap(m_pending_layers);
        res.swap(m_pending);
        opt.swap(m_pending_opt);
        reinf.swap(m_pending_reinf);
        lattice.swap(m_pending_lattice);
    }
    if (resin_opt) {
        if (m_thread.joinable())
            m_thread.join();
        if (! resin_opt->ok) {
            m_error = resin_opt->error == "Cancelled" ? std::string() : resin_opt->error;
            return;
        }
        m_resin        = std::move(*resin_opt);
        m_resin_object = m_result_object;
        // Show the lightest option (or the solid part) on the part.
        m_result = m_resin->best >= 0 ? m_resin->options[size_t(m_resin->best)].result : m_resin->solid;
        m_result_is_lattice = false;
        m_result_stale = false;
        m_field = Field::Safety;
        m_result_models_dirty = true;
        return;
    }
    if (layers) {
        if (m_thread.joinable())
            m_thread.join();
        if (! layers->ok) {
            m_error = layers->error == "Cancelled" ? std::string() : layers->error;
            return;
        }
        m_layers        = std::move(*layers);
        m_layers_object = m_result_object;
        // Show the analysis with the curved layers.
        m_result = m_layers->curved;
        m_result_is_lattice = false;
        m_result_stale = false;
        m_result_models_dirty = true;
        return;
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
        if (m_skin_core_pending) {
            // Skin + core: apply the core found (by zones when they are lighter).
            m_skin_core_pending = false;
            if (m_opt->feasible) {
                apply_optimization(true);
                m_applied_note = m_skin_core_note + "\n" + _u8L("Core:") + " " + m_applied_note;
            } else {
                m_applied_note = m_skin_core_note + "\n" +
                    _u8L("Core: not even 100 % infill meets the requirements: add perimeters, choose another material or reinforce the shape.");
                notify(m_applied_note);
            }
        }
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
    m_load_marks.clear();
    m_regions_dirty = false;
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;
    const Transform3d instance_trafo = mo->instances[instance_idx()]->get_transformation().get_matrix();
    // The arrows of the loads: at the center of the loaded faces (area weighted) or at the point, with the outer
    // normal of the surface there (of the faces, or of the triangle nearest to the point).
    for (const EngineeringLoad &load : mo->engineering.loads) {
        const int vol = load.type == EngineeringLoad::Type::Point ? load.volume : load.faces.volume;
        if (vol < 0 || vol >= int(mo->volumes.size()))
            continue;
        const ModelVolume &v = *mo->volumes[vol];
        const indexed_triangle_set &its = v.mesh().its;
        auto triangle = [&its](size_t t, Vec3d &a, Vec3d &b, Vec3d &c) {
            a = its.vertices[its.indices[t][0]].cast<double>();
            b = its.vertices[its.indices[t][1]].cast<double>();
            c = its.vertices[its.indices[t][2]].cast<double>();
        };
        Vec3d point = Vec3d::Zero(), normal = Vec3d::Zero();
        if (load.type == EngineeringLoad::Type::Point) {
            point = load.point;
            double best = std::numeric_limits<double>::max();
            for (size_t t = 0; t < its.indices.size(); ++ t) {
                Vec3d a, b, c;
                triangle(t, a, b, c);
                const double d = ((a + b + c) / 3. - point).squaredNorm();
                if (d < best) {
                    best   = d;
                    normal = (b - a).cross(c - a);
                }
            }
        } else {
            double area = 0.;
            for (int t : load.faces.triangles)
                if (t >= 0 && t < int(its.indices.size())) {
                    Vec3d a, b, c;
                    triangle(size_t(t), a, b, c);
                    const Vec3d n = (b - a).cross(c - a);
                    point  += 0.5 * n.norm() * (a + b + c) / 3.;
                    area   += 0.5 * n.norm();
                    normal += n;
                }
            if (area <= 0.)
                continue;
            point /= area;
        }
        m_load_marks.push_back({ vol, point, normal, load.force });
    }
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
    for (const EngineeringLimit &l : mo->engineering.limits)
        add(l.faces, LIMIT_COLOR);
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
    // Red at the allowed displacement when there is one (above it stays red), else at the largest displacement.
    const double max_d  = m_result_disp_limit > 0. ? m_result_disp_limit : std::max(r.max_displacement, 1e-12);
    // Broken voxels are black in the views of the results (not in the infill view).
    auto broken = [&](size_t e) { return m_field != Field::Density && e < r.failure_index.size() && r.failure_index[e] > 1.f; };
    auto value = [&](size_t e) -> float {
        switch (m_field) {
        case Field::Safety:       return r.failure_index[e];    // 1 = breaks
        case Field::Stress:       return float(r.von_mises[e] / max_vm);
        case Field::Displacement: return float(r.displacement[e].norm() / max_d);
        case Field::Density:      return e < r.density.size() ? r.density[e] : 1.f;
        }
        return 0.f;
    };

    // The colors of the scale and, last, the broken voxels.
    std::vector<GLModel::Geometry> geos(COLOR_STEPS + 1);
    for (GLModel::Geometry &g : geos)
        g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    static const int dirs[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    const float h = float(r.h);
    for (size_t e = 0; e < r.voxels.size(); ++ e) {
        if (! shown(e))
            continue;
        const int idx = r.voxels[e];
        const int i = idx % size.x(), j = (idx / size.x()) % size.y(), k = idx / (size.x() * size.y());
        const int step = broken(e) ? COLOR_STEPS : std::clamp(int(value(e) * (COLOR_STEPS - 1) + 0.5f), 0, COLOR_STEPS - 1);
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
    m_result_models.resize(COLOR_STEPS + 1);
    for (int s = 0; s <= COLOR_STEPS; ++ s) {
        if (geos[s].is_empty())
            continue;
        m_result_models[s].init_from(std::move(geos[s]));
        m_result_models[s].set_color(s == COLOR_STEPS ? BROKEN_COLOR : scale_color(float(s) / float(COLOR_STEPS - 1)));
    }
}

void GLGizmoEngineering::on_render()
{
    fetch_result();
    fetch_aero();
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;
    if (m_mode == Mode::Aero) {
        const bool results = m_show_results && m_aero && m_aero->ok && m_aero_object == mo->id();
        show_object(! results);
        if (results)
            render_aero();
        return;
    }

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
    if (m_regions_dirty)
        update_region_models();
    const ModelObject *mo = model_object();
    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
    if (mo == nullptr || shader == nullptr || m_load_marks.empty())
        return;
    const Transform3d instance_trafo = mo->instances[instance_idx()]->get_transformation().get_matrix();
    const BoundingBoxf3 bbox = mo->instance_bounding_box(size_t(instance_idx()));
    const double scale = std::max(0.5, 0.02 * bbox.size().maxCoeff());

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
    for (const LoadMark &local : m_load_marks) {
        if (local.volume >= int(mo->volumes.size()))
            continue;
        const Transform3d volume_trafo = instance_trafo * mo->volumes[local.volume]->get_matrix();
        Vec3d normal = volume_trafo.matrix().block<3, 3>(0, 0).inverse().transpose() * local.normal;
        normal = normal.norm() > 0. ? normal.normalized() : Vec3d::UnitZ();
        const LoadMark mark { local.volume, volume_trafo * local.point, normal, local.force };
        m_sphere.set_color(LOAD_COLOR);
        render_model(m_sphere, Geometry::translation_transform(mark.point) * Geometry::scale_transform(scale * 0.8));
        if (mark.force.norm() > 0.) {
            // The arrow stays outside of the part, along the force: a load that pushes into the surface ends at it, a
            // load that pulls starts at it. Lifted a bit off the surface, so that a load along the surface shows too.
            const Vec3d dir = mark.force.normalized();
            Eigen::Quaterniond q;
            q.setFromTwoVectors(Vec3d::UnitZ(), dir);
            const bool   pulls = dir.dot(mark.normal) > 0.;
            const Vec3d  base  = mark.point + 0.5 * scale * mark.normal;
            const Transform3d trafo = Geometry::translation_transform(base) * Transform3d(q.toRotationMatrix()) *
                                      Geometry::scale_transform(scale) * Geometry::translation_transform(pulls ? Vec3d::Zero() : Vec3d(-12. * Vec3d::UnitZ()));
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
    case Field::Safety:       lo = "0"; hi = _u8L("1 = at the strength"); break;
    case Field::Stress:       lo = "0 MPa"; hi = GUI::format("%.2f MPa", r.max_von_mises); break;
    case Field::Displacement:
        lo = "0 mm";
        hi = m_result_disp_limit > 0. ? GUI::format(_u8L("%1$.3f mm (limit)"), m_result_disp_limit) : GUI::format("%.3f mm", r.max_displacement);
        break;
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
    // Black: where the material breaks.
    if (r.max_failure_index > 1.) {
        const ImVec2 q  = ImGui::GetCursorScreenPos();
        const float  sq = ImGui::GetTextLineHeight();
        draw->AddRectFilled(q, ImVec2(q.x + sq, q.y + sq), ImGui::GetColorU32(to_imvec(BROKEN_COLOR)), 2.f);
        draw->AddRect(q, ImVec2(q.x + sq, q.y + sq), ImGui::GetColorU32(ImVec4(0.55f, 0.55f, 0.6f, 1.f)), 2.f);
        ImGui::Dummy(ImVec2(sq, sq));
        ImGui::SameLine();
        ImGuiPureWrap::text(_u8L("Breaks (stress above the strength)"));
    }
}

void GLGizmoEngineering::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = model_object();
    if (mo == nullptr)
        return;
    EngineeringSetup &eng = mo->engineering;
    if (m_running)
        m_imgui->set_requires_extra_frame();

    // A fixed place under the main toolbar, so that the buttons do not move while items are added; the panel scrolls
    // when the canvas is not tall enough.
    const float top = std::min(y, m_parent.get_main_toolbar_height() + 0.5f * ImGui::GetFontSize());
    ImGui::SetNextWindowPos(ImVec2(x, top), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, std::max(200.f, bottom_limit - top - 10.f)));
    ImGuiPureWrap::begin(get_name(false), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    const float width   = 24.f * ImGui::GetFontSize();
    const float label_w = 9.f * ImGui::GetFontSize();
    ImGui::PushItemWidth(10.f * ImGui::GetFontSize());

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, mo->name);

    // Two tabs: each shows only its own controls. Resin printers: only the structural analysis (walls and interior
    // infill of the hollowed part).
    const bool is_resin = resin();
    if (is_resin)
        m_mode = Mode::Structural;
    else if (ImGui::BeginTabBar("##engineering_mode")) {
        if (ImGui::BeginTabItem(_u8L("Structural").c_str())) {
            m_mode = Mode::Structural;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(_u8L("Aerodynamics").c_str())) {
            m_mode = Mode::Aero;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (m_mode == Mode::Aero) {
        render_aero_panel(width);
        ImGui::PopItemWidth();
        ImGuiPureWrap::end();
        return;
    }

    // ---- Conditions: material, temperature, safety factor, model of the part.
    ui::section(_u8L("Conditions"), width);
    const std::string filament = filament_type(*mo);
    const Fea::Material *from_filament = is_resin ? Fea::material_for_resin(filament) : Fea::material_for_filament_type(filament);
    std::vector<std::string> options;
    options.push_back(GUI::format(is_resin ? _u8L("Resin (%1%)") : _u8L("Filament (%1%)"), from_filament ? from_filament->name : filament));
    int selection = 0;
    for (size_t i = 0; i < Fea::materials().size(); ++ i) {
        // Materials of the user and values changed by the user are marked.
        options.push_back(Fea::materials()[i].name + (Fea::materials()[i].user ? " *" : ""));
        if (Fea::materials()[i].key == eng.material)
            selection = int(i + 1);
    }
    if (ImGuiPureWrap::combo(_u8L("Material"), options, selection, 0, label_w, width - label_w)) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Change material"));
        eng.material = selection == 0 ? std::string() : Fea::materials()[selection - 1].key;
        m_result_stale = true;
    }
    if (ImGuiPureWrap::button(_u8L("Edit the material table") + "...")) {
        // A modal dialog cannot be opened while ImGui renders.
        wxGetApp().CallAfter([this]() {
            if (edit_fea_materials(wxGetApp().plater())) {
                m_result_stale = true;
                m_parent.set_as_dirty();
            }
        });
    }
    const Fea::Material *material = eng.material.empty() ? from_filament : Fea::find_material(eng.material);
    if (material == nullptr)
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("The filament type has no material in the table: choose one."));
    else if (! material->note.empty())
        ui::muted(material->note, width);

    auto edit_value = [&](const std::string &label, float &buffer, double &target, float lo, float hi, const std::string &snapshot_name) {
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine(label_w);
        ImGui::PushItemWidth(width - label_w);
        ImGui::InputFloat(("##" + label).c_str(), &buffer, 0.f, 0.f, "%.1f");
        ImGui::PopItemWidth();
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
    {
        std::vector<std::string> qualities = { _u8L("Fast"), _u8L("Normal"), _u8L("High (voxel 2 line widths)"),
                                               _u8L("Ultra (voxel 1 line width)") };
        int q = int(m_quality);
        if (ImGuiPureWrap::combo(_u8L("Resolution"), qualities, q, 0, label_w, width - label_w)) {
            m_quality = Quality(q);
            wxGetApp().app_config->set("tisma_fea_quality", std::to_string(q));
            m_result_stale = true;
        }
        double h = 0., voxels = 0.;
        estimate_quality(*mo, h, voxels);
        if (voxels > Fea::LARGE_VOXELS_SINGLE)
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT,
                GUI::format(_u8L("About %1% voxels of %2$.2f mm: a large analysis, it may take minutes and much memory."),
                            size_t(voxels), h));
        else
            ui::muted(GUI::format(_u8L("About %1% voxels of %2$.2f mm."), size_t(voxels), h));
        if (voxels > Fea::LARGE_VOXELS_MANY)
            ui::muted(_u8L("The infill search, the reinforcement, the lattice and the orientation run many analyses of this "
                           "size: a lower resolution is faster."), width);
    }
    if (ImGuiPureWrap::checkbox(is_resin ? _u8L("Printed part (hollowing and interior infill of the profile)") :
                                           _u8L("Printed part (walls and infill of the profile)"), m_as_printed))
        m_result_stale = true;
    if (ImGuiPureWrap::checkbox(_u8L("Compute in the cloud"), m_cloud))
        wxGetApp().app_config->set("tisma_fea_cloud", m_cloud ? "1" : "0");
    if (m_cloud) {
        const CloudAccess access = cloud_access();
        ui::muted(access.logged_in() ? GUI::format(_u8L("On the Tisma Cloud servers (%1%): the analysis, the lightest infill and the walls of resin parts."), access.email) :
                                       _u8L("Log in first: Configuration > Tisma Cloud account."), width);
    }

    // ---- Supports, loads and displacement limits: what a click on the part adds.
    ui::section(_u8L("Supports, loads and limits"), width);
    ui::muted(_u8L("Click on the part to add:"));
    {
        struct Option { std::string label; Tool tool; };
        const std::vector<std::vector<Option>> rows = {
            { { _u8L("Nothing (rotate)"), Tool::None },     { _u8L("Fixed face"), Tool::Fixture } },
            { { _u8L("Load on a face"), Tool::FaceLoad },   { _u8L("Load on a point"), Tool::PointLoad } },
            { { _u8L("Displacement limit"), Tool::Limit } } };
        for (size_t r = 0; r < rows.size(); ++ r) {
            std::vector<std::string> labels;
            int sel = -1;
            for (size_t i = 0; i < rows[r].size(); ++ i) {
                labels.push_back(rows[r][i].label);
                if (rows[r][i].tool == m_tool)
                    sel = int(i);
            }
            if (ui::segmented(("tool" + std::to_string(r)).c_str(), labels, sel, width))
                m_tool = rows[r][size_t(sel)].tool;
        }
    }
    auto limit_inputs = [&](const char *id, float &mm, float &percent) {
        ImGuiPureWrap::text(_u8L("Max. displacement"));
        ImGui::SameLine(label_w);
        const float w = 0.5f * (width - label_w - ImGui::GetStyle().ItemSpacing.x);
        ImGui::PushItemWidth(w);
        ImGui::InputFloat((std::string("##mm") + id).c_str(), &mm, 0.f, 0.f, "%.2f mm");
        ImGui::SameLine();
        ImGui::InputFloat((std::string("##pct") + id).c_str(), &percent, 0.f, 0.f, "%.2f %%");
        ImGui::PopItemWidth();
        mm      = std::max(0.f, mm);
        percent = std::max(0.f, percent);
    };
    if (m_tool == Tool::FaceLoad || m_tool == Tool::PointLoad) {
        ImGuiPureWrap::text(_u8L("Force X, Y, Z [N]"));
        ImGui::PushItemWidth(width);
        ImGui::InputFloat3("##force", m_new_force, "%.1f");
        ImGui::PopItemWidth();
        limit_inputs("load", m_new_limit_mm, m_new_limit_percent);
        if (m_tool == Tool::PointLoad) {
            ImGuiPureWrap::text(_u8L("Load radius [mm]"));
            ImGui::SameLine(label_w);
            ImGui::PushItemWidth(width - label_w);
            ImGui::InputFloat("##radius", &m_new_radius, 0.f, 0.f, "%.1f");
            ImGui::PopItemWidth();
        }
        ui::muted(_u8L("0 = no limit. Z is up, as on the bed: -100 in Z pushes down with 100 N (about 10 kg)."), width);
    } else if (m_tool == Tool::Limit) {
        limit_inputs("zone", m_new_zone_mm, m_new_zone_percent);
        ui::muted(_u8L("Faces which must not move more than this, whatever the loads (a fit, a sealing face, a mechanism). "
                       "Elsewhere the part may deform more: only its strength is checked there. Click a limited face again to remove it."),
                  width);
    }

    // Lists of what was added, with the displacement measured by the last analysis.
    const bool measured = m_result && ! m_result_stale && m_result_object == mo->id();
    const ImVec4 red = to_imvec(verdict_color(Fea::Verdict::OutOfLoad));
    if (! eng.fixtures.empty() || ! eng.loads.empty() || ! eng.limits.empty())
        ImGui::Dummy(ImVec2(0.f, 0.2f * ImGui::GetFontSize()));
    for (size_t i = 0; i < eng.fixtures.size(); ++ i) {
        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(FIXTURE_COLOR));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(GUI::format(_u8L("Fixed face %1%"), i + 1).c_str());
        ImGui::PopStyleColor();
        if (ui::remove_button(("fixture" + std::to_string(i)).c_str(), width)) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove fixed face"));
            eng.fixtures.erase(eng.fixtures.begin() + i);
            m_regions_dirty = m_result_stale = true;
            break;
        }
    }
    for (size_t i = 0; i < eng.loads.size(); ++ i) {
        const EngineeringLoad &l = eng.loads[i];
        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(l.type == EngineeringLoad::Type::Faces ? FACE_LOAD_COLOR : LOAD_COLOR));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(GUI::format("%1%: (%2%, %3%, %4%) N", l.name, l.force.x(), l.force.y(), l.force.z()).c_str());
        ImGui::PopStyleColor();
        if (ui::remove_button(("load" + std::to_string(i)).c_str(), width)) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove load"));
            eng.loads.erase(eng.loads.begin() + i);
            m_regions_dirty = m_result_stale = true;
            break;
        }
        std::string details;
        if (l.type == EngineeringLoad::Type::Point && l.radius > 0.)
            details += GUI::format(_u8L("radius %1% mm"), l.radius);
        if (l.max_displacement > 0.)
            details += (details.empty() ? "" : ", ") + GUI::format(_u8L("max %1% mm"), l.max_displacement);
        if (l.max_displacement_percent > 0.)
            details += (details.empty() ? "" : ", ") + GUI::format(_u8L("max %1% %%"), l.max_displacement_percent);
        if (measured && i < m_result->load_displacement.size())
            details += (details.empty() ? "" : ", ") + GUI::format(_u8L("moves %1$.3f mm"), m_result->load_displacement[i]);
        if (! details.empty())
            ui::muted("    " + details, width);
    }
    for (size_t i = 0; i < eng.limits.size(); ++ i) {
        EngineeringLimit &l = eng.limits[i];
        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(LIMIT_COLOR));
        std::string text = l.name + ":";
        if (l.max_displacement > 0.)
            text += GUI::format(_u8L(" max %1% mm"), l.max_displacement);
        if (l.max_displacement_percent > 0.)
            text += GUI::format(_u8L(" max %1% %%"), l.max_displacement_percent);
        if (l.max_displacement <= 0. && l.max_displacement_percent <= 0.)
            text += " " + _u8L("no limit");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
        if (ui::remove_button(("limit" + std::to_string(i)).c_str(), width)) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove displacement limit"));
            eng.limits.erase(eng.limits.begin() + i);
            m_regions_dirty = m_result_stale = true;
            break;
        }
        // The measured displacement of the zone: red when it is above its limit.
        size_t k = 0;   // index among the zones with a limit (the analysis skips the others)
        for (size_t j = 0; j < i; ++ j)
            k += eng.limits[j].max_displacement > 0. || eng.limits[j].max_displacement_percent > 0.;
        const bool has_limit = l.max_displacement > 0. || l.max_displacement_percent > 0.;
        if (measured && has_limit && k < m_result->limit_displacement.size()) {
            const double d = m_result->limit_displacement[k];
            const double lim = k < m_result_zone_limits.size() ? m_result_zone_limits[k] : 0.;
            const std::string t = "    " + GUI::format(_u8L("moves %1$.3f mm of %2$.3f mm allowed"), d, lim);
            if (lim > 0. && d > lim)
                ImGuiPureWrap::text_colored(red, t);
            else
                ui::muted(t, width);
        }
    }
    if (! eng.loads.empty() || ! eng.fixtures.empty() || ! eng.limits.empty()) {
        if (! eng.loads.empty()) {
            if (ImGuiPureWrap::button(_u8L("Remove last load"), _u8L("Removes the last load added (Ctrl+Z restores it)"))) {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Remove load"));
                eng.loads.pop_back();
                m_regions_dirty = m_result_stale = true;
            }
            ImGui::SameLine();
        }
        if (ImGuiPureWrap::button(_u8L("Clear all"), _u8L("Removes all the fixed faces, loads and limits of the part (Ctrl+Z restores them)"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Clear supports and loads"));
            eng.loads.clear();
            eng.fixtures.clear();
            eng.limits.clear();
            m_regions_dirty = m_result_stale = true;
            m_result.reset();
            m_result_models_dirty = true;
            m_orient.reset();
        }
    }

    // ---- The analysis.
    ImGui::Dummy(ImVec2(0.f, 0.3f * ImGui::GetFontSize()));
    if (m_running) {
        ImGui::ProgressBar(float(m_progress.load()) / 100.f, ImVec2(width - 6.f * ImGui::GetFontSize(), 0.f));
        ImGui::SameLine();
        if (ImGuiPureWrap::button(_u8L("Cancel")))
            cancel_analysis();
    } else {
        const bool can_run = ! eng.fixtures.empty() && ! eng.loads.empty();
        if (ui::primary_button(_u8L("Analyze"), width, can_run))
            start_analysis();
        if (! can_run)
            ui::muted(_u8L("Add a fixed face and a load."));
    }
    if (! m_error.empty())
        ImGuiPureWrap::text_colored(red, m_error);

    // ---- Result.
    if (m_result && m_result_object == mo->id()) {
        const Fea::Result &r = *m_result;
        ui::section(_u8L("Result"), width, m_result_stale ? _u8L("setup changed: analyze again") : std::string(), ImGuiPureWrap::COL_ORANGE_LIGHT);
        ImGui::PushStyleColor(ImGuiCol_Text, to_imvec(verdict_color(r.verdict)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(verdict_text(r.verdict).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        const ImVec4 text_col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const ImVec4 sf_col = to_imvec(verdict_color(r.safety_factor >= m_result_safety ? Fea::Verdict::Holds :
                                                     r.safety_factor >= 1. ? Fea::Verdict::LowMargin : Fea::Verdict::OutOfLoad));
        ui::metrics({ { _u8L("Safety factor"), GUI::format("%1$.2f", r.safety_factor), sf_col },
                      { _u8L("Max. displacement"), GUI::format("%1$.3f mm", r.max_displacement),
                        r.verdict == Fea::Verdict::TooFlexible ? red : text_col } }, width);
        ui::row(_u8L("Required safety factor"), GUI::format("%1$.1f", m_result_safety), width);
        ui::row(_u8L("Max. stress (von Mises)"), GUI::format("%1$.2f MPa", r.max_von_mises), width);
        ui::row(_u8L("Estimated mass"), GUI::format(_u8L("%1$.1f g (solid %2$.1f g)"), r.mass, r.solid_mass), width);
        ui::row(_u8L("Material"), GUI::format(_u8L("%1% at %2% °C (x %3$.2f)"), m_result_material, m_result_temperature, r.temperature_factor), width);
        if (is_resin)
            ;   // The resin layers cure into one material (isotropic resin, no layer adhesion factor).
        else if (m_result_print_temperature > 0.)
            ui::row(_u8L("Layer adhesion"), GUI::format(_u8L("x %1$.2f (nozzle at %2% °C)"), r.layer_adhesion_factor,
                                                        int(std::round(m_result_print_temperature))), width);
        else
            ui::row(_u8L("Layer adhesion"), _u8L("nominal (nozzle temperature unknown)"), width);
        if (r.layer_orientations > 1)
            ui::row(_u8L("Curved layers"), GUI::format(_u8L("%1% directions"), r.layer_orientations), width);
        if (! r.alternatives.empty()) {
            std::string alt;
            for (size_t i = 0; i < r.alternatives.size() && i < 5; ++ i)
                alt += (i ? ", " : "") + GUI::format("%1% (%2$.1f)", r.alternatives[i].first, r.alternatives[i].second);
            ui::muted(GUI::format(_u8L("Materials that would hold (safety factor): %1%"), alt), width);
        }

        // What the colors on the part show.
        int view = m_show_results ? int(m_field) : 4;
        if (ui::segmented("view", { _u8L("Safety"), _u8L("Stress"), _u8L("Displ."), _u8L("Infill"), _u8L("Hide") }, view, width)) {
            m_show_results = view != 4;
            if (m_show_results)
                m_field = Field(view);
            m_result_models_dirty = true;
        }
        if (m_show_results)
            render_legend(width);
        ui::muted(r.density.empty() || r.mass >= r.solid_mass * 0.999 ?
            GUI::format(_u8L("Solid part, %1$.2f mm voxels."), r.h) :
            GUI::format(_u8L("Walls and homogenized infill, %1$.2f mm voxels."), r.h), width);
        if (! m_quality_note.empty())
            ui::muted(m_quality_note, width);
        if (! r.gpu_device.empty())
            ui::muted(GUI::format(_u8L("Computed on the graphics card (%1%)."), r.gpu_device), width);
    }

    // ---- Optimize: the lightest structure for these loads.
    if (is_resin) {
        render_resin_optimize(*mo, width);
        ImGui::PopItemWidth();
        ImGuiPureWrap::end();
        return;
    }
    ui::section(_u8L("Optimize"), width);
    // Skin + core in one click.
    if (ImGuiPureWrap::button(_u8L("Skin + core")) && ! m_running)
        start_skin_core();
    if (ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(_u8L("In one click, a part finished as injection molded and designed for its loads: the skin with automatic "
                                    "non-planar layers (top faces that follow the surface, fewer supports) and the core with the lightest "
                                    "infill by zones and ribs along the stresses, applied when found."), ImGui::GetFontSize() * 25.f);
    ImGui::SameLine();
    ui::muted(_u8L("automatic non-planar skin + lightest infill with ribs"), width);
    if (m_open_infill_section) {
        ImGui::SetNextItemOpen(true);
        m_open_infill_section = false;
    }
    if (ImGui::CollapsingHeader(_u8L("Lightest infill").c_str())) {
        ImGuiPureWrap::text_wrapped(_u8L("Searches the lowest infill which meets the safety factor and the deformation limits of the loads, "
                                         "with the walls and the infill pattern of the profile."), width);
        ImGuiPureWrap::checkbox(_u8L("Also by zones (more infill where the stresses are high)"), m_opt_zones);
        ImGuiPureWrap::checkbox(_u8L("Infill ribs along the stresses"), m_opt_ribs);
        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(_u8L("Where the part works in one direction (pulled or bent along it) its infill lines are laid along that "
                                        "direction in every layer, as the ribs of a part designed for the loads. The analysis counts them "
                                        "(stiffer and stronger along the lines, much weaker across) and keeps them only if the infill is lighter."),
                                   ImGui::GetFontSize() * 25.f);
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
                if (! o.ribs.empty())
                    ImGuiPureWrap::text_wrapped(GUI::format(_u8L("With %1% zones of ribs along the stresses (%2% %% without them)."),
                        o.ribs.size(), int(std::round(o.density_without_ribs * 100.))), width);
                else if (m_opt_ribs_tried)
                    ImGuiPureWrap::text_wrapped(_u8L("Ribs along the stresses would not make the infill lighter: not used."), width);
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
      // Belt printers: the layers are inclined by the gantry; the orientations compared here are the ones of a flat bed.
      if (Belt::enabled(wxGetApp().preset_bundle->printers.get_edited_preset().config))
        ImGuiPureWrap::text_wrapped(_u8L("Belt printer: the analysis uses the layers inclined by the gantry. "
                                         "The recommended orientation is only available for flat beds."), width);
      else {
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
    }

    // Curved layers that follow the stresses (non-planar).
    if (ImGui::CollapsingHeader(_u8L("Curved layers for strength").c_str())) {
        ImGuiPureWrap::text_wrapped(_u8L("Tilts the layers towards where the part is pulled, as far as the print head allows, so that "
                                         "the pull runs more along the layers (non-planar layers, 3 axes)."), width);
        if (ImGuiPureWrap::button(_u8L("Compute curved layers")) && can_run)
            start_curved_layers();
        if (m_layers && m_layers->ok && m_layers_object == mo->id()) {
            const double flat = m_layers->flat.safety_factor, curved = m_layers->curved.safety_factor;
            ImGuiPureWrap::text(GUI::format(_u8L("Flat layers: safety factor %1$.2f"), flat));
            const std::string text = GUI::format(_u8L("Curved layers: safety factor %1$.2f (x %2$.2f)"), curved, curved / std::max(flat, 1e-9));
            if (curved > 1.02 * flat)
                ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::Holds)), text);
            else
                ImGuiPureWrap::text(text);
            if (curved > 1.02 * flat) {
                if (ImGuiPureWrap::button(_u8L("Use the curved layers")))
                    apply_curved_layers();
            } else
                ImGuiPureWrap::text_wrapped(_u8L("The curved layers do not help these loads: keep the flat layers."), width);
        }
        if (! mo->engineering.layer_field.empty()) {
            const EngineeringLayerField &f = mo->engineering.layer_field;
            ImGuiPureWrap::text_wrapped(f.safety_flat > 0. ?
                GUI::format(_u8L("The object has curved layers (safety factor %1$.2f -> %2$.2f)."), f.safety_flat, f.safety_curved) :
                _u8L("The object has curved layers."), width);
            if (ImGuiPureWrap::button(_u8L("Remove the curved layers")))
                remove_curved_layers();
        }
        ImGuiPureWrap::text_wrapped(_u8L("Experimental: check the strength with your own test pieces. The layer thickness, the print "
                                         "head and the first flat layers limit the tilt."), width);
    }

    ImGui::PopItemWidth();
    ImGuiPureWrap::end();
}

// ---------------------------------------------------------------------------------------------------------------------
// Aerodynamics.

namespace {
// Pressure coefficient shown from strong suction to stagnation.
constexpr float CP_MIN = -1.5f;
constexpr float CP_MAX = 1.f;
// Ra shown up to 60 µm.
constexpr float RA_MAX = 60.f;
}

Vec3d GLGizmoEngineering::aero_flow_direction() const
{
    static const Vec3d dirs[6] = { Vec3d::UnitX(), -Vec3d::UnitX(), Vec3d::UnitY(), -Vec3d::UnitY(), Vec3d::UnitZ(), -Vec3d::UnitZ() };
    return m_aero_dir >= 0 && m_aero_dir < 6 ? dirs[m_aero_dir] : m_aero_view_dir;
}

void GLGizmoEngineering::start_aero()
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || m_running)
        return;
    if (m_thread.joinable())
        m_thread.join();
    // The part as printed, in world coordinates (all its model parts).
    const Transform3d instance_trafo = mo->instances[instance_idx()]->get_transformation().get_matrix();
    indexed_triangle_set mesh;
    for (const ModelVolume *v : mo->volumes)
        if (v->is_model_part()) {
            indexed_triangle_set its = v->mesh().its;
            its_transform(its, instance_trafo * v->get_matrix());
            its_merge(mesh, its);
        }
    if (mesh.indices.empty())
        return;
    Fea::AeroSetup setup;
    setup.flow_direction = aero_flow_direction();
    setup.speed          = std::max(0.1, double(m_aero_speed));
    const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    setup.layer_height   = config.has("layer_height") ? config.opt_float("layer_height") : 0.2;
    static const int resolutions[3] = { 16, 24, 32 };
    setup.resolution        = resolutions[std::clamp(m_aero_quality, 0, 2)];
    setup.simulate_pressure = m_aero_quality != 3;
    setup.cancel   = [this]() { return m_cancel.load(); };
    setup.progress = [this](int p) { m_progress = p; };
    m_aero_mesh   = mesh;
    m_aero_object = mo->id();
    m_error.clear();
    launch<Fea::AeroResult>([mesh = std::move(mesh), setup]() { return Fea::analyze_aero(mesh, setup); }, &m_pending_aero);
}

void GLGizmoEngineering::fetch_aero()
{
    std::optional<Fea::AeroResult> res;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        res.swap(m_pending_aero);
    }
    if (! res)
        return;
    if (m_thread.joinable())
        m_thread.join();
    if (! res->ok) {
        m_error = res->error == "Cancelled" ? std::string() : res->error;
        return;
    }
    m_aero = std::move(*res);
    m_aero_models_dirty = true;
}

void GLGizmoEngineering::render_aero()
{
    if (m_aero_models_dirty) {
        m_aero_models_dirty = false;
        m_aero_models.clear();
        std::vector<GLModel::Geometry> geos(COLOR_STEPS);
        for (GLModel::Geometry &g : geos)
            g.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
        const Fea::AeroResult &r = *m_aero;
        for (size_t t = 0; t < m_aero_mesh.indices.size(); ++ t) {
            const Vec3i32 &tri = m_aero_mesh.indices[t];
            float value;
            if (m_aero_show_ra)
                value = t < r.triangle_ra.size() ? r.triangle_ra[t] / RA_MAX : 0.f;
            else {
                float cp = 0.f;
                for (int k = 0; k < 3; ++ k)
                    cp += size_t(tri[k]) < r.vertex_cp.size() ? r.vertex_cp[tri[k]] : 0.f;
                value = (cp / 3.f - CP_MIN) / (CP_MAX - CP_MIN);
            }
            const int step = std::clamp(int(value * (COLOR_STEPS - 1) + 0.5f), 0, COLOR_STEPS - 1);
            GLModel::Geometry &g = geos[step];
            const Vec3f &a = m_aero_mesh.vertices[tri[0]], &b = m_aero_mesh.vertices[tri[1]], &c = m_aero_mesh.vertices[tri[2]];
            const Vec3f n = (b - a).cross(c - a).normalized();
            const unsigned int base = unsigned(g.vertices_count());
            g.add_vertex(a, n);
            g.add_vertex(b, n);
            g.add_vertex(c, n);
            g.add_triangle(base, base + 1, base + 2);
        }
        m_aero_models.resize(COLOR_STEPS);
        for (int s = 0; s < COLOR_STEPS; ++ s)
            if (! geos[s].is_empty()) {
                m_aero_models[s].init_from(std::move(geos[s]));
                m_aero_models[s].set_color(scale_color(float(s) / float(COLOR_STEPS - 1)));
            }
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
    for (GLModel &m : m_aero_models)
        if (m.is_initialized())
            m.render();
    shader->stop_using();
}

void GLGizmoEngineering::render_aero_panel(float width)
{
    const ModelObject *mo = model_object();
    const float label_w = 9.f * ImGui::GetFontSize();
    ui::muted(_u8L("Air flowing around the part as printed, with the roughness of the layers. An estimate to compare "
                   "designs and orientations."), width);

    // ---- Conditions.
    ui::section(_u8L("Conditions"), width);
    std::vector<std::string> dirs = { _u8L("Towards +X"), _u8L("Towards -X"), _u8L("Towards +Y"), _u8L("Towards -Y"),
                                      _u8L("Upwards (+Z)"), _u8L("Downwards (-Z)"), _u8L("Into the view") };
    if (ImGuiPureWrap::combo(_u8L("Air flow"), dirs, m_aero_dir, 0, label_w, width - label_w)) {
        if (m_aero_dir == 6)
            m_aero_view_dir = wxGetApp().plater()->get_camera().get_dir_forward();
    }
    ImGuiPureWrap::text(_u8L("Speed (m/s)"));
    ImGui::SameLine(label_w);
    ImGui::PushItemWidth(width - label_w);
    ImGui::InputFloat("##aero_speed", &m_aero_speed, 1.f, 10.f, "%.1f");
    ImGui::PopItemWidth();
    m_aero_speed = std::clamp(m_aero_speed, 0.1f, 300.f);
    ImGui::SameLine(0.f, 0.f);
    ImGui::NewLine();
    ui::row("", GUI::format(_u8L("= %1$.0f km/h"), m_aero_speed * 3.6), width);
    std::vector<std::string> qualities = { _u8L("Fast"), _u8L("Normal"), _u8L("High"), _u8L("Friction only (instant)") };
    ImGuiPureWrap::combo(_u8L("Quality"), qualities, m_aero_quality, 0, label_w, width - label_w);

    // ---- The analysis.
    ImGui::Dummy(ImVec2(0.f, 0.3f * ImGui::GetFontSize()));
    if (m_running) {
        ImGui::ProgressBar(float(m_progress.load()) / 100.f, ImVec2(width - 6.f * ImGui::GetFontSize(), 0.f),
                           GUI::format(_u8L("Simulating the air... %1%%%"), int(m_progress)).c_str());
        ImGui::SameLine();
        if (ImGuiPureWrap::button(_u8L("Cancel")))
            cancel_analysis();
    } else if (ui::primary_button(_u8L("Analyze the air flow"), width))
        start_aero();
    if (! m_error.empty())
        ImGuiPureWrap::text_colored(to_imvec(verdict_color(Fea::Verdict::OutOfLoad)), m_error);

    if (! m_aero || ! m_aero->ok || mo == nullptr || m_aero_object != mo->id())
        return;
    const Fea::AeroResult &r = *m_aero;

    // ---- Result.
    ui::section(_u8L("Result"), width, r.converged ? std::string() : _u8L("not converged"), ImGuiPureWrap::COL_ORANGE_LIGHT);
    ui::metrics({ { _u8L("Drag"), GUI::format("%1$.3f N", r.drag) }, { "Cd", GUI::format("%1$.2f", r.cd) } }, width);
    ui::row(_u8L("Cd pressure / friction"), GUI::format("%1$.2f / %2$.2f", r.cd_pressure, r.cd_friction), width);
    const double lift = std::sqrt(std::max(0., r.force.squaredNorm() - r.drag * r.drag));
    ui::row(_u8L("Force across the flow"), GUI::format("%1$.3f N", lift), width);
    ui::row(_u8L("Frontal area"), GUI::format("%1$.0f mm²", r.frontal_area), width);
    ui::row(_u8L("Reynolds"), r.converged && r.sim_reynolds < r.reynolds ?
                GUI::format(_u8L("%1$.0f (simulated %2$.0f)"), r.reynolds, r.sim_reynolds) : GUI::format("%1$.0f", r.reynolds), width);
    ui::row(_u8L("Roughness Ra"), GUI::format(_u8L("%1$.0f µm (%2$.0f %% of the friction)"), r.mean_roughness_ra,
                                              100. * r.roughness_friction_share), width);

    // What the colors on the part show.
    int view = ! m_show_results ? 2 : m_aero_show_ra ? 1 : 0;
    if (ui::segmented("aero_view", { _u8L("Pressure"), _u8L("Roughness"), _u8L("Hide") }, view, width)) {
        m_show_results = view != 2;
        if (m_show_results && m_aero_show_ra != (view == 1)) {
            m_aero_show_ra = view == 1;
            m_aero_models_dirty = true;
        }
    }
    if (! m_show_results)
        return;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  hh = ImGui::GetFrameHeight() * 0.6f;
    for (int i = 0; i < 32; ++ i)
        draw->AddRectFilled(ImVec2(p.x + width * i / 32, p.y), ImVec2(p.x + width * (i + 1) / 32 + 1.f, p.y + hh),
                            ImGui::GetColorU32(to_imvec(scale_color(float(i) / 31.f))));
    ImGui::Dummy(ImVec2(width, hh));
    const std::string lo = m_aero_show_ra ? "0 µm" : _u8L("Cp -1.5 (suction)");
    const std::string hi = m_aero_show_ra ? GUI::format("%1$.0f µm", RA_MAX) : _u8L("1 (stagnation)");
    ui::row(lo, hi, width);
}

} // namespace GUI
} // namespace Slic3r
