///|/ Tisma Slicer: the groups of tools of the plate and their tools.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PlateTools.hpp"

#include <algorithm>
#include <climits>

#include <wx/dcbuffer.h>
#include <wx/popupwin.h>

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "TismaTheme.hpp"

namespace Slic3r {
namespace GUI {

static constexpr int ICON_PX = 24;

// A tool or a group: its icon over its label, the label cut to the width; a rounded highlight behind the selected or
// hovered item. Only the icon when the item is low.
static void draw_item(wxDC& dc, wxWindow* win, const wxRect& rc, const wxBitmapBundle& bmp, const wxString& label,
                      bool selected, bool hovered, bool enabled)
{
    const bool dark = wxGetApp().dark_mode();
    const int  em   = wxGetApp().em_unit();
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
        wxBitmap b = bmp.GetBitmapFor(win);
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
}

// The tools of a group of the horizontal bar, under the group: up to 8 in a row, each an icon over its name. A click
// on a tool closes the panel and runs the tool.
class PlateToolsPopup : public wxPopupTransientWindow
{
public:
    PlateToolsPopup(PlateTools* owner, size_t group) : wxPopupTransientWindow(owner, wxBORDER_SIMPLE), m_owner(owner), m_group(group)
    {
        m_tools = owner->visible_tools(group);
        m_panel = new wxPanel(this, wxID_ANY);
        m_panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
        const int em = wxGetApp().em_unit();
        m_cell       = wxSize(int(8 * em), int(5.4 * em));
        m_cols       = std::max<size_t>(1, std::min<size_t>(m_tools.size(), 8));
        const size_t rows = (m_tools.size() + m_cols - 1) / m_cols;
        const wxSize size(int(m_cols) * m_cell.x + em / 2, int(std::max<size_t>(rows, 1)) * m_cell.y + em / 2);
        m_panel->SetSize(size);
        SetClientSize(size);
        m_panel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
        m_panel->Bind(wxEVT_MOTION, [this](wxMouseEvent& evt) {
            const int hit = hit_test(evt.GetPosition());
            if (hit != m_hovered) {
                m_hovered = hit;
                m_panel->SetToolTip(hit >= 0 ? m_owner->m_groups[m_group].tools[m_tools[hit]].tooltip : wxString());
                m_panel->Refresh();
            }
        });
        m_panel->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = -1; m_panel->Refresh(); });
        m_panel->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& evt) {
            const int hit = hit_test(evt.GetPosition());
            if (hit < 0)
                return;
            const PlateTools::Tool& t = m_owner->m_groups[m_group].tools[m_tools[hit]];
            if ((t.is_enabled && ! t.is_enabled()) || ! t.on_click)
                return;
            // After the panel is gone (the tool may open a dialog or a gizmo).
            std::function<void()> run = t.on_click;
            PlateTools* owner = m_owner;
            Dismiss();
            owner->CallAfter([owner, run]() {
                run();
                owner->m_state.clear();
                owner->Refresh();
            });
        });
    }

private:
    PlateTools*         m_owner;
    size_t              m_group;
    std::vector<size_t> m_tools;
    wxPanel*            m_panel { nullptr };
    wxSize              m_cell;
    size_t              m_cols { 1 };
    int                 m_hovered { -1 };

    wxRect cell(size_t pos) const
    {
        const int em = wxGetApp().em_unit();
        return wxRect(em / 4 + int(pos % m_cols) * m_cell.x, em / 4 + int(pos / m_cols) * m_cell.y, m_cell.x, m_cell.y);
    }
    int hit_test(const wxPoint& pt) const
    {
        for (size_t i = 0; i < m_tools.size(); ++ i)
            if (cell(i).Contains(pt))
                return int(i);
        return -1;
    }
    void paint()
    {
        wxAutoBufferedPaintDC dc(m_panel);
        const bool dark = wxGetApp().dark_mode();
        dc.SetPen(wxPen(TismaTheme::card_bg(dark)));
        dc.SetBrush(wxBrush(TismaTheme::card_bg(dark)));
        dc.DrawRectangle(wxPoint(0, 0), m_panel->GetClientSize());
        dc.SetFont(wxGetApp().small_font());
        for (size_t i = 0; i < m_tools.size(); ++ i) {
            const PlateTools::Tool& t = m_owner->m_groups[m_group].tools[m_tools[i]];
            draw_item(dc, m_panel, cell(i), t.bmp, t.label, t.is_selected && t.is_selected(), m_hovered == int(i),
                      ! t.is_enabled || t.is_enabled());
        }
    }
};

PlateTools::PlateTools(wxWindow* parent, bool horizontal) : wxPanel(parent, wxID_ANY), m_horizontal(horizontal)
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
    update_size();
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
    const int em = wxGetApp().em_unit();
    if (m_horizontal) {
        // As low as the top bar; as narrow as the icons alone (show_labels() decides with the width given).
        int icons = 0;
        for (size_t i = 0; i < m_groups.size(); ++ i)
            icons += group_width_h(i, false);
        SetMinSize(wxSize(icons, int(3.4 * em)));
        return;
    }
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

int PlateTools::group_width_h(size_t idx, bool labels) const
{
    // Half an em on each side of the icon (and of its name, a third of an em after the icon).
    const int em = wxGetApp().em_unit();
    int w = em + ICON_PX;
    if (labels) {
        wxClientDC dc(const_cast<PlateTools*>(this));
        dc.SetFont(wxGetApp().small_font());
        w += dc.GetTextExtent(m_groups[idx].label).x + em / 3;
    }
    return w;
}

