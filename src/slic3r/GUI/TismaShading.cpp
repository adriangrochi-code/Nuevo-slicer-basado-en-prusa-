///|/ Tisma Slicer: modern shading of the 3D scene (phase 7): per-pixel lighting, shadows, ambient occlusion and
///|/ physically based shading.
///|/
///|/ The techniques (shadow map of the main light with 3x3 PCF; G-buffer, screen space ambient occlusion and deferred
///|/ lighting, Phong or Cook-Torrance) follow the renderer of PrusaSlicer 3.0 (Copyright (c) Prusa Research, AGPLv3),
///|/ ported to the renderer of PrusaSlicer 2.9.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Method (see docs/RENDER.md):
// - The main light ("top" light of the shaders) is fixed to the camera, as in PrusaSlicer, so its direction in the world
//   changes with the camera and the shadow map is rendered every frame.
// - The light camera is orthographic and fits the bounding box of the casters (objects, toolpaths).
// - The receivers compute the position in the shadow map from their eye space position (eye_to_shadow matrix), so the
//   same matrix works for every model matrix (objects, bed, toolpaths of the active bed).
// - Receivers behind the far plane of the light (the bed far from a tall object) use the shadow map as a silhouette.
// Deferred shading (qualities AmbientOcclusion and PBR, phase 7c), as the "AO" and "PBR" modes of PrusaSlicer 3.0:
// - The opaque scene (parts, bed model and texture, toolpaths) is rendered to a G-buffer: depth, the color before
//   lighting with the material in its alpha (RGBA8) and the eye normal encoded in two floats (RG16F). The same shaders
//   as the forward rendering write it when their uniform tisma_gbuffer is set.
// - Ambient occlusion at half size (32 samples in a hemisphere around the normal, 4x4 noise, 5x5 box blur).
// - A full screen pass lights every pixel (Phong with the lights of PrusaSlicer 2.9, or Cook-Torrance with the
//   materials of PrusaSlicer 3.0), with the shadows of the main light and the ambient occlusion on the ambient light,
//   and writes the depth, so the transparent objects, the gizmos and the rest are rendered over it as before.
#include "libslic3r/libslic3r.h"
#include "TismaShading.hpp"

#include "3DScene.hpp"
#include "Camera.hpp"
#include "GLShader.hpp"
#include "GUI_App.hpp"
#if !SLIC3R_OPENGL_ES
#include "OpenGLManager.hpp"
#endif // !SLIC3R_OPENGL_ES
#include "LibVGCode/LibVGCodeWrapper.hpp"
#include "libslic3r/AppConfig.hpp"

#include <GL/glew.h>

#include <boost/log/trivial.hpp>

#include <boost/algorithm/string/case_conv.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <random>
#include <string>

