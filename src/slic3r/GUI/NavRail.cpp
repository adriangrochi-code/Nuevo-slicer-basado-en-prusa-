///|/ Tisma Slicer: navigation column at the left of the main window (PrusaSlicer 3.0 style).
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "NavRail.hpp"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

// Tisma dark palette.
static const wxColour RAIL_BG        (0x17, 0x17, 0x1B);
static const wxColour RAIL_BORDER    (0x2A, 0x2A, 0x31);
static const wxColour RAIL_HOVER     (0x2A, 0x2A, 0x31);
static const wxColour RAIL_SELECTED  (0x7A, 0x24, 0xC9);
static const wxColour RAIL_TEXT      (0xB8, 0xB8, 0xC0);
static const wxColour RAIL_TEXT_SEL  (0xFF, 0xFF, 0xFF);

static constexpr int ICON_PX = 24;

// Full height of an item (icon and label) and the minimum one (icon only).
static int full_item_height(int em)    { return int(6.2 * em); }
static int compact_item_height(int em) { return int(3.6 * em); }
static int items_top(int em)           { return int(6 * em); }

// Horizontal bar: height, side padding of a tab and size of the icons of the bottom items.
static int bar_height(int em)          { return int(4.4 * em); }
static int tab_padding(int em)         { return int(1.4 * em); }
static constexpr int BAR_ICON_PX = 18;


NavRail::NavRail(wxWindow* parent, bool horizontal)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
    , m_horizontal(horizontal)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(horizontal ? parent->GetBackgroundColour() : RAIL_BG);
    msw_rescale();

    Bind(wxEVT_PAINT,        &NavRail::on_paint,  this);
    Bind(wxEVT_MOTION,       &NavRail::on_motion, this);
    Bind(wxEVT_LEFT_UP,      &NavRail::on_click,  this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = -1; UnsetToolTip(); Refresh(); });
}

void NavRail::add_item(Item item)
{
    m_items.push_back(std::move(item));
    load_bitmaps();
    update_min_height();
    Refresh();
}

void NavRail::update_visibility()
{
    m_hovered = -1;
    update_min_height();
    Refresh();
}

void NavRail::update_min_height()
{
    if (m_horizontal) {
        const int em = wxGetApp().em_unit();
        int w = tabs_left();
        for (size_t i = 0; i < m_items.size(); ++i)
            if (visible(i))
                w += tab_width(i);
        SetMinSize(wxSize(w + em, bar_height(em)));
        if (GetContainingSizer())
            GetParent()->Layout();
        return;
    }
    // All the items must fit at least with the compact height.
    const int em = wxGetApp().em_unit();
    int n = 0;
    for (size_t i = 0; i < m_items.size(); ++i)
        if (visible(i))
            ++n;
    SetMinSize(wxSize(int(9.4 * em), items_top(em) + n * compact_item_height(em) + em));
}

void NavRail::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    if (m_horizontal) {
        SetFont(wxGetApp().normal_font());
        load_bitmaps();
        update_min_height();
        Refresh();
        return;
    }
    SetMinSize(wxSize(int(9.4 * em), -1));
    SetMaxSize(wxSize(int(9.4 * em), -1));
    update_min_height();
    SetFont(wxGetApp().small_font());
    load_bitmaps();
    Refresh();
}

void NavRail::load_bitmaps()
{
    // Light icons for the dark background, brand color lightened.
    static BitmapCache cache;
    const int icon_px = m_horizontal ? BAR_ICON_PX : ICON_PX;
    const int logo_px = m_horizontal ? 24 : 36;
    const bool dark   = !m_horizontal || wxGetApp().dark_mode();
    for (Item& item : m_items)
        if (wxBitmapBundle* bmp = cache.from_svg(item.icon, icon_px, icon_px, dark, dark ? "#C9A2F5" : "#7A24C9"))
            item.bmp = *bmp;
    if (wxBitmapBundle* logo = cache.from_svg(wxGetApp().logo_name(), logo_px, logo_px, false))
        m_logo = *logo;
}

int NavRail::item_height() const
{
    if (m_horizontal)
        return GetClientSize().GetHeight();
    // Shrink the items when the window is too low to show all of them with their labels.
    const int em = wxGetApp().em_unit();
    int n = 0;
    for (size_t i = 0; i < m_items.size(); ++i)
        if (visible(i))
            ++n;
    if (n == 0)
        return full_item_height(em);
    const int available = GetClientSize().GetHeight() - items_top(em) - em;
    return std::clamp(available / n, compact_item_height(em), full_item_height(em));
}

