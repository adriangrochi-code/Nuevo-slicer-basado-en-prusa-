///|/ Tisma Slicer: Devices workspace. The printers connected over USB and over the network (the physical printers of
///|/ PrusaSlicer: OctoPrint, Klipper / Moonraker with Mainsail or Fluidd, PrusaLink, Duet, Repetier, MKS, ...), the web
///|/ interface of the selected printer (camera, temperatures, console, calibrations of its firmware), as the Device tab
///|/ of OrcaSlicer, and sending the sliced G-code.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DevicesPanel_hpp_
#define slic3r_DevicesPanel_hpp_

#include <string>
#include <vector>

#include <wx/panel.h>

class wxListBox;
class wxStaticText;
class wxButton;
class wxCheckBox;

namespace Slic3r {
namespace GUI {

class PrinterWebViewPanel;

class DevicesPanel : public wxPanel
{
public:
    explicit DevicesPanel(wxWindow *parent);

    // Reloads the list of printers (after adding or editing one).
    void update_list();
    void sys_color_changed();

private:
    struct Entry
    {
        enum class Type { USB, Network } type;
        // Network: the name of the physical printer.
        std::string name;
    };

    void on_select();
    void add_network_printer();
    void edit_printer();
    void send_sliced();
    void open_in_browser();
    // URL of the web interface of a physical printer.
    static std::string web_url(const std::string &print_host, int host_type);

    std::vector<Entry>   m_entries;
    wxListBox           *m_list       { nullptr };
    wxStaticText        *m_title      { nullptr };
    wxStaticText        *m_hint       { nullptr };
    wxButton            *m_btn_edit   { nullptr };
    wxButton            *m_btn_send   { nullptr };
    wxButton            *m_btn_browser{ nullptr };
    wxCheckBox          *m_start      { nullptr };
    PrinterWebViewPanel *m_web        { nullptr };
    bool                 m_web_created{ false };
    std::string          m_url;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_DevicesPanel_hpp_
