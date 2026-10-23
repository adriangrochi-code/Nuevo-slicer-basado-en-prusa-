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
#include <functional>
#include <limits>
#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>

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
#include "Widgets/TextInput.hpp"
#include "WipeTowerDialog.hpp"

using Slic3r::Preset;
using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

// Trigger Plater::schedule_background_process().
wxDEFINE_EVENT(EVT_SCHEDULE_BACKGROUND_PROCESS,     SimpleEvent);

// The chips of the supports: none, on the build plate only, everywhere, where the user painted (enforcers) only.
enum SupportChip { scNone, scBuildPlate, scEverywhere, scEnforcers };
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

// The labels of the support chips, in the order of SupportChip.
static std::vector<wxString> support_labels()
{
    return { _L("None"), _L("Build plate only"), _L("Everywhere"), _L("Painted only") };
}

// A small field of a number typed with the keyboard, added to the row with its unit after it: Enter or leaving the
// field gives the number to on_value (NaN when the text is not a number).
static ::TextInput* number_field(wxWindow* parent, wxSizer* row, const wxString& unit, std::function<void(double)> on_value)
{
    const int em = wxGetApp().em_unit();
    auto* field = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(6 * em, -1),
                                  wxTE_PROCESS_ENTER);
    auto* unit_text = new wxStaticText(parent, wxID_ANY, unit);
    unit_text->SetForegroundColour(TismaTheme::text_muted(wxGetApp().dark_mode()));
    row->Add(field, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em / 3);
    auto apply = [field, on_value]() {
        // The number at the start, with a comma or a point ("15", "12,5 %", "5mm").
        wxString text = field->GetTextCtrl()->GetValue();
        text.Replace(",", ".");
        text.Trim(false);
        size_t end = 0;
        while (end < text.length() && wxString("0123456789.+-").Find(text[end]) != wxNOT_FOUND)
            ++ end;
        double value = std::numeric_limits<double>::quiet_NaN();
        if (end == 0 || ! text.Left(end).ToCDouble(&value))
            value = std::numeric_limits<double>::quiet_NaN();
        on_value(value);
    };
    field->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent&) { apply(); });
    field->Bind(wxEVT_KILL_FOCUS, [apply](wxFocusEvent& e) { apply(); e.Skip(); });
    return field;
}