bool PlateTools::show_labels() const
{
    int w = 0;
    for (size_t i = 0; i < m_groups.size(); ++ i)
        w += group_width_h(i, true);
    return w <= GetClientSize().GetWidth();
}

wxRect PlateTools::group_rect_h(size_t idx, bool labels) const
{
    int x = 0;
    for (size_t i = 0; i < idx; ++ i)
        x += group_width_h(i, labels);
    return wxRect(x, 0, group_width_h(idx, labels), GetClientSize().GetHeight());
}

wxRect PlateTools::group_rect(size_t idx) const
{
    if (m_horizontal)
        return group_rect_h(idx, show_labels());
    const int ih = item_height(m_groups.size());
    return wxRect(0, int(0.5 * wxGetApp().em_unit()) + int(idx) * ih, groups_width(), ih);
}

std::vector<size_t> PlateTools::visible_tools(size_t group) const
{
    std::vector<size_t> out;
    if (group >= m_groups.size())
        return out;
    const std::vector<Tool>& tools = m_groups[group].tools;
    for (size_t i = 0; i < tools.size(); ++i)
        if (! tools[i].is_visible || tools[i].is_visible())
            out.push_back(i);
    return out;
}

// idx: the position among the visible tools.
wxRect PlateTools::tool_rect(size_t idx) const
{
    const size_t n  = visible_tools().size();
    const int    ih = item_height(n);
    return wxRect(groups_width(), int(0.5 * wxGetApp().em_unit()) + int(idx) * ih, tools_width(), ih);
}

int PlateTools::hit_test(const wxPoint& pt) const
{
    for (size_t i = 0; i < m_groups.size(); ++i)
        if (group_rect(i).Contains(pt))
            return int(i);
    if (m_horizontal)
        return INT_MIN;
    const std::vector<size_t> tools = visible_tools();
    for (size_t i = 0; i < tools.size(); ++i)
        if (tool_rect(i).Contains(pt))
            return -1 - int(tools[i]);
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
        out.push_back((t.is_enabled && ! t.is_enabled() ? 0 : 1) + (t.is_selected && t.is_selected() ? 2 : 0) +
                      (t.is_visible && ! t.is_visible() ? 4 : 0));
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

void PlateTools::open_tools(size_t group)
{
    if (visible_tools(group).empty())
        return;
    // The tooltip of the group would cover the first row of tools.
    UnsetToolTip();
    auto *popup = new PlateToolsPopup(this, group);
    const wxRect rc = group_rect(group);
    popup->Position(ClientToScreen(wxPoint(rc.x, rc.GetBottom())), wxSize(0, 0));
    popup->Popup();
}

void PlateTools::on_click(wxMouseEvent& evt)
{
    const int hit = hit_test(evt.GetPosition());
    if (hit >= 0) {
        select_group(size_t(hit));
        if (m_horizontal)
            open_tools(size_t(hit));
    } else if (hit != INT_MIN) {
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

    if (m_horizontal) {
        // In the top bar: its background, each group an icon and its name side by side (or only the icon).
        const wxColour bg = GetParent()->GetBackgroundColour();
        dc.SetPen(wxPen(bg));
        dc.SetBrush(wxBrush(bg));
        dc.DrawRectangle(wxPoint(0, 0), sz);
        if (m_groups.empty())
            return;
        // The names smaller than the sections of the bar: the tools of one of them.
        dc.SetFont(wxGetApp().small_font());
        const bool   labels  = show_labels();
        const size_t current = current_group();
        for (size_t i = 0; i < m_groups.size(); ++ i) {
            const wxRect rc = group_rect_h(i, labels);
            const wxRect hl(rc.x + em / 8, rc.y + int(0.4 * em), rc.width - em / 4, rc.height - int(0.8 * em));
            if (i == current || m_hovered == int(i)) {
                const wxColour c = i == current ? (dark ? wxColour(0x3B, 0x2A, 0x55) : wxColour(0xEC, 0xE1, 0xF9))
                                                : (dark ? wxColour(0x30, 0x30, 0x38) : wxColour(0xE6, 0xE6, 0xEC));
                dc.SetPen(wxPen(c));
                dc.SetBrush(wxBrush(c));
                dc.DrawRoundedRectangle(hl, int(0.8 * em));
            }
            int x = rc.x + em / 2;
            if (m_groups[i].bmp.IsOk()) {
                const wxBitmap b = m_groups[i].bmp.GetBitmapFor(this);
                dc.DrawBitmap(b, x, rc.y + (rc.height - b.GetLogicalSize().y) / 2, true);
                x += b.GetLogicalSize().x + em / 3;
            }
            if (labels) {
                dc.SetTextForeground(i == current ? TismaTheme::accent_text(dark) : TismaTheme::text(dark));
                dc.DrawText(m_groups[i].label, x, rc.y + (rc.height - dc.GetCharHeight()) / 2);
            }
        }
        return;
    }

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

    dc.SetFont(wxGetApp().small_font());
    const size_t current = current_group();
    for (size_t i = 0; i < m_groups.size(); ++i)
        draw_item(dc, this, group_rect(i), m_groups[i].bmp, m_groups[i].label, i == current, m_hovered == int(i), true);
    const Group& g = m_groups[current];
    const std::vector<size_t> shown = visible_tools();
    for (size_t pos = 0; pos < shown.size(); ++pos) {
        const size_t i = shown[pos];
        const Tool&  t = g.tools[i];
        draw_item(dc, this, tool_rect(pos), t.bmp, t.label, t.is_selected && t.is_selected(), m_hovered == -1 - int(i),
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
