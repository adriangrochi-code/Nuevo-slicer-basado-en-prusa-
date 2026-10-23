///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "UsbCamera.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <tuple>

#include <boost/log/trivial.hpp>

#include <openpnp-capture.h>

namespace Slic3r {

namespace {

// The messages of openpnp-capture in the log of Tisma (it prints them otherwise), only the errors and warnings.
void capture_log(uint32_t level, const char *text)
{
    std::string line = text ? text : "";
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    if (level <= 3)
        BOOST_LOG_TRIVIAL(error) << "USB camera: " << line;
    else
        BOOST_LOG_TRIVIAL(warning) << "USB camera: " << line;
}

void setup_log()
{
    static const bool done = [] {
        Cap_installCustomLogFunction(capture_log);
        Cap_setLogLevel(4);
        return true;
    }();
    (void)done;
}

// MJPG (Windows DirectShow, Linux V4L2, little endian) or 'jpeg' (macOS, a four char code read big endian).
bool is_mjpeg(uint32_t fourcc)
{
    std::string name = UsbCamera::fourcc_name(fourcc);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return name == "mjpg" || name == "gepj" || name == "jpeg";
}

// Finds a camera by its id in a context. -1 when it is not plugged in.
int find_device(CapContext ctx, const std::string &id)
{
    const uint32_t count = Cap_getDeviceCount(ctx);
    for (uint32_t i = 0; i < count; ++ i)
        if (const char *unique = Cap_getDeviceUniqueID(ctx, i); unique != nullptr && id == unique)
            return int(i);
    return -1;
}

double now_s()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

std::string UsbCamera::fourcc_name(uint32_t fourcc)
{
    std::string name;
    for (int i = 0; i < 4; ++ i) {
        const char c = char((fourcc >> (8 * i)) & 0xff);
        name += std::isprint(static_cast<unsigned char>(c)) ? c : '?';
    }
    return name;
}

std::vector<UsbCameraInfo> UsbCamera::list()
{
    setup_log();
    std::vector<UsbCameraInfo> cameras;
    CapContext ctx = Cap_createContext();
    if (ctx == nullptr)
        return cameras;
    const uint32_t count = Cap_getDeviceCount(ctx);
    for (uint32_t i = 0; i < count; ++ i) {
        const char *id   = Cap_getDeviceUniqueID(ctx, i);
        const char *name = Cap_getDeviceName(ctx, i);
        // A device without formats is not a camera (Linux lists the metadata nodes of a webcam too).
        if (id != nullptr && Cap_getNumFormats(ctx, i) > 0)
            cameras.push_back({ id, name != nullptr ? name : id });
    }
    Cap_releaseContext(ctx);
    return cameras;
}

int UsbCamera::choose_format(const std::vector<UsbCameraFormat> &formats, uint32_t max_width, uint32_t max_height,
                             uint32_t min_fps)
{
    int best = -1;
    // The larger tuple wins: (tier, the size or the speed of the tier, then the other, MJPEG).
    std::tuple<int, uint64_t, uint64_t, int> best_key;
    for (size_t i = 0; i < formats.size(); ++ i) {
        const UsbCameraFormat &f = formats[i];
        if (f.width == 0 || f.height == 0)
            continue;
        const uint64_t area  = uint64_t(f.width) * f.height;
        const bool     fits  = f.width <= max_width && f.height <= max_height;
        const int      mjpeg = is_mjpeg(f.fourcc) ? 1 : 0;
        std::tuple<int, uint64_t, uint64_t, int> key;
        if (fits && f.fps >= min_fps)
            key = { 2, area, f.fps, mjpeg };
        else if (fits)
            key = { 1, f.fps, area, mjpeg };
        else
            key = { 0, ~area, f.fps, mjpeg };
        if (best < 0 || key > best_key) {
            best     = int(i);
            best_key = key;
        }
    }
    return best;
}

bool UsbCamera::stream(const std::string &id, double max_fps, const std::function<bool(UsbCameraFrame &&)> &on_frame,
                       const std::atomic<bool> &stop, UsbCameraFormat &format, std::string &error)
{
    setup_log();
    CapContext ctx = Cap_createContext();
    if (ctx == nullptr) {
        error = "the cameras of the system cannot be used";
        return false;
    }
    bool ok = false;
    CapStream stream = -1;
    do {
        const int device = find_device(ctx, id);
        if (device < 0) {
            error = "not connected";
            break;
        }
        std::vector<UsbCameraFormat> formats;
        for (int32_t i = 0, n = Cap_getNumFormats(ctx, uint32_t(device)); i < n; ++ i) {
            CapFormatInfo info {};
            UsbCameraFormat f;
            if (Cap_getFormatInfo(ctx, uint32_t(device), uint32_t(i), &info) == CAPRESULT_OK)
                f = { info.width, info.height, info.fps, info.fourcc };
            formats.push_back(f);
        }
        // 1280 x 720 is enough to see a print (Obico looks at 416 x 416) and leaves USB bandwidth for a second camera.
        const int chosen = choose_format(formats, 1280, 720, 10);
        if (chosen < 0) {
            error = "the camera gives no image format";
            break;
        }
        format = formats[chosen];
        stream = Cap_openStream(ctx, uint32_t(device), uint32_t(chosen));
        if (stream < 0) {
            error = "the camera could not be opened (in use by another program?)";
            break;
        }
        BOOST_LOG_TRIVIAL(info) << "USB camera " << id << ": " << format.width << " x " << format.height << " "
                                << fourcc_name(format.fourcc) << " at " << format.fps << " fps";
        const double period    = max_fps > 0. ? 1. / max_fps : 0.;
        double       last_sent = -1e9;
        double       last_new  = now_s();
        ok = true;
        while (! stop) {
            if (! Cap_isOpenStream(ctx, stream)) {
                error = "the camera was disconnected";
                ok    = false;
                break;
            }
            const double t = now_s();
            if (Cap_hasNewFrame(ctx, stream)) {
                last_new = t;
                if (t - last_sent >= period) {
                    UsbCameraFrame frame;
                    frame.width  = int(format.width);
                    frame.height = int(format.height);
                    frame.rgb.resize(size_t(format.width) * format.height * 3);
                    if (Cap_captureFrame(ctx, stream, frame.rgb.data(), uint32_t(frame.rgb.size())) == CAPRESULT_OK) {
                        last_sent = t;
                        if (! on_frame(std::move(frame)))
                            break;
                    }
                    continue;
                }
            } else if (t - last_new > 10.) {
                error = "the camera sends no images";
                ok    = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } while (false);
    if (stream >= 0)
        Cap_closeStream(ctx, stream);
    Cap_releaseContext(ctx);
    return ok;
}

double FrameRate::add(double t)
{
    m_times.push_back(t);
    if (m_times.size() > m_window)
        m_times.erase(m_times.begin());
    if (m_times.size() < 2 || m_times.back() <= m_times.front())
        return 0.;
    return double(m_times.size() - 1) / (m_times.back() - m_times.front());
}

} // namespace Slic3r
