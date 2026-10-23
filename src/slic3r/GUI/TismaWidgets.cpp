///|/ Tisma Slicer: widgets of the interface of Tisma.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TismaWidgets.hpp"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/control.h>
#include <wx/treectrl.h>
#include <wx/wrapsizer.h>

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "TismaTheme.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r {
namespace GUI {

static constexpr int CHIP_ICON_PX = 16;

TismaChip::TismaChip(wxWindow* parent, const wxString& text, const std::string& icon, std::function<void()> on_click,
                     double height_em)
    : wxPanel(parent, wxID_ANY), m_text(text), m_icon_name(icon), m_on_click(std::move(on_click)), m_height_em(height_em)
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
    const wxSize sz(w, int(m_height_em * em));
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

void style_tisma_button(::Button *button, bool primary)
{
    const bool dark = wxGetApp().dark_mode();
    button->SetCornerRadius(0.6 * wxGetApp().em_unit());
    if (primary) {
        button->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::disabled_bg(dark),   StateColor::Disabled),
            std::pair<wxColour, int>(TismaTheme::accent_pressed(),    StateColor::Pressed),
            std::pair<wxColour, int>(TismaTheme::accent_hover(),      StateColor::Hovered),
            std::pair<wxColour, int>(TismaTheme::accent(),            StateColor::Normal)));
        button->SetBorderColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::disabled_bg(dark),   StateColor::Disabled),
            std::pair<wxColour, int>(TismaTheme::accent(),            StateColor::Normal)));
        button->SetTextColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::disabled_text(dark), StateColor::Disabled),
            std::pair<wxColour, int>(*wxWHITE,                        StateColor::Normal)));
    } else {
        button->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::card_bg(dark),       StateColor::Hovered),
            std::pair<wxColour, int>(TismaTheme::panel_bg(dark),      StateColor::Normal)));
        button->SetBorderColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::separator(dark),     StateColor::Disabled),
            std::pair<wxColour, int>(TismaTheme::accent_text(dark),   StateColor::Normal)));
        button->SetTextColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::disabled_text(dark), StateColor::Disabled),
            std::pair<wxColour, int>(TismaTheme::accent_text(dark),   StateColor::Normal)));
    }
    button->Refresh();
}

TismaTitle::TismaTitle(wxWindow* parent, const wxString& text, Style style)
    : wxStaticText(parent, wxID_ANY, text), m_style(style)
{
    wxFont font = wxGetApp().normal_font();
    if (style == Style::Group) {
        font.SetPointSize(int(std::lround(font.GetPointSize() * 1.25)));
        font.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    } else
        font = wxGetApp().small_font().Bold();
    SetFont(font);
    sys_color_changed();
}

void TismaTitle::sys_color_changed()
{
    const bool dark = wxGetApp().dark_mode();
    SetForegroundColour(m_style == Style::Group ? TismaTheme::accent_text(dark) : TismaTheme::text_muted(dark));
    Refresh();
}

TismaChoiceChips::TismaChoiceChips(wxWindow* parent, const std::vector<wxString>& labels, std::function<void(int)> on_select)
    : wxPanel(parent, wxID_ANY), m_on_select(std::move(on_select))
{
    SetBackgroundColour(parent->GetBackgroundColour());
    const int gap = int(0.4 * wxGetApp().em_unit());
    auto* sizer = new wxWrapSizer(wxHORIZONTAL);
    for (size_t i = 0; i < labels.size(); ++i) {
        auto* chip = new TismaChip(this, labels[i], "", [this, i]() {
            set_selection(int(i));
            if (m_on_select)
                m_on_select(int(i));
        }, 2.3);
        m_chips.push_back(chip);
        sizer->Add(chip, 0, wxRIGHT | wxBOTTOM, gap);
    }
    SetSizer(sizer);
}

void TismaChoiceChips::set_selection(int idx)
{
    m_selection = idx;
    for (size_t i = 0; i < m_chips.size(); ++i)
        m_chips[i]->set_selected(int(i) == idx);
}

void TismaChoiceChips::set_label(int idx, const wxString& label)
{
    if (idx >= 0 && idx < int(m_chips.size()))
        m_chips[idx]->set_text(label);
}