namespace Slic3r {
namespace GUI {

TismaShading *TismaShading::s_current = nullptr;

namespace {

// Direction towards the "top" light in eye space (LIGHT_TOP_DIR of the shaders).
const Vec3d LIGHT_TOP_DIR_EYE = Vec3d(-0.4574957, 0.4574957, 0.7624929);

Transform3d look_at(const Vec3d &eye, const Vec3d &target, const Vec3d &up_hint)
{
    const Vec3d f = (target - eye).normalized();
    Vec3d up = up_hint;
    if (std::abs(f.dot(up)) > 0.99)
        up = Vec3d::UnitY();
    const Vec3d s = f.cross(up).normalized();
    const Vec3d u = s.cross(f);
    Transform3d view = Transform3d::Identity();
    view.matrix().row(0) << s.x(), s.y(), s.z(), -s.dot(eye);
    view.matrix().row(1) << u.x(), u.y(), u.z(), -u.dot(eye);
    view.matrix().row(2) << -f.x(), -f.y(), -f.z(), f.dot(eye);
    return view;
}

// Parameters of the materials of the deferred shading. PBR (metalness, roughness, index of refraction): the values of
// PrusaSlicer 3.0 (LightingHelper.hpp: DEFAULT_VOLUME_PBRPARAMS, DEFAULT_SLA_SUPPORTS_PBRPARAMS,
// DEFAULT_BED_PLATE_PBRPARAMS, DEFAULT_BED_MODEL_PBRPARAMS, DEFAULT_GCODE_SEGMENTS_PBRPARAMS,
// DEFAULT_GCODE_OPTIONS_PBRPARAMS). Silk and matte filaments: a visual approximation of Tisma (glossier and rougher
// than the default), not data of the makers. Emission: the one of the toolpaths shaders of libvgcode. shadow, lit and
// tinted_specular keep the look of the forward shading in the Phong mode (the bed texture is only shadowed, at 60 %).
struct MaterialParams
{
    float metal;
    float roughness;
    float ior;
    float emission;
    float shadow;
    float lit;
    float tinted_specular;
};
const std::array<MaterialParams, size_t(TismaShading::Material::Count)> MATERIALS = { {
    { 0.0f, 0.25f, 1.5f, 0.0f,  1.0f, 1.0f, 0.0f },  // Part
    { 0.2f, 0.12f, 1.5f, 0.0f,  1.0f, 1.0f, 0.0f },  // PartSilk
    { 0.0f, 0.7f,  1.5f, 0.0f,  1.0f, 1.0f, 0.0f },  // PartMatte
    { 0.0f, 0.5f,  1.4f, 0.0f,  1.0f, 1.0f, 0.0f },  // SlaSupport
    { 0.5f, 0.5f,  1.5f, 0.0f,  0.6f, 0.0f, 0.0f },  // BedPlate
    { 0.5f, 0.5f,  1.5f, 0.0f,  1.0f, 1.0f, 0.0f },  // BedModel
    { 0.0f, 0.5f,  1.5f, 0.15f, 1.0f, 1.0f, 1.0f },  // ToolpathSegments
    { 0.0f, 0.2f,  2.0f, 0.25f, 1.0f, 1.0f, 1.0f },  // ToolpathOptions
} };

// Ambient occlusion and PBR: the defaults of PrusaSlicer 3.0 (GraphicsSettings.hpp, Lights.hpp).
constexpr int   AO_KERNEL_SIZE    = 32;
constexpr int   AO_NOISE_SIZE     = 4;
constexpr float AO_INTENSITY      = 1.0f;
constexpr float AO_RADIUS         = 30.0f;
constexpr float AO_BIAS           = 1.5f;
constexpr float AO_Z_THRESHOLD    = 5.0f;
constexpr int   AO_BLUR_SIZE      = 5;
constexpr float PBR_INTENSITY     = 20.0f;
constexpr float PBR_AMBIENT       = 0.5f;

// Texture units of the passes of the deferred shading.
constexpr int UNIT_DEPTH  = 0;
constexpr int UNIT_NORMAL = 1;
constexpr int UNIT_COLOR  = 2;
constexpr int UNIT_AO     = 3;
constexpr int UNIT_NOISE  = 3;

unsigned int create_texture(GLint internal_format, GLenum format, GLenum type, int width, int height, GLint filter, GLint wrap, const void *data = nullptr)
{
    GLuint id = 0;
    glsafe(::glGenTextures(1, &id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, id));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, data));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    return id;
}

void delete_texture(unsigned int &id)
{
    if (id != 0) {
        glsafe(::glDeleteTextures(1, &id));
        id = 0;
    }
}

void delete_framebuffer(unsigned int &id)
{
    if (id != 0) {
        glsafe(::glDeleteFramebuffers(1, &id));
        id = 0;
    }
}

void bind_texture(int unit, unsigned int id)
{
    glsafe(::glActiveTexture(GL_TEXTURE0 + unit));
    glsafe(::glBindTexture(GL_TEXTURE_2D, id));
}

Transform3d ortho(double l, double r, double b, double t, double n, double f)
{
    Transform3d m = Transform3d::Identity();
    m.matrix() << 2. / (r - l), 0., 0., -(r + l) / (r - l),
                  0., 2. / (t - b), 0., -(t + b) / (t - b),
                  0., 0., -2. / (f - n), -(f + n) / (f - n),
                  0., 0., 0., 1.;
    return m;
}

} // namespace

TismaShading::~TismaShading()
{
    if (s_current == this)
        s_current = nullptr;
}

bool TismaShading::is_supported()
{
#if SLIC3R_OPENGL_ES
    return false;
#else
    // The shaders 140 are used from OpenGL 3.1, libvgcode needs OpenGL 3.2.
    return wxGetApp().is_gl_version_greater_or_equal_to(3, 2);
#endif // SLIC3R_OPENGL_ES
}

TismaShading::Quality TismaShading::quality()
{
    return is_supported() ? configured_quality() : Quality::Classic;
}

