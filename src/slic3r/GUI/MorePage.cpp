///|/ Tisma Slicer: the More page.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MorePage.hpp"

#include <algorithm>
#include <iterator>

#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/wrapsizer.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "TismaTheme.hpp"
#include "TismaWidgets.hpp"
#include "Widgets/StaticBox.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// A row of a card: its label, its keyboard shortcut on the right, a check mark for the items which can be checked.
class MoreRow : public wxPanel
{
public:
    MoreRow(wxWindow *parent, const wxString &label, const wxString &shortcut, std::function<void()> run)
        : wxPanel(parent, wxID_ANY), m_label(label), m_shortcut(shortcut), m_run(std::move(run))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(-1, int(2.4 * wxGetApp().em_unit())));
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &MoreRow::on_paint, this);
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { m_hover = true; Refresh(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { m_hover = false; Refresh(); });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
            if (m_enabled && m_run)
                m_run();
        });
        Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &e) {
            if (! ::StaticBox::ForwardWheelToScrolledParent(this, e))
                e.Skip();
        });
    }

    void set_state(bool enabled, bool checkable, bool checked)
    {
        if (enabled != m_enabled || checkable != m_checkable || checked != m_checked) {
            m_enabled   = enabled;
            m_checkable = checkable;
            m_checked   = checked;
            SetCursor(wxCursor(enabled ? wxCURSOR_HAND : wxCURSOR_ARROW));
            Refresh();
        }
    }

private:
    void on_paint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        const bool   dark = wxGetApp().dark_mode();
        const int    em   = wxGetApp().em_unit();
        const wxSize sz   = GetClientSize();
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        if (m_hover && m_enabled) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(dark ? wxColour(0x30, 0x30, 0x38) : wxColour(0xEE, 0xEE, 0xF2)));
            dc.DrawRoundedRectangle(wxRect(0, 0, sz.x, sz.y), 0.5 * em);
        }
        int x = int(0.6 * em);
        dc.SetFont(wxGetApp().normal_font());
        if (m_checkable) {
            // A check mark in the accent color, or an empty circle.
            const int d = int(1.2 * em);
            const wxRect box(x, (sz.y - d) / 2, d, d);
            dc.SetPen(wxPen(m_checked ? TismaTheme::accent() : TismaTheme::text_muted(dark)));
            dc.SetBrush(m_checked ? wxBrush(TismaTheme::accent()) : *wxTRANSPARENT_BRUSH);
            dc.DrawEllipse(box);
            if (m_checked) {
                dc.SetPen(wxPen(*wxWHITE, std::max(1, em / 6)));
                dc.DrawLine(box.x + d / 4, box.y + d / 2, box.x + d * 5 / 12, box.y + d * 2 / 3);
                dc.DrawLine(box.x + d * 5 / 12, box.y + d * 2 / 3, box.x + d * 3 / 4, box.y + d / 3);
            }
            x += d + int(0.6 * em);
        }
        int right = sz.x - int(0.6 * em);
        if (! m_shortcut.IsEmpty()) {
            dc.SetFont(wxGetApp().small_font());
            dc.SetTextForeground(TismaTheme::text_muted(dark));
            const wxSize ssz = dc.GetTextExtent(m_shortcut);
            right -= ssz.x;
            dc.DrawText(m_shortcut, right, (sz.y - ssz.y) / 2);
            right -= em;
        }
        dc.SetFont(wxGetApp().normal_font());
        dc.SetTextForeground(m_enabled ? TismaTheme::text(dark) : TismaTheme::disabled_text(dark));
        const wxString label = wxControl::Ellipsize(m_label, dc, wxELLIPSIZE_MIDDLE, std::max(0, right - x));
        const wxSize   tsz   = dc.GetTextExtent(label);
        dc.DrawText(label, x, (sz.y - tsz.y) / 2);
    }

    wxString              m_label;
    wxString              m_shortcut;
    std::function<void()> m_run;
    bool                  m_hover     { false };
    bool                  m_enabled   { true };
    bool                  m_checkable { false };
    bool                  m_checked   { false };
};

} // namespace

