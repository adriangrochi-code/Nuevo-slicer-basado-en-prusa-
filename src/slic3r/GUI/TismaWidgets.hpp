///|/ Tisma Slicer: widgets of the interface of Tisma (the look of the Android app): chips, titles of groups, options
///|/ as chips, the list of the pages of the settings.
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
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/treebase.h>

class Button;

namespace Slic3r {
namespace GUI {

// The buttons of the interface of Tisma: filled with the accent color (the main action of a page) or outlined in it.
void style_tisma_button(::Button *button, bool primary);

// The label of a standard button (OK, Cancel, Yes, No, Close, Apply, Save) from the translations of Tisma: wxWidgets
// has its own labels, not translated by Tisma.
wxString tisma_stock_label(wxWindowID id);

// A button of the row at the bottom of a dialog. It has the standard id (wxID_OK, wxID_CANCEL...) of the native button
// it replaces, the dialogs find their buttons by it. An empty label: the standard label of the id.
::Button* tisma_dialog_button(wxWindow *parent, wxWindowID id, const wxString &label = wxString(), bool primary = false);

// The keys of a dialog with the buttons above, which wxWidgets handles only for its native buttons: Escape clicks the
// button with wxID_CANCEL, Enter clicks the default item of the dialog (wxTopLevelWindow::SetDefaultItem()) unless a
// button or a text of several lines has the focus.
void tisma_dialog_keys(wxDialog *dialog);

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

// The list of the pages of a settings tab: an icon and a title on each row, the page shown on a rounded accent
// background, as the navigation of the modern slicers. It replaces the tree of PrusaSlicer with the same calls (the part
// of the wxTreeCtrl interface the tabs use): a hidden root, its items, and wxEVT_TREE_SEL_CHANGED when another page is
// selected, by a click, the keyboard or SelectItem().
class TismaPageList : public wxScrolledWindow
{
public:
    TismaPageList(wxWindow* parent, const wxSize& size);

    wxTreeItemId AddRoot(const wxString& text);
    wxTreeItemId GetRootItem() const;
    wxTreeItemId AppendItem(const wxTreeItemId& parent, const wxString& text, int image = -1);
    void         DeleteChildren(const wxTreeItemId& item);
    void         SetImages(const wxVector<wxBitmapBundle>& images);
    void         SetIndent(unsigned int) {}

    wxTreeItemId GetFirstVisibleItem() const;
    wxTreeItemId GetNextVisible(const wxTreeItemId& item) const;
    bool         IsVisible(const wxTreeItemId& item) const { return index(item) >= 0; }
    wxString     GetItemText(const wxTreeItemId& item) const;
    void         SetItemTextColour(const wxTreeItemId& item, const wxColour& colour);
    void         SetItemFont(const wxTreeItemId&, const wxFont&) {}

    wxTreeItemId GetSelection() const;
    void         SelectItem(const wxTreeItemId& item, bool select = true);

    // As a list: its rows by their index, the selection set without an event (as wxListBox::SetSelection()). A double
    // click sends wxEVT_TREE_ITEM_ACTIVATED.
    void         Clear() { DeleteChildren(GetRootItem()); }
    int          GetSelectionIndex() const { return m_selection; }
    void         SetSelectionIndex(int idx);

private:
    struct Item {
        wxString text;
        int      image;
        wxColour colour;
    };
    std::vector<Item>           m_items;
    std::vector<wxBitmapBundle> m_images;
    int                         m_selection { -1 };
    int                         m_hover     { -1 };

    int  index(const wxTreeItemId& item) const;
    static wxTreeItemId id(int index);
    int  row_height() const;
    int  row_at(const wxPoint& pos) const;
    void select(int idx, bool send_event);
    void update_virtual_size();
    void on_paint(wxPaintEvent&);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_TismaWidgets_hpp_
