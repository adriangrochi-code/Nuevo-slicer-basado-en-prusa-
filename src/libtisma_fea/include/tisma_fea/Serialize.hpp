///|/ Tisma Slicer: results of the structural analysis in a file (".tfea"), so that an analysis computed elsewhere (by
///|/ tisma-fea on a computing server) is shown by the GUI as if computed by it.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef tisma_fea_Serialize_hpp_
#define tisma_fea_Serialize_hpp_

#include <iosfwd>
#include <map>
#include <string>

#include "Analysis.hpp"
#include "Optimize.hpp"

namespace Slic3r {
namespace Fea {

// Binary format: "TFEA", version (uint32), kind (uint32), the info and the result of the kind. Little endian, floats
// and doubles as in memory (IEEE 754).
constexpr unsigned int RESULT_FILE_VERSION = 1;

struct ResultFile
{
    enum class Kind : unsigned int { Analysis = 1, LightestInfill = 2, ResinStructure = 3 };
    Kind kind { Kind::Analysis };
    // Values of the job which the GUI shows with the result (material, temperatures, mass of the current infill...).
    std::map<std::string, std::string> info;
    Result              analysis;   // Kind::Analysis
    OptimizeResult      infill;     // Kind::LightestInfill
    ResinOptimizeResult resin;      // Kind::ResinStructure
};

bool write_result_file(std::ostream &out, const ResultFile &file);
bool read_result_file(std::istream &in, ResultFile &file, std::string &error);

bool save_result_file(const std::string &path, const ResultFile &file);
bool load_result_file(const std::string &path, ResultFile &file, std::string &error);

} // namespace Fea
} // namespace Slic3r

#endif // tisma_fea_Serialize_hpp_