void TismaChoiceChips::sys_color_changed()
{
    SetBackgroundColour(GetParent()->GetBackgroundColour());
    for (TismaChip* chip : m_chips)
        chip->sys_color_changed();
}

void TismaChoiceChips::msw_rescale()
{
    for (TismaChip* chip : m_chips)
        chip->msw_rescale();
    Layout();
}

// The root is item 1, the pages are items 2, 3 ... (0 is no item).
static constexpr uintptr_t ROOT_ID = 1;

TismaPageList::TismaPageList(wxWindow* parent, const wxSize& size)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, size, wxVSCROLL | wxWANTS_CHARS | wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetScrollRate(0, row_height() / 2);
    Bind(wxEVT_PAINT, &TismaPageList::on_paint, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        SetFocus();
        if (const int row = row_at(e.GetPosition()); row >= 0)
            select(row, true);
    });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& e) {
        if (const int row = row_at(e.GetPosition()); row >= 0 && row == m_selection) {
            wxTreeEvent event(wxEVT_TREE_ITEM_ACTIVATED, GetId());
            event.SetEventObject(this);
            event.SetItem(id(row));
            GetEventHandler()->ProcessEvent(event);
        }
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
        if (const int row = row_at(e.GetPosition()); row != m_hover) {
            m_hover = row;
            Refresh();
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        m_hover = -1;
        Refresh();
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
        const int key = e.GetKeyCode();
        if ((key == WXK_UP || key == WXK_DOWN) && ! m_items.empty()) {
            const int next = std::clamp(m_selection + (key == WXK_UP ? -1 : 1), 0, int(m_items.size()) - 1);
            if (next != m_selection)
                select(next, true);
        } else
            e.Skip();
    });
}

int TismaPageList::row_height() const
{
    return int(2.8 * wxGetApp().em_unit());
}

int TismaPageList::row_at(const wxPoint& pos) const
{
    int x = 0, y = 0;
    CalcUnscrolledPosition(pos.x, pos.y, &x, &y);
    const int pad = int(0.6 * wxGetApp().em_unit());
    const int row = (y - pad) / row_height();
    return y >= pad && row >= 0 && row < int(m_items.size()) ? row : -1;
}

int TismaPageList::index(const wxTreeItemId& item) const
{
    const uintptr_t raw = reinterpret_cast<uintptr_t>(item.GetID());
    return raw > ROOT_ID && raw - ROOT_ID - 1 < m_items.size() ? int(raw - ROOT_ID - 1) : -1;
}

wxTreeItemId TismaPageList::id(int index)
{
    return index < 0 ? wxTreeItemId() : wxTreeItemId(reinterpret_cast<void*>(uintptr_t(index) + ROOT_ID + 1));
}

wxTreeItemId TismaPageList::AddRoot(const wxString&)
{
    return GetRootItem();
}

wxTreeItemId TismaPageList::GetRootItem() const
{
    return wxTreeItemId(reinterpret_cast<void*>(ROOT_ID));
}

wxTreeItemId TismaPageList::AppendItem(const wxTreeItemId&, const wxString& text, int image)
{
    m_items.push_back({ text, image, wxColour() });
    update_virtual_size();
    Refresh();
    return id(int(m_items.size()) - 1);
}

void TismaPageList::DeleteChildren(const wxTreeItemId&)
{
    m_items.clear();
    m_selection = -1;
    m_hover     = -1;
    update_virtual_size();
    Refresh();
}

void TismaPageList::SetImages(const wxVector<wxBitmapBundle>& images)
{
    m_images.assign(images.begin(), images.end());
    Refresh();
}

wxTreeItemId TismaPageList::GetFirstVisibleItem() const
{
    return id(m_items.empty() ? -1 : 0);
}

wxTreeItemId TismaPageList::GetNextVisible(const wxTreeItemId& item) const
{
    const int i = index(item);
    return id(i >= 0 && i + 1 < int(m_items.size()) ? i + 1 : -1);
}

wxString TismaPageList::GetItemText(const wxTreeItemId& item) const
{
    const int i = index(item);
    return i >= 0 ? m_items[i].text : wxString();
}