TismaShading::Quality TismaShading::configured_quality()
{
    const std::string value = wxGetApp().app_config->get(CONFIG_KEY);
    if (value.empty())
        return DEFAULT_RENDER_QUALITY;
    const int q = std::clamp(std::atoi(value.c_str()), 0, int(Quality::PBR));
    return Quality(q);
}

void TismaShading::begin_frame(const Camera &camera)
{
    m_quality          = quality();
    m_camera_view      = camera.get_view_matrix();
    m_shadow_map_valid = false;
    m_in_frame         = true;
    s_current          = this;
}

bool TismaShading::init_shadow_map()
{
    if (m_fbo != 0 && m_size == SHADOW_MAP_SIZE)
        return true;
    shutdown();

    glsafe(::glGenTextures(1, &m_depth_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_depth_texture));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    GLint prev_fbo = 0;
    glsafe(::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo));
    glsafe(::glGenFramebuffers(1, &m_fbo));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));
    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_texture, 0));
    glsafe(::glDrawBuffer(GL_NONE));
    glsafe(::glReadBuffer(GL_NONE));
    const bool complete = ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prev_fbo)));
    if (!complete) {
        BOOST_LOG_TRIVIAL(error) << "Tisma shading: the framebuffer of the shadow map is not complete, shadows disabled";
        shutdown();
        return false;
    }
    m_size = SHADOW_MAP_SIZE;
    return true;
}

void TismaShading::render_shadow_map(const BoundingBoxf3 &casters, const std::function<void(const Transform3d &, const Transform3d &)> &render_casters)
{
    m_shadow_map_valid = false;
    if (!m_in_frame || m_quality < Quality::Shadows || !casters.defined || casters.size().norm() < EPSILON)
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr || !init_shadow_map())
        return;

    // Light camera: orthographic, looking at the casters from the direction of the top light.
    const Vec3d  light_dir = (m_camera_view.linear().transpose() * LIGHT_TOP_DIR_EYE).normalized();
    const Vec3d  center    = casters.center();
    const double radius    = 0.5 * casters.size().norm();
    const Transform3d light_view = look_at(center + (radius + 10.) * light_dir, center, Vec3d::UnitZ());
    Vec3d lo = Vec3d::Constant(DBL_MAX), hi = Vec3d::Constant(-DBL_MAX);
    for (int i = 0; i < 8; ++i) {
        const Vec3d corner((i & 1) ? casters.max.x() : casters.min.x(), (i & 2) ? casters.max.y() : casters.min.y(), (i & 4) ? casters.max.z() : casters.min.z());
        const Vec3d c = light_view * corner;
        lo = lo.cwiseMin(c);
        hi = hi.cwiseMax(c);
    }
    // Margin of a few texels, near and far planes along -z of the light view.
    const double margin = 0.01 * std::max(hi.x() - lo.x(), hi.y() - lo.y()) + 0.5;
    const Transform3d light_projection = ortho(lo.x() - margin, hi.x() + margin, lo.y() - margin, hi.y() + margin,
                                               std::max(0.1, -hi.z() - 1.), -lo.z() + 1.);
    m_eye_to_shadow = (light_projection * light_view * m_camera_view.inverse()).matrix();

    // Depth pass.
    GLint prev_fbo = 0;
    GLint prev_viewport[4];
    glsafe(::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo));
    glsafe(::glGetIntegerv(GL_VIEWPORT, prev_viewport));
    const bool cull_face = ::glIsEnabled(GL_CULL_FACE);
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));
    glsafe(::glViewport(0, 0, m_size, m_size));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    // Slope scaled offset against the self shadowing ("shadow acne").
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(1.5f, 4.0f));

    shader->start_using();
    render_casters(light_view, light_projection);
    shader->stop_using();

    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    if (cull_face)
        glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prev_fbo)));
    glsafe(::glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]));
    m_shadow_map_valid = true;
}

TismaShading::Material TismaShading::part_material(const std::string &filament_name)
{
    const std::string name = boost::algorithm::to_upper_copy(filament_name);
    if (name.find("SILK") != std::string::npos)
        return Material::PartSilk;
    if (name.find("MATTE") != std::string::npos || name.find("MATT ") != std::string::npos)
        return Material::PartMatte;
    return Material::Part;
}

