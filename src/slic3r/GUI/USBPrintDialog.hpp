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

    static void run(wxWindow *parent);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    struct priv;
    std::unique_ptr<priv> p;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_USBPrintDialog_hpp_
