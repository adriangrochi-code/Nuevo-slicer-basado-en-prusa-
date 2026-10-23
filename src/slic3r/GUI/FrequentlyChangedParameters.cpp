///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 Michael Kirsch
///|/ Copyright (c) 2021 Boleslaw Ciesielski
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ ported from lib/Slic3r/GUI/Plater.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/ Copyright (c) 2017 Matthias Gazzari @qtux
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) 2015 Daren Schwenke
///|/ Copyright (c) 2014 Mark Hindess
///|/ Copyright (c) 2012 Mike Sheldrake @mesheldrake
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2012 Sam Wong
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "FrequentlyChangedParameters.hpp"
#include "Plater.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>

#include "libslic3r/PresetBundle.hpp"

#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "I18N.hpp"
#include "TismaTheme.hpp"
#include "TismaWidgets.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/SwitchButton.hpp"
#include "WipeTowerDialog.hpp"

using Slic3r::Preset;
using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

// Trigger Plater::schedule_background_process().
wxDEFINE_EVENT(EVT_SCHEDULE_BACKGROUND_PROCESS,     SimpleEvent);

// The chips of the supports: none, on the build plate only, everywhere, where the user painted (enforcers) only.
enum SupportChip { scNone, scBuildPlate, scEverywhere, scEnforcers };
// The chips of the infill: these densities and a last chip for any other one.
static const std::vector<double> INFILL_DENSITIES = { 0., 10., 15., 20., 30., 50., 100. };
// The chips of the pad of a resin print.
enum PadChip { pcNone, pcBelow, pcAround };

static int support_chip(const DynamicPrintConfig& config, bool is_fff)
{
    if (! config.opt_bool(is_fff ? "support_material" : "supports_enable"))
        return scNone;
    if (is_fff ? ! config.opt_bool("support_material_auto") : config.opt_bool("support_enforcers_only"))
        return scEnforcers;
    const std::string buildplate_only = is_fff ? "support_material_buildplate_only" :
                                                 get_sla_suptree_prefix(config) + "support_buildplate_only";
    return config.opt_bool(buildplate_only) ? scBuildPlate : scEverywhere;
}

static wxString percent(double value)
{
    return wxString::Format("%g%%", value);
}

FreqChangedParams::FreqChangedParams(wxWindow* fff_parent, wxWindow* sla_parent)
{
    const int em = wxGetApp().em_unit();
    auto caption = [this, em](wxWindow* parent, wxSizer* sizer, const wxString& text) {
        auto* title = new TismaTitle(parent, text, TismaTitle::Style::Caption);
        m_captions.push_back(title);
        sizer->Add(title, 0, wxTOP | wxBOTTOM, int(0.4 * em));
    };
    const std::vector<wxString> supports = { _L("None"), _L("Build plate only"), _L("Everywhere"), _L("Painted only") };

    // Filament printers: supports, infill, brim and the purging volumes of a multi-material printer.
    m_fff_sizer = new wxBoxSizer(wxVERTICAL);
    caption(fff_parent, m_fff_sizer, _L("Supports"));
    m_support = new TismaChoiceChips(fff_parent, supports, [this](int idx) { on_support(true, idx); });
    m_support->SetToolTip(_L("Select what kind of support do you need"));
    m_fff_sizer->Add(m_support, 0, wxEXPAND);

    caption(fff_parent, m_fff_sizer, _L("Infill"));
    std::vector<wxString> densities;
    for (double d : INFILL_DENSITIES)
        densities.push_back(percent(d));
    densities.push_back(_L("Other") + dots);
    m_infill = new TismaChoiceChips(fff_parent, densities, [this](int idx) { on_infill(idx); });
    m_fff_sizer->Add(m_infill, 0, wxEXPAND);

    auto* brim_row = new wxBoxSizer(wxHORIZONTAL);
    auto* brim_label = new TismaTitle(fff_parent, _L("Brim"), TismaTitle::Style::Caption);
    m_captions.push_back(brim_label);
    brim_row->Add(brim_label, 1, wxALIGN_CENTER_VERTICAL);
    m_brim = new ::SwitchButton(fff_parent);
    m_brim->SetToolTip(_L("This flag enables the brim that will be printed around each object on the first layer."));
    m_brim->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) { on_brim(e.IsChecked()); });
    brim_row->Add(m_brim, 0, wxALIGN_CENTER_VERTICAL);
    m_fff_sizer->Add(brim_row, 0, wxEXPAND | wxTOP, int(0.4 * em));

    m_wiping_dialog_button = new ::Button(fff_parent, _L("Purging volumes") + dots);
    m_wiping_dialog_button->Bind(wxEVT_BUTTON, [this, fff_parent](wxCommandEvent&) { show_wiping_dialog(fff_parent); });
    m_wiping_dialog_button->Hide();
    m_fff_sizer->Add(m_wiping_dialog_button, 0, wxTOP, int(0.4 * em));

    // Resin printers: supports and pad.
    m_sla_sizer = new wxBoxSizer(wxVERTICAL);
    caption(sla_parent, m_sla_sizer, _L("Supports"));
    m_sla_support = new TismaChoiceChips(sla_parent, supports, [this](int idx) { on_support(false, idx); });
    m_sla_support->SetToolTip(_L("Select what kind of support do you need"));
    m_sla_sizer->Add(m_sla_support, 0, wxEXPAND);
    caption(sla_parent, m_sla_sizer, _L("Pad"));
    m_pad = new TismaChoiceChips(sla_parent, { _L("None"), _L("Below object"), _L("Around object") },
                                 [this](int idx) { on_pad(idx); });
    m_pad->SetToolTip(_L("Select what kind of pad do you need"));
    m_sla_sizer->Add(m_pad, 0, wxEXPAND);

    m_brim_width = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("brim_width");
    sys_color_changed();
    update_from_config();
}

