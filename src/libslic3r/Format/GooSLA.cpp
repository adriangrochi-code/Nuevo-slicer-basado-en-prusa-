///|/ Tisma Slicer: Elegoo GOO archive (Mars and Saturn resin printers).
///|/ Written from Elegoo's published specification (https://github.com/elegooofficial/GOO, "Goo Format Spec V1.2").
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GooSLA.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>

#include <boost/algorithm/string/replace.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/SLAPrint.hpp"
#include "LocalesUtils.hpp"
#include "libslic3r_version.h"

namespace Slic3r {
namespace Goo {

// --- Layer image: run length encoding -------------------------------------------------------------------------

namespace {

// Chunk of `length` pixels of one value. Byte 0: [7:6] type (00 = 0x00, 01 = gray value in the next byte,
// 11 = 0xff), [5:4] number of extra length bytes, [3:0] the lowest 4 bits of the length; the extra bytes hold the
// higher bits, most significant first.
void put_run(std::vector<std::uint8_t> &out, std::uint8_t value, std::uint32_t length)
{
    constexpr std::uint32_t max_run = 0x0FFFFFFF;
    while (length > 0) {
        const std::uint32_t n = std::min(length, max_run);
        length -= n;
        const int extra = n <= 0xF ? 0 : n <= 0xFFF ? 1 : n <= 0xFFFFF ? 2 : 3;
        std::uint8_t b0 = std::uint8_t((extra << 4) | (n & 0xF));
        if (value == 0xFF)
            b0 |= 0xC0;
        else if (value != 0)
            b0 |= 0x40;
        out.push_back(b0);
        if (value != 0 && value != 0xFF)
            out.push_back(value);
        if (extra >= 3) out.push_back(std::uint8_t(n >> 20));
        if (extra >= 2) out.push_back(std::uint8_t(n >> 12));
        if (extra >= 1) out.push_back(std::uint8_t(n >> 4));
    }
}

} // namespace

std::vector<std::uint8_t> encode_layer(const std::uint8_t *pixels, std::size_t count)
{
    std::vector<std::uint8_t> out;
    out.reserve(count / 32 + 16);
    out.push_back(LAYER_MAGIC);
    std::size_t i = 0;
    while (i < count) {
        const std::uint8_t value = pixels[i];
        std::size_t j = i + 1;
        while (j < count && pixels[j] == value)
            ++ j;
        put_run(out, value, std::uint32_t(j - i));
        i = j;
    }
    std::uint8_t sum = 0;
    for (std::size_t k = 1; k < out.size(); ++ k)
        sum = std::uint8_t(sum + out[k]);
    out.push_back(std::uint8_t(~sum));
    return out;
}

bool decode_layer(const std::uint8_t *data, std::size_t size, std::size_t count, std::vector<std::uint8_t> &pixels)
{
    pixels.clear();
    pixels.reserve(count);
    if (size < 2 || data[0] != LAYER_MAGIC)
        return false;
    std::uint8_t sum = 0;
    for (std::size_t k = 1; k + 1 < size; ++ k)
        sum = std::uint8_t(sum + data[k]);
    if (std::uint8_t(~sum) != data[size - 1])
        return false;

    const std::size_t end = size - 1;
    std::uint8_t previous = 0;
    for (std::size_t i = 1; i < end; ) {
        const std::uint8_t b0   = data[i ++];
        const int          type = b0 >> 6;
        std::uint8_t value      = 0;
        std::uint32_t length    = 0;
        if (type == 2) {
            // Difference from the previous value, run of 1 or of the next byte.
            const int  diff     = b0 & 0xF;
            const bool negative = (b0 >> 5) & 1;
            const bool has_run  = (b0 >> 4) & 1;
            value  = std::uint8_t(negative ? previous - diff : previous + diff);
            length = 1;
            if (has_run) {
                if (i >= end)
                    return false;
                length = data[i ++];
            }
        } else {
            if (type == 1) {
                if (i >= end)
                    return false;
                value = data[i ++];
            } else
                value = type == 3 ? 0xFF : 0x00;
            const int extra = (b0 >> 4) & 3;
            if (i + extra > end)
                return false;
            length = 0;
            for (int k = 0; k < extra; ++ k)
                length = (length << 8) | data[i ++];
            length = (length << 4) | (b0 & 0xF);
        }
        if (pixels.size() + length > count)
            return false;
        pixels.insert(pixels.end(), length, value);
        previous = value;
    }
    return pixels.size() == count;
}

// --- Parameters from the material notes ---------------------------------------------------------------------

MotionParams motion_params(const DynamicPrintConfig &cfg)
{
    MotionParams p;
    // The notes are KEY=value lines (the same keys as the Anycubic archives, in their units: mm and mm/s).
    std::string notes = cfg.has("material_notes") ? cfg.opt_string("material_notes") : std::string();
    boost::replace_all(notes, "\\n", "\n");
    std::map<std::string, double> values;
    {
        CNumericLocalesSetter locales;
        std::size_t start = 0;
        while (start <= notes.size()) {
            std::size_t stop = notes.find('\n', start);
            if (stop == std::string::npos)
                stop = notes.size();
            const std::string line = notes.substr(start, stop - start);
            if (const std::size_t eq = line.find('='); eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
                try {
                    values[key] = std::stod(line.substr(eq + 1));
                } catch (...) {}
            }
            start = stop + 1;
        }
    }
    auto get = [&values](const char *key, float &target, double scale, double lo, double hi) {
        if (auto it = values.find(key); it != values.end())
            target = float(std::clamp(it->second * scale, lo, hi));
    };
    get("LIFT_DISTANCE",          p.lift_distance,        1.,  0., 100.);
    get("LIFT_SPEED",             p.lift_speed,           60., 1., 1200.);
    get("RETRACT_SPEED",          p.retract_speed,        60., 1., 1200.);
    get("BOTTOM_LIFT_DISTANCE",   p.bottom_lift_distance, 1.,  0., 100.);
    get("BOTTOM_LIFT_SPEED",      p.bottom_lift_speed,    60., 1., 1200.);
    get("DELAY_BEFORE_EXPOSURE",  p.wait_before_cure,     1.,  0., 1000.);
    get("WAIT_AFTER_CURE",        p.wait_after_cure,      1.,  0., 1000.);
    get("WAIT_AFTER_LIFT",        p.wait_after_lift,      1.,  0., 1000.);
    if (values.find("BOTTOM_LIFT_DISTANCE") == values.end())
        p.bottom_lift_distance = p.lift_distance;
    if (values.find("BOTTOM_LIFT_SPEED") == values.end())
        p.bottom_lift_speed = p.lift_speed;
    float aa = float(p.antialiasing), pwm = float(p.light_pwm), bpwm = float(p.bottom_light_pwm);
    get("ANTIALIASING",           aa,   1., 1., 16.);
    get("LIGHT_PWM",              pwm,  1., 0., 255.);
    get("BOTTOM_LIGHT_PWM",       bpwm, 1., 0., 255.);
    p.antialiasing     = int(aa);
    p.light_pwm        = int(pwm);
    p.bottom_light_pwm = int(bpwm);
    return p;
}

} // namespace Goo

// --- Writer -----------------------------------------------------------------------------------------------------

namespace {

class BigEndianWriter
{
public:
    std::vector<std::uint8_t> data;
    void u8(std::uint8_t v)   { data.push_back(v); }
    void u16(std::uint16_t v) { data.push_back(std::uint8_t(v >> 8)); data.push_back(std::uint8_t(v)); }
    void u32(std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) data.push_back(std::uint8_t(v >> s)); }
    void f32(float v)         { std::uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void boolean(bool v)      { u8(v ? 1 : 0); }
    void bytes(const std::uint8_t *p, std::size_t n) { data.insert(data.end(), p, p + n); }
    // Fixed length string, zero padded (and cut).
    // A fixed size field, always with a terminating zero (longer text is cut).
    void str(const std::string &s, std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++ i)
            data.push_back(i + 1 < len && i < s.size() ? std::uint8_t(s[i]) : 0);
    }
    void delimiter() { u8(0x0D); u8(0x0A); }
};

// RGB565 preview of the given size from the first thumbnail (nearest pixel), rows from the top.
void write_preview(BigEndianWriter &w, const ThumbnailsList &thumbnails, unsigned size)
{
    const ThumbnailData *t = nullptr;
    for (const ThumbnailData &th : thumbnails)
        if (th.is_valid() && th.pixels.size() >= std::size_t(th.width) * th.height * 4) {
            t = &th;
            break;
        }
    for (unsigned y = 0; y < size; ++ y)
        for (unsigned x = 0; x < size; ++ x) {
            std::uint16_t pixel = 0;
            if (t != nullptr) {
                const unsigned sx = std::min(t->width - 1, x * t->width / size);
                // The thumbnails are stored from the bottom row up.
                const unsigned sy = t->height - 1 - std::min(t->height - 1, y * t->height / size);
                const unsigned char *px = &t->pixels[(std::size_t(sy) * t->width + sx) * 4];
                pixel = std::uint16_t(((px[0] >> 3) << 11) | ((px[1] >> 2) << 5) | (px[2] >> 3));
            }
            w.u16(pixel);
        }
}

float cfg_float(const DynamicPrintConfig &cfg, const char *key, float def = 0.f)
{
    const ConfigOption *opt = cfg.option(key);
    return opt ? float(opt->getFloat()) : def;
}

int cfg_int(const DynamicPrintConfig &cfg, const char *key, int def = 0)
{
    const ConfigOption *opt = cfg.option(key);
    return opt ? opt->getInt() : def;
}

std::string cfg_string(const DynamicPrintConfig &cfg, const char *key)
{
    const ConfigOption *opt = cfg.option(key);
    return opt ? opt->serialize() : std::string();
}

} // namespace

