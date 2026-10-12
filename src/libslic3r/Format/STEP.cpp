///|/ Copyright (c) Prusa Research 2022 Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "STEP.hpp"
#include "occt_wrapper/OCCTWrapper.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/CadSource.hpp"

#include <boost/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <string>
#include <functional>

#ifdef _WIN32
    #include<windows.h>
#else
    #include<occt_wrapper/OCCTWrapper.hpp>
    #include <dlfcn.h>
#endif


namespace Slic3r {

#if __APPLE__
extern "C" bool load_step_internal(const char *path, OCCTResult* res, std::optional<std::pair<double, double>> deflections /*= std::nullopt*/);
#endif

// Inside deflections pair:
// * first value is linear deflection
// * second value is angle deflection
LoadStepFn get_load_step_fn()
{
    static LoadStepFn load_step_fn = nullptr;

#ifndef __APPLE__
    constexpr const char* fn_name = "load_step_internal";
#endif

    if (!load_step_fn) {
        auto libpath = boost::dll::program_location().parent_path();
#ifdef _WIN32
        libpath /= "OCCTWrapper.dll";
        HMODULE module = LoadLibraryW(libpath.wstring().c_str());
        if (module == NULL)
            throw Slic3r::RuntimeError("Cannot load OCCTWrapper.dll");

        try {
            FARPROC farproc = GetProcAddress(module, fn_name);
            if (! farproc) {
                DWORD ec = GetLastError();
                throw Slic3r::RuntimeError(std::string("Cannot load function from OCCTWrapper.dll: ") + fn_name
                                           + "\n\nError code: " + std::to_string(ec));
            }
            load_step_fn = reinterpret_cast<LoadStepFn>(farproc);
        } catch (const Slic3r::RuntimeError&) {
            FreeLibrary(module);
            throw;
        }
#elif __APPLE__
        load_step_fn = &load_step_internal;
#else
        libpath /= "OCCTWrapper.so";
        void *plugin_ptr = dlopen(libpath.c_str(), RTLD_NOW | RTLD_GLOBAL);

        if (plugin_ptr) {
            load_step_fn = reinterpret_cast<LoadStepFn>(dlsym(plugin_ptr, fn_name));
            if (!load_step_fn) {
                dlclose(plugin_ptr);
                throw Slic3r::RuntimeError(std::string("Cannot load function from OCCTWrapper.so: ") + fn_name
                                           + "\n\n" + dlerror());
            }
        } else {
            throw Slic3r::RuntimeError(std::string("Cannot load OCCTWrapper.so:\n\n") + dlerror());
        }
#endif
    }

    return load_step_fn;
}

bool load_step(const char *path, Model *model /*BBS:, ImportStepProgressFn proFn*/, std::optional<std::pair<double, double>> deflections)
{
    OCCTResult occt_object;

    LoadStepFn load_step_fn = get_load_step_fn();

    if (!load_step_fn)
        return false;

    load_step_fn(path, &occt_object, deflections);

    assert(! occt_object.volumes.empty());
    
    assert(boost::algorithm::iends_with(occt_object.object_name, ".stp")
        || boost::algorithm::iends_with(occt_object.object_name, ".step"));
    occt_object.object_name.erase(occt_object.object_name.find("."));
    assert(! occt_object.object_name.empty());


    ModelObject* new_object = model->add_object();
    new_object->input_file = path;
    if (new_object->volumes.size() == 1 && ! occt_object.volumes.front().volume_name.empty())
        new_object->name = new_object->volumes.front()->name;
    else
        new_object->name = occt_object.object_name;

    // Tisma: the STEP file is kept with the project, so that the parts can be tessellated again (phase 4).
    std::shared_ptr<const CadStepFile> step_file;
    {
        constexpr std::streamoff max_size = std::streamoff(256) * 1024 * 1024;
        boost::nowide::ifstream in(path, std::ios::binary | std::ios::ate);
        const std::streamoff size = in ? std::streamoff(in.tellg()) : std::streamoff(-1);
        if (size > 0 && size <= max_size) {
            std::string data(size_t(size), '\0');
            in.seekg(0);
            if (in.read(data.data(), size))
                step_file = cad_step_file_register(boost::filesystem::path(path).filename().string(), std::move(data));
        } else if (size > max_size)
            BOOST_LOG_TRIVIAL(warning) << "STEP file " << path << " is too big to be stored in the project";
    }
    const double linear_deflection  = deflections.has_value() ? deflections->first  : OCCT_DEFAULT_LINEAR_DEFLECTION;
    const double angular_deflection = deflections.has_value() ? deflections->second : OCCT_DEFAULT_ANGULAR_DEFLECTION;

    for (size_t i = 0; i < occt_object.volumes.size(); ++i) {
        TriangleMesh triangle_mesh;
        std::vector<uint16_t> face_tags;
        triangle_mesh.from_facets(std::move(occt_object.volumes[i].facets), true, face_tags);
        ModelVolume* new_volume = new_object->add_volume(std::move(triangle_mesh));

        auto cad = std::make_shared<CadSource>();
        cad->step               = step_file;
        cad->solid_index        = occt_object.volumes[i].solid_index;
        cad->linear_deflection  = linear_deflection;
        cad->angular_deflection = angular_deflection;
        cad->face_ids           = cad_face_ids_from_tags(face_tags);
        cad->face_count         = occt_object.volumes[i].face_count;
        cad->brep_valid         = occt_object.volumes[i].brep_valid;
        cad->brep_report        = occt_object.volumes[i].brep_report;
        new_volume->cad_source  = std::move(cad);

        new_volume->name = occt_object.volumes[i].volume_name.empty()
                       ? std::string("Part") + std::to_string(i + 1)
                       : occt_object.volumes[i].volume_name;
        new_volume->source.input_file = path;
        new_volume->source.object_idx = (int)model->objects.size() - 1;
        new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    }

    return true;
}

bool step_tessellate_solid(const char *path, int solid_index, double linear_deflection, double angular_deflection,
                           TriangleMesh &mesh_out, std::vector<uint16_t> &face_tags_out, std::string &error)
{
    LoadStepFn load_step_fn = get_load_step_fn();
    if (! load_step_fn) {
        error = "The STEP import library is not available";
        return false;
    }
    OCCTResult result;
    if (! load_step_fn(path, &result, std::make_pair(linear_deflection, angular_deflection))) {
        error = result.error_str.empty() ? std::string("Cannot read the STEP model") : result.error_str;
        return false;
    }
    for (OCCTVolume &volume : result.volumes)
        if (volume.solid_index == solid_index) {
            mesh_out = TriangleMesh();
            mesh_out.from_facets(std::move(volume.facets), true, face_tags_out);
            return true;
        }
    error = "The solid was not found in the STEP model";
    return false;
}

}; // namespace Slic3r
