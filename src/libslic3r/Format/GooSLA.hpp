///|/ Tisma Slicer: Elegoo GOO archive (Mars and Saturn resin printers).
///|/ Written from Elegoo's published specification (https://github.com/elegooofficial/GOO, "Goo Format Spec V1.2").
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Format_GooSLA_hpp_
#define slic3r_Format_GooSLA_hpp_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveFormatRegistry.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

namespace Goo {

// Size of the header: the layers start at this offset.
constexpr std::uint32_t LAYERS_OFFSET      = 195477;
constexpr unsigned      SMALL_PREVIEW    = 116;
constexpr unsigned      BIG_PREVIEW      = 290;
constexpr std::uint8_t  LAYER_MAGIC      = 0x55;

// RLE of an 8-bit grayscale image (row by row): the 0x55 magic, the chunks and the checksum (the bitwise NOT of
// the 8-bit sum of the chunk bytes).
std::vector<std::uint8_t> encode_layer(const std::uint8_t *pixels, std::size_t count);
// Decodes an encoded layer (as written by encode_layer or by other slicers, including the difference chunks).
// Returns false when the data is corrupt (wrong magic, wrong checksum, too many or too few pixels).
bool decode_layer(const std::uint8_t *data, std::size_t size, std::size_t count, std::vector<std::uint8_t> &pixels);

// Print parameters of the header and of each layer that are not in the PrusaSlicer configuration: taken from the
// material notes (KEY=value lines), with the defaults below.
struct MotionParams {
    float lift_distance          { 5.f };   // mm
    float lift_speed             { 65.f };  // mm/min
    float retract_speed          { 150.f }; // mm/min
    float bottom_lift_distance   { 5.f };
    float bottom_lift_speed      { 65.f };
    float wait_before_cure       { 1.f };   // s, after the retraction
    float wait_after_cure        { 0.f };   // s, before the lift
    float wait_after_lift        { 0.f };   // s
    int   antialiasing           { 8 };
    int   light_pwm              { 255 };
    int   bottom_light_pwm       { 255 };
};
MotionParams motion_params(const DynamicPrintConfig &material_notes_cfg);

} // namespace Goo

class GooSLAArchive : public SLAArchiveWriter
{
    SLAPrinterConfig m_cfg;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

public:
    GooSLAArchive() = default;
    explicit GooSLAArchive(const SLAPrinterConfig &cfg) : m_cfg(cfg) {}

    void export_print(const std::string     fname,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "") override;
};

inline ArchiveEntry goo_sla_format()
{
    ArchiveEntry entry("GOO");
    entry.desc        = "Elegoo GOO";
    entry.ext         = "goo";
    entry.wrfactoryfn = [](const auto &cfg) { return std::make_unique<GooSLAArchive>(cfg); };
    return entry;
}

} // namespace Slic3r

#endif // slic3r_Format_GooSLA_hpp_
