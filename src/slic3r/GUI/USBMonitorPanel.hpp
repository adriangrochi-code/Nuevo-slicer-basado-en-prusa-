///|/ Tisma Slicer: the USB printer in the Device page, as the Monitor of Cura with more of OctoPrint: the printer is
///|/ detected by itself (port and baud rate), temperatures with pre-heat, jog of the axes, extruder and fan, G-code
///|/ commands, the active print (progress, time, pause, resume, cancel), a temperature graph and a terminal.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_USBMonitorPanel_hpp_
#define slic3r_USBMonitorPanel_hpp_

#include <memory>
#include <string>

#include <wx/panel.h>

namespace Slic3r {
namespace GUI {

class USBMonitorPanel : public wxPanel
{
public:
    explicit USBMonitorPanel(wxWindow *parent);
    ~USBMonitorPanel() override;

    // The panel of the Device page (a single one), nullptr before it is created.
    static USBMonitorPanel* instance();
    // Shows the Device page with the USB printer.
    static void show();
    // Prints a G-code file (the sliced one) on the USB printer: shows the Device page and starts the print as soon as
    // the printer is connected (it is detected if needed). The file is read right away (it may be a temporary file).
    static void print_file(const std::string &path, const std::string &name);
    // A USB printer is connected, or was connected once (its port is saved).
    static bool has_printer();

    void sys_color_changed();

private:
    struct priv;
    std::unique_ptr<priv> p;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_USBMonitorPanel_hpp_
