///|/ Tisma Slicer: calibration suite (temperature, pressure advance, retraction, speeds, ...).
///|/ Inspired by the calibrations of OrcaSlicer (AGPLv3). The models are generated, not loaded from files.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationDialog_hpp_
#define slic3r_CalibrationDialog_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/string.h>

class wxWindow;
class wxMenu;

namespace Slic3r {
namespace GUI {

// Menu with all the calibration tests (FFF, or resin with resin = true). Selecting one opens its dialog and loads
// the test in the plater. With models_only, only the tests that load a model (the right click menu of the plate):
// without the dialogs that apply a result.
wxMenu* create_calibration_menu(wxWindow* parent, bool resin = false, bool models_only = false);
// Opens the menu at the mouse position.
void    show_calibration_menu(wxWindow* parent);

// A calibration as a tool of the Calibrate group of the plate: a short name under its icon, the title and what it is
// for in the tooltip, and what it opens (the dialog of the test, or of applying a result).
struct CalibrationAction
{
    wxString              label;
    wxString              tooltip;
    std::string           icon;
    std::function<void()> run;
};
// All the calibrations of filament printers (or of resin printers with resin = true), as in the menu.
std::vector<CalibrationAction> calibration_actions(wxWindow* parent, bool resin);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_CalibrationDialog_hpp_
