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
#include <wx/timer.h>

namespace Slic3r {
namespace GUI {

// Bar of the workspaces.
// Vertical: icon buttons with a short label below the icon, always in the dark Tisma palette.
// Horizontal (Órbita Pro): logo and text tabs underlined when selected, placed in the top bar; the
// items marked as bottom go to the right end as icons only. It takes the colors of the parent.
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
        // Returns false to hide the item (e.g. advanced workspaces outside of the Expert mode).
        std::function<bool()>       is_visible;
        bool                        bottom { false };
        wxBitmapBundle              bmp;
    };

    explicit NavRail(wxWindow* parent, bool horizontal = false);
    bool horizontal() const { return m_horizontal; }

    void add_item(Item item);
    // Repaint the selection, e.g. after the page of the main window changed.
    void update_selection() { Refresh(); }
    // The visibility of the items changed (e.g. another mode was selected).
    void update_visibility();
    void msw_rescale();

private:
    void        on_paint(wxPaintEvent&);
    void        on_motion(wxMouseEvent&);
    void        on_click(wxMouseEvent&);
    void        load_bitmaps();
    int         item_height() const;
    bool        compact() const;
    void        update_min_height();
    void        paint_horizontal(wxDC& dc);
    int         tab_width(size_t idx) const;
    int         tabs_left() const;
    wxRect      item_rect(size_t idx) const;
    int         hit_test(const wxPoint& pt) const;
    bool        visible(size_t idx) const { return !m_items[idx].is_visible || m_items[idx].is_visible(); }

    std::vector<Item>   m_items;
    wxBitmapBundle      m_logo;
    int                 m_hovered { -1 };
    bool                m_horizontal { false };
};

// Chip of the top bar (Órbita Pro) with the current printer: a dot (filled when the printer has a
// network or USB connection configured) and its name. The text is polled, so it follows any change of
// the selected printer without extra notifications.
class PrinterChip : public wxPanel
{
public:
    struct State {
        wxString    name;
        wxString    tooltip;
        bool        connected { false };
    };

    PrinterChip(wxWindow* parent, std::function<State()> get_state, std::function<void()> on_click);
    ~PrinterChip() override;
    void update();
    void msw_rescale() { update_size(); Refresh(); }

private:
    void on_paint(wxPaintEvent&);
    void update_size();

    std::function<State()>  m_get_state;
    std::function<void()>   m_on_click;
    State                   m_state;
    bool                    m_hovered { false };
    wxTimer                 m_timer;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_NavRail_hpp_
