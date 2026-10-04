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
#include "Plater.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

static const char *APP_CONFIG_KEY = "tisma_quick_settings";

static const DynamicPrintConfig& print_config()
{
    return wxGetApp().preset_bundle->prints.get_edited_preset().config;
}

const std::vector<std::string>& QuickSettings::default_favorites()
{
    static const std::vector<std::string> keys {
        "layer_height", "first_layer_height",
        "perimeters", "top_solid_layers", "bottom_solid_layers",
        "fill_pattern", "infill_dense",
        "overhang_arcs",
        "perimeter_speed", "infill_speed", "travel_speed",
        "skirts",
    };
    return keys;
}

bool QuickSettings::can_be_favorite(const std::string& opt_key)
{
    const std::vector<std::string>& print_options = Preset::print_options();
    if (std::find(print_options.begin(), print_options.end(), opt_key) == print_options.end())
        return false;
    // Options edited by dedicated widgets of the sidebar or of the settings tab.
    static const std::vector<std::string> excluded { "inherits", "compatible_printers", "compatible_printers_condition",
                                                     "compatible_prints", "compatible_prints_condition", "fill_density",
                                                     "support_material", "brim_width" };
    if (std::find(excluded.begin(), excluded.end(), opt_key) != excluded.end())
        return false;
    const ConfigOptionDef* def = print_config_def.get(opt_key);
    if (def == nullptr || def->label.empty() || def->multiline || def->is_code || def->full_width || def->readonly)
        return false;
    switch (def->type) {
    case coFloat: case coInt: case coBool: case coEnum: case coPercent: case coFloatOrPercent: case coString:
        return true;
    default:
        return false;
    }
}

std::string QuickSettings::category(const std::string& opt_key)
{
    const ConfigOptionDef* def = print_config_def.get(opt_key);
    if (def != nullptr && !def->category.empty())
        return def->category;
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

std::vector<std::string> QuickSettings::favorites()
{
    const AppConfig* app_config = wxGetApp().app_config;
    if (app_config == nullptr || !app_config->has(APP_CONFIG_KEY))
        return default_favorites();
    std::vector<std::string> stored, keys;
    const std::string value = app_config->get(APP_CONFIG_KEY);
    if (!value.empty())
        boost::split(stored, value, boost::is_any_of(";"));
    for (const std::string& key : stored)
        if (can_be_favorite(key) && std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(key);
    return keys;
}

void QuickSettings::set_favorites(const std::vector<std::string>& keys)
{
    wxGetApp().app_config->set(APP_CONFIG_KEY, boost::algorithm::join(keys, ";"));
}

QuickSettings::QuickSettings(wxWindow* parent) : m_parent(parent)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);

    auto header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(parent, wxID_ANY, _L("Quick settings"));
    m_title->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(m_title);
    header->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);

    m_edit_btn = new ScalableButton(parent, wxID_ANY, "edit");
    m_edit_btn->SetToolTip(_L("Choose the settings shown here"));
    m_edit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_favorites(); });
    header->Add(m_edit_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, int(0.3 * wxGetApp().em_unit()));

    m_sizer->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, int(0.3 * wxGetApp().em_unit()));

    m_groups_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_groups_sizer, 0, wxEXPAND);

    rebuild();
}

QuickSettings::~QuickSettings() = default;

void QuickSettings::rebuild()
{
    m_groups_sizer->Clear(true);
    m_groups.clear();
    m_keys = favorites();

    DynamicPrintConfig* config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;

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

    for (const auto& [cat, keys] : groups) {
        auto og = std::make_shared<ConfigOptionsGroup>(m_parent, _(cat), config);
        og->label_width = 18;
        og->sidetext_width = 6;
        og->on_change = [this](const t_config_option_key& opt_key, const boost::any&) { on_change(opt_key); };
        for (const std::string& key : keys) {
            Option option = og->get_option(key);
            if (!option.opt.full_label.empty())
                option.opt.label = option.opt.full_label;
            option.opt.width = 8;
            og->append_single_option_line(option);
        }
        og->activate();
        og->reload_config();
        m_groups_sizer->Add(og->sizer, 0, wxEXPAND | wxBOTTOM, int(0.5 * wxGetApp().em_unit()));
        m_groups.push_back(og);
    }

    Show(m_shown);
}

void QuickSettings::on_change(const std::string& opt_key)
{
    // The options group already wrote the value into the edited print preset.
    Tab* tab_print = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (tab_print == nullptr)
        return;
    tab_print->update_dirty();
    tab_print->reload_config();
    tab_print->update();
    // Some options (e.g. support or infill toggles) may change the other fields of this panel.
    for (const auto& og : m_groups)
        for (const std::string& key : m_keys)
            if (key != opt_key && og->get_field(key) != nullptr)
                og->set_value(key, og->get_config_value(print_config(), key));
}

void QuickSettings::update_value(const std::string& opt_key)
{
    for (const auto& og : m_groups)
        if (og->get_field(opt_key) != nullptr)
            og->set_value(opt_key, og->get_config_value(print_config(), opt_key));
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
    m_edit_btn->sys_color_changed();
    wxGetApp().UpdateDarkUI(m_title);
    for (const auto& og : m_groups)
        og->sys_color_changed();
}

void QuickSettings::edit_favorites()
{
    // All the options that can be shown, sorted by category and label.
    struct Entry { std::string key; wxString label; };
    std::vector<Entry> entries;
    for (const std::string& key : Preset::print_options()) {
        if (!can_be_favorite(key))
            continue;
        const ConfigOptionDef* def = print_config_def.get(key);
        const wxString label = _(def->full_label.empty() ? def->label : def->full_label);
        entries.push_back({ key, _(category(key)) + " › " + label });
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.label.CmpNoCase(b.label) < 0; });

    std::vector<std::string> selected = m_keys;

    wxDialog dlg(m_parent, wxID_ANY, _L("Quick settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxGetApp().UpdateDlgDarkUI(&dlg);
    const int em = wxGetApp().em_unit();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(new wxStaticText(&dlg, wxID_ANY, _L("Select the print settings to show in the sidebar.")), 0, wxALL, em);
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
    reset_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { selected = default_favorites(); fill(); });
    wxGetApp().UpdateDarkUI(reset_btn);
    btns->Add(reset_btn, 0);
    btns->AddStretchSpacer();
    if (wxSizer* std_btns = dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL))
        btns->Add(std_btns, 0);
    main_sizer->Add(btns, 0, wxEXPAND | wxALL, em);

    dlg.SetSizerAndFit(main_sizer);
    dlg.CenterOnParent();
    if (dlg.ShowModal() != wxID_OK)
        return;

    set_favorites(selected);
    wxWindowUpdateLocker no_updates(m_parent);
    rebuild();
    m_parent->Layout();
    if (wxWindow* top = m_parent->GetParent())
        top->Layout();
}

} // namespace GUI
} // namespace Slic3r