bool NavRail::compact() const
{
    if (m_horizontal)
        return false;
    return item_height() < int(5.2 * wxGetApp().em_unit());
}

int NavRail::tabs_left() const
{
    // Logo and the name of the application.
    const int em = wxGetApp().em_unit();
    wxClientDC dc(const_cast<NavRail*>(this));
    dc.SetFont(GetFont().Bold());
    return int(1.2 * em) + 24 + int(0.7 * em) + dc.GetTextExtent("Tisma").x + int(1.2 * em);
}

int NavRail::tab_width(size_t idx) const
{
    const int em = wxGetApp().em_unit();
    if (m_items[idx].bottom)
        return BAR_ICON_PX + 2 * int(0.8 * em);
    wxClientDC dc(const_cast<NavRail*>(this));
    dc.SetFont(GetFont());
    return dc.GetTextExtent(m_items[idx].label).x + 2 * tab_padding(em);
}

wxRect NavRail::item_rect(size_t idx) const
{
    if (m_horizontal) {
        const int h = GetClientSize().GetHeight();
        if (m_items[idx].bottom) {
            // Bottom items at the right end, in their order.
            int x = GetClientSize().GetWidth() - wxGetApp().em_unit() / 2;
            for (size_t i = m_items.size(); i-- > idx;)
                if (m_items[i].bottom && visible(i))
                    x -= tab_width(i);
            return wxRect(x, 0, tab_width(idx), h);
        }
        int x = tabs_left();
        for (size_t i = 0; i < idx; ++i)
            if (!m_items[i].bottom && visible(i))
                x += tab_width(i);
        return wxRect(x, 0, tab_width(idx), h);
    }
    const int em    = wxGetApp().em_unit();
    const int h     = item_height();
    const int width = GetClientSize().GetWidth();
    const int top   = items_top(em);
    int n_top = 0, n_bottom = 0;
    for (size_t i = 0; i < m_items.size(); ++i)
        if (visible(i))
            (m_items[i].bottom ? n_bottom : n_top) += 1;
    if (!m_items[idx].bottom) {
        int pos = 0;
        for (size_t i = 0; i < idx; ++i)
            if (!m_items[i].bottom && visible(i))
                ++pos;
        return wxRect(0, top + pos * h, width, h);
    }
    int pos = 0;
    for (size_t i = 0; i < idx; ++i)
        if (m_items[i].bottom && visible(i))
            ++pos;
    // Bottom items stick to the bottom of the column, but never above the last top item.
    const int bottom_start = std::max(GetClientSize().GetHeight() - em - n_bottom * h, top + n_top * h);
    return wxRect(0, bottom_start + pos * h, width, h);
}

int NavRail::hit_test(const wxPoint& pt) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
        if (visible(i) && item_rect(i).Contains(pt))
            return int(i);
    return -1;
}