void TismaShading::apply(GLShaderProgram &shader, bool receive_shadows, Material material)
{
    if (m_in_gbuffer) {
        shader.set_uniform("tisma_gbuffer", true);
        shader.set_uniform("tisma_material", material_value(material));
        shader.set_uniform("tisma_per_pixel", false);
        shader.set_uniform("tisma_shadows", false);
        if (std::find(m_shaders_in_gbuffer.begin(), m_shaders_in_gbuffer.end(), &shader) == m_shaders_in_gbuffer.end())
            m_shaders_in_gbuffer.push_back(&shader);
        return;
    }
    shader.set_uniform("tisma_gbuffer", false);
    shader.set_uniform("tisma_per_pixel", per_pixel());
    const bool shadows = receive_shadows && this->shadows();
    shader.set_uniform("tisma_shadows", shadows);
    if (!shadows)
        return;
    shader.set_uniform("tisma_eye_to_shadow", m_eye_to_shadow);
    shader.set_uniform("tisma_shadow_intensity", SHADOW_INTENSITY);
    shader.set_uniform("tisma_shadow_map", SHADOW_MAP_TEXTURE_UNIT);
    glsafe(::glActiveTexture(GL_TEXTURE0 + SHADOW_MAP_TEXTURE_UNIT));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_depth_texture));
    glsafe(::glActiveTexture(GL_TEXTURE0));
    if (std::find(m_shaders_with_shadows.begin(), m_shaders_with_shadows.end(), &shader) == m_shaders_with_shadows.end())
        m_shaders_with_shadows.push_back(&shader);
}

void TismaShading::set_material(GLShaderProgram &shader, Material material) const
{
    if (m_in_gbuffer)
        shader.set_uniform("tisma_material", material_value(material));
}

void TismaShading::stop_shadows(GLShaderProgram &shader)
{
    // The shader may not be in use anymore (nested renders stop it).
    GLint prev_program = 0;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program));
    if (GLuint(prev_program) != shader.get_id())
        glsafe(::glUseProgram(shader.get_id()));
    shader.set_uniform("tisma_shadows", false);
    if (GLuint(prev_program) != shader.get_id())
        glsafe(::glUseProgram(GLuint(prev_program)));
}

void TismaShading::apply(libvgcode::Viewer &viewer) const
{
    libvgcode::Viewer::Shading shading;
    shading.per_pixel = per_pixel();
    shading.gbuffer           = m_in_gbuffer;
    shading.segments_material = material_value(Material::ToolpathSegments);
    shading.options_material  = material_value(Material::ToolpathOptions);
    if (shadows() && !m_in_gbuffer) {
        shading.shadow_map_tex_id    = m_depth_texture;
        shading.eye_to_shadow_matrix = libvgcode::convert(static_cast<Matrix4f>(m_eye_to_shadow.cast<float>()));
        shading.shadow_intensity     = SHADOW_INTENSITY;
    }
    viewer.set_shading(shading);
}

void TismaShading::end_frame()
{
    if (m_in_gbuffer)
        end_gbuffer();
    if (!m_shaders_with_shadows.empty()) {
        for (GLShaderProgram *shader : m_shaders_with_shadows)
            stop_shadows(*shader);
        m_shaders_with_shadows.clear();
    }
    if (m_depth_texture != 0) {
        glsafe(::glActiveTexture(GL_TEXTURE0 + SHADOW_MAP_TEXTURE_UNIT));
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
        glsafe(::glActiveTexture(GL_TEXTURE0));
    }
    m_in_frame         = false;
    m_shadow_map_valid = false;
    if (s_current == this)
        s_current = nullptr;
}

void TismaShading::shutdown()
{
    if (m_fbo != 0) {
        glsafe(::glDeleteFramebuffers(1, &m_fbo));
        m_fbo = 0;
    }
    if (m_depth_texture != 0) {
        glsafe(::glDeleteTextures(1, &m_depth_texture));
        m_depth_texture = 0;
    }
    m_size = 0;
    m_shadow_map_valid = false;
    release_gbuffer();
    delete_texture(m_noise_texture);
    m_screen_quad.reset();
}

