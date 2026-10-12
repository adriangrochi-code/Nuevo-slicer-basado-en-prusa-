
#ifndef occtwrapper_OCCTWrapper_hpp_
#define occtwrapper_OCCTWrapper_hpp_

#include <array>
#include <string>
#include <vector>
#include <utility>
#include <optional>

struct stl_facet;

namespace Slic3r {

// Tisma: facet.extra of every facet holds the index of the B-Rep face it was tessellated from, as a little endian
// uint16 (OCCT_FACE_ID_NONE when the solid has more faces than the 16 bits can hold). The index counts the faces
// of the solid in TopExp_Explorer order, which is stable for the same file and OCCT version.
constexpr unsigned int OCCT_FACE_ID_NONE = 0xFFFF;

struct OCCTVolume {
    std::string            volume_name;
    std::vector<stl_facet> facets;
    // Tisma: index of the solid in the file (counting all the solids found, also the ones without triangles).
    int                    solid_index { -1 };
    // Tisma: number of B-Rep faces of the solid.
    int                    face_count  { 0 };
    // Tisma: result of BRepCheck_Analyzer on the solid, and a short description of the problems found.
    bool                   brep_valid  { true };
    std::string            brep_report;
};

struct OCCTResult {
    std::string error_str;
    std::string object_name;
    std::vector<OCCTVolume> volumes;
};

using LoadStepFn = bool (*)(const char *path, OCCTResult* occt_result, std::optional<std::pair<double, double>> deflections);

// Tisma: default tessellation of load_step_internal (linear deflection [mm], angular deflection [rad]).
constexpr double OCCT_DEFAULT_LINEAR_DEFLECTION  = 0.005;
constexpr double OCCT_DEFAULT_ANGULAR_DEFLECTION = 1.;

}; // namespace Slic3r

#endif // occtwrapper_OCCTWrapper_hpp_
