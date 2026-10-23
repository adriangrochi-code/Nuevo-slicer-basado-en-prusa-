///|/ Tisma Slicer: the groups of tools of the plate and their tools.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PlateTools.hpp"

#include <climits>

#include <wx/dcbuffer.h>

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "TismaTheme.hpp"

namespace Slic3r {
namespace GUI {

static constexpr int ICON_PX = 24;

PlateTools::PlateTools(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &PlateTools::on_paint, this);
    Bind(wxEVT_MOTION, &PlateTools::on_motion, this);
    Bind(wxEVT_LEFT_DOWN, &PlateTools::on_click, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = INT_MIN; Refresh(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) { Refresh(); evt.Skip(); });
    m_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &PlateTools::on_timer, this);
    m_timer.Start(300);
    update_size();
}

PlateTools::~PlateTools()
{
    m_timer.Stop();
}

void PlateTools::add_group(Group group)
{
    m_groups.emplace_back(std::move(group));
    load_bitmaps();
    Refresh();
}

void PlateTools::select_group(size_t idx)
{
    if (idx >= m_groups.size())
        return;
    m_chosen = idx;
    if (m_groups[idx].on_select)
        m_groups[idx].on_select();
    m_state.clear();
    Refresh();
}

size_t PlateTools::current_group() const
{
    if (m_chosen < m_groups.size() && m_groups[m_chosen].neutral)
        return m_chosen;
    for (size_t i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].is_active && m_groups[i].is_active())
            return i;
    // The group chosen last, unless it follows the plater and the plater left it.
    if (m_chosen < m_groups.size() && ! m_groups[m_chosen].is_active)
        return m_chosen;
    for (size_t i = 0; i < m_groups.size(); ++i)
        if (! m_groups[i].is_active)
            return i;
    return 0;
}

void PlateTools::load_bitmaps()
{
    static BitmapCache cache;
    const bool dark = wxGetApp().dark_mode();
    auto load = [dark](const std::string& icon, wxBitmapBundle& bmp) {
        if (wxBitmapBundle* b = cache.from_svg(icon, ICON_PX, ICON_PX, dark, dark ? "#C9A2F5" : "#7A24C9"))
            bmp = *b;
    };
    for (Group& g : m_groups) {
        load(g.icon, g.bmp);
        for (Tool& t : g.tools)
            load(t.icon, t.bmp);
    }
}

void PlateTools::update_size()
{
    SetMinSize(wxSize(groups_width() + tools_width(), -1));
}

int PlateTools::groups_width() const { return int(8 * wxGetApp().em_unit()); }
int PlateTools::tools_width() const  { return int(8 * wxGetApp().em_unit()); }

int PlateTools::item_height(size_t count) const
{
    const int em   = wxGetApp().em_unit();
    const int full = int(5.4 * em);
    const int h    = GetClientSize().GetHeight();
    // A low window: the items shrink, down to the icon.
    return count == 0 ? full : std::max(int(3.2 * em), std::min(full, (h - em) / int(count)));
}

wxRect PlateTools::group_rect(size_t idx) const
{
    const int ih = item_height(m_groups.size());
    return wxRect(0, int(0.5 * wxGetApp().em_unit()) + int(idx) * ih, groups_width(), ih);
}

wxRect PlateTools::tool_rect(size_t idx) const
{
    const size_t n  = m_groups.empty() ? 0 : m_groups[current_group()].tools.size();
    const int    ih = item_height(n);
    return wxRect(groups_width(), int(0.5 * wxGetApp().em_unit()) + int(idx) * ih, tools_width(), ih);
}

int PlateTools::hit_test(const wxPoint& pt) const
{
    for (size_t i = 0; i < m_groups.size(); ++i)
        if (group_rect(i).Contains(pt))
            return int(i);
    if (! m_groups.empty())
        for (size_t i = 0; i < m_groups[current_group()].tools.size(); ++i)
            if (tool_rect(i).Contains(pt))
                return -1 - int(i);
    return INT_MIN;
}

std::vector<int> PlateTools::state() const
{
    std::vector<int> out;
    if (m_groups.empty())
        return out;
    const size_t g = current_group();
    out.push_back(int(g));
    for (const Tool& t : m_groups[g].tools)
        out.push_back((t.is_enabled && ! t.is_enabled() ? 0 : 1) + (t.is_selected && t.is_selected() ? 2 : 0));
    return out;
}

void PlateTools::on_timer(wxTimerEvent&)
{
    if (! IsShownOnScreen())
        return;
    std::vector<int> s = state();
    if (s != m_state) {
        m_state = std::move(s);
        Refresh();
    }
}

