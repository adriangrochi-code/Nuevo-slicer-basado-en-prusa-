///|/ Tisma Slicer: the More page.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MorePage_hpp_
#define slic3r_MorePage_hpp_

#include <functional>
#include <vector>

#include <wx/scrolwin.h>

class wxMenu;
class wxMenuBar;
class wxMenuItem;
class wxSizer;

namespace Slic3r {
namespace GUI {

class TismaTitle;

// The More page of the navigation, as in the phone: cards with the actions of the program instead of the menus of
// PrusaSlicer. The cards of the menus are made from the menus themselves (File, Edit, Window, View, Configuration,
// Calibration, Help), so that every function and its keyboard shortcut stays; a click on a row runs its menu item.
class MorePage : public wxScrolledWindow
{
public:
    struct Action {
        wxString              label;
        wxString              tooltip;
        std::function<void()> run;
    };

    explicit MorePage(wxWindow *parent);

    // The actions of Tisma, in the first card (Thingiverse, Tisma Cloud account, preferences).
    void set_actions(std::vector<Action> actions) { m_actions = std::move(actions); }
    // Builds the page from the menus the first time, then shows the state of their items (enabled, checked).
    void update(wxMenuBar *menubar, wxWindow *update_source);
    void sys_color_changed();

private:
    struct Row {
        wxWindow   *window;
        wxMenu     *menu;
        wxMenuItem *item;
    };

    void build(wxMenuBar *menubar);
    wxSizer *add_card(wxSizer *cards, const wxString &title, wxWindow *&card);
    void add_menu_rows(wxWindow *card, wxSizer *sizer, wxMenu *menu, int depth);
    wxWindow *add_row(wxWindow *card, wxSizer *sizer, const wxString &label, const wxString &shortcut, const wxString &tooltip,
                      std::function<void()> run);

    std::vector<Action>     m_actions;
    std::vector<Row>        m_rows;
    std::vector<TismaTitle*> m_titles;
    std::vector<wxWindow*>  m_cards;
    bool                    m_built { false };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_MorePage_hpp_
