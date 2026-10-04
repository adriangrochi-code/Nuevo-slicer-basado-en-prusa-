///|/ Tisma Slicer: quick settings panel of the sidebar.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_QuickSettings_hpp_
#define slic3r_QuickSettings_hpp_

#include <memory>
#include <string>
#include <vector>

#include <wx/sizer.h>

class wxStaticText;
class wxWindow;
class ScalableButton;

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

class ConfigOptionsGroup;

// Favorite print settings shown in the sidebar, grouped by category (as the process panel of OrcaSlicer),
// editable by the user (as the favorite settings of PrusaSlicer 3.0). They edit the print preset directly,
// the same way as the "Infill" field of the frequently changed parameters.
class QuickSettings
{
public:
    explicit QuickSettings(wxWindow* parent);
    ~QuickSettings();

    wxSizer*    get_sizer() noexcept { return m_sizer; }
    void        Show(bool show);

    // A print option was changed in the print settings tab.
    void        update_value(const std::string& opt_key);
    // Another print preset was selected.
    void        reload_config();

    void        msw_rescale();
    void        sys_color_changed();

    // Stored favorites, or the default list.
    static std::vector<std::string> favorites();
    static void                     set_favorites(const std::vector<std::string>& keys);
    static const std::vector<std::string>& default_favorites();
    // Options that can be shown in the panel (print options with a simple editor).
    static bool                     can_be_favorite(const std::string& opt_key);
    // Category used to group an option in the panel.
    static std::string              category(const std::string& opt_key);

private:
    void        rebuild();
    void        edit_favorites();
    void        on_change(const std::string& opt_key);

    wxWindow*                                         m_parent       { nullptr };
    wxBoxSizer*                                       m_sizer        { nullptr };
    wxBoxSizer*                                       m_groups_sizer { nullptr };
    wxStaticText*                                     m_title        { nullptr };
    ScalableButton*                                   m_edit_btn     { nullptr };
    std::vector<std::shared_ptr<ConfigOptionsGroup>>  m_groups;
    std::vector<std::string>                          m_keys;
    bool                                              m_shown        { true };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_QuickSettings_hpp_
