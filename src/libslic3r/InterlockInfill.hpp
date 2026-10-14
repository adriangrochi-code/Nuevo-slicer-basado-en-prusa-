///|/ Tisma Slicer: interlocking non-planar infill.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_InterlockInfill_hpp_
#define slic3r_InterlockInfill_hpp_

// Interlocking non-planar infill: the sparse infill of every layer follows the same egg-crate surface in Z,
// z = layer + a * sin(2 pi x / wavelength) * sin(2 pi y / wavelength), so that the layers of the infill nest into each
// other instead of meeting on flat planes, where printed parts usually break. The perimeters, the solid infill and the
// top and bottom surfaces stay flat: the amplitude a goes down to zero next to them, by at most a fifth of the layer
// height from one layer to the next and over a quarter of the wavelength in the layer, and the flow follows the
// thickness left between the layers. The idea of wavy layers for strength is that of Active-Z printing (Penn State,
// 2017) and of the sinuous interlayer interfaces (Western Sydney University); this implementation is Tisma's own: a
// filter of the G-code of each layer.

#include <optional>
#include <string>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r {

class Print;

namespace Interlock {

struct Params
{
    // Amplitude, in fraction of the layer height of each object (up to 1).
    double amplitude  { 0.5 };
    // Period of the egg-crate surface along X and along Y, mm.
    double wavelength { 10. };
};

// The amplitude of the waves over the sparse infill of each object of the print, in G-code coordinates.
class Field
{
public:
    Field(const Print &print, const Params &params);

    struct Layer
    {
        double print_z { 0. };   // without the Z offset
        // Amplitude factor (0..255) per cell, empty: zero everywhere.
        std::vector<uint8_t> amplitude;
    };
    struct Object
    {
        // Grid in the coordinates of the object.
        Vec2d  origin { Vec2d::Zero() };
        double cell { 1. };
        int    nx { 0 }, ny { 0 };
        // Largest offset, mm.
        double amplitude { 0. };
        std::vector<Vec2d> shifts;   // of the instances, G-code = object + shift
        std::vector<Layer> layers;
    };

    // Offset of the infill (mm) at (x, y) of the G-code in a layer of an object (index of the layer, -1: none).
    double offset(const Object &object, int layer, double x, double y) const;
    // The object and its layer printed at print_z (without the Z offset) whose grid contains (x, y).
    std::optional<std::pair<const Object*, int>> find(double x, double y, double print_z) const;

    const std::vector<Object>& objects() const { return m_objects; }
    const Params& params() const { return m_params; }

private:
    // Amplitude factor (0..1) at (x, y) of the object, bilinear.
    double factor(const Object &object, int layer, const Vec2d &p) const;

    Params              m_params;
    std::vector<Object> m_objects;
};

struct FilterParams
{
    // Moves are split every this length to follow the waves.
    double seg_len     { 0.5 };
    bool   relative_e  { true };
    double z_offset    { 0. };
    double z_tolerance { 0.002 };
};

struct FilterStats
{
    size_t waved      { 0 };   // infill moves on the waves
    double max_offset { 0. };  // largest |offset| emitted, mm
};

// Puts the sparse infill on the waves. Absolute XYZ, relative or absolute E, no arcs.
class Filter
{
public:
    Filter(const Field &field, const FilterParams &params);

    std::string process_layer(const std::string &gcode);
    const FilterStats& stats() const { return m_stats; }

private:
    void process_line(const std::string &line, std::string &out);
    void move_z(double z, std::string &out);

    const Field  &m_field;
    FilterParams  m_params;
    FilterStats   m_stats;

    std::optional<double> m_x, m_y;
    double m_layer_z    { 0. };    // ;Z: of the layer (with the Z offset)
    double m_height     { 0. };
    double m_stream_z   { 0. };    // Z of the G-code
    double m_out_z      { 0. };    // Z where the nozzle is
    // Highest nozzle Z of the waved infill since the last layer change downwards: the travels stay above it.
    double m_crest_z    { 0. };
    bool   m_infill     { false };
    bool   m_relative_e { true };
    double m_e_in       { 0. };
    double m_e_shift    { 0. };
};

} // namespace Interlock
} // namespace Slic3r

#endif // slic3r_InterlockInfill_hpp_
