///|/ Tisma Slicer: quick settings panel of the sidebar.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "QuickSettings.hpp"

#include <algorithm>
#include <map>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wupdlock.h>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "OptionsGroup.hpp"
#include "TismaTheme.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "MainFrame.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r {
namespace GUI {

// Favorites of each section (the print one keeps the key of the first version of the panel).
static const char* const APP_CONFIG_KEYS[QuickSettings::SECTIONS] = {
    "tisma_quick_settings", "tisma_quick_settings_filament", "tisma_quick_settings_printer" };
// Last section shown.
static const char* const APP_CONFIG_SECTION = "tisma_quick_settings_section";

static PresetCollection& presets(QuickSettings::Section section)
{
    PresetBundle& bundle = *wxGetApp().preset_bundle;
    switch (section) {
    case QuickSettings::Section::Filament: return bundle.filaments;
    case QuickSettings::Section::Printer:  return bundle.printers;
    default:                               return bundle.prints;
    }
}

static bool is_vector(const ConfigOptionDef& def) { return (def.type & coVectorType) != 0; }

// Label of an option out of its page of the settings tab (not translated).
static std::string option_label(const std::string& opt_key, const ConfigOptionDef& def)
{
    // Labels which only make sense inside their page of the settings tab.
    if (opt_key == "min_fan_speed")
        return L("Min fan speed");
    if (opt_key == "max_fan_speed")
        return L("Max fan speed");
    return def.full_label.empty() ? def.label : def.full_label;
}

Preset::Type QuickSettings::preset_type(Section section)
{
    switch (section) {
    case Section::Filament: return Preset::TYPE_FILAMENT;
    case Section::Printer:  return Preset::TYPE_PRINTER;
    default:                return Preset::TYPE_PRINT;
    }
}

DynamicPrintConfig* QuickSettings::config() const
{
    return &presets(m_section).get_edited_preset().config;
}

const std::vector<std::string>& QuickSettings::default_favorites(Section section)
{
    static const std::vector<std::string> print {
        "layer_height", "first_layer_height",
        "perimeters", "top_solid_layers", "bottom_solid_layers",
        "fill_pattern", "infill_dense",
        "overhang_arcs",
        "perimeter_speed", "infill_speed", "travel_speed",
        "skirts",
    };
    static const std::vector<std::string> filament {
        "temperature", "first_layer_temperature", "bed_temperature", "first_layer_bed_temperature",
        "min_fan_speed", "max_fan_speed", "extrusion_multiplier", "filament_max_volumetric_speed",
    };
    static const std::vector<std::string> printer {
        "z_offset", "retract_length", "retract_speed", "deretract_speed", "retract_lift", "wipe",
    };
    switch (section) {
    case Section::Filament: return filament;
    case Section::Printer:  return printer;
    default:                return print;
    }
}

bool QuickSettings::can_be_favorite(Section section, const std::string& opt_key)
{
    const std::vector<std::string>& options = section == Section::Filament ? Preset::filament_options() :
                                              section == Section::Printer  ? Preset::printer_options() : Preset::print_options();
    if (std::find(options.begin(), options.end(), opt_key) == options.end())
        return false;
    // Options edited by dedicated widgets of the sidebar or of the settings tabs, or which are not settings.
    static const std::vector<std::string> excluded { "inherits", "compatible_printers", "compatible_printers_condition",
                                                     "compatible_prints", "compatible_prints_condition", "fill_density",
                                                     "support_material", "brim_width", "printer_technology", "printer_model",
                                                     "printer_variant", "thumbnails", "thumbnails_format", "host_type",
                                                     "filament_settings_id", "print_settings_id", "printer_settings_id" };
    if (std::find(excluded.begin(), excluded.end(), opt_key) != excluded.end())
        return false;
    const ConfigOptionDef* def = print_config_def.get(opt_key);
    if (def == nullptr || def->label.empty() || def->multiline || def->is_code || def->full_width || def->readonly || def->nullable)
        return false;
    switch (def->gui_type) {
    case ConfigOptionDef::GUIType::undefined:
    case ConfigOptionDef::GUIType::i_enum_open:
    case ConfigOptionDef::GUIType::f_enum_open:
    case ConfigOptionDef::GUIType::select_open:
    case ConfigOptionDef::GUIType::color:
        break;
    default:
        return false;
    }
    switch (def->type) {
    case coFloat: case coInt: case coBool: case coEnum: case coPercent: case coFloatOrPercent: case coString:
    // One value per extruder: the panel edits the first one.
    case coFloats: case coInts: case coBools: case coPercents: case coFloatsOrPercents:
        return true;
    case coStrings:
        return def->gui_type == ConfigOptionDef::GUIType::color;
    default:
        return false;
    }
}

std::string QuickSettings::category(const std::string& opt_key)
{
    const ConfigOptionDef* def = print_config_def.get(opt_key);
    if (def != nullptr && !def->category.empty())
        return def->category;
    // Before the speeds: the fan and retraction speeds belong to their own groups.
    if (boost::contains(opt_key, "fan"))
        return "Cooling";
    if (boost::starts_with(opt_key, "retract") || boost::starts_with(opt_key, "deretract") || opt_key == "wipe")
        return "Retraction";
    if (boost::contains(opt_key, "temperature"))
        return "Temperature";
    if (opt_key == "z_offset" || boost::starts_with(opt_key, "nozzle") || boost::starts_with(opt_key, "bed_"))
        return "General";
    if (boost::ends_with(opt_key, "_speed") || boost::starts_with(opt_key, "max_volumetric"))
        return "Speed";
    if (boost::ends_with(opt_key, "_acceleration"))
        return "Speed";
    if (boost::contains(opt_key, "layer_height"))
        return "Layers and Perimeters";
    if (boost::starts_with(opt_key, "skirt") || boost::starts_with(opt_key, "brim"))
        return "Skirt and brim";
    return "Other";
}

std::vector<std::string> QuickSettings::favorites(Section section)
{
    const AppConfig* app_config = wxGetApp().app_config;
    const char* key = APP_CONFIG_KEYS[int(section)];
    if (app_config == nullptr || !app_config->has(key))
        return default_favorites(section);
    std::vector<std::string> stored, keys;
    const std::string value = app_config->get(key);
    if (!value.empty())
        boost::split(stored, value, boost::is_any_of(";"));
    for (const std::string& k : stored)
        if (can_be_favorite(section, k) && std::find(keys.begin(), keys.end(), k) == keys.end())
            keys.push_back(k);
    return keys;
}

void QuickSettings::set_favorites(Section section, const std::vector<std::string>& keys)
{
    wxGetApp().app_config->set(APP_CONFIG_KEYS[int(section)], boost::algorithm::join(keys, ";"));
}

QuickSettings::QuickSettings(wxWindow* parent) : m_parent(parent)
{
    const int em = wxGetApp().em_unit();
    m_sizer = new wxBoxSizer(wxVERTICAL);

    auto header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(parent, wxID_ANY, _L("Quick settings").Upper());
    m_title->SetFont(wxGetApp().small_font().Bold());
    header->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);

