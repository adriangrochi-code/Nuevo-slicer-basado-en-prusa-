///|/ Tisma Slicer: Engineering workspace in the 3D view (phase 5): supports, loads, working temperature and material
///|/ of an object, structural analysis in the background and its results on the part.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoEngineering_hpp_
#define slic3r_GLGizmoEngineering_hpp_

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLModel.hpp"

#include <libslic3r/ObjectID.hpp>
#include <tisma_fea/Analysis.hpp>
#include <tisma_fea/Optimize.hpp>

namespace Slic3r {

class ModelObject;
class ModelVolume;
class DynamicPrintConfig;
struct EngineeringRegion;

namespace GUI {

class GLGizmoEngineering : public GLGizmoBase
{
public:
    explicit GLGizmoEngineering(GLCanvas3D &parent);
    ~GLGizmoEngineering() override;

    bool on_mouse(const wxMouseEvent &mouse_event) override;
    void data_changed(bool is_serializing) override;
    std::string get_tooltip() const override;
    // Opens the section of the lightest infill (Structures workspace).
    void show_infill_section() { m_open_infill_section = true; }

protected:
    bool               on_init() override;
    std::string        on_get_name() const override;
    bool               on_is_activable() const override;
    bool               on_is_selectable() const override { return false; }
    void               on_render() override;
    void               on_render_input_window(float x, float y, float bottom_limit) override;
    void               on_set_state() override;
    CommonGizmosDataID on_get_requirements() const override;

private:
    enum class Tool { None, Fixture, FaceLoad, PointLoad };
    enum class Field { Safety, Stress, Displacement, Density };

    ModelObject* model_object() const;
    int          instance_idx() const;
    // The model part under the mouse: index of the volume, triangle and hit point (mesh coordinates).
    bool         raycast(const Vec2d &mouse, int &volume_idx, int &facet, Vec3f &hit) const;
    // The face of the triangle: the B-Rep face for STEP parts, the flat region around it otherwise.
    void         pick_face(const ModelVolume &volume, int volume_idx, int facet, EngineeringRegion &region) const;
    void         add_at_mouse(const Vec2d &mouse);
    std::string  filament_type(const ModelObject &object) const;

    void start_analysis();
    void start_optimization();
    void apply_optimization(bool zones);
    void cancel_analysis();
    void fetch_result();

    void update_region_models();
    void update_result_models();
    void show_object(bool show);
    void render_regions();
    void render_loads();
    void render_results();
    void render_legend(float width);

    Tool  m_tool { Tool::None };
    Field m_field { Field::Safety };
    bool  m_show_results { true };
    // Parameters of the next load.
    float m_new_force[3] { 0.f, 0.f, -100.f };
    float m_new_limit_mm { 0.f };
    float m_new_limit_percent { 0.f };
    // Radius over which a point load is spread [mm]: a real load acts on an area (a screw, a support).
    float m_new_radius { 3.f };
    // Text fields being edited.
    float m_edit_temperature { 23.f };
    float m_edit_safety { 2.f };

    // Analysis in the background.
    std::thread       m_thread;
    std::atomic<bool> m_cancel { false };
    std::atomic<bool> m_running { false };
    std::atomic<int>  m_progress { 0 };
    std::mutex        m_mutex;
    std::optional<Fea::Result> m_pending;
    std::optional<Fea::OptimizeResult> m_pending_opt;
    bool              m_job_optimize { false };

    std::optional<Fea::Result> m_result;
    ObjectID                   m_result_object;
    std::string                m_result_material;
    double                     m_result_temperature { 23. };
    double                     m_result_safety { 2. };
    std::string                m_error;
    bool                       m_result_stale { false };
    bool                       m_as_printed { true };
    // Lightest infill.
    std::optional<Fea::OptimizeResult> m_opt;
    ObjectID                   m_opt_object;
    bool                       m_opt_zones { true };
    bool                       m_open_infill_section { false };
    std::atomic<double>        m_current_infill_mass { 0. };

    // Render caches.
    struct RegionModel { GLModel model; Transform3d trafo; ColorRGBA color; };
    std::vector<RegionModel> m_region_models;
    bool                     m_regions_dirty { true };
    std::vector<GLModel>     m_result_models;     // one per color of the scale
    bool                     m_result_models_dirty { true };
    GLModel                  m_sphere;
    GLModel                  m_arrow;
    bool                     m_object_hidden { false };
    ObjectID                 m_hidden_object;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoEngineering_hpp_
