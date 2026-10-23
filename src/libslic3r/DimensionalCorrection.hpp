///|/ Tisma Slicer: the corrections of the sizes of the prints from a measured test block.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DimensionalCorrection_hpp_
#define slic3r_DimensionalCorrection_hpp_

#include <vector>

namespace Slic3r {

// Tisma: the corrections of the sizes of the prints (Calibration > Dimensional accuracy), from the sizes of a test
// block measured with a caliper. A printed size is a line of the designed one:
//     measured = slope * nominal + offset
// The slope is the scale (the plastic shrinks while it cools down, the steps per mm may be off): it grows with the size.
// The offset does not: the outer line of the walls is wider than its centre (outer sizes bigger, holes smaller), the
// first layer is squashed (heights). With several sizes both are told apart.

struct LinearFit
{
    double slope  { 1. };
    double offset { 0. };
    // Root mean square of what the line does not explain: the noise of the printer and of the caliper.
    double rms    { 0. };
    // Points used (the ones with a measured value).
    int    points { 0 };
};

// Least squares line through the points with a measured value above zero (the others were not measured). With a single
// point the slope stays 1 and only the offset is fitted.
LinearFit fit_line(const std::vector<double> &nominal, const std::vector<double> &measured);

// The settings the test block was printed with.
struct DimensionalSettings
{
    double shrinkage_xy_percent { 0. }; // filament_shrinkage_compensation_xy
    double shrinkage_z_percent  { 0. }; // filament_shrinkage_compensation_z
    double xy_size              { 0. }; // xy_size_compensation, mm per side (positive: outer sizes bigger, holes smaller)
    double xy_hole              { 0. }; // xy_hole_compensation, mm per side (positive: holes bigger)
    double elephant_foot        { 0. }; // elefant_foot_compensation, mm per side of the first layer
};

// The sizes of the block (zero: not measured).
struct DimensionalMeasures
{
    std::vector<double> outer_nominal;  // widths of the steps
    std::vector<double> outer_x;        // measured from left to right as printed
    std::vector<double> outer_y;        // measured from front to back
    std::vector<double> hole_nominal;   // diameters of the holes
    std::vector<double> holes;
    std::vector<double> height_nominal; // heights of the top of each step over the bed
    std::vector<double> heights;
    // Width from left to right of the widest step at its first layer (zero: not measured), to compare with the same
    // width measured higher up for the elephant foot.
    double              first_layer_width { 0. };
};

struct DimensionalResult
{
    // The fits of the outer sizes (X, Y), of the holes (the slope of the outer sizes, its own offset) and of the heights.
    LinearFit x, y, holes, z;
    bool      xy_ok    { false }; // at least two outer sizes in X and in Y
    bool      holes_ok { false }; // the outer sizes and at least one hole
    bool      z_ok     { false }; // at least two heights
    bool      foot_ok  { false }; // the first layer width and the widest step
    // The settings that make the next print as designed (the same meaning as DimensionalSettings).
    DimensionalSettings settings;
    // Height the first layers add (+) or take (-), left to the Z offset or the first layer calibration.
    double    z_offset { 0. };
};

// The corrected settings from the measures of a block printed with the given settings. Settings whose measures are
// missing keep their value.
DimensionalResult dimensional_correction(const DimensionalMeasures &measures, const DimensionalSettings &printed_with);

} // namespace Slic3r

#endif // slic3r_DimensionalCorrection_hpp_
