// JNI layer of the Tisma Android app (uy.com.tisma.app.core.TismaCore): the slicing core of Tisma Slicer, called from
// Kotlin. Results are returned as small JSON strings; errors as exceptions (TismaCoreException).

#include <jni.h>
#include <android/log.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r_version.h"
#include "libslic3r/FileReader.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "arrange-wrapper/ModelArrange.hpp"

#include "engineering.hpp"

using namespace Slic3r;

namespace {

const char *TAG = "TismaCore";
JavaVM *g_vm = nullptr;

std::string to_std(JNIEnv *env, jstring s)
{
    if (s == nullptr)
        return {};
    const char *chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars);
    env->ReleaseStringUTFChars(s, chars);
    return out;
}

void throw_core(JNIEnv *env, const std::string &message)
{
    jclass cls = env->FindClass("uy/com/tisma/app/core/TismaCoreException");
    env->ThrowNew(cls, message.c_str());
}

std::string json_escape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else
                out += c;
        }
    }
    return out;
}

std::string number(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", std::isfinite(v) ? v : 0.);
    return buf;
}

// The printer, print and filament settings: the defaults of the core, then the INI file of the user (a Tisma or
// PrusaSlicer configuration export), if any.
DynamicPrintConfig load_config(const std::string &ini)
{
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    if (!ini.empty())
        config.load_from_ini(ini, ForwardCompatibilitySubstitutionRule::EnableSilent);
    config.normalize_fdm();
    return config;
}

// Reports the progress of the slicing to the Kotlin listener (from any thread of the core).
class ProgressReporter
{
public:
    ProgressReporter(JNIEnv *env, jobject listener)
        : m_listener(listener ? env->NewGlobalRef(listener) : nullptr)
    {
        if (m_listener) {
            jclass cls = env->GetObjectClass(listener);
            m_method   = env->GetMethodID(cls, "onProgress", "(ILjava/lang/String;)V");
            // Optional: listeners which can cancel (AnalysisListener).
            m_cancelled = env->GetMethodID(cls, "cancelled", "()Z");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                m_cancelled = nullptr;
            }
        }
    }
    ~ProgressReporter()
    {
        if (m_listener) {
            JNIEnv *env = nullptr;
            if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK)
                env->DeleteGlobalRef(m_listener);
        }
    }

    void report(int percent, const std::string &text) const
    {
        if (!m_listener || !m_method)
            return;
        with_env([&](JNIEnv *env) {
            jstring jtext = env->NewStringUTF(text.c_str());
            env->CallVoidMethod(m_listener, m_method, (jint) percent, jtext);
            env->DeleteLocalRef(jtext);
        });
    }

    // The listener asks to stop (false without a cancellable listener).
    bool cancelled() const
    {
        bool out = false;
        if (m_listener && m_cancelled)
            with_env([&](JNIEnv *env) { out = env->CallBooleanMethod(m_listener, m_cancelled); });
        return out;
    }