bool TismaShading::init_gbuffer(int width, int height)
{
    if (m_gbuffer_fbo != 0 && m_gbuffer_width == width && m_gbuffer_height == height)
        return true;
    release_gbuffer();

    m_gbuffer_depth  = create_texture(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, width, height, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gbuffer_color  = create_texture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, width, height, GL_NEAREST, GL_CLAMP_TO_EDGE);
    m_gbuffer_normal = create_texture(GL_RG16F, GL_RG, GL_FLOAT, width, height, GL_NEAREST, GL_CLAMP_TO_EDGE);
    const int ao_width  = std::max(1, width / 2);
    const int ao_height = std::max(1, height / 2);
    for (unsigned int &texture : m_ao_texture)
        texture = create_texture(GL_R8, GL_RED, GL_UNSIGNED_BYTE, ao_width, ao_height, GL_LINEAR, GL_CLAMP_TO_EDGE);

    GLint prev_fbo = 0;
    glsafe(::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo));
    bool complete = true;
    glsafe(::glGenFramebuffers(1, &m_gbuffer_fbo));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer_fbo));
    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gbuffer_color, 0));
    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gbuffer_normal, 0));
    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gbuffer_depth, 0));
    complete &= ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    for (size_t i = 0; i < m_ao_fbo.size(); ++i) {
        glsafe(::glGenFramebuffers(1, &m_ao_fbo[i]));
        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[i]));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ao_texture[i], 0));
        complete &= ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prev_fbo)));
    if (!complete) {
        BOOST_LOG_TRIVIAL(error) << "Tisma shading: the framebuffers of the G-buffer are not complete, deferred shading disabled";
        release_gbuffer();
        return false;
    }
    m_gbuffer_width  = width;
    m_gbuffer_height = height;

    if (m_noise_texture == 0) {
        // Random rotations around the normal and samples in a hemisphere, more of them near the center, as in
        // PrusaSlicer 3.0 (Scene::generate_ao_noise(), Scene::generate_ao_kernel()).
        std::uniform_real_distribution<float> random_floats(0.0f, 1.0f);
        std::default_random_engine generator;
        std::vector<Vec3f> noise;
        for (int i = 0; i < AO_NOISE_SIZE * AO_NOISE_SIZE; ++i)
            noise.emplace_back(random_floats(generator) * 2.0f - 1.0f, random_floats(generator) * 2.0f - 1.0f, 0.0f);
        m_noise_texture = create_texture(GL_RGB16F, GL_RGB, GL_FLOAT, AO_NOISE_SIZE, AO_NOISE_SIZE, GL_NEAREST, GL_REPEAT, noise.data());
        m_ao_kernel.clear();
        for (int i = 0; i < AO_KERNEL_SIZE; ++i) {
            Vec3f sample(random_floats(generator) * 2.0f - 1.0f, random_floats(generator) * 2.0f - 1.0f, random_floats(generator));
            sample.normalize();
            sample *= random_floats(generator);
            float scale = float(i) / float(AO_KERNEL_SIZE);
            scale = 0.1f + scale * scale * 0.9f;
            m_ao_kernel.emplace_back(sample * scale);
        }
    }
    return true;
}

void TismaShading::release_gbuffer()
{
    delete_framebuffer(m_gbuffer_fbo);
    for (unsigned int &fbo : m_ao_fbo)
        delete_framebuffer(fbo);
    delete_texture(m_gbuffer_depth);
    delete_texture(m_gbuffer_normal);
    delete_texture(m_gbuffer_color);
    for (unsigned int &texture : m_ao_texture)
        delete_texture(texture);
    m_gbuffer_width  = 0;
    m_gbuffer_height = 0;
}

bool TismaShading::begin_gbuffer(const Camera &camera)
{
    if (!deferred() || m_in_gbuffer || wxGetApp().get_shader("tisma_ssao") == nullptr || wxGetApp().get_shader("tisma_blur") == nullptr ||
        wxGetApp().get_shader("tisma_deferred") == nullptr)
        return false;
    glsafe(::glGetIntegerv(GL_VIEWPORT, m_prev_viewport));
    if (m_prev_viewport[2] <= 0 || m_prev_viewport[3] <= 0)
        return false;
    glsafe(::glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prev_fbo));
    if (!init_gbuffer(m_prev_viewport[2], m_prev_viewport[3]))
        return false;

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer_fbo));
    const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glsafe(::glDrawBuffers(2, buffers));
    glsafe(::glViewport(0, 0, m_gbuffer_width, m_gbuffer_height));
    GLfloat clear_color[4];
    glsafe(::glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color));
    glsafe(::glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]));
    glsafe(::glDisable(GL_BLEND));
    m_in_gbuffer = true;
    return true;
}

