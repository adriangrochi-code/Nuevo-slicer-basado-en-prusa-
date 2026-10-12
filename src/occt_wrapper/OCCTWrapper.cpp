#include "OCCTWrapper.hpp"

#include "occtwrapper_export.h"

#include <cassert>

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#else
#define DIR_SEPARATOR '/'
#endif

#include "STEPCAFControl_Reader.hxx"
#include "BRepMesh_IncrementalMesh.hxx"
#include "XCAFDoc_DocumentTool.hxx"
#include "XCAFDoc_ShapeTool.hxx"
#include "XCAFApp_Application.hxx"
#include "TopoDS_Builder.hxx"
#include "TopoDS.hxx"
#include "TDataStd_Name.hxx"
#include "BRepBuilderAPI_Transform.hxx"
#include "TopExp_Explorer.hxx"
#include "BRep_Tool.hxx"
#include "BRepCheck_Analyzer.hxx"
#include "BRepCheck_ListOfStatus.hxx"
#include "BRepCheck_Result.hxx"
#include "TopTools_IndexedMapOfShape.hxx"
#include "TopExp.hxx"
#include "admesh/stl.h"
#include "libslic3r/Point.hpp"

const double STEP_TRANS_CHORD_ERROR = Slic3r::OCCT_DEFAULT_LINEAR_DEFLECTION;
const double STEP_TRANS_ANGLE_RES = Slic3r::OCCT_DEFAULT_ANGULAR_DEFLECTION;

// const int LOAD_STEP_STAGE_READ_FILE          = 0;
// const int LOAD_STEP_STAGE_GET_SOLID          = 1;
// const int LOAD_STEP_STAGE_GET_MESH           = 2;

