///|/ Tisma Slicer: widgets of the interface of Tisma.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TismaWidgets.hpp"

#include <wx/dcbuffer.h>

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "TismaTheme.hpp"

namespace Slic3r {
namespace GUI {

static constexpr int CHIP_ICON_PX = 16;

TismaChip::TismaChip(wxWindow* parent, const wxString& text, const std::string& icon, std::function<void()> on_click)
    : wxPanel(parent, wxID_ANY), m_text(text), m_icon_name(icon), m_on_click(std::move(on_click))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    load_icon();
    update_size();
    Bind(wxEVT_PAINT, &TismaChip::on_paint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hovered = true; Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = false; Refresh(); });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { if (m_on_click) m_on_click(); });
    SetCursor(wxCursor(wxCURSOR_HAND));
}

void TismaChip::set_text(const wxString& text)
{
    if (text == m_text)
        return;
    m_text = text;
    update_size();
    Refresh();
}

void TismaChip::set_icon(const std::string& icon)
{
    m_icon_name = icon;
    load_icon();
    update_size();
    Refresh();
}

void TismaChip::set_dot(const wxColour& color)
{
    m_dot = color;
    update_size();
    Refresh();
}

void TismaChip::set_selected(bool selected)
{
    if (selected != m_selected) {
        m_selected = selected;
        Refresh();
    }
}

void TismaChip::load_icon()
{
    static BitmapCache cache;
    m_icon = wxBitmapBundle();
    if (! m_icon_name.empty())
        if (wxBitmapBundle* b = cache.from_svg(m_icon_name, CHIP_ICON_PX, CHIP_ICON_PX, wxGetApp().dark_mode(), "#7A24C9"))
            m_icon = *b;
}

void TismaChip::update_size()
{
    const int em = wxGetApp().em_unit();
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    int w = dc.GetTextExtent(m_text).x + 2 * int(1.1 * em);
    if (m_icon.IsOk())
        w += FromDIP(CHIP_ICON_PX) + int(0.5 * em);
    if (m_dot.IsOk())
        w += int(0.8 * em) + int(0.5 * em);
    const wxSize sz(w, int(2.8 * em));
    SetMinSize(sz);
    SetSize(sz);
    if (GetParent())
        GetParent()->Layout();
}

void TismaChip::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const bool   dark = wxGetApp().dark_mode();
    const int    em   = wxGetApp().em_unit();
    const wxSize sz   = GetClientSize();

    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    const wxColour fill   = m_selected ? (dark ? wxColour(0x3B, 0x2A, 0x55) : wxColour(0xEC, 0xE1, 0xF9))
                          : m_hovered  ? (dark ? wxColour(0x30, 0x30, 0x38) : wxColour(0xEE, 0xEE, 0xF2))
                                       : TismaTheme::card_bg(dark);
    dc.SetPen(wxPen(m_selected ? TismaTheme::accent() : TismaTheme::separator(dark)));
    dc.SetBrush(wxBrush(fill));
    dc.DrawRoundedRectangle(wxRect(0, 0, sz.x, sz.y), sz.y / 2.);

    int x = int(1.1 * em);
    if (m_dot.IsOk()) {
        const int d = int(0.8 * em);
        dc.SetPen(wxPen(m_dot));
        dc.SetBrush(wxBrush(m_dot));
        dc.DrawEllipse(x, (sz.y - d) / 2, d, d);
        x += d + int(0.5 * em);
    }
    if (m_icon.IsOk()) {
        const wxBitmap b   = m_icon.GetBitmapFor(this);
        const wxSize   bsz = b.GetLogicalSize();
        dc.DrawBitmap(b, x, (sz.y - bsz.y) / 2, true);
        x += bsz.x + int(0.5 * em);
    }
    dc.SetFont(GetFont());
    dc.SetTextForeground(m_selected ? TismaTheme::accent_text(dark) : TismaTheme::text(dark));
    const wxSize tsz = dc.GetTextExtent(m_text);
    dc.DrawText(m_text, x, (sz.y - tsz.y) / 2);
}

void TismaChip::sys_color_changed()
{
    load_icon();
    Refresh();
}

void TismaChip::msw_rescale()
{
    load_icon();
    update_size();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
