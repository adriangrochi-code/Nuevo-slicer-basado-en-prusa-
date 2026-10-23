///|/ Copyright (c) Prusa Research 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

#include <wx/event.h>

namespace Slic3r {
namespace GUI {

#if __APPLE__
extern bool mac_dark_mode();
// Tisma: the appearance of the application: 0 the one of the system, 1 light, 2 dark (macOS 10.14 and newer).
extern void mac_set_appearance(int mode);
extern double mac_max_scaling_factor();
#endif


} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
