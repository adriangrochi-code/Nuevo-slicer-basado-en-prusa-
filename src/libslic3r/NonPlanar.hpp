#ifndef slic3r_NonPlanar_hpp_
#define slic3r_NonPlanar_hpp_

// Non-planar layers by "deform - slice planar - transform back".
//
// The real layers are the surfaces  z = s + D(x, y, z)  with  D = g(x, y, z) * ramp(z):
//   * g gives the shape of a layer (waves, cone ...),
//   * ramp is zero for the first layers (flat first layer on the bed) and optionally
//     goes back to zero at the top (flat top surface).
// The object mesh is moved to the "slice space" z' = z - D before slicing, sliced and
// processed by the regular planar pipeline, and the resulting G-code is transformed
// back point by point (z = z' + D), so the layers become curved.
//
// The vertical layer thickness changes by J = dz/dz' = 1 / (1 - d(D)/dz), the extrusion
// is scaled by J and the feed rate adjusted to keep the volumetric flow, and the feed
// rate is limited so that the Z axis stays within its firmware speed / acceleration.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

struct indexed_triangle_set;

namespace Slic3r {

class ModelVolume;
class PrintConfig;
struct EngineeringLayerField;

namespace NonPlanar {

// SlopeTest: calibration of the maximum layer slope (ridges whose slope grows in bands along X).
// Overhangs (Tisma): the layers are tilted only under the overhangs that would need supports (OverhangField).
// Stress (Tisma): the layers follow the stresses of the structural analysis (StressField).
enum class Mode { Disabled, Wave, Conical, SlopeTest, Overhangs, Stress };
enum class Pattern { Egg, Ridges, Twisted };
enum class FlowPolicy { Preserve, Uniform, Off };

class OverhangField;
class StressField;

struct FieldParams
{
    Mode    mode             { Mode::Disabled };
    Pattern pattern          { Pattern::Egg };
    double  amplitude        { 0.6 };   // mm
    double  wavelength       { 16. };   // mm
    double  angle_deg        { 0. };    // direction of ridges / twisted ridges
    double  twist_deg_per_mm { 3. };    // twisted: rotation of the ridges per mm of height
    double  cone_angle_deg   { 15. };   // conical: positive = cone rises outwards
    double  cone_tip_radius  { 2. };    // conical: smoothing of the cone tip
    // Center of the field (object coordinates).
    Vec2d   center           { Vec2d::Zero() };
    // SlopeTest: the slope of the ridges (wavelength above, along Y) is slope_start_deg + i * slope_step_deg in
    // the band i of width slope_band from slope_x0 along X, up to slope_end_deg.
    double  slope_x0         { 0. };
    double  slope_band       { 10. };
    double  slope_start_deg  { 10. };
    double  slope_step_deg   { 5. };
    double  slope_end_deg    { 40. };
    // Overhangs: the layer shape computed from the mesh (shared by the copies of the parameters), scaled by
    // overhang_scale (reduced until the layer thickness and the slope are acceptable).
    std::shared_ptr<const OverhangField> overhangs;
    double  overhang_scale   { 1. };
    // Stress: the layer shape computed by the structural analysis, scaled by stress_scale (reduced until the layer
    // thickness and the slope are acceptable).
    std::shared_ptr<const StressField> stress;
    double  stress_scale     { 1. };
    // Tangent of the slope at x and its derivative along x (smooth transitions between the bands).
    std::pair<double, double> slope_test_tangent(double x) const;
};

// Shape of the layers g(x, y, z) and its derivatives.
class Field
{
public:
    explicit Field(const FieldParams &params) : m_params(params) {}

    double g(double x, double y, double z) const;
    Vec2d  grad(double x, double y, double z) const;
    double dgdz(double x, double y, double z) const;
    // Upper bound of |g| at (x, y) for any z (brackets the inverse transform).
    double bound(double x, double y) const;

    const FieldParams& params() const { return m_params; }

private:
    double theta(double z) const;
    FieldParams m_params;
};

// Flat below z_flat, smooth transition over z_ramp, optionally flat again at the top.
struct Ramp
{
    double z_flat     { 0.6 };
    double z_ramp     { 5. };
    // Height of the object if the top shall be flat again, otherwise empty.
    std::optional<double> z_top;
    double z_ramp_top { 5. };

    double value(double z) const;
    double deriv(double z) const;
};

class Deformation
{
public:
    Deformation(const FieldParams &field, const Ramp &ramp) : m_field(field), m_ramp(ramp) {}

