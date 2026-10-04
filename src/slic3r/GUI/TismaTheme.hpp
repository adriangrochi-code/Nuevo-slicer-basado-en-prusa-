///|/ Tisma Slicer: colors of the interface (PrusaSlicer 3.0 style with the Tisma palette).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_TismaTheme_hpp_
#define slic3r_TismaTheme_hpp_

#include <wx/colour.h>

namespace Slic3r {
namespace GUI {
namespace TismaTheme {

// Brand
inline wxColour accent()            { return wxColour(0x7A, 0x24, 0xC9); }
inline wxColour accent_hover()      { return wxColour(0x8E, 0x34, 0xDF); }
inline wxColour accent_pressed()    { return wxColour(0x5B, 0x0F, 0xA7); }
inline wxColour accent_text(bool dark) { return dark ? wxColour(0xC9, 0xA2, 0xF5) : wxColour(0x7A, 0x24, 0xC9); }

// Panels
inline wxColour panel_bg(bool dark)     { return dark ? wxColour(0x1F, 0x1F, 0x24) : wxColour(0xF7, 0xF7, 0xF9); }
inline wxColour card_bg(bool dark)      { return dark ? wxColour(0x27, 0x27, 0x2D) : wxColour(0xFF, 0xFF, 0xFF); }
inline wxColour separator(bool dark)    { return dark ? wxColour(0x34, 0x34, 0x3C) : wxColour(0xE2, 0xE2, 0xE8); }
inline wxColour text(bool dark)         { return dark ? wxColour(0xEC, 0xEC, 0xF0) : wxColour(0x1A, 0x1A, 0x1F); }
inline wxColour text_muted(bool dark)   { return dark ? wxColour(0x9A, 0x9A, 0xA6) : wxColour(0x6B, 0x6B, 0x76); }
inline wxColour disabled_bg(bool dark)  { return dark ? wxColour(0x3A, 0x3A, 0x42) : wxColour(0xD9, 0xD9, 0xDF); }
inline wxColour disabled_text(bool dark){ return dark ? wxColour(0x7A, 0x7A, 0x84) : wxColour(0x9A, 0x9A, 0xA4); }

} // namespace TismaTheme
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_TismaTheme_hpp_