namespace Slic3r {

struct NamedSolid {
    NamedSolid(const TopoDS_Shape& s,
               const std::string& n) : solid{s}, name{n} {}
    const TopoDS_Shape solid;
    const std::string  name;
};

static void getNamedSolids(const TopLoc_Location& location, const Handle(XCAFDoc_ShapeTool) shapeTool,
                           const TDF_Label label, std::vector<NamedSolid>& namedSolids)
{
    TDF_Label referredLabel{label};
    if (shapeTool->IsReference(label))
        shapeTool->GetReferredShape(label, referredLabel);

    std::string name;
    Handle(TDataStd_Name) shapeName;
    if (referredLabel.FindAttribute(TDataStd_Name::GetID(), shapeName))
        name = TCollection_AsciiString(shapeName->Get()).ToCString();

    TopLoc_Location localLocation = location * shapeTool->GetLocation(label);
    TDF_LabelSequence components;
    if (shapeTool->GetComponents(referredLabel, components)) {
        for (Standard_Integer compIndex = 1; compIndex <= components.Length(); ++compIndex) {
            getNamedSolids(localLocation, shapeTool, components.Value(compIndex), namedSolids);
        }
    } else {
        TopoDS_Shape shape;
        shapeTool->GetShape(referredLabel, shape);
        TopAbs_ShapeEnum shape_type = shape.ShapeType();
        BRepBuilderAPI_Transform transform(shape, localLocation, Standard_True);
        switch (shape_type) {
        case TopAbs_COMPOUND:
            namedSolids.emplace_back(TopoDS::Compound(transform.Shape()), name);
            break;
        case TopAbs_COMPSOLID:
            namedSolids.emplace_back(TopoDS::CompSolid(transform.Shape()), name);
            break;
        case TopAbs_SOLID:
            namedSolids.emplace_back(TopoDS::Solid(transform.Shape()), name);
            break;
        default:
            break;
        }
    }
}

// Tisma: checks the topology and geometry of a solid with BRepCheck_Analyzer. Returns false and a short report
// (number of invalid faces, edges and vertices) when problems are found.
static bool check_solid(const TopoDS_Shape &shape, std::string &report)
{
    BRepCheck_Analyzer analyzer(shape, Standard_True);
    if (analyzer.IsValid())
        return true;
    int bad[3] = { 0, 0, 0 };
    const TopAbs_ShapeEnum types[3] = { TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX };
    for (int i = 0; i < 3; ++ i) {
        TopTools_IndexedMapOfShape map;
        TopExp::MapShapes(shape, types[i], map);
        for (int j = 1; j <= map.Extent(); ++ j)
            if (! analyzer.IsValid(map(j)))
                ++ bad[i];
    }
    report = "invalid faces: " + std::to_string(bad[0]) + ", edges: " + std::to_string(bad[1]) +
             ", vertices: " + std::to_string(bad[2]);
    return false;
}

extern "C" OCCTWRAPPER_EXPORT bool load_step_internal(const char *path, OCCTResult* res /*BBS:, ImportStepProgressFn proFn*/, std::optional<std::pair<double, double>> deflections /*= std::nullopt*/)
{
try {
    //bool cb_cancel = false;
    //if (proFn) {
    //    proFn(LOAD_STEP_STAGE_READ_FILE, 0, 1, cb_cancel);
    //    if (cb_cancel)
    //        return false;
    //}
    

    std::vector<NamedSolid> namedSolids;
    Handle(TDocStd_Document) document;
    Handle(XCAFApp_Application) application = XCAFApp_Application::GetApplication();
    application->NewDocument(path, document);
    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    //BBS: Todo, read file is slow which cause the progress_bar no update and gui no response
    IFSelect_ReturnStatus stat = reader.ReadFile(path);
    if (stat != IFSelect_RetDone || !reader.Transfer(document)) {
        application->Close(document);
        res->error_str = std::string{"Could not read '"} + path + "'";
        return false;
    }
    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence topLevelShapes;
    shapeTool->GetFreeShapes(topLevelShapes);

    Standard_Integer topShapeLength = topLevelShapes.Length() + 1;
    for (Standard_Integer iLabel = 1; iLabel < topShapeLength; ++iLabel) {
        //if (proFn) {
        //    proFn(LOAD_STEP_STAGE_GET_SOLID, iLabel, topShapeLength, cb_cancel);
        //    if (cb_cancel) {
        //        shapeTool.reset(nullptr);
        //        application->Close(document);
        //        return false;
        //    }
        //}
        getNamedSolids(TopLoc_Location{}, shapeTool, topLevelShapes.Value(iLabel), namedSolids);
    }

    

    // Now the object name. Set it to filename without suffix.
    // This will later be changed if only one volume is loaded.
    const char *last_slash = strrchr(path, DIR_SEPARATOR);
    std::string obj_name((last_slash == nullptr) ? path : last_slash + 1);
    res->object_name = obj_name;

    for (size_t solid_idx = 0; solid_idx < namedSolids.size(); ++ solid_idx) {
        const NamedSolid &namedSolid = namedSolids[solid_idx];
        BRepMesh_IncrementalMesh mesh(namedSolid.solid, 
                                      deflections.has_value() ? deflections.value().first  : STEP_TRANS_CHORD_ERROR, false, 
                                      deflections.has_value() ? deflections.value().second : STEP_TRANS_ANGLE_RES, true);
        res->volumes.emplace_back();

        std::vector<Vec3f>      vertices;
        std::vector<stl_facet> &facets = res->volumes.back().facets;
        res->volumes.back().solid_index = int(solid_idx);
        res->volumes.back().brep_valid  = check_solid(namedSolid.solid, res->volumes.back().brep_report);
        int face_idx = -1;
        for (TopExp_Explorer anExpSF(namedSolid.solid, TopAbs_FACE); anExpSF.More(); anExpSF.Next()) {
            // Tisma: the face index is counted also for the faces without triangulation, so that it stays stable.
            ++ face_idx;
            const unsigned int face_id = face_idx < int(OCCT_FACE_ID_NONE) ? unsigned(face_idx) : OCCT_FACE_ID_NONE;
            const int aNodeOffset = int(vertices.size());
            const TopoDS_Shape& aFace = anExpSF.Current();
            TopLoc_Location aLoc;
            Handle(Poly_Triangulation) aTriangulation = BRep_Tool::Triangulation(TopoDS::Face(aFace), aLoc);
            if (aTriangulation.IsNull())
                continue;

            // First copy vertices (will create duplicates).
            gp_Trsf aTrsf = aLoc.Transformation();
            for (Standard_Integer aNodeIter = 1; aNodeIter <= aTriangulation->NbNodes(); ++aNodeIter) {
                gp_Pnt aPnt = aTriangulation->Node(aNodeIter);
                aPnt.Transform(aTrsf);
                vertices.emplace_back(std::move(Vec3f(float(aPnt.X()), float(aPnt.Y()), float(aPnt.Z()))));
            }

            // Now copy the facets.
            const TopAbs_Orientation anOrientation = anExpSF.Current().Orientation();
            for (Standard_Integer aTriIter = 1; aTriIter <= aTriangulation->NbTriangles(); ++aTriIter) {
                const int aTriangleOffet = int(facets.size());
                Poly_Triangle aTri = aTriangulation->Triangle(aTriIter);

                Standard_Integer anId[3];
                aTri.Get(anId[0], anId[1], anId[2]);
                if (anOrientation == TopAbs_REVERSED) {
                    std::swap(anId[1], anId[2]);
                }

                stl_facet facet;
                facet.vertex[0] = vertices[anId[0] + aNodeOffset - 1];
                facet.vertex[1] = vertices[anId[1] + aNodeOffset - 1];
                facet.vertex[2] = vertices[anId[2] + aNodeOffset - 1];
                facet.normal    = (facet.vertex[1] - facet.vertex[0]).cross(facet.vertex[2] - facet.vertex[1]).normalized();
                facet.extra[0]  = char(face_id & 0xFF);
                facet.extra[1]  = char((face_id >> 8) & 0xFF);
                facets.emplace_back(std::move(facet));
            }
        }

        res->volumes.back().volume_name = namedSolid.name;
        res->volumes.back().face_count  = face_idx + 1;

        if (vertices.empty())
            res->volumes.pop_back();        
    }

    shapeTool.reset(nullptr);
    application->Close(document);

    if (res->volumes.empty())
        return false;
} catch (const std::exception& ex) {
    res->error_str = ex.what();
    return false;
} catch (...) {
    res->error_str = "An exception was thrown in load_step_internal.";
    return false;
}
    
    return true;
}

}; // namespace Slic3r
