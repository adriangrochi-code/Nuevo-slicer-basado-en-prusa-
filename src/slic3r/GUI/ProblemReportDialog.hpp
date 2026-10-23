///|/ Tisma Slicer: "Report a problem": what the user found, sent to Tisma Cloud for the programmers (see the reports of
///|/ the server, server/README.md).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_ProblemReportDialog_hpp_
#define slic3r_GUI_ProblemReportDialog_hpp_

#include <string>

class wxWindow;

namespace Slic3r {
namespace GUI {

// What is attached to a report when the user agrees: the version, the system, the printer in use and the last lines of
// the log of this session.
std::string problem_report_attachment();

void show_problem_report_dialog(wxWindow *parent);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ProblemReportDialog_hpp_
