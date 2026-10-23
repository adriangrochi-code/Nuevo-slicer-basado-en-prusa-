///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_UsbCamera_hpp_
#define slic3r_UsbCamera_hpp_

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {

// The USB cameras of this computer (UVC webcams), through openpnp-capture (bundled_deps): a camera pointed at a printer
// that has none (most Marlin printers) or a slow one (the A1 and P1 series of Bambu Lab).
struct UsbCameraInfo
{
    // The same while the camera stays in the same USB port (its name and the path of the system): saved per printer.
    std::string id;
    std::string name;
};

struct UsbCameraFormat
{
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint32_t fps{ 0 };
    // FourCC of the camera (MJPG, YUY2...), as the system gives it.
    uint32_t fourcc{ 0 };
};

struct UsbCameraFrame
{
    int width{ 0 };
    int height{ 0 };
    // 24-bit RGB, from the top row.
    std::vector<unsigned char> rgb;
};

class UsbCamera
{
public:
    // The cameras connected now (the list changes when one is plugged in or out).
    static std::vector<UsbCameraInfo> list();

    // The format to open: among the formats of at least min_fps frames per second that fit in max_width x max_height,
    // the largest; then the fastest one that fits; then the smallest. MJPEG first among equal ones (less USB bandwidth:
    // several cameras on one port). -1 when there is none.
    static int choose_format(const std::vector<UsbCameraFormat> &formats, uint32_t max_width, uint32_t max_height,
                             uint32_t min_fps);

    // Streams the camera until stop is set or on_frame returns false, at most max_fps frames a second (the rest are not
    // copied). Blocks: run it in a thread. false and error set when the camera is not there, cannot be opened, sends
    // nothing for 10 s or is unplugged. format: the format opened (to show it).
    static bool stream(const std::string &id, double max_fps, const std::function<bool(UsbCameraFrame &&)> &on_frame,
                       const std::atomic<bool> &stop, UsbCameraFormat &format, std::string &error);

    // "MJPG" from a FourCC.
    static std::string fourcc_name(uint32_t fourcc);
};

// Frames a second over the last frames (what the camera really gives, shown with the image).
class FrameRate
{
public:
    explicit FrameRate(size_t window = 30) : m_window(window) {}
    // A frame at time t (seconds); returns the rate, 0 until there are two frames.
    double add(double t);

private:
    size_t              m_window;
    std::vector<double> m_times;
};

} // namespace Slic3r

#endif // slic3r_UsbCamera_hpp_
