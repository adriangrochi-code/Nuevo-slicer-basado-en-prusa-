///|/ Tisma Slicer: dialog of "Split to fit the printer" (pieces joined by connectors).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FragmentDialog_hpp_
#define slic3r_FragmentDialog_hpp_

#include <cstddef>

#include "libslic3r/Fragment.hpp"

class wxWindow;

namespace Slic3r {

class ModelObject;

namespace GUI {

// Asks for the connectors and shows the pieces they give. params.max_size is the printable size; on OK params holds
// the connectors chosen (also remembered for the next time) and plan the cuts to make.
bool show_fragment_dialog(wxWindow *parent, const ModelObject &object, size_t instance, Fragment::Params &params,
                          Fragment::Plan &plan);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_FragmentDialog_hpp_
