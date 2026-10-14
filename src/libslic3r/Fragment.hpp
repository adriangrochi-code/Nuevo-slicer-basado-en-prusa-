///|/ Tisma Slicer: splitting an object larger than the printer into pieces joined by connectors.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Fragment_hpp_
#define slic3r_Fragment_hpp_

// An object that does not fit the printable volume is cut by planes perpendicular to X, Y and Z into pieces that fit,
// and every cut gets connectors so that the pieces align when glued (docs/FRAGMENT.md):
// - the number of pieces along an axis is the least that fits; the planes are placed where the cross section of the
//   object is smallest (less to glue and a shorter seam) among the positions that keep every piece within the
//   printable size, preferring even pieces when the sections are alike;
// - the connectors are those of the cut tool (Plug: a peg on one piece and a hole on the other; Dowel: holes on both
//   pieces and a separate pin), placed on each section of each pair of pieces, away from the outline and from the
//   other cuts, so they never cross another plane; where a wall is too thin for the diameter asked, a thinner one;
// - the cuts are made by the cut tool (Cut::perform_with_plane()), so the pieces are the same as when cut by hand.

#include <array>
#include <string>
#include <vector>

#include "BoundingBox.hpp"
#include "Point.hpp"

namespace Slic3r {

class Model;
class ModelObject;
using ModelObjectPtrs = std::vector<ModelObject*>;

namespace Fragment {

enum class Joint { None, Plug, Dowel };

struct Params
{
    Vec3d  max_size{ 250., 210., 220. };  // printable size, mm
    double margin{ 5. };                  // left free on the bed around each piece, mm
    Joint  joint{ Joint::Plug };
    double joint_diameter{ 8. };          // mm; smaller (down to min_joint_diameter) where the section is thin
    double min_joint_diameter{ 3. };      // mm
    double joint_depth{ 6. };             // into each piece, mm
    double clearance{ 0.15 };             // radial and axial gap of the holes, mm
    double joint_wall{ 2. };              // least wall around a connector, mm
    int    max_joints_per_section{ 4 };   // per island of each section between two pieces
};

struct Connector
{
    Vec3d  position;                     // centre on the plane, world coordinates
    double diameter{ 0. };               // mm
};

struct CutPlane
{
    int                axis{ 0 };        // 0 = X, 1 = Y, 2 = Z
    double             position{ 0. };   // world coordinate along the axis
    double             area{ 0. };       // of the cross section, mm2
    std::vector<Connector> connectors;
    int                sections_without_joint{ 0 };  // islands too thin for a connector: glue only
};

struct Plan
{
    BoundingBoxf3         bbox;          // of the instance, world coordinates
    std::array<int, 3>    pieces{ 1, 1, 1 };
    std::vector<CutPlane> cuts;          // X planes first, then Y, then Z
    std::string           error;         // why the object cannot be split, empty when it can

    bool   needed() const { return pieces[0] * pieces[1] * pieces[2] > 1; }
    int    piece_count() const { return pieces[0] * pieces[1] * pieces[2]; }
    size_t joint_count() const;
};

// Plans the cuts of an instance of the object. Throws nothing; a plan that cannot be made has an error.
Plan plan(const ModelObject &object, size_t instance, const Params &params);

// Cuts a copy of the object (its instance only) as planned. The pieces (and the pins of dowels) are added to out and
// returned; the object itself is not changed. Empty when the plan is not needed or has an error.
ModelObjectPtrs apply(const ModelObject &object, size_t instance, const Plan &plan, const Params &params, Model &out);

} // namespace Fragment
} // namespace Slic3r

#endif // slic3r_Fragment_hpp_
