///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CameraView_hpp_
#define slic3r_CameraView_hpp_

#include <string>
#include <vector>

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/panel.h>

namespace Slic3r {
namespace GUI {

// The last image of the camera of the printer (Bambu Lab A1, P1) or of a USB camera, scaled to the panel keeping its
// proportions, or a message while there is none. In the Devices page and in the USB printer panel.
class CameraView : public wxPanel
{
public:
    explicit CameraView(wxWindow *parent);

    // A JPEG of the printer; false when it cannot be read.
    bool set_jpeg(const std::string &jpeg);
    // A frame of a USB camera (24-bit RGB).
    void set_rgb(int width, int height, const std::vector<unsigned char> &rgb);
    void set_message(const wxString &message);

private:
    wxImage  m_image;
    wxBitmap m_scaled;
    wxString m_message;

    void paint();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_CameraView_hpp_
