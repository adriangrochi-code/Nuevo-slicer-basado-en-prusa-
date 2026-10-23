///|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
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
#ifndef slic3r_Sidebar_hpp_
#define slic3r_Sidebar_hpp_

#include <map>
#include <vector>
#include <string>

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/sizer.h>

#include "libslic3r/Preset.hpp"
#include "libslic3r/SLA/Workflows.hpp"
#include "GUI.hpp"
#include "Event.hpp"

class wxButton;
class wxScrolledWindow;
class ScalableButton;
class ModeSizer;

class Button;
class SwitchButton;
class wxStaticText;

class StaticBox;

namespace Slic3r {

namespace GUI {

class ConfigOptionsGroup;
class GLCanvas3D;
class FreqChangedParams;
class QuickSettings;
class ObjectInfo;
class SlicedInfo;
class ObjectManipulation;
class ObjectSettings;
class ObjectLayers;
class ObjectList;
class PlaterPresetComboBox;
class TismaTitle;
class Plater;
class BitmapComboBox;

enum class ActionButtonType : int {
    Reslice,
    Export,
    SendGCode,
    Connect
};

class Sidebar : public wxPanel
{
    ConfigOptionMode    m_mode{ConfigOptionMode::comSimple};

    Plater*             m_plater            { nullptr };

    wxScrolledWindow*   m_scrolled_panel    { nullptr };
    wxPanel*            m_presets_panel     { nullptr }; // Used for MSW better layouts

    wxFlexGridSizer*    m_presets_sizer     { nullptr };
    wxBoxSizer*         m_filaments_sizer   { nullptr };

    PlaterPresetComboBox*               m_combo_print       { nullptr };
    PlaterPresetComboBox*               m_combo_sla_print   { nullptr };
    PlaterPresetComboBox*               m_combo_sla_material{ nullptr };
    PlaterPresetComboBox*               m_combo_printer     { nullptr };
    BitmapComboBox*                     m_workflow          { nullptr };
    std::vector<sla::Workflow>          m_available_workflows;
    std::vector<PlaterPresetComboBox*>  m_combos_filament;
    // Tisma: the sections of the presets as cards (rounded panels), as in OrcaSlicer: printer (with its nozzle and its
    // bed), filaments, print settings, SLA print settings, SLA material.
    std::vector<::StaticBox*>           m_cards;
    ::StaticBox*                        m_card_printer          { nullptr };
    wxBoxSizer*                         m_card_printer_sizer    { nullptr };
    ::StaticBox*                        m_card_filament         { nullptr };
    wxBoxSizer*                         m_card_filament_sizer   { nullptr };
    // The cards of the print settings hold the chips of the settings changed most often (FreqChangedParams).
    ::StaticBox*                        m_card_print            { nullptr };
    wxBoxSizer*                         m_card_print_sizer      { nullptr };
    ::StaticBox*                        m_card_sla_print        { nullptr };
    wxBoxSizer*                         m_card_sla_print_sizer  { nullptr };
    // Tisma: the nozzle and the bed of the printer, chosen apart from the printer as in OrcaSlicer and Bambu Studio.
    // The printer combo lists a model once (by its name); the nozzle combo offers the nozzles of the model (its printer
    // presets) and the usual diameters, which are set on the printer.
    wxStaticText*                       m_label_nozzle      { nullptr };
    BitmapComboBox*                     m_combo_nozzle      { nullptr };
    wxStaticText*                       m_label_bed         { nullptr };
    BitmapComboBox*                     m_combo_bed         { nullptr };
    wxSizerItem*                        m_printer_parts_row { nullptr };
    struct NozzleChoice {
        double      diameter;
        // The printer preset of the vendor for this nozzle; empty: the diameter is set on the edited printer.
        std::string preset;
    };
    std::vector<NozzleChoice>           m_nozzles;          // item of the nozzle combo -> nozzle
    // Rows of the preset chooser shown for the filament (FFF) and the resin (SLA) printers, and for the Prusa SLX.
    std::vector<wxSizerItem*>           m_fff_rows, m_sla_rows, m_slx_rows;
    wxSizerItem*                        m_full_spectrum_row { nullptr };

    ObjectList*     m_object_list               { nullptr };
    ObjectInfo*     m_object_info               { nullptr };
    SlicedInfo*     m_sliced_info               { nullptr };
    // The slicing information is up to date (shown in the panel for a resin print, with the legend for a filament one).
    bool            m_sliced_info_shown         { false };
    std::vector<std::pair<std::string, std::string>> m_sliced_info_rows;
    wxBoxSizer*     m_btns_sizer                { nullptr };
    wxBoxSizer*     m_autoslicing_btns_sizer    { nullptr };


