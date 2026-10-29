///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PrintReview_hpp_
#define slic3r_PrintReview_hpp_

// Tisma: the review before printing. After slicing and making the G-code, the problems a print is likely to have,
// each with the layers where it happens: parts that will fall or come loose without supports (the support spots of
// PrusaSlicer), details too thin to be printed, layers too short to cool down, travels over the air (strings), and
// moves the printer will slow down. It warns and never blocks: the person decides.

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {

class Print;
struct GCodeProcessorResult;

struct PrintReviewItem
{
    enum class Severity { Info, Warning, Critical };
    enum class Kind {
        FloatingPart,         // a part of the object starts in the air
        CollapsingOverhang,   // large overhang that prints badly or not at all
        LooseExtrusions,      // overhanging extrusions that curl or come loose
        LongBridge,
        FloatingBridgeAnchor,
        LowBedAdhesion,       // the object may come off the bed
        FragilePart,          // thin part that may break while printing
        LostDetails,          // parts of the model too thin to get any extrusion
        Overheating,          // layers slowed down below the minimum speed to take the cooling time
        TravelsOverAir,       // travels leaving the part: strings
        SpeedLimited,         // moves faster than the printer allows (it slows them down)
    };

    Kind                     kind;
    Severity                 severity;
    // Translated texts: what it is, what was found (with numbers) and what to do.
    std::string              title;
    std::string              detail;
    std::string              hint;
    // Objects concerned (empty: the whole print).
    std::vector<std::string> objects;
    // G-code Z of the layers concerned (the heights shown by the preview): the one to show first and the range.
    double                   z     { 0. };
    double                   z_min { 0. };
    double                   z_max { 0. };
    // How many places, layers or moves.
    size_t                   count { 0 };
};

// The review of a sliced print and of the G-code made from it (GCodeProcessorResult filled by Print::export_gcode()),
// the most severe first, then from the bottom up.
std::vector<PrintReviewItem> review_print(const Print &print, const GCodeProcessorResult &result);

} // namespace Slic3r

#endif // slic3r_PrintReview_hpp_