MorePage::MorePage(wxWindow *parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
{
    SetScrollRate(0, 10);
    SetBackgroundColour(TismaTheme::panel_bg(wxGetApp().dark_mode()));
}

wxSizer *MorePage::add_card(wxSizer *cards, const wxString &title, wxWindow *&card)
{
    const bool dark = wxGetApp().dark_mode();
    const int  em   = wxGetApp().em_unit();
    auto *box = new ::StaticBox(this);
    box->SetCornerRadius(0.8 * em);
    box->SetBackgroundColorNormal(TismaTheme::card_bg(dark));
    box->SetBorderColorNormal(TismaTheme::separator(dark));
    box->SetBackgroundColour(TismaTheme::card_bg(dark));
    box->SetMinSize(wxSize(34 * em, -1));
    auto *content = new wxBoxSizer(wxVERTICAL);
    auto *title_text = new TismaTitle(box, title);
    m_titles.push_back(title_text);
    content->Add(title_text, 0, wxLEFT | wxBOTTOM, int(0.6 * em));
    auto *padding = new wxBoxSizer(wxVERTICAL);
    padding->Add(content, 1, wxEXPAND | wxALL, int(1.0 * em));
    box->SetSizer(padding);
    cards->Add(box, 0, wxALL, int(0.6 * em));
    m_cards.push_back(box);
    card = box;
    return content;
}

wxWindow *MorePage::add_row(wxWindow *card, wxSizer *sizer, const wxString &label, const wxString &shortcut,
                            const wxString &tooltip, std::function<void()> run)
{
    auto *row = new MoreRow(card, label, shortcut, std::move(run));
    if (! tooltip.IsEmpty())
        row->SetToolTip(tooltip);
    sizer->Add(row, 0, wxEXPAND);
    return row;
}

void MorePage::add_menu_rows(wxWindow *card, wxSizer *sizer, wxMenu *menu, int depth)
{
    const int em = wxGetApp().em_unit();
    for (wxMenuItem *item : menu->GetMenuItems()) {
        if (item->IsSeparator()) {
            sizer->AddSpacer(int(0.4 * em));
            continue;
        }
        if (wxMenu *sub = item->GetSubMenu()) {
            // A submenu: its title as a caption, then its items.
            auto *caption = new TismaTitle(card, item->GetItemLabelText(), TismaTitle::Style::Caption);
            m_titles.push_back(caption);
            sizer->Add(caption, 0, wxLEFT | wxTOP | wxBOTTOM, int(0.6 * em));
            add_menu_rows(card, sizer, sub, depth + 1);
            continue;
        }
        // The keyboard shortcut: after the tab of the label of the item, or after " - " in the label (as PrusaSlicer
        // writes the shortcuts handled by the 3D view).
        const wxString full     = item->GetItemLabel();
        wxString       label    = item->GetItemLabelText();
        wxString       shortcut = full.Contains('\t') ? full.AfterFirst('\t') : wxString();
        if (shortcut.IsEmpty())
            if (const size_t dash = label.rfind(" - "); dash != wxString::npos) {
                const wxString key = label.Mid(dash + 3);
                static const wxString keys[] = { "Del", "Esc", "Tab", "Space", "Enter", "Backspace", "Delete" };
                const bool is_key = key.Contains("Ctrl") || key.Contains("Alt") || key.Contains("Shift") || key.Contains("Cmd") ||
                                    key.Contains(wxString::FromUTF8("\xe2\x8c\x98")) || key.length() == 1 ||
                                    (key.length() <= 3 && key[0] == 'F') ||
                                    std::find(std::begin(keys), std::end(keys), key) != std::end(keys);
                if (is_key) {
                    shortcut = key;
                    label    = label.Left(dash);
                }
            }
        wxWindow *row = add_row(card, sizer, label, shortcut, item->GetHelp(), [this, menu, item]() {
            // As the menu does: the new state of a checked item, then its event to the menu, else to the main window.
            wxCommandEvent evt(wxEVT_MENU, item->GetId());
            if (item->IsCheckable()) {
                item->Check(! item->IsChecked());
                evt.SetInt(item->IsChecked() ? 1 : 0);
            }
            evt.SetEventObject(menu);
            if (! menu->ProcessEvent(evt))
                if (wxWindow *top = wxGetApp().GetTopWindow())
                    top->ProcessWindowEvent(evt);
            update(nullptr, wxGetApp().GetTopWindow());
        });
        m_rows.push_back({ row, menu, item });
    }
}

void MorePage::build(wxMenuBar *menubar)
{
    const int em = wxGetApp().em_unit();
    auto *main = new wxBoxSizer(wxVERTICAL);
    auto *title = new TismaTitle(this, _L("More"));
    m_titles.push_back(title);
    main->Add(title, 0, wxLEFT | wxTOP, int(1.6 * em));
    auto *cards = new wxWrapSizer(wxHORIZONTAL);

    if (! m_actions.empty()) {
        wxWindow *card = nullptr;
        wxSizer *content = add_card(cards, "Tisma", card);
        for (const Action &action : m_actions)
            add_row(card, content, action.label, wxEmptyString, action.tooltip, action.run);
    }
    if (menubar != nullptr)
        for (size_t i = 0; i < menubar->GetMenuCount(); ++i) {
            wxWindow *card = nullptr;
            wxSizer *content = add_card(cards, wxStripMenuCodes(menubar->GetMenuLabel(i)), card);
            add_menu_rows(card, content, menubar->GetMenu(i), 0);
        }
    main->Add(cards, 0, wxEXPAND | wxALL, int(1.0 * em));
    SetSizer(main);
    FitInside();
    m_built = true;
}

void MorePage::update(wxMenuBar *menubar, wxWindow *update_source)
{
    if (! m_built) {
        if (menubar == nullptr)
            return;
        build(menubar);
    }
    // The menus say which items can be used now (wxEVT_UPDATE_UI of the main window).
    std::vector<wxMenu*> updated;
    for (const Row &row : m_rows)
        if (std::find(updated.begin(), updated.end(), row.menu) == updated.end()) {
            row.menu->UpdateUI(update_source);
            updated.push_back(row.menu);
        }
    for (const Row &row : m_rows)
        static_cast<MoreRow*>(row.window)->set_state(row.item->IsEnabled(), row.item->IsCheckable(),
                                                    row.item->IsCheckable() && row.item->IsChecked());
}

void MorePage::sys_color_changed()
{
    const bool dark = wxGetApp().dark_mode();
    SetBackgroundColour(TismaTheme::panel_bg(dark));
    for (wxWindow *card : m_cards) {
        auto *box = static_cast<::StaticBox*>(card);
        box->SetBackgroundColorNormal(TismaTheme::card_bg(dark));
        box->SetBorderColorNormal(TismaTheme::separator(dark));
        box->SetBackgroundColour(TismaTheme::card_bg(dark));
    }
    for (TismaTitle *title : m_titles)
        title->sys_color_changed();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
