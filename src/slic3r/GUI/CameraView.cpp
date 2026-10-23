///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "CameraView.hpp"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/log.h>
#include <wx/mstream.h>

#include "GUI_App.hpp"

namespace Slic3r {
namespace GUI {

CameraView::CameraView(wxWindow *parent) : wxPanel(parent, wxID_ANY)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, 16 * wxGetApp().em_unit()));
    Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) { Refresh(); evt.Skip(); });
}

bool CameraView::set_jpeg(const std::string &jpeg)
{
    wxLogNull no_log;
    wxMemoryInputStream in(jpeg.data(), jpeg.size());
    wxImage image;
    if (!image.LoadFile(in, wxBITMAP_TYPE_JPEG) || !image.IsOk())
        return false;
    m_image = image;
    m_scaled = wxBitmap();
    Refresh();
    return true;
}

void CameraView::set_rgb(int width, int height, const std::vector<unsigned char> &rgb)
{
    if (width <= 0 || height <= 0 || rgb.size() < size_t(width) * height * 3)
        return;
    wxImage image(width, height, false);
    std::copy(rgb.begin(), rgb.begin() + size_t(width) * height * 3, image.GetData());
    m_image = image;
    m_scaled = wxBitmap();
    Refresh();
}

void CameraView::set_message(const wxString &message)
{
    m_image = wxImage();
    m_scaled = wxBitmap();
    m_message = message;
    Refresh();
}

void CameraView::paint()
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    dc.SetBackground(wxBrush(wxColour(32, 33, 36)));
    dc.Clear();
    if (m_image.IsOk() && size.x > 0 && size.y > 0) {
        const double scale = std::min(double(size.x) / m_image.GetWidth(), double(size.y) / m_image.GetHeight());
        const int w = std::max(1, int(m_image.GetWidth() * scale));
        const int h = std::max(1, int(m_image.GetHeight() * scale));
        if (!m_scaled.IsOk() || m_scaled.GetWidth() != w || m_scaled.GetHeight() != h)
            m_scaled = wxBitmap(m_image.Scale(w, h, wxIMAGE_QUALITY_BILINEAR));
        dc.DrawBitmap(m_scaled, (size.x - w) / 2, (size.y - h) / 2);
        return;
    }
    const int em = wxGetApp().em_unit();
    dc.SetTextForeground(wxColour(200, 200, 205));
    dc.SetFont(GetFont());
    // Wrapped by words to the width of the panel.
    wxArrayString lines;
    wxString line;
    for (const wxString &word : wxSplit(m_message, ' ')) {
        const wxString longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && dc.GetTextExtent(longer).x > size.x - 2 * em) {
            lines.Add(line);
            line = word;
        } else
            line = longer;
    }
    if (!line.empty())
        lines.Add(line);
    const int line_h = dc.GetCharHeight() * 5 / 4;
    int y = (size.y - line_h * int(lines.size())) / 2;
    for (const wxString &l : lines) {
        dc.DrawText(l, (size.x - dc.GetTextExtent(l).x) / 2, y);
        y += line_h;
    }
}

} // namespace GUI
} // namespace Slic3r
