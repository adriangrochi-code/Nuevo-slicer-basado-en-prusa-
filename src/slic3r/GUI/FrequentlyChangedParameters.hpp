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
#ifndef slic3r_FreqChangedParams_hpp_
#define slic3r_FreqChangedParams_hpp_

#include <memory>
#include <vector>

#include "Event.hpp"

class wxSizer;
class wxWindow;
class Button;
class SwitchButton;
class TextInput;

namespace Slic3r {

namespace GUI {

class TismaChip;
class TismaChoiceChips;
class TismaSlider;
class TismaTitle;

// Tisma: the settings changed most often, in the cards of the print settings of the plate panel: the supports (a row with
// the choice, which unfolds its chips), the infill (a slider) and the brim of a filament print, the supports and the
// pad of a resin print (chips); the density of the infill and the width of the brim can also be typed. A choice is
// loaded in the
// settings tab of the print (Tab::load_config()), which then shows the new values here (update_from_config()).
class FreqChangedParams
{
    // The width of the brim switched off, given back when it is switched on again.
    double                          m_brim_width { 0.0 };
    ::Button*                       m_wiping_dialog_button { nullptr };
    wxSizer*                        m_fff_sizer { nullptr };
    wxSizer*                        m_sla_sizer { nullptr };

    TismaChoiceChips*               m_support     { nullptr };
    // The choice of the supports, which shows or hides their chips.
    TismaChip*                      m_support_choice { nullptr };
    TismaSlider*                    m_infill      { nullptr };
    // Tisma: the density and the width of the brim typed with the keyboard, beside their captions.
    ::TextInput*                    m_infill_value { nullptr };
    ::SwitchButton*                 m_brim        { nullptr };
    ::TextInput*                    m_brim_value  { nullptr };
    wxSizer*                        m_brim_row    { nullptr };
    TismaChoiceChips*               m_sla_support { nullptr };
    TismaChoiceChips*               m_pad         { nullptr };
    std::vector<TismaTitle*>        m_captions;

    void on_support(bool is_fff, int idx);
    void unfold_supports(bool unfold);
    void set_infill(double density);
    void on_brim(bool on);
    void set_brim_width(double width);
    void on_pad(int idx);
    void show_wiping_dialog(wxWindow* parent);

public:
    // fff_parent, sla_parent: the cards of the print settings of the filament and of the resin printers.
    FreqChangedParams(wxWindow* fff_parent, wxWindow* sla_parent);
    ~FreqChangedParams() = default;

    wxWindow*       get_wiping_dialog_button() noexcept;
    wxSizer*        get_sizer(bool is_fff) noexcept { return is_fff ? m_fff_sizer : m_sla_sizer; }
    // Shows the values of the edited print settings.
    void            update_from_config();

    void            msw_rescale();
    void            sys_color_changed();
};

} // namespace GUI
} // namespace Slic3r

#endif
