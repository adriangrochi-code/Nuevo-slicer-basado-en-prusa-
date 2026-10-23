///|/ Tisma Slicer: Devices workspace. The printers connected over USB and over the network (the physical printers of
///|/ PrusaSlicer: OctoPrint, Klipper / Moonraker with Mainsail or Fluidd, PrusaLink, Duet, Repetier, MKS, ...), the web
///|/ interface of the selected printer (camera, temperatures, console, calibrations of its firmware), as the Device tab
///|/ of OrcaSlicer, and sending the sliced G-code.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DevicesPanel_hpp_
#define slic3r_DevicesPanel_hpp_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <wx/panel.h>

class wxStaticText;
class Button;
class SwitchButton;

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

class BambuCameraView;
class PrinterWebViewPanel;
class TismaPageList;
class TismaTitle;
class USBMonitorPanel;

class DevicesPanel : public wxPanel
{
public:
    explicit DevicesPanel(wxWindow *parent);
    ~DevicesPanel() override;

    // Reloads the list of printers (after adding or editing one).
    void update_list();
    // Selects the USB printer.
    void select_usb();
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
    // Asks for the new access code / API key of the selected printer, tests it and saves it.
    void change_access_code();
    void send_sliced();
    void open_in_browser();
    // The camera of a Bambu Lab printer (A1, P1), streamed in the background while the printer is selected.
    void start_camera(const DynamicPrintConfig &config);
    void stop_camera();
    // URL of the web interface of a physical printer.
    static std::string web_url(const std::string &print_host, int host_type);

    std::vector<Entry>   m_entries;
    // Tisma: the list of the pages of the settings (a row with an icon for each printer), buttons and a switch of Tisma.
    TismaPageList       *m_list       { nullptr };
    TismaTitle          *m_caption    { nullptr };
    TismaTitle          *m_title      { nullptr };
    wxStaticText        *m_hint       { nullptr };
    ::Button            *m_btn_add    { nullptr };
    ::Button            *m_btn_edit   { nullptr };
    ::Button            *m_btn_code   { nullptr };
    ::Button            *m_btn_send   { nullptr };
    ::Button            *m_btn_browser{ nullptr };
    ::SwitchButton      *m_start      { nullptr };
    wxStaticText        *m_start_label{ nullptr };
    void                 style_controls();
    PrinterWebViewPanel *m_web        { nullptr };
    USBMonitorPanel     *m_usb        { nullptr };
    BambuCameraView     *m_camera     { nullptr };
    // Set to end the camera stream in flight.
    std::shared_ptr<std::atomic<bool>> m_camera_stop;
    bool                 m_web_created{ false };
    std::string          m_url;
    // Bambu Lab printers: the status request in flight (older answers are ignored).
    int                  m_status_request{ 0 };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_DevicesPanel_hpp_
