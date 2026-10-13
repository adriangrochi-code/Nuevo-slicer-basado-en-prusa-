///|/ Tisma Slicer: editable material table of the structural analysis (Engineering).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FeaMaterialsDialog_hpp_
#define slic3r_FeaMaterialsDialog_hpp_

#include <string>

class wxWindow;

namespace Slic3r {
namespace GUI {

// Path of the materials of the user: <data dir>/fea_materials.json.
std::string fea_materials_path();
// Loads the materials of the user into the table of the analysis (Fea::set_user_materials()). Once; again with
// force. Returns false and the reason when the file exists but cannot be read.
bool        load_fea_materials(std::string &error, bool force = false);
// Saves the materials of the user of the table of the analysis.
bool        save_fea_materials(std::string &error);
// Opens the table editor. Returns true when the table changed.
bool        edit_fea_materials(wxWindow *parent);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_FeaMaterialsDialog_hpp_
