///|/ Tisma Slicer: results of the structural analysis in a file (".tfea").
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "tisma_fea/Serialize.hpp"

#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <type_traits>

#include <boost/nowide/fstream.hpp>

namespace Slic3r {
namespace Fea {

namespace {

const char MAGIC[4] = { 'T', 'F', 'E', 'A' };
// Largest count of a vector or length of a string accepted when reading (a damaged file must not allocate gigabytes).
constexpr uint64_t MAX_COUNT = uint64_t(1) << 28;

class Writer
{
public:
    explicit Writer(std::ostream &out) : m_out(out) {}

    template<class T> void pod(const T &v)
    {
        static_assert(std::is_trivially_copyable<T>::value, "plain data only");
        m_out.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }
    void u64(size_t v) { pod(uint64_t(v)); }
    void str(const std::string &s) { u64(s.size()); m_out.write(s.data(), std::streamsize(s.size())); }
    template<class T> void pods(const std::vector<T> &v)
    {
        u64(v.size());
        if (! v.empty())
            m_out.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size() * sizeof(T)));
    }
    void vec(const Vec3d &v) { pod(v.x()); pod(v.y()); pod(v.z()); }
    void mesh(const indexed_triangle_set &its)
    {
        u64(its.vertices.size());
        for (const stl_vertex &p : its.vertices) { pod(p.x()); pod(p.y()); pod(p.z()); }
        u64(its.indices.size());
        for (const stl_triangle_vertex_indices &t : its.indices) { pod(int32_t(t[0])); pod(int32_t(t[1])); pod(int32_t(t[2])); }
    }
    bool ok() const { return bool(m_out); }

private:
    std::ostream &m_out;
};

class Reader
{
public:
    explicit Reader(std::istream &in) : m_in(in) {}

    template<class T> void pod(T &v)
    {
        static_assert(std::is_trivially_copyable<T>::value, "plain data only");
        if (m_ok && ! m_in.read(reinterpret_cast<char*>(&v), sizeof(T)))
            m_ok = false;
    }
    size_t count()
    {
        uint64_t n = 0;
        pod(n);
        if (n > MAX_COUNT)
            m_ok = false;
        return m_ok ? size_t(n) : 0;
    }
    void str(std::string &s)
    {
        s.assign(count(), '\0');
        if (m_ok && ! s.empty() && ! m_in.read(&s[0], std::streamsize(s.size())))
            m_ok = false;
    }
    template<class T> void pods(std::vector<T> &v)
    {
        v.assign(count(), T());
        if (m_ok && ! v.empty() && ! m_in.read(reinterpret_cast<char*>(v.data()), std::streamsize(v.size() * sizeof(T))))
            m_ok = false;
    }
    void vec(Vec3d &v) { pod(v.x()); pod(v.y()); pod(v.z()); }
    void mesh(indexed_triangle_set &its)
    {
        its.vertices.assign(count(), stl_vertex::Zero());
        for (stl_vertex &p : its.vertices) { pod(p.x()); pod(p.y()); pod(p.z()); }
        its.indices.assign(count(), stl_triangle_vertex_indices(0, 0, 0));
        for (stl_triangle_vertex_indices &t : its.indices) {
            int32_t a = 0, b = 0, c = 0;
            pod(a); pod(b); pod(c);
            const int32_t nv = int32_t(its.vertices.size());
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv)
                m_ok = false;
            t = stl_triangle_vertex_indices(a, b, c);
        }
    }
    bool ok() const { return m_ok; }
    void fail() { m_ok = false; }

private:
    std::istream &m_in;
    bool          m_ok { true };
};

void io_alternatives(Writer &w, const std::vector<std::pair<std::string, double>> &v)
{
    w.u64(v.size());
    for (const auto &a : v) { w.str(a.first); w.pod(a.second); }
}
void io_alternatives(Reader &r, std::vector<std::pair<std::string, double>> &v)
{
    v.assign(r.count(), {});
    for (auto &a : v) { r.str(a.first); r.pod(a.second); }
}

// Every field is written and read by the same function (io is a Writer or a Reader), so that the two stay in step.
template<class IO, class R> void io_result(IO &io, R &r)
{
    io.pod(r.ok);
    io.str(r.error);
    io.vec(r.origin);
    io.pod(r.h);
    io.pod(r.size.x()); io.pod(r.size.y()); io.pod(r.size.z());
    io.pods(r.voxels);
    io.pods(r.von_mises);
    io.pods(r.failure_index);
    io.pods(r.displacement);
    io.pods(r.stress);
    io.pods(r.density);
    io.pods(r.interior);
    io.pod(r.mass);
    io.pod(r.solid_mass);
    io.pod(r.max_displacement);
    io.pod(r.max_von_mises);
    io.pod(r.max_failure_index);
    io.pod(r.safety_factor);
    io.vec(r.critical_point);
    io.pod(r.temperature_factor);
    io.pod(r.layer_adhesion_factor);
    io.pods(r.load_displacement);
    io.pods(r.limit_displacement);
    io.pod(r.verdict);
    io_alternatives(io, r.alternatives);
    io.pod(r.iterations);
    uint64_t levels = r.multigrid_levels, orientations = r.layer_orientations, aligned = r.aligned_voxels;
    io.pod(levels); io.pod(orientations); io.pod(aligned);
    r.multigrid_levels = size_t(levels); r.layer_orientations = size_t(orientations); r.aligned_voxels = size_t(aligned);
    io.str(r.gpu_device);
    io.pod(r.residual);
}