void NavRail::paint_horizontal(wxDC& dc)
{
    const wxSize sz   = GetClientSize();
    const int    em   = wxGetApp().em_unit();
    const bool   dark = wxGetApp().dark_mode();
    const wxColour bg       = GetParent()->GetBackgroundColour();
    const wxColour text     = dark ? wxColour(0x9A, 0x9A, 0xA6) : wxColour(0x5A, 0x5A, 0x66);
    const wxColour text_sel = dark ? wxColour(0xEC, 0xEC, 0xF0) : wxColour(0x14, 0x14, 0x17);
    const wxColour hover    = dark ? wxColour(0x27, 0x27, 0x2D) : wxColour(0xE6, 0xE6, 0xEA);

    dc.SetPen(wxPen(bg));
    dc.SetBrush(wxBrush(bg));
    dc.DrawRectangle(0, 0, sz.x, sz.y);

    // Logo and name.
    int x = int(1.2 * em);
    if (m_logo.IsOk()) {
        const wxBitmap logo = m_logo.GetBitmapFor(this);
        const wxSize   lsz  = logo.GetLogicalSize();
        dc.DrawBitmap(logo, x, (sz.y - lsz.y) / 2, true);
        x += lsz.x + int(0.7 * em);
    }
    dc.SetFont(GetFont().Bold());
    dc.SetTextForeground(text_sel);
    dc.DrawText("Tisma", x, (sz.y - dc.GetTextExtent("Tisma").y) / 2);

    dc.SetFont(GetFont());
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (!visible(i))
            continue;
        const Item&  item     = m_items[i];
        const wxRect rc       = item_rect(i);
        const bool   selected = item.is_selected && item.is_selected();
        const bool   hovered  = int(i) == m_hovered;
        if (hovered && !selected) {
            dc.SetPen(wxPen(hover));
            dc.SetBrush(wxBrush(hover));
            dc.DrawRoundedRectangle(wxRect(rc.x + 2, rc.y + int(0.6 * em), rc.width - 4, rc.height - int(1.2 * em)), int(0.4 * em));
        }
        if (item.bottom) {
            if (item.bmp.IsOk()) {
                const wxBitmap bmp = item.bmp.GetBitmapFor(this);
                const wxSize   bsz = bmp.GetLogicalSize();
                dc.DrawBitmap(bmp, rc.x + (rc.width - bsz.x) / 2, rc.y + (rc.height - bsz.y) / 2, true);
            }
        } else {
            const wxSize tsz = dc.GetTextExtent(item.label);
            dc.SetTextForeground(selected ? text_sel : text);
            dc.DrawText(item.label, rc.x + (rc.width - tsz.x) / 2, rc.y + (rc.height - tsz.y) / 2);
        }
        if (selected) {
            // Underline in the brand color at the bottom edge of the bar.
            const int t = std::max(2, em / 5);
            dc.SetPen(wxPen(RAIL_SELECTED));
            dc.SetBrush(wxBrush(RAIL_SELECTED));
            dc.DrawRectangle(rc.x + int(0.4 * em), rc.GetBottom() - t + 1, rc.width - int(0.8 * em), t);
        }
    }
}

void NavRail::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    if (m_horizontal) {
        paint_horizontal(dc);
        return;
    }
    const wxSize sz = GetClientSize();
    const int    em = wxGetApp().em_unit();

    dc.SetPen(wxPen(RAIL_BG));
    dc.SetBrush(wxBrush(RAIL_BG));
    dc.DrawRectangle(0, 0, sz.x, sz.y);
    dc.SetPen(wxPen(RAIL_BORDER));
    dc.DrawLine(sz.x - 1, 0, sz.x - 1, sz.y);

    // Logo at the top.
    if (m_logo.IsOk()) {
        const wxBitmap logo = m_logo.GetBitmapFor(this);
        const wxSize   lsz  = logo.GetLogicalSize();
        dc.DrawBitmap(logo, (sz.x - lsz.x) / 2, int(1.2 * em), true);
    }

    dc.SetFont(GetFont());
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (!visible(i))
            continue;
        const Item&  item     = m_items[i];
        const wxRect rc       = item_rect(i);
        const bool   selected = item.is_selected && item.is_selected();
        const bool   hovered  = int(i) == m_hovered;

        // Highlight: rounded square behind the icon.
        const int    pad = int(0.45 * em);
        const wxRect hl(rc.x + pad, rc.y + int(0.3 * em), rc.width - 2 * pad, rc.height - int(0.6 * em));
        if (selected || hovered) {
            const wxColour c = selected ? RAIL_SELECTED : RAIL_HOVER;
            dc.SetPen(wxPen(c));
            dc.SetBrush(wxBrush(c));
            dc.DrawRoundedRectangle(hl, int(0.6 * em));
        }

        const bool is_compact = compact();
        int y = hl.y + int(0.6 * em);
        if (item.bmp.IsOk()) {
            const wxBitmap bmp = item.bmp.GetBitmapFor(this);
            const wxSize   bsz = bmp.GetLogicalSize();
            if (is_compact)
                y = hl.y + (hl.height - bsz.y) / 2;
            dc.DrawBitmap(bmp, rc.x + (rc.width - bsz.x) / 2, y, true);
            y += bsz.y + int(0.3 * em);
        }
        // In a low window only the icons are shown; the label stays in the tooltip.
        if (is_compact)
            continue;
        wxString label = item.label;
        wxSize   tsz   = dc.GetTextExtent(label);
        if (tsz.x > hl.width - 4) {
            label = wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, hl.width - 4);
            tsz   = dc.GetTextExtent(label);
        }
        dc.SetTextForeground(selected ? RAIL_TEXT_SEL : RAIL_TEXT);
        dc.DrawText(label, rc.x + (rc.width - tsz.x) / 2, y);
    }
}

