///|/ Tisma Slicer: the groups of tools of the plate and their tools, at the left of the 3D view (the same arrangement
///|/ as the Android app in landscape).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PlateTools_hpp_
#define slic3r_PlateTools_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/panel.h>
#include <wx/timer.h>

namespace Slic3r {
namespace GUI {

// Two columns: the groups (an icon over its name), then the tools of the group chosen. The state of the tools
// (enabled, selected) is polled, so they follow the plater without extra notifications.
class PlateTools : public wxPanel
{
public:
    struct Tool {
        wxString                label;
        wxString                tooltip;
        std::string             icon;
        std::function<void()>   on_click;
        // Optional: false greys the tool out.
        std::function<bool()>   is_enabled;
        // Optional: true highlights the tool (e.g. the gizmo open).
        std::function<bool()>   is_selected;
        // Optional: false hides the tool (e.g. a tool of resin printers with a filament printer).
        std::function<bool()>   is_visible;
        wxBitmapBundle          bmp;
    };
    struct Group {
        wxString                label;
        wxString                tooltip;
        std::string             icon;
        std::vector<Tool>       tools;
        // Called when the group is chosen.
        std::function<void()>   on_select;
        // Optional: the group follows the plater (e.g. Slice while the preview is shown). A group with it which
        // returns false is not the current one even when it was chosen last.
        std::function<bool()>   is_active;
        // The group stays shown over any state of the plater (e.g. the views, for the plate and the preview).
        bool                    neutral { false };
        wxBitmapBundle          bmp;
    };

    explicit PlateTools(wxWindow* parent);
    ~PlateTools() override;

    void   add_group(Group group);
    void   select_group(size_t idx);
    // The group shown: a neutral group chosen last, else the one the plater is in, else the one chosen last.
    size_t current_group() const;
    void   sys_color_changed();
    void   msw_rescale();

private:
    void   on_paint(wxPaintEvent&);
    void   on_motion(wxMouseEvent&);
    void   on_click(wxMouseEvent&);
    void   on_timer(wxTimerEvent&);
    void   load_bitmaps();
    void   update_size();
    int    groups_width() const;
    int    tools_width() const;
    int    item_height(size_t count) const;
    wxRect group_rect(size_t idx) const;
    wxRect tool_rect(size_t idx) const;
    // Group index, or -1 - tool index, or INT_MIN for nothing.
    int    hit_test(const wxPoint& pt) const;
    // Enabled and selected states of all the tools of the current group, to repaint only when they change.
    std::vector<int> state() const;
    // Indices of the tools of the current group which are shown.
    std::vector<size_t> visible_tools() const;

    std::vector<Group>  m_groups;
    size_t              m_chosen  { 0 };
    int                 m_hovered { INT_MIN };
    std::vector<int>    m_state;
    wxTimer             m_timer;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlateTools_hpp_
