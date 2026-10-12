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
#include <tisma_fea/Aero.hpp>
#include <tisma_fea/Analysis.hpp>
#include <tisma_fea/Optimize.hpp>
#include <tisma_fea/Structures.hpp>
#include <tisma_fea/ModelSetup.hpp>
#include <tisma_fea/Orientation.hpp>

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
    enum class Tool { None, Fixture, FaceLoad, PointLoad, Limit };
    enum class Field { Safety, Stress, Displacement, Density };
    // Resolution of the analysis (saved in the application config as "tisma_fea_quality"): Fast and Normal give a
    // number of voxels, High and Ultra a voxel of twice and once the line width of the print.
    enum class Quality : int { Fast = 0, Normal = 1, High = 2, Ultra = 3 };
    // Jobs of one analysis or of many (optimizers, orientation): the latter have a lower limit of voxels.
    enum class JobSize { Single, Many };

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
    // Builds the input of an analysis of the printed part and remembers its material, temperature and object.
    bool prepare_input(Fea::ModelAnalysisInput &input, JobSize size = JobSize::Many);
    // Sets the size of the voxels of the analysis for the chosen quality (limited to a number of voxels).
    void apply_quality(Fea::ModelAnalysisInput &input, JobSize size);
    // The size of the voxel [mm] and the number of voxels of the chosen quality, without limit.
    void estimate_quality(const ModelObject &object, double &h, double &voxels) const;
    void start_orientation();
    // Rotates the instance so that the recommended axis is printed vertically (the loads rotate with the part).
    void rotate_to_recommended();
    // Runs a job in the background; its result is stored in *pending under the mutex.
    template<class T> void launch(std::function<T()> job, std::optional<T> *pending);
    void start_reinforcement();
    void apply_reinforcement();
    void start_lattice();
    void apply_lattice();
    void render_lattice();
    void cancel_analysis();
    void fetch_result();

    void update_region_models();
    void update_result_models();
    void show_object(bool show);
    void render_regions();
    void render_loads();
    void render_results();
    void render_legend(float width);
    // Aerodynamics tab.
    void start_aero();
    void fetch_aero();
    void render_aero_panel(float width);
    void render_aero();
    Vec3d aero_flow_direction() const;

    enum class Mode { Structural, Aero };
    Mode  m_mode { Mode::Structural };
    Tool  m_tool { Tool::None };
    Field m_field { Field::Safety };
    bool  m_show_results { true };
    // Parameters of the next load.
    float m_new_force[3] { 0.f, 0.f, -100.f };
    float m_new_limit_mm { 0.f };
    float m_new_limit_percent { 0.f };
    // Radius over which a point load is spread [mm]: a real load acts on an area (a screw, a support).
    float m_new_radius { 3.f };
    // Displacement limit of the next zone: in mm and in % of the largest dimension (0 = not used).
    float m_new_zone_mm { 0.5f };
    float m_new_zone_percent { 0.f };
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
    std::optional<Fea::ReinforcementResult> m_pending_reinf;
    std::optional<Fea::LatticeResult>       m_pending_lattice;
    std::optional<Fea::OrientationResult>   m_pending_orient;
    bool              m_job_optimize { false };

    std::optional<Fea::Result> m_result;
    ObjectID                   m_result_object;
    std::string                m_result_material;
    double                     m_result_temperature { 23. };
    double                     m_result_safety { 2. };
    std::string                m_error;
    bool                       m_result_stale { false };
    bool                       m_as_printed { true };
    Quality                    m_quality { Quality::Normal };
    // Set when the quality was limited by the number of voxels.
    std::string                m_quality_note;
    // Nozzle temperature of the analyzed part (0 = unknown) and the factor of the layer adhesion.
    double                     m_result_print_temperature { 0. };
    // Smallest displacement limit of the loads and the limited zones of the analysis [mm], 0 = none: the
    // displacement view reaches red at it.
    double                     m_result_disp_limit { 0. };
    // Displacement limit of each limited zone of the analysis [mm], in the order of Fea::Result::limit_displacement.
    std::vector<double>        m_result_zone_limits;
    // Recommended orientation.
    std::optional<Fea::OrientationResult> m_orient;
    ObjectID                   m_orient_object;
    // Applied infill, shown until the object changes.
    std::string                m_applied_note;
    ObjectID                   m_applied_object;
    // Lightest infill.
    std::optional<Fea::OptimizeResult> m_opt;
    ObjectID                   m_opt_object;
    bool                       m_opt_zones { true };
    bool                       m_open_infill_section { false };
    std::atomic<double>        m_current_infill_mass { 0. };
    // Local reinforcement.
    std::optional<Fea::ReinforcementResult> m_reinf;
    ObjectID                   m_reinf_object;
    float                      m_reinf_radius { 0.f };      // 0 = automatic
    int                        m_reinf_perimeters { 2 };
    float                      m_reinf_density { 40.f };    // %
    int                        m_reinf_base_perimeters { 2 };
    // Lattice.
    std::optional<Fea::LatticeResult> m_lattice;
    ObjectID                   m_lattice_object;
    float                      m_lattice_cell { 8.f };
    float                      m_lattice_min_d { 0.9f };
    float                      m_lattice_max_d { 3.f };
    GLModel                    m_lattice_model;
    // The shown result is the one of the lattice (its struts are drawn in the infill view).
    bool                       m_result_is_lattice { false };
    bool                       m_lattice_model_dirty { true };

    // Render caches.
    struct RegionModel { GLModel model; Transform3d trafo; ColorRGBA color; };
    std::vector<RegionModel> m_region_models;
    bool                     m_regions_dirty { true };
    std::vector<GLModel>     m_result_models;     // one per color of the scale
    bool                     m_result_models_dirty { true };
    GLModel                  m_sphere;
    GLModel                  m_arrow;
    bool                     m_object_hidden { false };
    // Aerodynamics: flow direction (+X, -X, +Y, -Y, +Z, -Z, from the view), speed, quality (fast, normal, high,
    // friction only), result and its pressure map on the part (world coordinates).
    int                      m_aero_dir { 0 };
    float                    m_aero_speed { 10.f };
    int                      m_aero_quality { 1 };
    Vec3d                    m_aero_view_dir { 1., 0., 0. };
    std::optional<Fea::AeroResult> m_pending_aero;
    std::optional<Fea::AeroResult> m_aero;
    ObjectID                 m_aero_object;
    indexed_triangle_set     m_aero_mesh;
    std::vector<GLModel>     m_aero_models;
    bool                     m_aero_models_dirty { true };
    bool                     m_aero_show_ra { false };
    ObjectID                 m_hidden_object;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoEngineering_hpp_