wxWindow* FreqChangedParams::get_wiping_dialog_button() noexcept
{
    return m_wiping_dialog_button;
}

void FreqChangedParams::update_from_config()
{
    const DynamicPrintConfig& fff = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    m_support->set_selection(support_chip(fff, true));

    const double density = fff.option<ConfigOptionPercent>("fill_density")->value;
    const auto it = std::find_if(INFILL_DENSITIES.begin(), INFILL_DENSITIES.end(),
                                 [density](double d) { return std::abs(d - density) < 1e-6; });
    const int other = int(INFILL_DENSITIES.size());
    m_infill->set_selection(it == INFILL_DENSITIES.end() ? other : int(it - INFILL_DENSITIES.begin()));
    // The chip of another density shows it.
    m_infill->set_label(other, it == INFILL_DENSITIES.end() ? percent(density) : _L("Other") + dots);

    m_brim->SetValue(fff.opt_float("brim_width") > 0.);

    const DynamicPrintConfig& sla = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    m_sla_support->set_selection(support_chip(sla, false));
    m_pad->set_selection(! sla.opt_bool("pad_enable") ? pcNone : sla.opt_bool("pad_around_object") ? pcAround : pcBelow);
}

void FreqChangedParams::on_support(bool is_fff, int idx)
{
    Tab* tab = wxGetApp().get_tab(is_fff ? Preset::TYPE_PRINT : Preset::TYPE_SLA_PRINT);
    if (! tab)
        return;
    DynamicPrintConfig new_conf = is_fff ? wxGetApp().preset_bundle->prints.get_edited_preset().config :
                                           wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    if (is_fff) {
        new_conf.set_key_value("support_material", new ConfigOptionBool(idx != scNone));
        if (idx != scNone) {
            new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(idx == scBuildPlate));
            new_conf.set_key_value("support_material_auto", new ConfigOptionBool(idx != scEnforcers));
        }
    } else {
        new_conf.set_key_value("supports_enable", new ConfigOptionBool(idx != scNone));
        if (idx == scEnforcers)
            new_conf.set_key_value("support_enforcers_only", new ConfigOptionBool(true));
        else if (idx != scNone) {
            new_conf.set_key_value(get_sla_suptree_prefix(new_conf) + "support_buildplate_only", new ConfigOptionBool(idx == scBuildPlate));
            new_conf.set_key_value("support_enforcers_only", new ConfigOptionBool(false));
        }
    }
    tab->load_config(new_conf);
}