void NavRail::on_motion(wxMouseEvent& evt)
{
    const int idx = hit_test(evt.GetPosition());
    if (idx != m_hovered) {
        m_hovered = idx;
        if (idx >= 0)
            SetToolTip(m_items[idx].tooltip.empty() ? m_items[idx].label : m_items[idx].tooltip);
        else
            UnsetToolTip();
        Refresh();
    }
    evt.Skip();
}

void NavRail::on_click(wxMouseEvent& evt)
{
    const int idx = hit_test(evt.GetPosition());
    if (idx >= 0 && m_items[idx].on_click) {
        // The action may show a modal dialog: run it after the mouse event is processed.
        auto cb = m_items[idx].on_click;
        CallAfter([this, cb]() { cb(); Refresh(); });
    }
    evt.Skip();
}

PrinterChip::PrinterChip(wxWindow* parent, std::function<State()> get_state, std::function<void()> on_click)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
    , m_get_state(std::move(get_state))
    , m_on_click(std::move(on_click))
    , m_timer(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(wxGetApp().normal_font());
    Bind(wxEVT_PAINT,        &PrinterChip::on_paint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { m_hovered = true;  Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { m_hovered = false; Refresh(); });
    Bind(wxEVT_LEFT_UP,      [this](wxMouseEvent& evt) { if (m_on_click) CallAfter(m_on_click); evt.Skip(); });
    Bind(wxEVT_TIMER,        [this](wxTimerEvent&) { update(); });
    update();
    m_timer.Start(1500);
}

PrinterChip::~PrinterChip()
{
    m_timer.Stop();
}

void PrinterChip::update()
{
    State st = m_get_state ? m_get_state() : State();
    if (st.name == m_state.name && st.connected == m_state.connected && st.tooltip == m_state.tooltip)
        return;
    m_state = std::move(st);
    SetToolTip(m_state.tooltip);
    update_size();
    Refresh();
}

void PrinterChip::update_size()
{
    const int em = wxGetApp().em_unit();
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    const int text_w = std::min(dc.GetTextExtent(m_state.name).x, 24 * em);
    SetMinSize(wxSize(text_w + int(3.4 * em), int(3.2 * em)));
    if (GetContainingSizer())
        GetParent()->Layout();
}

void PrinterChip::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize sz   = GetClientSize();
    const int    em   = wxGetApp().em_unit();
    const bool   dark = wxGetApp().dark_mode();
    const wxColour bg     = GetParent()->GetBackgroundColour();
    const wxColour card   = dark ? (m_hovered ? wxColour(0x33, 0x33, 0x3A) : wxColour(0x27, 0x27, 0x2D))
                                 : (m_hovered ? wxColour(0xE6, 0xE6, 0xEA) : wxColour(0xF2, 0xF2, 0xF5));
    const wxColour border = dark ? wxColour(0x34, 0x34, 0x3C) : wxColour(0xD0, 0xD0, 0xD8);
    const wxColour text   = dark ? wxColour(0xEC, 0xEC, 0xF0) : wxColour(0x14, 0x14, 0x17);

    dc.SetPen(wxPen(bg));
    dc.SetBrush(wxBrush(bg));
    dc.DrawRectangle(0, 0, sz.x, sz.y);
    dc.SetPen(wxPen(border));
    dc.SetBrush(wxBrush(card));
    dc.DrawRoundedRectangle(wxRect(0, 0, sz.x, sz.y), int(0.5 * em));

    // Dot: filled green with a connection, hollow gray without it.
    const int r  = std::max(3, int(0.35 * em));
    const int cx = em + r;
    const int cy = sz.y / 2;
    if (m_state.connected) {
        dc.SetPen(wxPen(wxColour(0x3F, 0xB9, 0x7A)));
        dc.SetBrush(wxBrush(wxColour(0x3F, 0xB9, 0x7A)));
    } else {
        dc.SetPen(wxPen(wxColour(0x8A, 0x8A, 0x96)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    }
    dc.DrawCircle(cx, cy, r);

    dc.SetFont(GetFont());
    dc.SetTextForeground(text);
    const int x0 = cx + r + int(0.7 * em);
    wxString label = m_state.name;
    if (dc.GetTextExtent(label).x > sz.x - x0 - em)
        label = wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, sz.x - x0 - em);
    dc.DrawText(label, x0, (sz.y - dc.GetTextExtent(label).y) / 2);
}

} // namespace GUI
} // namespace Slic3r
