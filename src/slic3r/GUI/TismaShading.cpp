///|/ Tisma Slicer: modern shading of the 3D scene (phase 7): per-pixel lighting and shadows.
///|/
///|/ The techniques (shadow map of the main light, 3x3 PCF) follow the renderer of PrusaSlicer 3.0
///|/ (Copyright (c) Prusa Research, AGPLv3), ported to the renderer of PrusaSlicer 2.9.
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

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>

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
        return DEFAULT_QUALITY;
    const int q = std::clamp(std::atoi(value.c_str()), 0, int(Quality::Shadows));
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
    if (!m_in_frame || m_quality != Quality::Shadows || !casters.defined || casters.size().norm() < EPSILON)
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

void TismaShading::apply(GLShaderProgram &shader, bool receive_shadows)
{
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
    if (shadows()) {
        shading.shadow_map_tex_id    = m_depth_texture;
        shading.eye_to_shadow_matrix = libvgcode::convert(static_cast<Matrix4f>(m_eye_to_shadow.cast<float>()));
        shading.shadow_intensity     = SHADOW_INTENSITY;
    }
    viewer.set_shading(shading);
}

void TismaShading::end_frame()
{
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
}

} // namespace GUI
} // namespace Slic3r