    // "+": add settings of the section; "All settings": its full settings tab.
    m_add_btn = new Button(parent, "+ " + _L("Add"));
    m_add_btn->SetToolTip(_L("Search and add settings to this panel, or remove them"));
    m_add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_favorites(); });
    header->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, int(0.3 * em));
    m_tab_btn = new Button(parent, _L("All settings"));
    m_tab_btn->SetToolTip(_L("Open the full settings of this section"));
    m_tab_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_tab(); });
    header->Add(m_tab_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, int(0.3 * em));
    m_sizer->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, int(0.3 * em));

    // Segmented selector of the section.
    auto segments = new wxBoxSizer(wxHORIZONTAL);
    const wxString names[SECTIONS] = { _CTX("Print", "Section"), _CTX("Filament", "Section"), _CTX("Printer", "Section") };
    for (size_t i = 0; i < SECTIONS; ++i) {
        m_section_btns[i] = new Button(parent, names[i]);
        m_section_btns[i]->SetCornerRadius(int(0.4 * em));
        m_section_btns[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { select_section(Section(i)); });
        segments->Add(m_section_btns[i], 1, wxEXPAND | (i > 0 ? wxLEFT : 0), int(0.3 * em));
    }
    m_sizer->Add(segments, 0, wxEXPAND | wxBOTTOM | wxRIGHT, int(0.3 * em));

    m_groups_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_groups_sizer, 0, wxEXPAND);

    if (const std::string s = wxGetApp().app_config->get(APP_CONFIG_SECTION); !s.empty())
        m_section = Section(std::clamp(std::atoi(s.c_str()), 0, int(SECTIONS) - 1));
    update_section_buttons();
    rebuild();
}

QuickSettings::~QuickSettings() = default;

