// Structural analysis of the Tisma Android app: the analysis of libtisma_fea (the one of the Engineering gizmo of the
// desktop program) for the first object of a model file, with fixed faces and loads chosen on the phone.

#ifndef tisma_android_engineering_hpp_
#define tisma_android_engineering_hpp_

#include <functional>
#include <string>

namespace Slic3r {
class DynamicPrintConfig;
}

namespace TismaApp {

// request: JSON (see EngineeringRequest in Engineering.kt). Points and normals are in the coordinates of the file.
// Writes the failure index of the voxels to field_path (format in Engineering.kt, FieldMap) and returns the result as
// JSON. Throws Slic3r::RuntimeError on errors.
std::string analyze(const std::string &model_path, const Slic3r::DynamicPrintConfig &config, const std::string &request,
                    const std::string &field_path, std::function<void(int)> progress, std::function<bool()> cancel);

// The materials of the analysis: [{"key":"PLA","name":"PLA"},...]
std::string materials_json();

} // namespace TismaApp

#endif // tisma_android_engineering_hpp_
