///|/ Tisma Slicer: widgets of the interface of Tisma (the look of the Android app): chips.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_TismaWidgets_hpp_
#define slic3r_TismaWidgets_hpp_

#include <functional>
#include <string>

#include <wx/bmpbndl.h>
#include <wx/colour.h>
#include <wx/panel.h>

namespace Slic3r {
namespace GUI {

// A rounded chip: an icon, a dot of a color or neither, and a text. A click calls on_click.
class TismaChip : public wxPanel
{
public:
    TismaChip(wxWindow* parent, const wxString& text, const std::string& icon, std::function<void()> on_click);

    void set_text(const wxString& text);
    void set_icon(const std::string& icon);
    // An invalid color: no dot.
    void set_dot(const wxColour& color);
    // The chip of the current choice: the accent background.
    void set_selected(bool selected);
    void sys_color_changed();
    void msw_rescale();

private:
    void on_paint(wxPaintEvent&);
    void update_size();
    void load_icon();

    wxString               m_text;
    std::string            m_icon_name;
    wxBitmapBundle         m_icon;
    wxColour               m_dot;
    bool                   m_hovered  { false };
    bool                   m_selected { false };
    std::function<void()>  m_on_click;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_TismaWidgets_hpp_
