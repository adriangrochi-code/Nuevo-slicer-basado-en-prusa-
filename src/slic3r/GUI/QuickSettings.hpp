///|/ Tisma Slicer: quick settings panel of the sidebar.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_QuickSettings_hpp_
#define slic3r_QuickSettings_hpp_

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <wx/sizer.h>

#include "libslic3r/Preset.hpp"

class wxStaticText;
class wxWindow;
class ScalableButton;
class Button;

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

class ConfigOptionsGroup;

// Favorite settings shown in the sidebar, grouped by category (as the process panel of OrcaSlicer), editable by the
// user (as the favorite settings of PrusaSlicer 3.0). Three sections: print, filament and printer settings; each one
// edits its preset directly (the same way as the "Infill" field of the frequently changed parameters) and has a button
// to open its full settings tab. Vector options (one value per extruder) edit the first extruder.
class QuickSettings
{
public:
    enum class Section : int { Print = 0, Filament = 1, Printer = 2 };
    static constexpr size_t SECTIONS = 3;

    explicit QuickSettings(wxWindow* parent);
    ~QuickSettings();

    wxSizer*    get_sizer() noexcept { return m_sizer; }
    void        Show(bool show);

    // An option of a preset was changed in its settings tab.
    void        update_value(Preset::Type type, const std::string& opt_key);
    // Another preset was selected.
    void        reload_config();

    void        msw_rescale();
    void        sys_color_changed();

    // Stored favorites of a section, or its default list.
    static std::vector<std::string> favorites(Section section);
    static void                     set_favorites(Section section, const std::vector<std::string>& keys);
    static const std::vector<std::string>& default_favorites(Section section);
    // Options of a section that can be shown in the panel (options with a simple editor).
    static bool                     can_be_favorite(Section section, const std::string& opt_key);
    // Category used to group an option in the panel.
    static std::string              category(const std::string& opt_key);
    static Preset::Type             preset_type(Section section);

private:
    void        rebuild();
    void        select_section(Section section);
    void        edit_favorites();
    void        open_tab();
    void        on_change(const std::string& opt_key);
    void        update_section_buttons();
    DynamicPrintConfig* config() const;

    wxWindow*                                         m_parent       { nullptr };
    wxBoxSizer*                                       m_sizer        { nullptr };
    wxBoxSizer*                                       m_groups_sizer { nullptr };
    wxStaticText*                                     m_title        { nullptr };
    std::array<Button*, SECTIONS>                     m_section_btns { nullptr, nullptr, nullptr };
    Button*                                           m_add_btn      { nullptr };
    Button*                                           m_tab_btn      { nullptr };
    Section                                           m_section      { Section::Print };
    std::vector<std::shared_ptr<ConfigOptionsGroup>>  m_groups;
    std::vector<std::string>                          m_keys;
    bool                                              m_shown        { true };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_QuickSettings_hpp_