std::unique_ptr<sla::RasterBase> GooSLAArchive::create_raster() const
{
    double w  = m_cfg.display_width.getFloat();
    double h  = m_cfg.display_height.getFloat();
    auto   pw = size_t(m_cfg.display_pixels_x.getInt());
    auto   ph = size_t(m_cfg.display_pixels_y.getInt());

    std::array<bool, 2> mirror;
    mirror[X] = m_cfg.display_mirror_x.getBool();
    mirror[Y] = m_cfg.display_mirror_y.getBool();

    const auto ro = m_cfg.display_orientation.getInt();
    const sla::RasterBase::Orientation orientation =
        ro == sla::RasterBase::roPortrait ? sla::RasterBase::roPortrait : sla::RasterBase::roLandscape;
    if (orientation == sla::RasterBase::roPortrait) {
        std::swap(w, h);
        std::swap(pw, ph);
    }
    const sla::RasterBase::Trafo tr{ orientation, mirror };
    return sla::create_raster_grayscale_aa(sla::Resolution{ pw, ph }, sla::PixelDim{ w / pw, h / ph },
                                           m_cfg.gamma_correction.getFloat(), tr);
}

sla::RasterEncoder GooSLAArchive::get_encoder() const
{
    return [](const void *ptr, size_t w, size_t h, size_t num_components) {
        const std::uint8_t *src = static_cast<const std::uint8_t*>(ptr);
        std::vector<std::uint8_t> gray;
        if (num_components != 1) {
            // Keep the first component.
            gray.resize(w * h);
            for (size_t i = 0; i < w * h; ++ i)
                gray[i] = src[i * num_components];
            src = gray.data();
        }
        return sla::EncodedRaster(Goo::encode_layer(src, w * h), "goo");
    };
}

