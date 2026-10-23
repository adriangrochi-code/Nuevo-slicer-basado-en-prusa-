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

#include "../Utils/UsbCamera.hpp"

class wxComboBox;
class wxStaticText;
class Button;
class SwitchButton;

namespace Slic3r {

class DynamicPrintConfig;

namespace GUI {

class CameraView;
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
    // The printer selected in the list (on_select, then the cameras).
    void select_printer();
    // The camera of a Bambu Lab printer (A1, P1), streamed in the background while the printer is selected.
    void start_camera(const DynamicPrintConfig &config);
    void stop_camera();
    // The camera shown for the selected printer: its own (Bambu Lab A1, P1) or a USB camera of this computer, chosen
    // per printer and kept in the configuration of Tisma (section tisma_usb_cameras).
    void refresh_cameras();
    void on_camera_choice();
    void show_selected_camera();
    void start_usb_camera(const UsbCameraInfo &camera);
    // Shows the view of the camera of the selected printer and returns it.
    CameraView* show_camera_view();
    // Key of the selected printer in tisma_usb_cameras.
    std::string camera_key() const;
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
    CameraView          *m_camera     { nullptr };
    wxStaticText        *m_camera_label { nullptr };
    wxComboBox          *m_camera_choice{ nullptr };
    wxStaticText        *m_camera_info  { nullptr };
    // The USB cameras in m_camera_choice, after its first item (the camera of the printer, or none).
    std::vector<UsbCameraInfo> m_usb_cameras;
    // The selected printer streams its own camera (Bambu Lab in LAN mode): its configuration.
    std::unique_ptr<DynamicPrintConfig> m_printer_camera;
    // The search of the USB cameras in flight (older answers are ignored).
    int                  m_cameras_request{ 0 };
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
