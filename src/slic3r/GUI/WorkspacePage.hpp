///|/ Tisma Slicer: pages of the advanced workspaces (Engineering, Structures).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_WorkspacePage_hpp_
#define slic3r_WorkspacePage_hpp_

#include <vector>

#include <wx/scrolwin.h>
#include <wx/string.h>

namespace Slic3r {
namespace GUI {

// Page of a workspace whose tools are not implemented yet. It describes the planned workflow and the
// development phase of each step, without controls that would pretend to work.
class WorkspacePage : public wxScrolledWindow
{
public:
    struct Step {
        wxString title;
        wxString description;
        int      phase;     // development phase of docs/IMPLEMENTATION_ROADMAP.md
    };

    WorkspacePage(wxWindow* parent, const wxString& title, const wxString& intro, const std::vector<Step>& steps);

    void sys_color_changed();

private:
    void apply_colors();
};

// The page of the Structures workspace (adaptive infill, lattice, reinforcements). Engineering works on the 3D view
// (GLGizmoEngineering).
WorkspacePage* create_structures_page(wxWindow* parent);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_WorkspacePage_hpp_
