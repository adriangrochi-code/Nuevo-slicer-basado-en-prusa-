///|/ Tisma Slicer: approximate aerodynamic analysis of a printed part (Engineering workspace).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Aero_hpp_
#define tisma_fea_Aero_hpp_

#include <functional>
#include <string>
#include <vector>

#include <libslic3r/Point.hpp>
#include <admesh/stl.h>

namespace Slic3r {
namespace Fea {

// Units: mesh in mm, air in SI units.
//
// The drag is split in two parts:
// - Pressure (form) drag, lift and the pressure map: lattice Boltzmann simulation (D3Q19, BGK with a Smagorinsky
//   subgrid model) of the air around the voxelized part. The grid cannot resolve the boundary layer of a real part at
//   a real speed, so the simulation runs at a Reynolds number limited by the resolution (see AeroResult::sim_reynolds);
//   the pressure drag of bluff shapes changes little above Re ~ 1000.
// - Skin friction: flat plate correlations along the flow on every face, with the equivalent sand roughness of the
//   printed surface (layer steps on sloped faces) at the real Reynolds number.
// It is meant to compare designs and orientations, not to replace a wind tunnel.
struct AeroSetup
{
    // Direction the air moves to, relative to the part (in the coordinates of the mesh). Not normalized.
    Vec3d  flow_direction { 1., 0., 0. };
    double speed          { 10. };       // m/s
    double air_density    { 1.204 };     // kg/m³ (20 °C, sea level)
    double air_viscosity  { 1.81e-5 };   // Pa·s
    // Printing: layer height for the roughness of the faces (0 = smooth faces).
    double layer_height   { 0.2 };       // mm
    // Cells of the lattice across the largest frontal size of the part.
    int    resolution     { 24 };
    // Lateral free space around the part, in frontal sizes on each side (blockage).
    double lateral_margin { 1.5 };
    // Flow-through times of the domain to simulate (the force is averaged over the last third).
    double flow_throughs  { 3. };
    // Upper limit of the simulated Reynolds number (stability at the given resolution); 0 = automatic.
    double max_sim_reynolds { 0. };
    // false: skin friction and roughness only (quick estimate, no simulation of the pressure).
    bool   simulate_pressure { true };

    std::function<bool()>     cancel;
    std::function<void(int)>  progress;   // percent
};

struct AeroResult
{
    bool        ok { false };
    std::string error;

    double reynolds      { 0. };   // of the real flow, on the frontal size
    double sim_reynolds  { 0. };   // of the simulation
    double frontal_area  { 0. };   // mm², projected on the plane normal to the flow
    double wetted_area   { 0. };   // mm²
    double length        { 0. };   // mm, along the flow
    double frontal_size  { 0. };   // mm, largest size across the flow

    double cd_pressure   { 0. };   // on the frontal area
    double cd_friction   { 0. };
    double cd            { 0. };
    double cl            { 0. };   // force across the flow, on the frontal area (magnitude)
    Vec3d  force         { 0., 0., 0. };   // N, in mesh coordinates (pressure + friction)
    double drag          { 0. };   // N, along the flow
    // Share of the skin friction caused by the roughness of the printed surface (0 = as smooth).
    double roughness_friction_share { 0. };
    double mean_roughness_ra { 0. };   // µm, area weighted

    int    iterations    { 0 };
    bool   converged     { false };

    // Pressure coefficient at every vertex of the input mesh, and roughness (Ra, µm) of every triangle.
    std::vector<float> vertex_cp;
    std::vector<float> triangle_ra;
};

// Average roughness Ra [mm] of a printed face with unit normal n (Z = build direction): the steps of the layers on
// sloped faces (sawtooth of height layer_height * |n_z|, Ra = Rz / 4) and the rounded edge of the beads (about 5 %
// of the layer height) on walls, tops and bottoms.
double printed_roughness_ra(double layer_height, const Vec3d &normal);
// Equivalent sand grain roughness [mm] from Ra (ks ~ 4 Ra).
inline double equivalent_sand_roughness(double ra) { return 4. * ra; }
// Local skin friction coefficient on a flat plate at distance x [mm] from the leading edge: laminar (Blasius),
// smooth turbulent, or fully rough (Schlichting, the largest of the turbulent ones). The flow is turbulent above
// Re_x = 5e5 or when the roughness trips it (U ks / nu > 300).
double local_skin_friction(double x, double ks, double speed, double kinematic_viscosity);

AeroResult analyze_aero(const indexed_triangle_set &mesh, const AeroSetup &setup);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Aero_hpp_