    ::Button*       m_btn_export_gcode          { nullptr };
    ::Button*       m_btn_reslice               { nullptr };
    wxButton*       m_btn_connect_gcode         { nullptr };
    ScalableButton* m_btn_send_gcode            { nullptr };
    ::Button*       m_btn_usb_print             { nullptr };
    ScalableButton* m_btn_export_gcode_removable{ nullptr }; //exports to removable drives (appears only if removable drive is connected)
    // Tisma: the output of the sliced plate as in Cura: the main button does the chosen action, the arrow next to it
    // lists the other ones available (print via USB, send to the printer, export to the SD card ...). The buttons of
    // those actions are kept hidden, they only say which actions are available.
    enum class OutputAction { Export, USBPrint, Removable, Send, Connect };
    ::Button*       m_btn_more_actions          { nullptr };
    mutable std::map<OutputAction, bool> m_action_available;
    mutable wxString m_export_label;
    OutputAction    main_action() const;
    void            update_main_action() const;
    void            run_action(OutputAction action);
    void            show_actions_menu(bool at_mouse = false);
    static wxString action_label(OutputAction action, const wxString &export_label);
    // Tisma: arrow that hides the settings of the sidebar and keeps the slice and output buttons.
    ::Button*       m_btn_compact               { nullptr };
    bool            m_compact                   { false };
    void            set_compact(bool compact);
    // Tisma: slice right after every change (the background processing of PrusaSlicer, as a switch above the slice
    // button).
    TismaTitle*     m_auto_slice_label          { nullptr };
    ::SwitchButton* m_auto_slice                { nullptr };
    void            set_auto_slice(bool on);
    void            slice_now();
    // Sets whether an output action is available; true if it changed.
    bool            set_available(OutputAction action, bool available) const;
                                                             //
    wxButton* m_btn_export_all_gcode                { nullptr };
    wxButton* m_btn_connect_gcode_all               { nullptr };
	ScalableButton* m_btn_export_all_gcode_removable{ nullptr };

    wxButton*                           m_btn_full_spectrum { nullptr };
    std::unique_ptr<FreqChangedParams>  m_frequently_changed_parameters;
    std::unique_ptr<QuickSettings>      m_quick_settings;
    std::unique_ptr<ObjectManipulation> m_object_manipulation;
    std::unique_ptr<ObjectSettings>     m_object_settings;
    std::unique_ptr<ObjectLayers>       m_object_layers;

    bool m_autoslicing_mode{ false };
#ifdef _WIN32
    wxString m_reslice_btn_tooltip;
#endif

    void init_filament_combo(PlaterPresetComboBox **combo, int extr_idx);
    void remove_unused_filament_combos(const size_t current_extruder_count);
    void update_all_preset_comboboxes();
    void update_reslice_btn_tooltip();
    void init_workflow_combo(int margin_5);
    void init_printer_parts();
    void update_printer_parts();
    void on_select_nozzle();
    // Tisma: a diameter typed in the nozzle field.
    void on_type_nozzle();
    void apply_nozzle(const NozzleChoice &nozzle);
    void on_select_bed();

    void show_preset_comboboxes();
    // Tisma colors of the panel and of the action buttons.
    void apply_tisma_theme();
    // The titles of the cards.
    std::vector<TismaTitle*> m_titles;
    void on_select_preset(wxCommandEvent& evt);

public:
    Sidebar(Plater *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    ObjectManipulation*     obj_manipul();
    ObjectList*             obj_list();
    ObjectSettings*         obj_settings();
    ObjectLayers*           obj_layers();

    wxWindow*               get_wiping_dialog_button();
    // The chips of the supports, infill, brim and pad show the values of the edited print settings.
    void                    update_frequently_changed_parameters();
    QuickSettings*          quick_settings();

    void show_info_sizer();
    void show_sliced_info_sizer(const bool show);
    void show_btns_sizer(const bool show);
    void show_bulk_btns_sizer(const bool show);

    void update_sliced_info_sizer();
    // Tisma: the slicing information of a filament print (name, value; the estimated time apart), shown with the legend
    // of the preview (GCodeViewer::render_legend()) instead of in the panel. Empty while the plate is not sliced.
    const std::vector<std::pair<std::string, std::string>>& sliced_info_rows() const { return m_sliced_info_rows; }

    void enable_buttons(bool enable);
    void set_btn_label(const ActionButtonType btn_type, const wxString& label) const;
    bool show_reslice(bool show) const;
    bool show_export(bool show) const;
    bool show_send(bool show) const;
    bool show_usb_print(bool show) const;
    bool show_export_removable(bool show) const;
    bool show_connect(bool show) const;

    void enable_bulk_buttons(bool enable);
    bool show_export_all(bool show) const;
    bool show_export_removable_all(bool show) const;
    bool show_connect_all(bool show) const;

    void switch_to_autoslicing_mode();
    void switch_from_autoslicing_mode();

    void collapse(bool collapse);
    // Tisma: with the settings hidden, the slice and output buttons float over the 3D view (drawn with ImGui).
    void render_compact_actions(GLCanvas3D &canvas);
    bool is_compact() const { return m_compact; }
    void set_extruders_count(size_t extruders_count);

    void update_mode();
    void update_ui_from_settings();
    void update_objects_list_extruder_column(size_t extruders_count);
    void update_presets(Preset::Type preset_type);
    void update_printer_presets_combobox();
    void update_all_filament_comboboxes();
    void update_workflow_combobox_if_needed();

    void on_workflow_changed();

    void msw_rescale();
    void sys_color_changed();

    bool is_collapsed{ false };
};

} // namespace GUI
} // namespace Slic3r

#endif