void QuickSettings::update_section_buttons()
{
    const bool dark = wxGetApp().dark_mode();
    m_title->SetForegroundColour(TismaTheme::text_muted(dark));
    for (size_t i = 0; i < SECTIONS; ++i) {
        Button* btn = m_section_btns[i];
        const bool selected = Section(i) == m_section;
        // Selected: filled with the accent color; the others: flat.
        btn->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(selected ? TismaTheme::accent_hover() : TismaTheme::card_bg(dark), StateColor::Hovered),
            std::pair<wxColour, int>(selected ? TismaTheme::accent() : TismaTheme::panel_bg(dark),     StateColor::Normal)));
        btn->SetBorderColor(StateColor(
            std::pair<wxColour, int>(selected ? TismaTheme::accent() : TismaTheme::separator(dark),    StateColor::Normal)));
        btn->SetTextColor(StateColor(
            std::pair<wxColour, int>(selected ? *wxWHITE : TismaTheme::text_muted(dark),             StateColor::Normal)));
        btn->Refresh();
    }
    for (Button* btn : { m_add_btn, m_tab_btn }) {
        btn->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::card_bg(dark),      StateColor::Hovered),
            std::pair<wxColour, int>(TismaTheme::panel_bg(dark),     StateColor::Normal)));
        btn->SetBorderColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::panel_bg(dark),     StateColor::Normal)));
        btn->SetTextColor(StateColor(
            std::pair<wxColour, int>(TismaTheme::accent_text(dark),  StateColor::Normal)));
        btn->Refresh();
    }
}

void QuickSettings::select_section(Section section)
{
    if (section == m_section)
        return;
    m_section = section;
    wxGetApp().app_config->set(APP_CONFIG_SECTION, std::to_string(int(section)));
    wxWindowUpdateLocker no_updates(m_parent);
    update_section_buttons();
    rebuild();
    m_parent->Layout();
    if (wxWindow* top = m_parent->GetParent())
        top->Layout();
}

void QuickSettings::open_tab()
{
    if (Tab* tab = wxGetApp().get_tab(preset_type(m_section)))
        wxGetApp().mainframe->select_tab(tab);
}