    bool   enabled() const { return m_field.params().mode != Mode::Disabled; }
    double offset(double x, double y, double z) const { return m_field.g(x, y, z) * m_ramp.value(z); }
    // Real space -> slice space.
    double to_slice_z(double x, double y, double z) const { return z - this->offset(x, y, z); }
    // Slice space -> real space (monotonic in z, Newton with bisection fallback).
    double to_real_z(double x, double y, double z_slice) const;
    // dz'/dz, must stay positive for the transformation to be invertible.
    double dzs_dz(double x, double y, double z) const;
    // Real layer thickness / nominal layer thickness.
    double jacobian(double x, double y, double z) const { return 1. / this->dzs_dz(x, y, z); }
    // Slope (tangent) of the real layer surface.
    double layer_slope(double x, double y, double z) const;
    // Unit normal of the real layer surface through (x, y, z): the gradient of the slice coordinate z - D.
    Vec3d  layer_normal(double x, double y, double z) const;

    struct Check {
        double j_min { 1. };
        double j_max { 1. };
        double max_slope_deg { 0. };
    };
    // Samples the deformation over a bounding box (object coordinates).
    Check check(const BoundingBoxf3 &bbox) const;
    // Steepest layer slope relative to the free angles of the print head along X and Y (given as tangents), over a
    // bounding box: <= 1 if the head allows it. The head is modeled as an elliptic cone of these slopes around the
    // nozzle, so a layer of gradient (gx, gy) fits if (gx / tan_x)^2 + (gy / tan_y)^2 <= 1.
    double head_slope_ratio(const BoundingBoxf3 &bbox, double tan_x, double tan_y) const;
    // Maximum |offset| over the XY footprint of the bounding box.
    double max_offset(const BoundingBoxf3 &bbox) const;

    const Field& field() const { return m_field; }
    const Ramp&  ramp()  const { return m_ramp; }

private:
    Field m_field;
    Ramp  m_ramp;
};

// Limits of the layer thickness ratio accepted by validation.
constexpr double J_MIN = 0.6;
constexpr double J_MAX = 1.4;

// Print head around the nozzle (Tisma): its lowest parts (heater block, cooling duct) are `height` above the nozzle
// tip and reach `radius` around the nozzle. The nozzle cone itself is covered by the maximum layer slope.
struct HeadClearance
{
    double height { 0. };
    double radius { 0. };
    // Measured profile (static gauge): (height of the ramp, free angle in degrees), sorted by height. A ramp of that
    // angle up to that height, with a plateau behind, did not touch the head. Empty = use height.
    std::vector<std::pair<double, double>> profile;

    // Highest rise of the part allowed at a horizontal distance from the nozzle tip (within the radius).
    double allowed_rise(double distance) const;
};

// Parses "height:angle;height:angle..." (nonplanar_head_profile); invalid pairs are skipped.
std::vector<std::pair<double, double>> parse_head_profile(const std::string &text);
// From the printer settings (nonplanar_head_clearance_height / _radius / _profile).
HeadClearance head_clearance(const PrintConfig &config);

// Tisma: layers tilted only under the overhangs that would need supports.
//
// An overhang that leans out by more than the printable angle (from the vertical) needs supports with flat layers.
// If the layers sink towards the overhang by an angle b, the overhang leans out by b less relative to them (the layer
// normal tilts the same way as the overhang), so it may print without supports: the supports are generated on the
// deformed mesh, where the overhang is that much less steep. The layer surface sinks as it moves away from the part
// already printed below the overhang (the cross section where the overhang starts), as steep as the print head allows
// along X and Y, up to the farthest point of the overhang, and stays flat inside that cross section. Each group of
// overhangs (a zone, by height) brings its shape in gradually below the overhang and takes it back out above it, so
// that the layer thickness stays within J_MIN..J_MAX. The first layers stay flat.
class OverhangField
{
public:
    struct Zone
    {
        // How much the layers sink (mm) at the nodes of the grid, nx * ny, row by row.
        std::vector<float> sink;
        // Weight of the zone over the height: 0 below z_in0, full from z_in1 to z_out0, 0 above z_out1.
        double z_in0 { 0. }, z_in1 { 0. }, z_out0 { 0. }, z_out1 { 0. };
        // Overhang heights of the zone and the steepest overhang (degrees from the vertical), for reports and tests.
        double z_min { 0. }, z_max { 0. }, steepest_deg { 0. };

        double weight(double z) const;
        double dweight(double z) const;
    };

    Vec2d             origin { Vec2d::Zero() };   // node (0, 0)
    double            cell   { 1. };
    int               nx     { 0 };
    int               ny     { 0 };
    std::vector<Zone> zones;

