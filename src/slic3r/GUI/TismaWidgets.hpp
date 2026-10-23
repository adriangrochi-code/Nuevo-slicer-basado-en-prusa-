///|/ Tisma Slicer: widgets of the interface of Tisma (the look of the Android app): chips, titles of groups, options
///|/ as chips.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_TismaWidgets_hpp_
#define slic3r_TismaWidgets_hpp_

#include <functional>
#include <string>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/stattext.h>

namespace Slic3r {
namespace GUI {

// A rounded chip: an icon, a dot of a color or neither, and a text. A click calls on_click.
class TismaChip : public wxPanel
{
public:
    // height_em: the height in em units (2.8: the chips of the top bar; smaller in the panels).
    TismaChip(wxWindow* parent, const wxString& text, const std::string& icon, std::function<void()> on_click,
              double height_em = 2.8);

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
    double                 m_height_em;
};

// The title of a group (the Group of the phone): big, semibold, in the accent color. A caption: the small muted label
// of a setting.
class TismaTitle : public wxStaticText
{
public:
    enum class Style { Group, Caption };
    TismaTitle(wxWindow* parent, const wxString& text, Style style = Style::Group);
    void sys_color_changed();

private:
    Style m_style;
};

// Options shown as chips, one of them chosen (the Choices of the phone). The chips wrap to more lines when the panel is
// narrow. on_select is called with the index of the chip clicked by the user.
class TismaChoiceChips : public wxPanel
{
public:
    TismaChoiceChips(wxWindow* parent, const std::vector<wxString>& labels, std::function<void(int)> on_select);

    // -1: none chosen.
    void set_selection(int idx);
    int  get_selection() const { return m_selection; }
    void set_label(int idx, const wxString& label);
    void sys_color_changed();
    void msw_rescale();

private:
    std::vector<TismaChip*>  m_chips;
    int                      m_selection { -1 };
    std::function<void(int)> m_on_select;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_TismaWidgets_hpp_
