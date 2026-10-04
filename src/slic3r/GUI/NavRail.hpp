///|/ Tisma Slicer: navigation column at the left of the main window (PrusaSlicer 3.0 style).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_NavRail_hpp_
#define slic3r_NavRail_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/panel.h>

namespace Slic3r {
namespace GUI {

// Vertical bar of icon buttons with a short label below the icon. It always uses the dark Tisma palette.
class NavRail : public wxPanel
{
public:
    struct Item {
        wxString                    label;
        wxString                    tooltip;
        std::string                 icon;
        std::function<void()>       on_click;
        // Returns true when the item corresponds to the current page. Items without it are actions.
        std::function<bool()>       is_selected;
        bool                        bottom { false };
        wxBitmapBundle              bmp;
    };

    explicit NavRail(wxWindow* parent);

    void add_item(Item item);
    // Repaint the selection, e.g. after the page of the main window changed.
    void update_selection() { Refresh(); }
    void msw_rescale();

private:
    void        on_paint(wxPaintEvent&);
    void        on_motion(wxMouseEvent&);
    void        on_click(wxMouseEvent&);
    void        load_bitmaps();
    int         item_height() const;
    wxRect      item_rect(size_t idx) const;
    int         hit_test(const wxPoint& pt) const;

    std::vector<Item>   m_items;
    wxBitmapBundle      m_logo;
    int                 m_hovered { -1 };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_NavRail_hpp_