    bool empty() const { return zones.empty(); }
    // Depth the layers sink at (x, y, z) (>= 0), its horizontal gradient and its derivative along z.
    double sink(double x, double y, double z) const;
    Vec2d  sink_grad(double x, double y, double z) const;
    double sink_dz(double x, double y, double z) const;
    // Upper bound of sink(x, y, z) for any z.
    double bound(double x, double y) const;
    // Value and gradient of a zone at (x, y), bilinear between the nodes.
    std::pair<double, Vec2d> sample(const Zone &zone, double x, double y) const;
};

// Tisma: layers that follow the stresses of the structural analysis (EngineeringLayerField, computed by libtisma_fea).
// Trilinear between the nodes of the grid; zero outside of it.
class StressField
{
public:
    // part_min: minimum corner of the part in the coordinates where the field is sampled.
    StressField(const EngineeringLayerField &field, const Vec3d &part_min);

    double offset(double x, double y, double z) const;
    // Horizontal gradient and derivative along z of the offset.
    Vec2d  grad(double x, double y, double z) const;
    double dz(double x, double y, double z) const;
    // Upper bound of |offset(x, y, z)| for any z.
    double bound(double x, double y) const;

private:
    // Node coordinates (fractional) of a point, false outside of the grid.
    bool   locate(double x, double y, double z, int &i, int &j, int &k, double &tx, double &ty, double &tz) const;
    double node(int i, int j, int k) const { return m_offset[(size_t(k) * m_ny + j) * m_nx + i]; }

    Vec3d              m_origin;   // node (0, 0, 0) in the coordinates of the samples
    double             m_cell { 1. };
    int                m_nx { 0 }, m_ny { 0 }, m_nz { 0 };
    std::vector<float> m_offset;
    // Largest |offset| of each column of nodes.
    std::vector<float> m_column_max;
};

struct OverhangParams
{
    // Steepest overhang that prints well on flat layers, degrees from the vertical.
    double threshold_deg { 45. };
    // Overhangs flatter than this (ceilings, bridges) are left to the bridges and supports.
    double ceiling_deg   { 80. };
    // Free angles of the print head along X and Y (tangents) and its clearance (part already printed nearby).
    double tan_x         { 0. };
    double tan_y         { 0. };
    HeadClearance head;
    // Layers below this height stay flat (first layers on the bed).
    double z_flat        { 0.6 };
};

// Overhang field of a mesh (object coordinates, real space), empty if no overhang needs it.
std::shared_ptr<const OverhangField> build_overhang_field(const indexed_triangle_set &mesh, const OverhangParams &params);
// The model parts of an object transformed to object coordinates (the mesh the non-planar layers are built from).
indexed_triangle_set object_mesh(const std::vector<ModelVolume*> &volumes, const Transform3d &trafo);

struct HeadCollision
{
    bool   collides { false };
    // Highest rise of the part already printed above the nozzle tip within the head radius.
    double rise     { 0. };
    // Horizontal distance from the nozzle to that point.
    double distance { 0. };
    // Nozzle tip position where it happens (object coordinates, real Z).
    Vec3d  nozzle   { Vec3d::Zero() };
};

// Checks whether the print head would hit the part already printed while printing the curved layers. The part
// already printed when printing at a height is bounded by the convex hull of the mesh below that height and by the
// current layer surface (conservative). mesh: object coordinates, real (not deformed) space.
HeadCollision check_head_collision(const Deformation &deformation, const indexed_triangle_set &mesh, const HeadClearance &head);

// Splits the mesh so that no edge is longer than max_edge (watertight, no T-junctions)
// and moves its vertices to slice space.
void deform_mesh(indexed_triangle_set &its, const Deformation &deformation, double max_edge);
// Only the subdivision, exposed for testing.
void subdivide_mesh(indexed_triangle_set &its, double max_edge);

struct GCodeFilterParams
{
    double seg_len           { 0.5 };    // mm, XY sampling of moves
    double z_tolerance       { 0.01 };   // mm, allowed Z error when merging samples
    double filament_diameter { 1.75 };
    double max_feedrate      { 0. };     // mm/min, 0 = no limit
    double min_feedrate      { 300. };   // mm/min
    double z_max_speed       { 0. };     // mm/s, 0 = no limit
    double z_max_accel       { 0. };     // mm/s^2, 0 = no limit
    double max_volumetric    { 0. };     // mm^3/s, 0 = no limit
    // Calibration of the Z speed: above the object bottom, z_max_speed is z_speed_test_start + i * z_speed_test_step
    // in the band i of height z_speed_test_band (0 = no test).
    double z_speed_test_start { 0. };
    double z_speed_test_step  { 0. };
    double z_speed_test_end   { 0. };
    double z_speed_test_band  { 0. };
    // Height above the object bottom where the bands start (after the flat layers and the transition).
    double z_speed_test_offset { 0. };
    FlowPolicy flow_policy   { FlowPolicy::Preserve };
    double uniform_flow      { 0. };     // mm^3/s for FlowPolicy::Uniform
    std::vector<std::string> uniform_exclude { "External perimeter", "Overhang perimeter", "Bridge infill", "Gap fill" };
    // G-code coordinates of the object origin (instance shift) and Z of the object bottom
    // (z_offset + raft).
    Vec2d  origin            { Vec2d::Zero() };
    double z_base            { 0. };
    // Above this slice space Z (top of the deformed object) the Z is only lifted by top_lift
    // (end G-code moves).
    double top_slice_z       { 1e10 };
    double top_lift          { 0. };
};

struct GCodeFilterStats
{
    size_t moves      { 0 };
    size_t lines_out  { 0 };
    double j_min      { 1e10 };
    double j_max      { 0. };
    size_t z_limited  { 0 };
};

// Transforms planar G-code (slice space) into non-planar G-code, layer by layer.
// Requires absolute XYZ, relative E and no arcs (checked by Print::validate()).
class GCodeFilter
{
public:
    GCodeFilter(const Deformation &deformation, const GCodeFilterParams &params);

