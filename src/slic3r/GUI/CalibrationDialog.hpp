///|/ Tisma Slicer: calibration suite (temperature, pressure advance, retraction, speeds, ...).
///|/ Inspired by the calibrations of OrcaSlicer (AGPLv3). The models are generated, not loaded from files.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationDialog_hpp_
#define slic3r_CalibrationDialog_hpp_

class wxWindow;
class wxMenu;

namespace Slic3r {
namespace GUI {

// Menu with all the calibration tests. Selecting one opens its dialog and loads the test in the plater.
wxMenu* create_calibration_menu(wxWindow* parent);
// Opens the menu at the mouse position.
void    show_calibration_menu(wxWindow* parent);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_CalibrationDialog_hpp_
