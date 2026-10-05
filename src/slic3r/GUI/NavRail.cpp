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


NavRail::NavRail(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(RAIL_BG);
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

void NavRail::update_min_height()
{
    // All the items must fit at least with the compact height.
    const int em = wxGetApp().em_unit();
    SetMinSize(wxSize(int(8.4 * em), items_top(em) + int(m_items.size()) * compact_item_height(em) + em));
}

void NavRail::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    SetMinSize(wxSize(int(8.4 * em), -1));
    SetMaxSize(wxSize(int(8.4 * em), -1));
    update_min_height();
    SetFont(wxGetApp().small_font());
    load_bitmaps();
    Refresh();
}

void NavRail::load_bitmaps()
{
    // Light icons for the dark background, brand color lightened.
    static BitmapCache cache;
    for (Item& item : m_items)
        if (wxBitmapBundle* bmp = cache.from_svg(item.icon, ICON_PX, ICON_PX, true, "#C9A2F5"))
            item.bmp = *bmp;
    if (wxBitmapBundle* logo = cache.from_svg(wxGetApp().logo_name(), 36, 36, false))
        m_logo = *logo;
}

int NavRail::item_height() const
{
    // Shrink the items when the window is too low to show all of them with their labels.
    const int em = wxGetApp().em_unit();
    const int n  = int(m_items.size());
    if (n == 0)
        return full_item_height(em);
    const int available = GetClientSize().GetHeight() - items_top(em) - em;
    return std::clamp(available / n, compact_item_height(em), full_item_height(em));
}

bool NavRail::compact() const
{
    return item_height() < int(5.2 * wxGetApp().em_unit());
}

wxRect NavRail::item_rect(size_t idx) const
{
    const int em    = wxGetApp().em_unit();
    const int h     = item_height();
    const int width = GetClientSize().GetWidth();
    const int top   = items_top(em);
    int n_top = 0, n_bottom = 0;
    for (const Item& item : m_items)
        (item.bottom ? n_bottom : n_top) += 1;
    if (!m_items[idx].bottom) {
        int pos = 0;
        for (size_t i = 0; i < idx; ++i)
            if (!m_items[i].bottom)
                ++pos;
        return wxRect(0, top + pos * h, width, h);
    }
    int pos = 0;
    for (size_t i = 0; i < idx; ++i)
        if (m_items[i].bottom)
            ++pos;
    // Bottom items stick to the bottom of the column, but never above the last top item.
    const int bottom_start = std::max(GetClientSize().GetHeight() - em - n_bottom * h, top + n_top * h);
    return wxRect(0, bottom_start + pos * h, width, h);
}

int NavRail::hit_test(const wxPoint& pt) const
{
    for (size_t i = 0; i < m_items.size(); ++i)
        if (item_rect(i).Contains(pt))
            return int(i);
    return -1;
}

void NavRail::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
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
        const Item&  item     = m_items[i];
        const wxRect rc       = item_rect(i);
        const bool   selected = item.is_selected && item.is_selected();
        const bool   hovered  = int(i) == m_hovered;

        // Highlight: rounded square behind the icon.
        const int    pad = int(0.6 * em);
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

} // namespace GUI
} // namespace Slic3r
