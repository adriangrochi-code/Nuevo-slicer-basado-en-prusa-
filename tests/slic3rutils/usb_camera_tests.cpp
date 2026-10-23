#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "slic3r/Utils/UsbCamera.hpp"

using namespace Slic3r;

namespace {
constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}
const uint32_t MJPG = fourcc('M', 'J', 'P', 'G');
const uint32_t YUYV = fourcc('Y', 'U', 'Y', 'V');
// macOS gives the four char code 'jpeg' as a number written big endian.
const uint32_t MAC_JPEG = fourcc('g', 'e', 'p', 'j');
} // namespace

TEST_CASE("USB camera: the format opened", "[UsbCamera]")
{
    // A usual webcam: uncompressed formats are slow at large sizes, MJPEG is fast at all of them.
    const std::vector<UsbCameraFormat> webcam = {
        { 1920, 1080, 5, YUYV }, { 1280, 720, 10, YUYV }, { 640, 480, 30, YUYV },
        { 1920, 1080, 30, MJPG }, { 1280, 720, 30, MJPG }, { 640, 480, 30, MJPG },
    };
    SECTION("the largest that fits and is fast enough, MJPEG among equal ones") {
        CHECK(UsbCamera::choose_format(webcam, 1280, 720, 10) == 4);
        CHECK(UsbCamera::choose_format(webcam, 640, 480, 10) == 5);
    }
    SECTION("macOS calls MJPEG 'jpeg'") {
        const std::vector<UsbCameraFormat> mac = { { 1280, 720, 30, fourcc('2', 'v', 'u', 'y') }, { 1280, 720, 30, MAC_JPEG } };
        CHECK(UsbCamera::choose_format(mac, 1280, 720, 10) == 1);
    }
    SECTION("none fast enough: the fastest that fits") {
        const std::vector<UsbCameraFormat> slow = { { 1280, 720, 5, YUYV }, { 640, 480, 8, YUYV } };
        CHECK(UsbCamera::choose_format(slow, 1280, 720, 10) == 1);
    }
    SECTION("none fits: the smallest") {
        const std::vector<UsbCameraFormat> big = { { 3840, 2160, 30, MJPG }, { 1920, 1080, 30, MJPG } };
        CHECK(UsbCamera::choose_format(big, 1280, 720, 10) == 1);
    }
    SECTION("no formats") {
        CHECK(UsbCamera::choose_format({}, 1280, 720, 10) == -1);
        CHECK(UsbCamera::choose_format({ { 0, 0, 0, 0 } }, 1280, 720, 10) == -1);
    }
    CHECK(UsbCamera::fourcc_name(MJPG) == "MJPG");
    CHECK(UsbCamera::fourcc_name(0x01020304) == "????");
}

TEST_CASE("USB camera: images per second over the last frames", "[UsbCamera]")
{
    FrameRate rate(5);
    CHECK(rate.add(10.) == 0.);
    for (int i = 1; i < 20; ++ i)
        rate.add(10. + i / 15.);
    CHECK(rate.add(10. + 20 / 15.) == Catch::Approx(15.));
    // A pause of the camera shows at once.
    CHECK(rate.add(12.4) < 5.);
}

TEST_CASE("USB camera: listing works without cameras", "[UsbCamera]")
{
    // On the build machines there are none: no crash, and every camera listed has an id and a name.
    for (const UsbCameraInfo &camera : UsbCamera::list()) {
        CHECK(! camera.id.empty());
        CHECK(! camera.name.empty());
    }
    UsbCameraFormat format;
    std::string error;
    std::atomic<bool> stop{ false };
    CHECK(! UsbCamera::stream("no such camera", 15., [](UsbCameraFrame &&) { return true; }, stop, format, error));
    CHECK(error == "not connected");
}
