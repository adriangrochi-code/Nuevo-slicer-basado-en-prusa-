///|/ Tisma Slicer: B-Rep origin of meshes imported from STEP files (phase 4 of docs/IMPLEMENTATION_ROADMAP.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CadSource_hpp_
#define slic3r_CadSource_hpp_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "TriangleSelector.hpp"

namespace Slic3r {

class ModelVolume;
class TriangleMesh;

// Contents of a STEP file kept with the project. All the volumes created from the same file share it.
struct CadStepFile
{
    // Original file name, without the path.
    std::string name;
    // Contents of the file.
    std::string data;
    // Key of the file in the registry and name of its entry in the 3MF (hash of the contents).
    std::string key;
};

// Registers the contents of a STEP file, or returns the already registered file with the same contents.
// The registry keeps the files alive for the whole session, so that Undo / Redo can find them by key.
std::shared_ptr<const CadStepFile> cad_step_file_register(std::string name, std::string data);
// Returns the registered file with the key, or nullptr.
std::shared_ptr<const CadStepFile> cad_step_file_find(const std::string &key);

// B-Rep origin of the mesh of a ModelVolume.
struct CadSource
{
    std::shared_ptr<const CadStepFile> step;
    // Index of the solid in the STEP file (see OCCTVolume::solid_index).
    int                                solid_index { -1 };
    // Tessellation used for the current mesh.
    double                             linear_deflection  { 0. };
    double                             angular_deflection { 0. };
    // Index of the B-Rep face of every triangle of the mesh, -1 when unknown.
    std::vector<int>                   face_ids;
    // Number of B-Rep faces of the solid.
    int                                face_count { 0 };
    // Result of the B-Rep check of the solid when it was imported.
    bool                               brep_valid { true };
    std::string                        brep_report;

    // The face ids refer to the triangles of the mesh (same number of triangles).
    bool matches(const TriangleMesh &mesh) const;
};

// Face ids stored as runs "face*count", separated by spaces. Consecutive triangles of a tessellation mostly
// belong to the same face, so the text stays short.
std::string      cad_face_ids_to_string(const std::vector<int> &face_ids);
bool             cad_face_ids_from_string(const std::string &str, std::vector<int> &face_ids);
// Converts the 16 bit tags written by the OCCT wrapper to face ids (-1 for OCCT_FACE_ID_NONE).
std::vector<int> cad_face_ids_from_tags(const std::vector<uint16_t> &tags);

struct CadRemapStats
{
    // Faces whose painting was uniform: copied exactly.
    size_t faces_exact       { 0 };
    // Faces painted only in part: every new triangle takes the state of the closest painted part of the face.
    size_t faces_approximate { 0 };
    // New triangles without a known face, reprojected by distance to the whole old painting.
    size_t triangles_unmatched { 0 };
};

// Reprojects a painting (supports, seam, multi-material, fuzzy skin) from a tessellation of a B-Rep to another
// tessellation of the same B-Rep, using the face of every triangle. Both meshes must be in the same coordinates.
// The result is written into new_selector, which must be built over new_mesh.
void cad_remap_painting(const TriangleMesh &old_mesh, const std::vector<int> &old_face_ids,
                        const TriangleSelector::TriangleSplittingData &old_data,
                        const TriangleMesh &new_mesh, const std::vector<int> &new_face_ids,
                        TriangleSelector &new_selector, CadRemapStats *stats = nullptr);

// Tessellates a solid of a STEP file. Returns false and an error message on failure.
bool cad_tessellate_solid(const CadStepFile &step, int solid_index, double linear_deflection, double angular_deflection,
                          TriangleMesh &mesh_out, std::vector<int> &face_ids_out, std::string &error);

// Tessellates again the volume from its STEP source with other deflections. The placement of the volume is kept
// (also after moves, mirroring or scaling of its mesh) and its painting is reprojected face by face.
// Returns false and an error message when the volume has no usable CAD source or its mesh was edited.
bool cad_retessellate_volume(ModelVolume &volume, double linear_deflection, double angular_deflection,
                             CadRemapStats *stats, std::string &error);

} // namespace Slic3r

#endif // slic3r_CadSource_hpp_