void FreqChangedParams::on_infill(int idx)
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (! tab)
        return;
    DynamicPrintConfig new_conf = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    double density = 0.;
    if (idx < int(INFILL_DENSITIES.size()))
        density = INFILL_DENSITIES[idx];
    else {
        const double current = new_conf.option<ConfigOptionPercent>("fill_density")->value;
        wxTextEntryDialog dlg(wxGetApp().GetTopWindow(), _L("Infill density, in percent:"), _L("Infill"),
                              wxString::Format("%g", current));
        double value = -1.;
        if (dlg.ShowModal() == wxID_OK) {
            wxString text = dlg.GetValue();
            text.Replace(",", ".");
            text.Replace("%", "");
            text.Trim().Trim(false);
            if (! text.ToCDouble(&value))
                value = -1.;
        }
        if (value < 0. || value > 100.) {
            update_from_config();
            return;
        }
        density = value;
    }
    new_conf.set_key_value("fill_density", new ConfigOptionPercent(density));
    tab->load_config(new_conf);
    update_from_config();
}

void FreqChangedParams::on_brim(bool on)
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    if (! tab)
        return;
    DynamicPrintConfig new_conf = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const double brim_width = new_conf.opt_float("brim_width");
    double new_val = 0.;
    if (on)
        new_val = m_brim_width == 0. ? 5 : m_brim_width < 0. ? -m_brim_width : m_brim_width;
    else
        m_brim_width = -brim_width;
    new_conf.set_key_value("brim_width", new ConfigOptionFloat(new_val));
    tab->load_config(new_conf);
}

void FreqChangedParams::on_pad(int idx)
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_SLA_PRINT);
    if (! tab)
        return;
    DynamicPrintConfig new_conf = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    new_conf.set_key_value("pad_enable", new ConfigOptionBool(idx != pcNone));
    if (idx != pcNone)
        new_conf.set_key_value("pad_around_object", new ConfigOptionBool(idx == pcAround));
    tab->load_config(new_conf);
}

void FreqChangedParams::show_wiping_dialog(wxWindow* parent)
{
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    DynamicPrintConfig& project_config = preset_bundle->project_config;
    const bool use_custom_matrix = (project_config.option<ConfigOptionBool>("wiping_volumes_use_custom_matrix"))->value;
    const std::vector<double> &init_matrix = (project_config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values;

    const std::vector<std::string> extruder_colours = wxGetApp().plater()->get_extruder_color_strings_from_plater_config();

    // Extract the relevant config options, even values from possibly modified presets.
    const double default_purge = static_cast<const ConfigOptionFloat*>(preset_bundle->printers.get_edited_preset().config.option("multimaterial_purging"))->value;
    std::vector<double> filament_purging_multipliers = preset_bundle->get_config_options_for_current_filaments<ConfigOptionPercents>("filament_purge_multiplier");

    WipingDialog dlg(parent, cast<float>(init_matrix), extruder_colours, default_purge, filament_purging_multipliers, use_custom_matrix);

    if (dlg.ShowModal() == wxID_OK) {
        std::vector<float> matrix = dlg.get_matrix();
        (project_config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values = std::vector<double>(matrix.begin(), matrix.end());
        (project_config.option<ConfigOptionBool>("wiping_volumes_use_custom_matrix"))->value = dlg.get_use_custom_matrix();
        // Update Project dirty state, update application title bar.
        Plater* plater = wxGetApp().plater();
        plater->update_project_dirty_from_presets();
        wxPostEvent(plater, SimpleEvent(EVT_SCHEDULE_BACKGROUND_PROCESS, plater));
    }
}

void FreqChangedParams::msw_rescale()
{
    for (TismaChoiceChips* chips : { m_support, m_infill, m_sla_support, m_pad })
        chips->msw_rescale();
    m_brim->Rescale();
    m_wiping_dialog_button->Rescale();
}

void FreqChangedParams::sys_color_changed()
{
    const bool dark = wxGetApp().dark_mode();
    for (TismaTitle* title : m_captions)
        title->sys_color_changed();
    for (TismaChoiceChips* chips : { m_support, m_infill, m_sla_support, m_pad })
        chips->sys_color_changed();
    m_brim->SysColorChange();
    // A link in the accent color.
    m_wiping_dialog_button->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(TismaTheme::panel_bg(dark), StateColor::Hovered),
        std::pair<wxColour, int>(TismaTheme::card_bg(dark),  StateColor::Normal)));
    m_wiping_dialog_button->SetBorderColor(StateColor(
        std::pair<wxColour, int>(TismaTheme::card_bg(dark),  StateColor::Normal)));
    m_wiping_dialog_button->SetTextColor(StateColor(
        std::pair<wxColour, int>(TismaTheme::accent_text(dark), StateColor::Normal)));
}

}}    // namespace Slic3r::GUI