void TismaShading::end_gbuffer()
{
    if (!m_in_gbuffer)
        return;
    m_in_gbuffer = false;
    // The shaders do not write the G-buffer anymore (the uniforms are kept by the programs).
    GLint prev_program = 0;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program));
    for (GLShaderProgram *shader : m_shaders_in_gbuffer) {
        glsafe(::glUseProgram(shader->get_id()));
        shader->set_uniform("tisma_gbuffer", false);
    }
    glsafe(::glUseProgram(GLuint(prev_program)));
    m_shaders_in_gbuffer.clear();
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, GLuint(m_prev_fbo)));
    glsafe(::glViewport(m_prev_viewport[0], m_prev_viewport[1], m_prev_viewport[2], m_prev_viewport[3]));
}

void TismaShading::render_screen_quad()
{
    if (!m_screen_quad.is_initialized()) {
        GLModel::Geometry geometry;
        geometry.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P2T2 };
        geometry.add_vertex(Vec2f(-1.0f, -1.0f), Vec2f(0.0f, 0.0f));
        geometry.add_vertex(Vec2f( 1.0f, -1.0f), Vec2f(1.0f, 0.0f));
        geometry.add_vertex(Vec2f( 1.0f,  1.0f), Vec2f(1.0f, 1.0f));
        geometry.add_vertex(Vec2f(-1.0f,  1.0f), Vec2f(0.0f, 1.0f));
        geometry.add_triangle(0, 1, 2);
        geometry.add_triangle(0, 2, 3);
        m_screen_quad.init_from(std::move(geometry));
    }
    m_screen_quad.render();
}

void TismaShading::render_pass(unsigned int fbo, int width, int height)
{
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, fbo != 0 ? fbo : GLuint(m_prev_fbo)));
    if (fbo != 0) {
        const GLenum buffer = GL_COLOR_ATTACHMENT0;
        glsafe(::glDrawBuffers(1, &buffer));
        glsafe(::glViewport(0, 0, width, height));
    } else
        glsafe(::glViewport(m_prev_viewport[0], m_prev_viewport[1], m_prev_viewport[2], m_prev_viewport[3]));
    render_screen_quad();
}