FreqChangedParams::FreqChangedParams(wxWindow* fff_parent, wxWindow* sla_parent)
{
    const int em = wxGetApp().em_unit();
    auto caption = [this, em](wxWindow* parent, wxSizer* sizer, const wxString& text) {
        auto* title = new TismaTitle(parent, text, TismaTitle::Style::Caption);
        m_captions.push_back(title);
        sizer->Add(title, 0, wxTOP | wxBOTTOM, int(0.4 * em));
    };
    const std::vector<wxString> supports = support_labels();

    // Filament printers: supports, infill, brim and the purging volumes of a multi-material printer.
    m_fff_sizer = new wxBoxSizer(wxVERTICAL);
    // The supports in one row, the caption and the choice; a click on the choice unfolds the chips to change it, and they
    // fold again once one is chosen.
    auto* support_row = new wxBoxSizer(wxHORIZONTAL);
    auto* support_label = new TismaTitle(fff_parent, _L("Supports"), TismaTitle::Style::Caption);
    m_captions.push_back(support_label);
    support_row->Add(support_label, 1, wxALIGN_CENTER_VERTICAL);
    m_support_choice = new TismaChip(fff_parent, supports.front(), "collapse_btn",
                                     [this]() { unfold_supports(! m_support->IsShown()); }, 2.2);
    m_support_choice->SetToolTip(_L("Select what kind of support do you need"));
    support_row->Add(m_support_choice, 0, wxALIGN_CENTER_VERTICAL);
    m_fff_sizer->Add(support_row, 0, wxEXPAND | wxTOP | wxBOTTOM, int(0.4 * em));
    m_support = new TismaChoiceChips(fff_parent, supports, [this](int idx) { on_support(true, idx); });
    m_support->SetToolTip(_L("Select what kind of support do you need"));
    m_fff_sizer->Add(m_support, 0, wxEXPAND | wxBOTTOM, int(0.4 * em));

    // The infill in one row: the caption, a slider and the density typed beside it. The slider shows the density while
    // it is dragged and loads it when it is released (a new slicing at each step would be of no use).
    auto* infill_row = new wxBoxSizer(wxHORIZONTAL);
    auto* infill_label = new TismaTitle(fff_parent, _L("Infill"), TismaTitle::Style::Caption);
    m_captions.push_back(infill_label);
    infill_row->Add(infill_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
    m_infill = new TismaSlider(fff_parent, 0., 100., 1., [this](double v, bool final) {
        if (final)
            set_infill(v);
        else
            m_infill_value->GetTextCtrl()->ChangeValue(wxString::Format("%g", v));
    });
    m_infill->SetToolTip(_L("Infill density, in percent:"));
    infill_row->Add(m_infill, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, em);
    m_infill_value = number_field(fff_parent, infill_row, "%", [this](double v) { set_infill(v); });
    m_infill_value->SetToolTip(_L("Infill density, in percent:"));
    m_fff_sizer->Add(infill_row, 0, wxEXPAND | wxTOP | wxBOTTOM, int(0.4 * em));

    // The brim: its width typed when it is on, and the switch.
    auto* brim_row = new wxBoxSizer(wxHORIZONTAL);
    auto* brim_label = new TismaTitle(fff_parent, _L("Brim"), TismaTitle::Style::Caption);
    m_captions.push_back(brim_label);
    brim_row->Add(brim_label, 1, wxALIGN_CENTER_VERTICAL);
    m_brim_row = brim_row;
    m_brim_value = number_field(fff_parent, brim_row, _L("mm"), [this](double v) { set_brim_width(v); });
    m_brim_value->SetToolTip(_L("Brim width"));
    brim_row->AddSpacer(em);
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
    unfold_supports(false);
}

void FreqChangedParams::unfold_supports(bool unfold)
{
    m_support->Show(unfold);
    // The chevrons down while the chips are folded (they open below), up while they are shown.
    m_support_choice->set_icon(unfold ? "expand_btn" : "collapse_btn");
    m_fff_sizer->Layout();
    if (wxWindow* card = m_support->GetParent()) {
        card->Layout();
        if (card->GetParent())
            card->GetParent()->Layout();
    }
}

wxWindow* FreqChangedParams::get_wiping_dialog_button() noexcept
{
    return m_wiping_dialog_button;
}

void FreqChangedParams::update_from_config()
{
    const DynamicPrintConfig& fff = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const int support = support_chip(fff, true);
    m_support->set_selection(support);
    m_support_choice->set_text(support_labels()[std::clamp(support, 0, int(scEnforcers))]);

    const double density = fff.option<ConfigOptionPercent>("fill_density")->value;
    m_infill->set_value(density);
    m_infill_value->GetTextCtrl()->ChangeValue(wxString::Format("%g", density));

    const double brim_width = fff.opt_float("brim_width");
    m_brim->SetValue(brim_width > 0.);
    m_brim_value->GetTextCtrl()->ChangeValue(wxString::Format("%g", brim_width));
    // The width and its unit only while the brim is on.
    m_brim_row->Show(size_t(1), brim_width > 0.);
    m_brim_row->Show(size_t(2), brim_width > 0.);
    m_fff_sizer->Layout();

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
    if (is_fff)
        unfold_supports(false);
}

void FreqChangedParams::set_infill(double density)
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    DynamicPrintConfig new_conf = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    // Not a density (or the same one): the field shows the density of the profile again.
    if (! tab || std::isnan(density) || density < 0. || density > 100. ||
        std::abs(new_conf.option<ConfigOptionPercent>("fill_density")->value - density) < 1e-9) {
        update_from_config();
        return;
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
    update_from_config();
}

void FreqChangedParams::set_brim_width(double width)
{
    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
    DynamicPrintConfig new_conf = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    // Not a width (or the same one): the field shows the width of the profile again; 0 switches the brim off.
    if (! tab || std::isnan(width) || width < 0. || width > 200. || std::abs(new_conf.opt_float("brim_width") - width) < 1e-9) {
        update_from_config();
        return;
    }
    if (width == 0.)
        m_brim_width = -new_conf.opt_float("brim_width");
    new_conf.set_key_value("brim_width", new ConfigOptionFloat(width));
    tab->load_config(new_conf);
    update_from_config();
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
    for (TismaChoiceChips* chips : { m_support, m_sla_support, m_pad })
        chips->msw_rescale();
    m_support_choice->msw_rescale();
    m_infill->msw_rescale();
    m_brim->Rescale();
    m_infill_value->Rescale();
    m_brim_value->Rescale();
    m_wiping_dialog_button->Rescale();
}

void FreqChangedParams::sys_color_changed()
{
    const bool dark = wxGetApp().dark_mode();
    for (TismaTitle* title : m_captions)
        title->sys_color_changed();
    for (TismaChoiceChips* chips : { m_support, m_sla_support, m_pad })
        chips->sys_color_changed();
    m_support_choice->sys_color_changed();
    m_infill->sys_color_changed();
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
