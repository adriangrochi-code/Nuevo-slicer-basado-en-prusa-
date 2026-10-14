///|/ Tisma Slicer: reading and writing the G0 / G1 lines of the G-code filters that change the Z of the moves.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GCodeMoveLine_hpp_
#define slic3r_GCodeMoveLine_hpp_

#include <cstdio>
#include <cstdlib>
#include <string>

namespace Slic3r {
namespace GCodeMoveLine {

struct Move
{
    bool has_x { false }, has_y { false }, has_z { false }, has_e { false }, has_f { false };
    double x { 0. }, y { 0. }, z { 0. }, e { 0. }, f { 0. };
    std::string comment;   // with its ';'
};

// G0 / G1 with X, Y, Z, E, F only (what PrusaSlicer emits); false for anything else.
inline bool parse(const std::string &line, Move &m)
{
    const size_t semi = line.find(';');
    const std::string code = line.substr(0, semi);
    if (semi != std::string::npos)
        m.comment = line.substr(semi);
    const char *c = code.c_str();
    while (*c == ' ' || *c == '\t')
        ++ c;
    if (! (c[0] == 'G' && (c[1] == '0' || c[1] == '1') && (c[2] == ' ' || c[2] == '\t' || c[2] == 0)))
        return false;
    c += 2;
    for (;;) {
        while (*c == ' ' || *c == '\t')
            ++ c;
        if (! *c)
            break;
        const char letter = *c ++;
        char *end = nullptr;
        const double v = std::strtod(c, &end);
        if (end == c)
            return false;
        c = end;
        switch (letter) {
        case 'X': m.has_x = true; m.x = v; break;
        case 'Y': m.has_y = true; m.y = v; break;
        case 'Z': m.has_z = true; m.z = v; break;
        case 'E': m.has_e = true; m.e = v; break;
        case 'F': m.has_f = true; m.f = v; break;
        default: return false;
        }
    }
    return true;
}

// " X1.234" without the trailing zeros.
inline void append_number(std::string &out, char axis, double value, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %c%.*f", axis, decimals, value);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0')
            s.pop_back();
        if (s.back() == '.')
            s.pop_back();
    }
    if (s == std::string(" ") + axis + "-0")
        s = std::string(" ") + axis + "0";
    out += s;
}

inline bool starts_with(const std::string &s, const char *prefix)
{
    return s.rfind(prefix, 0) == 0;
}

} // namespace GCodeMoveLine
} // namespace Slic3r

#endif // slic3r_GCodeMoveLine_hpp_