template<class IO, class Z> void io_zone(IO &io, Z &z)
{
    io.mesh(z.mesh);
    io.pod(z.density);
    io.pod(z.aligned_angle_deg);
    io.pod(z.wall_thickness);
    io.pod(z.stiffness_exponent);
    io.pod(z.strength_exponent);
    io.pod(z.deposit_factor);
    io.pod(z.volume_fraction);
    io.pod(z.homogenize_cell);
}

template<class IO, class Z> void io_rib(IO &io, Z &z)
{
    io.mesh(z.mesh);
    io.pod(z.angle_deg);
    io.pod(z.directionality);
    io.pod(z.cells);
}

// Vectors of structs with their own io function.
template<class T, class F> void io_list(Writer &w, std::vector<T> &v, F f) { w.u64(v.size()); for (T &x : v) f(w, x); }
template<class T, class F> void io_list(Reader &r, std::vector<T> &v, F f) { v.assign(r.count(), T()); for (T &x : v) f(r, x); }

template<class IO, class O> void io_infill(IO &io, O &o)
{
    io.pod(o.ok);
    io.str(o.error);
    io.pod(o.feasible);
    io.pod(o.uniform_density);
    io_result(io, o.uniform);
    io_list(io, o.ribs, [](auto &s, auto &z) { io_rib(s, z); });
    io.pod(o.density_without_ribs);
    io.pod(o.zones_found);
    io.pod(o.base_density);
    io_list(io, o.zones, [](auto &s, auto &z) { io_zone(s, z); });
    io_result(io, o.zoned);
    io.pod(o.analyses);
}

template<class IO, class O> void io_resin(IO &io, O &o)
{
    io.pod(o.ok);
    io.str(o.error);
    io_list(io, o.options, [](auto &s, auto &opt) {
        s.pod(opt.wall_thickness);
        s.pod(opt.infill_density);
        s.pod(opt.feasible);
        io_result(s, opt.result);
    });
    io_result(io, o.solid);
    io.pod(o.best);
    io.pod(o.analyses);
}

} // namespace

bool write_result_file(std::ostream &out, const ResultFile &file)
{
    Writer w(out);
    out.write(MAGIC, sizeof(MAGIC));
    w.pod(uint32_t(RESULT_FILE_VERSION));
    w.pod(uint32_t(file.kind));
    w.u64(file.info.size());
    for (const auto &kv : file.info) { w.str(kv.first); w.str(kv.second); }
    // The io functions take non-const references to serve the reader too; the writer does not modify them.
    ResultFile &f = const_cast<ResultFile&>(file);
    switch (file.kind) {
    case ResultFile::Kind::Analysis:       io_result(w, f.analysis); break;
    case ResultFile::Kind::LightestInfill: io_infill(w, f.infill); break;
    case ResultFile::Kind::ResinStructure: io_resin(w, f.resin); break;
    }
    return w.ok();
}

bool read_result_file(std::istream &in, ResultFile &file, std::string &error)
{
    char magic[4] = {};
    if (! in.read(magic, sizeof(magic)) || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) {
        error = "Not a Tisma analysis result file";
        return false;
    }
    Reader r(in);
    uint32_t version = 0, kind = 0;
    r.pod(version);
    r.pod(kind);
    if (! r.ok() || version != RESULT_FILE_VERSION) {
        error = "Unsupported version of the analysis result file";
        return false;
    }
    file = ResultFile();
    file.kind = ResultFile::Kind(kind);
    for (size_t i = 0, n = r.count(); i < n && r.ok(); ++ i) {
        std::string k, v;
        r.str(k);
        r.str(v);
        file.info[k] = v;
    }
    switch (file.kind) {
    case ResultFile::Kind::Analysis:       io_result(r, file.analysis); break;
    case ResultFile::Kind::LightestInfill: io_infill(r, file.infill); break;
    case ResultFile::Kind::ResinStructure: io_resin(r, file.resin); break;
    default:                               r.fail(); break;
    }
    if (! r.ok()) {
        error = "The analysis result file is damaged";
        return false;
    }
    return true;
}

bool save_result_file(const std::string &path, const ResultFile &file)
{
    boost::nowide::ofstream out(path, std::ios::binary | std::ios::trunc);
    return out && write_result_file(out, file) && bool(out.flush());
}

bool load_result_file(const std::string &path, ResultFile &file, std::string &error)
{
    boost::nowide::ifstream in(path, std::ios::binary);
    if (! in) {
        error = "Cannot open " + path;
        return false;
    }
    return read_result_file(in, file, error);
}

} // namespace Fea
} // namespace Slic3r