void TismaShading::render_deferred(const Camera &camera)
{
    if (m_gbuffer_fbo == 0)
        return;
    GLShaderProgram *ssao_shader     = wxGetApp().get_shader("tisma_ssao");
    GLShaderProgram *blur_shader     = wxGetApp().get_shader("tisma_blur");
    GLShaderProgram *deferred_shader = wxGetApp().get_shader("tisma_deferred");
    if (ssao_shader == nullptr || blur_shader == nullptr || deferred_shader == nullptr)
        return;

    const bool depth_test = ::glIsEnabled(GL_DEPTH_TEST);
    const bool cull_face  = ::glIsEnabled(GL_CULL_FACE);
    const bool blend      = ::glIsEnabled(GL_BLEND);
    GLint depth_func = GL_LESS;
    glsafe(::glGetIntegerv(GL_DEPTH_FUNC, &depth_func));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_FALSE));

    const Matrix4d projection     = camera.get_projection_matrix().matrix();
    const Matrix4d inv_projection = projection.inverse();
    const int ao_width  = std::max(1, m_gbuffer_width / 2);
    const int ao_height = std::max(1, m_gbuffer_height / 2);

    // Ambient occlusion.
    ssao_shader->start_using();
    ssao_shader->set_uniform("intensity", AO_INTENSITY);
    ssao_shader->set_uniform("radius", AO_RADIUS);
    ssao_shader->set_uniform("bias", AO_BIAS);
    ssao_shader->set_uniform("z_threshold", AO_Z_THRESHOLD);
    ssao_shader->set_uniform("kernel_size", int(m_ao_kernel.size()));
    ssao_shader->set_uniform("noise_scale", Vec2f(float(ao_width) / float(AO_NOISE_SIZE), float(ao_height) / float(AO_NOISE_SIZE)));
    ssao_shader->set_uniform("projection_matrix", projection);
    ssao_shader->set_uniform("inverse_projection_matrix", inv_projection);
    for (size_t i = 0; i < m_ao_kernel.size(); ++i)
        ssao_shader->set_uniform(("kernel[" + std::to_string(i) + "]").c_str(), m_ao_kernel[i]);
    ssao_shader->set_uniform("g_depth", UNIT_DEPTH);
    ssao_shader->set_uniform("g_normal", UNIT_NORMAL);
    ssao_shader->set_uniform("tex_noise", UNIT_NOISE);
    bind_texture(UNIT_DEPTH, m_gbuffer_depth);
    bind_texture(UNIT_NORMAL, m_gbuffer_normal);
    bind_texture(UNIT_NOISE, m_noise_texture);
    render_pass(m_ao_fbo[0], ao_width, ao_height);
    ssao_shader->stop_using();

    // Blur: horizontal to the second texture, vertical back to the first one.
    blur_shader->start_using();
    blur_shader->set_uniform("in_tex", UNIT_AO);
    blur_shader->set_uniform("filter_size", AO_BLUR_SIZE);
    blur_shader->set_uniform("direction", Vec2f(1.0f, 0.0f));
    bind_texture(UNIT_AO, m_ao_texture[0]);
    render_pass(m_ao_fbo[1], ao_width, ao_height);
    blur_shader->set_uniform("direction", Vec2f(0.0f, 1.0f));
    bind_texture(UNIT_AO, m_ao_texture[1]);
    render_pass(m_ao_fbo[0], ao_width, ao_height);
    blur_shader->stop_using();

    // Lighting into the framebuffer of the canvas, with the depth of the G-buffer.
    deferred_shader->start_using();
    deferred_shader->set_uniform("g_depth", UNIT_DEPTH);
    deferred_shader->set_uniform("g_normal", UNIT_NORMAL);
    deferred_shader->set_uniform("g_color", UNIT_COLOR);
    deferred_shader->set_uniform("ssao", UNIT_AO);
    deferred_shader->set_uniform("apply_ao", true);
    deferred_shader->set_uniform("apply_pbr", m_quality == Quality::PBR);
    deferred_shader->set_uniform("pbr_intensity", PBR_INTENSITY);
    deferred_shader->set_uniform("pbr_ambient", PBR_AMBIENT);
    deferred_shader->set_uniform("inverse_projection_matrix", inv_projection);
    deferred_shader->set_uniform("materials_count", int(MATERIALS.size()));
    for (size_t i = 0; i < MATERIALS.size(); ++i) {
        const MaterialParams &m = MATERIALS[i];
        deferred_shader->set_uniform(("materials[" + std::to_string(i) + "]").c_str(), std::array<float, 4>{ m.metal, m.roughness, m.ior, m.emission });
        deferred_shader->set_uniform(("material_flags[" + std::to_string(i) + "]").c_str(), std::array<float, 4>{ m.shadow, m.lit, m.tinted_specular, 0.0f });
    }
    deferred_shader->set_uniform("tisma_shadows", m_shadow_map_valid);
    if (m_shadow_map_valid) {
        deferred_shader->set_uniform("tisma_eye_to_shadow", m_eye_to_shadow);
        deferred_shader->set_uniform("tisma_shadow_intensity", SHADOW_INTENSITY);
        deferred_shader->set_uniform("tisma_shadow_map", SHADOW_MAP_TEXTURE_UNIT);
        bind_texture(SHADOW_MAP_TEXTURE_UNIT, m_depth_texture);
    }
    bind_texture(UNIT_DEPTH, m_gbuffer_depth);
    bind_texture(UNIT_NORMAL, m_gbuffer_normal);
    bind_texture(UNIT_COLOR, m_gbuffer_color);
    bind_texture(UNIT_AO, m_ao_texture[0]);
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthFunc(GL_ALWAYS));
    glsafe(::glDepthMask(GL_TRUE));
    render_pass(0, 0, 0);
    deferred_shader->stop_using();

    for (int unit : { UNIT_DEPTH, UNIT_NORMAL, UNIT_COLOR, UNIT_AO, SHADOW_MAP_TEXTURE_UNIT })
        bind_texture(unit, 0);
    glsafe(::glActiveTexture(GL_TEXTURE0));
    glsafe(::glDepthFunc(GLenum(depth_func)));
    if (!depth_test)
        glsafe(::glDisable(GL_DEPTH_TEST));
    if (cull_face)
        glsafe(::glEnable(GL_CULL_FACE));
    if (blend)
        glsafe(::glEnable(GL_BLEND));
}

} // namespace GUI
} // namespace Slic3r
