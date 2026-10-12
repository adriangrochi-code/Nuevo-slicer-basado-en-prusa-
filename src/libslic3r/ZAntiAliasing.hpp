///|/ Tisma Slicer: Z anti-aliasing of the top surfaces.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ZAntiAliasing_hpp_
#define slic3r_ZAntiAliasing_hpp_

// Z anti-aliasing: flat layers print a sloped top surface as stairs, each layer is either full height or nothing.
// Where the surface of the model crosses the layer (between its bottom and its top), the extrusions of the visible
// features (top infill, ironing, perimeters, gap fill) are lowered to the surface and their flow reduced to the
// thickness left, so that the top follows the model instead of the stairs. The idea is that of Song, Ray, Sokolov and
// Lefebvre, "Anti-aliasing for fused filament deposition", Computer-Aided Design 89, 2017; this is Tisma's own
// simpler implementation: a filter of the G-code of each layer that casts a ray up from the bottom of the layer.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Point.hpp"

struct indexed_triangle_set;

namespace Slic3r {

class Print;

namespace ZAA {

// The model in G-code coordinates, to find its top surface.
class Surface
{
public:
    explicit Surface(indexed_triangle_set mesh);
    ~Surface();

    // Height of the top surface of the model above (x, y) if it lies between z_bottom and z_top (the model is solid
    // from z_bottom up to there and empty above), otherwise nothing.
    std::optional<double> top_in_slab(double x, double y, double z_bottom, double z_top) const;

private:
    std::vector<Vec3f> m_vertices;
    std::vector<Vec3i> m_faces;
    AABBTreeIndirect::Tree3f m_tree;
};

// The model parts of all the objects of the print, in G-code coordinates.
indexed_triangle_set print_mesh(const Print &print);

struct FilterParams
{
    // Moves are split every this length to follow the surface.
    double seg_len           { 0.3 };
    // The thinnest extrusion, relative to the layer height.
    double min_thickness     { 0.25 };
    // Relative E (M83) at the start, from the configuration: the start G-code does not pass through the filter.
    bool   relative_e        { true };
    // Changes of Z smaller than this are not emitted.
    double z_tolerance       { 0.005 };
    // The features that are lowered (;TYPE: tags of the G-code).
    std::vector<std::string> types { "Top solid infill", "Ironing", "External perimeter", "Perimeter",
                                     "Overhang perimeter", "Gap fill" };
};

struct FilterStats
{
    size_t lowered    { 0 };   // extrusion moves lowered (at least partly)
    double min_z_drop { 0. };  // largest drop below the layer top (negative)
};

// Lowers the extrusions of each layer to the top surface of the model. Absolute XYZ, relative or absolute E, no arcs.
class Filter
{
public:
    Filter(const Surface &surface, const FilterParams &params);

    std::string process_layer(const std::string &gcode);
    const FilterStats& stats() const { return m_stats; }

private:
    void process_line(const std::string &line, std::string &out);

    const Surface &m_surface;
    FilterParams   m_params;
    FilterStats    m_stats;

    // Position from the G-code (planar) and the Z where the nozzle actually is.
    std::optional<double> m_x, m_y;
    double m_z      { 0. };
    double m_out_z  { 0. };
    double m_height { 0. };
    bool   m_lower_type { false };
    bool   m_relative_e { true };
    // Absolute E: the E emitted minus the E of the G-code.
    double m_e_in   { 0. };
    double m_e_shift { 0. };
};

} // namespace ZAA
} // namespace Slic3r

#endif // slic3r_ZAntiAliasing_hpp_