void QuickSettings::rebuild()
{
    m_groups_sizer->Clear(true);
    m_groups.clear();
    m_keys = favorites(m_section);

    DynamicPrintConfig* config = this->config();

    // Group by category, in the order of the first favorite of each category.
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    for (const std::string& key : m_keys) {
        if (!config->has(key))
            continue;
        const std::string cat = category(key);
        auto it = std::find_if(groups.begin(), groups.end(), [&cat](const auto& g) { return g.first == cat; });
        if (it == groups.end())
            groups.push_back({ cat, { key } });
        else
            it->second.push_back(key);
    }

    const bool dark = wxGetApp().dark_mode();
    const int  em   = wxGetApp().em_unit();
    if (groups.empty()) {
        auto hint = new wxStaticText(m_parent, wxID_ANY, _L("No settings here: press \"Add\" to choose them."));
        hint->SetForegroundColour(TismaTheme::text_muted(dark));
        m_groups_sizer->Add(hint, 0, wxALL, int(0.5 * em));
    }
    for (const auto& [cat, keys] : groups) {
        // Flat section (no frame) with a separator and a bold header, as in PrusaSlicer 3.0.
        auto line = new wxPanel(m_parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
        line->SetBackgroundColour(TismaTheme::separator(dark));
        m_groups_sizer->Add(line, 0, wxEXPAND | wxTOP | wxBOTTOM, int(0.5 * em));
        auto header = new wxStaticText(m_parent, wxID_ANY, _(cat));
        header->SetFont(wxGetApp().bold_font());
        header->SetForegroundColour(TismaTheme::accent_text(dark));
        m_groups_sizer->Add(header, 0, wxLEFT | wxBOTTOM, int(0.2 * em));

        auto og = std::make_shared<ConfigOptionsGroup>(m_parent, "", config);
        og->label_width = 18;
        og->sidetext_width = 6;
        og->on_change = [this](const t_config_option_key& opt_key, const boost::any&) { on_change(opt_key); };
        for (const std::string& key : keys) {
            const ConfigOptionDef* def = config->def()->get(key);
            Option option = og->get_option(key, def != nullptr && is_vector(*def) ? 0 : -1);
            if (def != nullptr)
                option.opt.label = option_label(key, *def);
            option.opt.width = 8;
            og->append_single_option_line(option);
        }
        og->activate();
        og->reload_config();
        m_groups_sizer->Add(og->sizer, 0, wxEXPAND | wxBOTTOM, int(0.3 * em));
        m_groups.push_back(og);
    }

    Show(m_shown);
}

void QuickSettings::on_change(const std::string& /* opt_key */)
{
    // The options group already wrote the value into the edited preset: the tab updates its fields, the dirty state
    // of the preset and invalidates the slicing.
    Tab* tab = wxGetApp().get_tab(preset_type(m_section));
    if (tab == nullptr)
        return;
    tab->update_dirty();
    tab->reload_config();
    tab->update();
    // Some options (e.g. support or infill toggles) may change the other fields of this panel.
    for (const auto& og : m_groups)
        og->reload_config();
}

void QuickSettings::update_value(Preset::Type type, const std::string& /* opt_key */)
{
    if (type != preset_type(m_section))
        return;
    for (const auto& og : m_groups)
        og->reload_config();
}

void QuickSettings::reload_config()
{
    for (const auto& og : m_groups)
        og->reload_config();
}

void QuickSettings::Show(bool show)
{
    m_shown = show;
    m_sizer->ShowItems(show);
}

void QuickSettings::msw_rescale()
{
    for (const auto& og : m_groups)
        og->msw_rescale();
}

void QuickSettings::sys_color_changed()
{
    update_section_buttons();
    rebuild();
    for (const auto& og : m_groups)
        og->sys_color_changed();
}

void QuickSettings::edit_favorites()
{
    // All the options of the section that can be shown, sorted by category and label.
    struct Entry { std::string key; wxString label; };
    std::vector<Entry> entries;
    const std::vector<std::string>& options = m_section == Section::Filament ? Preset::filament_options() :
                                              m_section == Section::Printer  ? Preset::printer_options() : Preset::print_options();
    for (const std::string& key : options) {
        if (!can_be_favorite(m_section, key) || !config()->has(key))
            continue;
        const ConfigOptionDef* def = print_config_def.get(key);
        const wxString label = _(option_label(key, *def));
        entries.push_back({ key, _(category(key)) + " › " + label });
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.label.CmpNoCase(b.label) < 0; });

    std::vector<std::string> selected = m_keys;

    const wxString titles[SECTIONS] = { _L("Quick print settings"), _L("Quick filament settings"), _L("Quick printer settings") };
    wxDialog dlg(m_parent, wxID_ANY, titles[int(m_section)], wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxGetApp().UpdateDlgDarkUI(&dlg);
    const int em = wxGetApp().em_unit();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(new wxStaticText(&dlg, wxID_ANY, _L("Select the settings to show in the sidebar.")), 0, wxALL, em);
    auto filter = new wxTextCtrl(&dlg, wxID_ANY);
    filter->SetHint(_L("Search"));
    main_sizer->Add(filter, 0, wxEXPAND | wxLEFT | wxRIGHT, em);
    auto list = new wxCheckListBox(&dlg, wxID_ANY, wxDefaultPosition, wxSize(45 * em, 40 * em));
    wxGetApp().UpdateDarkUI(list);
    main_sizer->Add(list, 1, wxEXPAND | wxALL, em);

    std::vector<std::string> shown_keys;
    auto fill = [&]() {
        const wxString text = filter->GetValue().Lower();
        list->Clear();
        shown_keys.clear();
        for (const Entry& e : entries) {
            if (!text.empty() && !e.label.Lower().Contains(text) && !wxString::FromUTF8(e.key).Contains(text))
                continue;
            const int idx = list->Append(e.label);
            list->Check(idx, std::find(selected.begin(), selected.end(), e.key) != selected.end());
            shown_keys.push_back(e.key);
        }
    };
    fill();
    filter->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { fill(); });
    list->Bind(wxEVT_CHECKLISTBOX, [&](wxCommandEvent& evt) {
        const int idx = evt.GetInt();
        const std::string& key = shown_keys[idx];
        auto it = std::find(selected.begin(), selected.end(), key);
        if (list->IsChecked(idx) && it == selected.end())
            selected.push_back(key);
        else if (!list->IsChecked(idx) && it != selected.end())
            selected.erase(it);
    });

    auto btns = new wxBoxSizer(wxHORIZONTAL);
    auto reset_btn = new wxButton(&dlg, wxID_ANY, _L("Reset to defaults"));
    reset_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { selected = default_favorites(m_section); fill(); });
    wxGetApp().UpdateDarkUI(reset_btn);
    btns->Add(reset_btn, 0);
    btns->AddStretchSpacer();
    if (wxSizer* std_btns = dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL))
        btns->Add(std_btns, 0);
    main_sizer->Add(btns, 0, wxEXPAND | wxALL, em);

    dlg.SetSizerAndFit(main_sizer);
    dlg.CenterOnParent();
    filter->SetFocus();
    if (dlg.ShowModal() != wxID_OK)
        return;

    set_favorites(m_section, selected);
    wxWindowUpdateLocker no_updates(m_parent);
    rebuild();
    m_parent->Layout();
    if (wxWindow* top = m_parent->GetParent())
        top->Layout();
}

} // namespace GUI
} // namespace Slic3r