void TismaPageList::SetItemTextColour(const wxTreeItemId& item, const wxColour& colour)
{
    if (const int i = index(item); i >= 0 && m_items[i].colour != colour) {
        m_items[i].colour = colour;
        Refresh();
    }
}

wxTreeItemId TismaPageList::GetSelection() const
{
    return id(m_selection);
}

void TismaPageList::SelectItem(const wxTreeItemId& item, bool select_it)
{
    const int i = index(item);
    if (i >= 0 && select_it)
        select(i, true);
}

void TismaPageList::SetSelectionIndex(int idx)
{
    if (idx >= -1 && idx < int(m_items.size()))
        select(idx, false);
}

void TismaPageList::select(int idx, bool send_event)
{
    if (idx == m_selection)
        return;
    m_selection = idx;
    // The row of the page in view.
    int x = 0, y = 0;
    GetViewStart(&x, &y);
    int ppu_x = 0, ppu_y = 0;
    GetScrollPixelsPerUnit(&ppu_x, &ppu_y);
    if (ppu_y > 0) {
        const int pad  = int(0.6 * wxGetApp().em_unit());
        const int top  = pad + idx * row_height();
        const int view = GetClientSize().y;
        if (top < y * ppu_y)
            Scroll(-1, top / ppu_y);
        else if (top + row_height() > y * ppu_y + view)
            Scroll(-1, (top + row_height() - view) / ppu_y + 1);
    }
    Refresh();
    if (send_event) {
        wxTreeEvent event(wxEVT_TREE_SEL_CHANGED, GetId());
        event.SetEventObject(this);
        event.SetItem(id(idx));
        GetEventHandler()->ProcessEvent(event);
    }
}

void TismaPageList::update_virtual_size()
{
    const int pad = int(0.6 * wxGetApp().em_unit());
    SetVirtualSize(-1, 2 * pad + int(m_items.size()) * row_height());
}

void TismaPageList::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const bool dark = wxGetApp().dark_mode();
    const int  em   = wxGetApp().em_unit();
    dc.SetBackground(wxBrush(TismaTheme::panel_bg(dark)));
    dc.Clear();
    DoPrepareDC(dc);

    const int pad   = int(0.6 * em);
    const int rh    = row_height();
    const int width = GetClientSize().x;
    wxFont font = wxGetApp().normal_font();
    for (size_t i = 0; i < m_items.size(); ++i) {
        const Item&  item     = m_items[i];
        const bool   selected = int(i) == m_selection;
        const wxRect row(pad, pad + int(i) * rh, width - 2 * pad, rh - int(0.3 * em));
        if (selected || int(i) == m_hover) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(selected ? (dark ? wxColour(0x3B, 0x2A, 0x55) : wxColour(0xEC, 0xE1, 0xF9)) :
                                           (dark ? wxColour(0x30, 0x30, 0x38) : wxColour(0xEE, 0xEE, 0xF2))));
            dc.DrawRoundedRectangle(row, 0.6 * em);
        }
        int x = row.x + int(0.8 * em);
        if (item.image >= 0 && item.image < int(m_images.size()) && m_images[item.image].IsOk()) {
            const wxBitmap bmp = m_images[item.image].GetBitmapFor(this);
            const wxSize   bsz = bmp.GetLogicalSize();
            dc.DrawBitmap(bmp, x, row.y + (row.height - bsz.y) / 2, true);
            x += bsz.x + int(0.7 * em);
        }
        font.SetWeight(selected ? wxFONTWEIGHT_SEMIBOLD : wxFONTWEIGHT_NORMAL);
        dc.SetFont(font);
        // The colour of the item tells about its values (modified ...); the page shown is in the accent colour.
        dc.SetTextForeground(selected ? TismaTheme::accent_text(dark) :
                             item.colour.IsOk() ? item.colour : TismaTheme::text(dark));
        const wxString text = wxControl::Ellipsize(item.text, dc, wxELLIPSIZE_END, row.GetRight() - x - int(0.4 * em));
        const wxSize   tsz  = dc.GetTextExtent(text);
        dc.DrawText(text, x, row.y + (row.height - tsz.y) / 2);
    }
}

} // namespace GUI
} // namespace Slic3r