private:
    // Runs f with the JNIEnv of the current thread, attaching the threads of the core to the VM.
    template<class F> void with_env(F &&f) const
    {
        JNIEnv *env      = nullptr;
        bool    attached = false;
        if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
#ifdef __ANDROID__
            if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
#else
            // The jni.h of the JDK (host tests) declares the first parameter as void**.
            if (g_vm->AttachCurrentThread(reinterpret_cast<void **>(&env), nullptr) != JNI_OK)
#endif
                return;
            attached = true;
        }
        f(env);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (attached)
            g_vm->DetachCurrentThread();
    }

    jobject   m_listener  = nullptr;
    jmethodID m_method    = nullptr;
    jmethodID m_cancelled = nullptr;
};

} // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *)
{
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL Java_uy_com_tisma_app_core_TismaCore_nativeVersion(JNIEnv *env, jclass)
{
    return env->NewStringUTF(SLIC3R_BUILD_ID);
}

// {"objects":n,"triangles":n,"size":[x,y,z]}
JNIEXPORT jstring JNICALL Java_uy_com_tisma_app_core_TismaCore_nativeModelInfo(JNIEnv *env, jclass, jstring jpath)
{
    try {
        Model model = FileReader::load_model(to_std(env, jpath), FileReader::LoadAttribute::AddDefaultInstances);
        size_t triangles = 0;
        for (const ModelObject *object : model.objects)
            for (const ModelVolume *volume : object->volumes)
                if (volume->is_model_part())
                    triangles += volume->mesh().facets_count();
        const Vec3d size = model.bounding_box_exact().size();
        std::string json = "{\"objects\":" + std::to_string(model.objects.size()) + ",\"triangles\":" + std::to_string(triangles) +
            ",\"size\":[" + number(size.x()) + "," + number(size.y()) + "," + number(size.z()) + "]}";
        return env->NewStringUTF(json.c_str());
    } catch (const std::exception &ex) {
        throw_core(env, ex.what());
        return nullptr;
    }
}

// Slices a model (STL, 3MF, OBJ...) to G-code, as the command line of the desktop program does: arranged on the bed,
// validated, exported. {"gcode":path,"time":"1h 2m","time_s":n,"filament_mm":n,"filament_g":n}
JNIEXPORT jstring JNICALL Java_uy_com_tisma_app_core_TismaCore_nativeSlice(JNIEnv *env, jclass, jstring jmodel, jstring jini,
                                                                           jstring jout, jobject listener)
{
    try {
        const std::string out    = to_std(env, jout);
        DynamicPrintConfig config = load_config(to_std(env, jini));
        Model model = FileReader::load_model(to_std(env, jmodel), FileReader::LoadAttribute::AddDefaultInstances);
        if (model.objects.empty())
            throw Slic3r::RuntimeError("The file has no objects");

        // As the command line of the desktop program: on the bed (z = 0), arranged on the first bed, and the
        // instances outside the print volume marked as such.
        for (ModelObject *object : model.objects)
            object->ensure_on_bed();
        const Pointfs bed_shape = config.option<ConfigOptionPoints>("bed_shape")->values;
        const BuildVolume build_volume(bed_shape, config.opt_float("max_print_height"));
        s_multiple_beds.update_build_volume(BoundingBoxf(bed_shape));

        const Vec2crd         gap{ s_multiple_beds.get_bed_gap() };
        arr2::ArrangeBed      bed = arr2::to_arrange_bed(get_bed_shape(config), gap);
        arr2::ArrangeSettings arrange_cfg;
        arrange_cfg.set_distance_from_objects(min_object_distance(config));
        arrange_objects(model, bed, arrange_cfg);

        ProgressReporter progress(env, listener);
        Print print;
        print.set_status_callback([&progress](const PrintBase::SlicingStatus &s) {
            if (s.percent >= 0)
                progress.report(s.percent, s.text);
        });
        for (ModelObject *object : model.objects)
            print.auto_assign_extruders(object);
        model.update_print_volume_state(build_volume);
        MultipleBedsUtils::with_single_bed_model_fff(model, 0, [&print, &model, &config]() {
            DynamicPrintConfig plate_config{ config };
            model.plate(0).apply_to(plate_config);
            print.apply(model, plate_config);
        });
        if (std::string err = print.validate(); !err.empty())
            throw Slic3r::RuntimeError(err);
        if (print.empty())
            throw Slic3r::RuntimeError("Nothing to print: the objects are outside the print volume");

        print.process();
        const std::string written = print.export_gcode(out, nullptr);
        const PrintStatistics &stats = print.print_statistics();
        std::string json = "{\"gcode\":\"" + json_escape(written) + "\",\"time\":\"" + json_escape(stats.estimated_normal_print_time) +
            "\",\"time_s\":" + number(stats.normal_print_time_seconds) + ",\"filament_mm\":" + number(stats.total_used_filament) +
            ",\"filament_g\":" + number(stats.total_weight) + "}";
        return env->NewStringUTF(json.c_str());
    } catch (const std::exception &ex) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "Slicing failed: %s", ex.what());
        throw_core(env, ex.what());
        return nullptr;
    }
}

// Structural analysis of the first object of a model (engineering.hpp). Writes the voxel field to jfield and returns
// the result as JSON.
JNIEXPORT jstring JNICALL Java_uy_com_tisma_app_core_TismaCore_nativeAnalyze(JNIEnv *env, jclass, jstring jmodel, jstring jini,
                                                                             jstring jrequest, jstring jfield, jobject listener)
{
    try {
        const DynamicPrintConfig config = load_config(to_std(env, jini));
        ProgressReporter progress(env, listener);
        const std::string json = TismaApp::analyze(to_std(env, jmodel), config, to_std(env, jrequest), to_std(env, jfield),
            [&progress](int percent) { progress.report(percent, std::string()); },
            [&progress]() { return progress.cancelled(); });
        return env->NewStringUTF(json.c_str());
    } catch (const std::exception &ex) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "Analysis failed: %s", ex.what());
        throw_core(env, ex.what());
        return nullptr;
    }
}

JNIEXPORT jstring JNICALL Java_uy_com_tisma_app_core_TismaCore_nativeMaterials(JNIEnv *env, jclass)
{
    return env->NewStringUTF(TismaApp::materials_json().c_str());
}

} // extern "C"