void GooSLAArchive::export_print(const std::string     fname,
                                 const SLAPrint       &print,
                                 const ThumbnailsList &thumbnails,
                                 const std::string    &projectname)
{
    CNumericLocalesSetter locales_setter;
    const DynamicPrintConfig &cfg = print.full_print_config();
    const Goo::MotionParams   mp  = Goo::motion_params(cfg);
    const std::uint32_t layer_count = std::uint32_t(m_layers.size());

    const float layer_height     = cfg_float(cfg, "layer_height", 0.05f);
    const float exposure         = cfg_float(cfg, "exposure_time", 2.f);
    const float bottom_exposure  = cfg_float(cfg, "initial_exposure_time", 30.f);
    // The faded layers as the slicing used them (the material preset can override the print preset).
    const int faded_layers = print.objects().empty() ? cfg_int(cfg, "faded_layers", 5) : print.objects().front()->config().faded_layers.getInt();
    const std::uint32_t bottom_layers = std::uint32_t(std::clamp(faded_layers, 1, int(std::max<std::uint32_t>(1, layer_count))));

    // Raster size as written (after the orientation swap of create_raster()).
    std::uint32_t res_x = std::uint32_t(m_cfg.display_pixels_x.getInt());
    std::uint32_t res_y = std::uint32_t(m_cfg.display_pixels_y.getInt());
    float size_x = float(m_cfg.display_width.getFloat());
    float size_y = float(m_cfg.display_height.getFloat());
    if (m_cfg.display_orientation.getInt() == sla::RasterBase::roPortrait) {
        std::swap(res_x, res_y);
        std::swap(size_x, size_y);
    }

    const SLAPrintStatistics &stats = print.print_statistics();
    const float volume_mm3 = float(stats.objects_used_material + stats.support_used_material);
    const float bottle_volume_ml = cfg_float(cfg, "bottle_volume", 1000.f);
    const float bottle_weight_kg = cfg_float(cfg, "bottle_weight", 1.f);
    const float bottle_cost      = cfg_float(cfg, "bottle_cost", 0.f);
    const float weight_g = bottle_volume_ml > 0.f ? volume_mm3 / 1000.f * bottle_weight_kg * 1000.f / bottle_volume_ml : 0.f;
    const float cost     = bottle_volume_ml > 0.f ? volume_mm3 / 1000.f * bottle_cost / bottle_volume_ml : 0.f;

    char time_str[32] = {};
    {
        const std::time_t now = std::time(nullptr);
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    }

    BigEndianWriter w;
    w.bytes(reinterpret_cast<const std::uint8_t*>("V3.0"), 4);
    static const std::uint8_t magic[8] = { 0x07, 0x00, 0x00, 0x00, 0x44, 0x4C, 0x50, 0x00 };
    w.bytes(magic, 8);
    w.str(SLIC3R_APP_NAME, 32);
    w.str(SLIC3R_VERSION, 24);
    w.str(time_str, 24);
    w.str(cfg_string(cfg, "printer_settings_id").empty() ? cfg_string(cfg, "printer_model") : cfg_string(cfg, "printer_settings_id"), 32);
    w.str("MSLA", 32);
    w.str(cfg_string(cfg, "sla_material_settings_id"), 32);
    w.u16(std::uint16_t(mp.antialiasing));
    w.u16(1);   // grey level
    w.u16(0);   // blur level
    write_preview(w, thumbnails, Goo::SMALL_PREVIEW);
    w.delimiter();
    write_preview(w, thumbnails, Goo::BIG_PREVIEW);
    w.delimiter();
    w.u32(layer_count);
    w.u16(std::uint16_t(res_x));
    w.u16(std::uint16_t(res_y));
    w.boolean(m_cfg.display_mirror_x.getBool());
    w.boolean(m_cfg.display_mirror_y.getBool());
    w.f32(size_x);
    w.f32(size_y);
    w.f32(cfg_float(cfg, "max_print_height", 150.f));
    w.f32(layer_height);
    w.f32(exposure);
    w.boolean(true);                 // exposure delay mode: the wait times below
    w.f32(0.f);                      // turn off time
    w.f32(mp.wait_after_cure);       // bottom: before the lift
    w.f32(mp.wait_after_lift);       // bottom: after the lift
    w.f32(mp.wait_before_cure);      // bottom: after the retraction
    w.f32(mp.wait_after_cure);
    w.f32(mp.wait_after_lift);
    w.f32(mp.wait_before_cure);
    w.f32(bottom_exposure);
    w.u32(bottom_layers);
    w.f32(mp.bottom_lift_distance);
    w.f32(mp.bottom_lift_speed);
    w.f32(mp.lift_distance);
    w.f32(mp.lift_speed);
    w.f32(mp.bottom_lift_distance);  // bottom retraction distance
    w.f32(mp.retract_speed);
    w.f32(mp.lift_distance);         // retraction distance
    w.f32(mp.retract_speed);
    for (int i = 0; i < 8; ++ i)     // second stage of the lift and the retraction: not used
        w.f32(0.f);
    w.u16(std::uint16_t(mp.bottom_light_pwm));
    w.u16(std::uint16_t(mp.light_pwm));
    w.boolean(false);                // advance mode: the header settings for every layer
    w.u32(std::uint32_t(std::max(0., stats.estimated_print_time)));
    w.f32(volume_mm3);
    w.f32(weight_g);
    w.f32(cost);
    w.str("$", 8);
    w.u32(Goo::LAYERS_OFFSET);
    w.boolean(true);                 // gray levels 0x00..0xff
    w.u16(0);                        // transition layers
    if (w.data.size() != Goo::LAYERS_OFFSET)
        throw std::runtime_error("GOO header size mismatch");

    const std::vector<SLAPrint::PrintLayer> &levels = print.print_layers();
    const float machine_z = cfg_float(cfg, "max_print_height", 150.f);
    for (std::uint32_t i = 0; i < layer_count; ++ i) {
        const bool  bottom = i < bottom_layers;
        const float z = i < levels.size() ? float(unscaled(levels[i].level())) : layer_height * float(i + 1);
        w.u16(0);                    // pause flag
        w.f32(machine_z);            // pause position
        w.f32(z);
        w.f32(bottom ? bottom_exposure : exposure);
        w.f32(0.f);                  // off time
        w.f32(mp.wait_after_cure);
        w.f32(mp.wait_after_lift);
        w.f32(mp.wait_before_cure);
        w.f32(bottom ? mp.bottom_lift_distance : mp.lift_distance);
        w.f32(bottom ? mp.bottom_lift_speed : mp.lift_speed);
        w.f32(0.f);                  // second lift distance
        w.f32(0.f);                  // second lift speed
        w.f32(bottom ? mp.bottom_lift_distance : mp.lift_distance);
        w.f32(mp.retract_speed);
        w.f32(0.f);                  // second retraction distance
        w.f32(0.f);                  // second retraction speed
        w.u16(std::uint16_t(bottom ? mp.bottom_light_pwm : mp.light_pwm));
        w.delimiter();
        const sla::EncodedRaster &rst = m_layers[i];
        w.u32(std::uint32_t(rst.size()));
        w.bytes(static_cast<const std::uint8_t*>(rst.data()), rst.size());
        w.delimiter();
    }
    static const std::uint8_t ending[11] = { 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x44, 0x4C, 0x50, 0x00 };
    w.bytes(ending, 11);

    std::ofstream out(fname, std::ios::binary | std::ios::out | std::ios::trunc);
    if (! out)
        throw std::runtime_error("Cannot open " + fname);
    out.write(reinterpret_cast<const char*>(w.data.data()), std::streamsize(w.data.size()));
    if (! out)
        throw std::runtime_error("Cannot write " + fname);
}

} // namespace Slic3r