    std::string process_layer(const std::string &gcode);
    const GCodeFilterStats& stats() const { return m_stats; }

private:
    void   process_line(const std::string &line, std::string &out);
    void   process_move(const std::string &line, std::string &out);
    double real_z(double x, double y, double z_slice) const;
    double target_flow() const;
    double z_limit(double feed, double seg_xy, double seg3, double dz, double curvature);
    void   emit_feed(double feed, std::string &out);

    const Deformation &m_deformation;
    GCodeFilterParams  m_params;
    GCodeFilterStats   m_stats;
    double             m_filament_area;

    // Current position in slice space (G-code coordinates).
    std::optional<double> m_x, m_y;
    double             m_z { 0. };
    double             m_feed { 1500. };
    std::optional<long> m_out_feed;
    bool               m_absolute_xyz { true };
    std::string        m_feature;
};

// What the print head allows (Tisma): free angles along X and Y as tangents and the clearance height (0 = unknown).
struct HeadLimits
{
    double tan_x  { 0. };
    double tan_y  { 0. };
    double height { 0. };
};
// From the printer settings: nonplanar_head_free_angle_x / _y, or the maximum layer slope where not measured.
HeadLimits head_limits(const PrintConfig &config);

// The largest wave amplitude / cone angle the print head allows: the layer slope within the free angles along X and Y,
// the wave height (peak to peak) within the clearance height and the layer thickness within J_MIN..J_MAX.
// Conical: the sign of the cone angle (rising outwards or inwards) is kept.
FieldParams fit_to_head(FieldParams field, const Ramp &ramp, const BoundingBoxf3 &object_bbox, const HeadLimits &head);

// Deformation for a print object, from the print configuration (fitted to the print head if nonplanar_fit_head).
// object_bbox: bounding box of the object in object (centered) coordinates. mesh: the object in the same coordinates,
// needed by the overhangs mode (without it the layers stay flat). stress: the layers computed by the structural
// analysis for the object, needed by the stresses mode (without it, or if the part changed since, the layers stay
// flat).
Deformation make_deformation(const PrintConfig &config, const BoundingBoxf3 &object_bbox,
                             const indexed_triangle_set *mesh = nullptr, const EngineeringLayerField *stress = nullptr);
// Whether the layers computed by the structural analysis still fit the part (same size of its bounding box).
bool layer_field_matches(const EngineeringLayerField &field, const BoundingBoxf3 &object_bbox);
// Mesh subdivision length suited to the configured field.
double mesh_max_edge(const PrintConfig &config);
GCodeFilterParams make_filter_params(const PrintConfig &config);
bool enabled(const PrintConfig &config);
// The calibration test of the non-planar layers (calib_mode), which replaces the non-planar settings.
bool calibration_test(const PrintConfig &config);
// Field of the calibration of the maximum slope (relative to x0 = 0 and the center set by the caller).
FieldParams slope_test_field(double start_deg, double step_deg, double end_deg, double band);
// Height above the bottom of the object where the calibration tests reach their full deformation.
double nonplanar_test_start_height(const PrintConfig &config);

} // namespace NonPlanar
} // namespace Slic3r

#endif // slic3r_NonPlanar_hpp_