void PlateTools::on_motion(wxMouseEvent& evt)
{
    const int hit = hit_test(evt.GetPosition());
    if (hit != m_hovered) {
        m_hovered = hit;
        wxString tip;
        if (hit >= 0)
            tip = m_groups[hit].tooltip;
        else if (hit != INT_MIN)
            tip = m_groups[current_group()].tools[-1 - hit].tooltip;
        SetToolTip(tip);
        Refresh();
    }
}

void PlateTools::on_click(wxMouseEvent& evt)
{
    const int hit = hit_test(evt.GetPosition());
    if (hit >= 0)
        select_group(size_t(hit));
    else if (hit != INT_MIN) {
        const Tool& t = m_groups[current_group()].tools[-1 - hit];
        if ((! t.is_enabled || t.is_enabled()) && t.on_click)
            t.on_click();
        m_state.clear();
        Refresh();
    }
}

void PlateTools::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const bool   dark = wxGetApp().dark_mode();
    const int    em   = wxGetApp().em_unit();
    const wxSize sz   = GetClientSize();

    dc.SetPen(wxPen(TismaTheme::panel_bg(dark)));
    dc.SetBrush(wxBrush(TismaTheme::panel_bg(dark)));
    dc.DrawRectangle(0, 0, groups_width(), sz.y);
    dc.SetPen(wxPen(TismaTheme::card_bg(dark)));
    dc.SetBrush(wxBrush(TismaTheme::card_bg(dark)));
    dc.DrawRectangle(groups_width(), 0, sz.x - groups_width(), sz.y);
    dc.SetPen(wxPen(TismaTheme::separator(dark)));
    dc.DrawLine(groups_width(), 0, groups_width(), sz.y);
    dc.DrawLine(sz.x - 1, 0, sz.x - 1, sz.y);
    if (m_groups.empty())
        return;

    // Icon over its label, the label cut to the width; a rounded highlight behind the selected or hovered item.
    auto draw_item = [&](const wxRect& rc, const wxBitmapBundle& bmp, const wxString& label, bool selected, bool hovered, bool enabled) {
        const int    pad = int(0.4 * em);
        const wxRect hl(rc.x + pad, rc.y + int(0.2 * em), rc.width - 2 * pad, rc.height - int(0.4 * em));
        if (selected || (hovered && enabled)) {
            const wxColour c = selected ? (dark ? wxColour(0x3B, 0x2A, 0x55) : wxColour(0xEC, 0xE1, 0xF9))
                                        : (dark ? wxColour(0x30, 0x30, 0x38) : wxColour(0xEE, 0xEE, 0xF2));
            dc.SetPen(wxPen(c));
            dc.SetBrush(wxBrush(c));
            dc.DrawRoundedRectangle(hl, int(0.8 * em));
        }
        const bool compact = rc.height < int(4.6 * em);
        int y = hl.y + int(0.5 * em);
        if (bmp.IsOk()) {
            wxBitmap b = bmp.GetBitmapFor(this);
            if (! enabled)
                b = b.ConvertToDisabled(dark ? 60 : 255);
            const wxSize bsz = b.GetLogicalSize();
            if (compact)
                y = hl.y + (hl.height - bsz.y) / 2;
            dc.DrawBitmap(b, rc.x + (rc.width - bsz.x) / 2, y, true);
            y += bsz.y + int(0.3 * em);
        }
        if (compact)
            return;
        wxString text = label;
        wxSize   tsz  = dc.GetTextExtent(text);
        if (tsz.x > hl.width - 4) {
            text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, hl.width - 4);
            tsz  = dc.GetTextExtent(text);
        }
        dc.SetTextForeground(! enabled ? TismaTheme::disabled_text(dark) : selected ? TismaTheme::accent_text(dark) : TismaTheme::text(dark));
        dc.DrawText(text, rc.x + (rc.width - tsz.x) / 2, y);
    };

    dc.SetFont(wxGetApp().small_font());
    const size_t current = current_group();
    for (size_t i = 0; i < m_groups.size(); ++i)
        draw_item(group_rect(i), m_groups[i].bmp, m_groups[i].label, i == current, m_hovered == int(i), true);
    const Group& g = m_groups[current];
    for (size_t i = 0; i < g.tools.size(); ++i) {
        const Tool& t = g.tools[i];
        draw_item(tool_rect(i), t.bmp, t.label, t.is_selected && t.is_selected(), m_hovered == -1 - int(i),
                  ! t.is_enabled || t.is_enabled());
    }
}

void PlateTools::sys_color_changed()
{
    load_bitmaps();
    Refresh();
}

void PlateTools::msw_rescale()
{
    load_bitmaps();
    update_size();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
