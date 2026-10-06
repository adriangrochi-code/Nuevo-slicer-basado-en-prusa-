#ifndef slic3r_USBPrintDialog_hpp_
#define slic3r_USBPrintDialog_hpp_

#include <memory>

#include <wx/dialog.h>
#include "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

// Print a G-code file over USB (serial port): connection, progress, temperatures,
// pause / resume / cancel and a console. Modeless, a single instance.
class USBPrintDialog : public DPIDialog
{
public:
    explicit USBPrintDialog(wxWindow *parent);
    ~USBPrintDialog() override;

    // Shows the window (a single instance). The connection stays open when the window is closed; it connects
    // automatically to the saved printer.
    static void run(wxWindow *parent);
    // Prints a G-code file (the sliced one): shows the window, connects to the saved printer if needed and starts
    // the print as soon as the printer is ready. The file is read right away (it may be a temporary file).
    static void print_file(wxWindow *parent, const std::string &path, const std::string &name);
    // A printer was connected at least once (its port is saved).
    static bool has_saved_printer();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct priv;
    std::unique_ptr<priv> p;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_USBPrintDialog_hpp_
