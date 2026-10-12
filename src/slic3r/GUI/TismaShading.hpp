///|/ Tisma Slicer: modern shading of the 3D scene (phase 7): per-pixel lighting and shadows.
///|/
///|/ The techniques (shadow map of the main light, 3x3 PCF) follow the renderer of PrusaSlicer 3.0
///|/ (Copyright (c) Prusa Research, AGPLv3), ported to the renderer of PrusaSlicer 2.9.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_TismaShading_hpp_
#define slic3r_GUI_TismaShading_hpp_

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

#include <functional>
#include <vector>

namespace libvgcode {
class Viewer;
}

namespace Slic3r {

class GLShaderProgram;

namespace GUI {

struct Camera;

class TismaShading
{
public:
    // Saved in the application config as "tisma_render_quality".
    enum class Quality : int
    {
        // PrusaSlicer 2.9: lighting computed per vertex.
        Classic   = 0,
        // Lighting computed per pixel (smooth highlights, rounded toolpaths).
        PerPixel  = 1,
        // Per pixel lighting and shadows of the main light.
        Shadows   = 2,
    };
    static constexpr const char *CONFIG_KEY = "tisma_render_quality";
    static constexpr Quality     DEFAULT_RENDER_QUALITY = Quality::Shadows;
    // Texture unit of the shadow map (libvgcode uses the units 0..3 for its texture buffers).
    static constexpr int         SHADOW_MAP_TEXTURE_UNIT = 4;
    static constexpr int         SHADOW_MAP_SIZE = 2048;
    static constexpr float       SHADOW_INTENSITY = 0.75f;

    TismaShading() = default;
    ~TismaShading();
    TismaShading(const TismaShading &) = delete;
    TismaShading &operator=(const TismaShading &) = delete;

    // The quality chosen in the preferences.
    static Quality configured_quality();
    // The quality chosen in the preferences, limited by what the OpenGL context supports (shaders 140, OpenGL 3.2).
    // Only with an initialized OpenGL context: the OpenGL info is detected once, at the first query.
    static Quality quality();
    static bool    is_supported();

    // Starts a frame of the given camera: reads the quality. The shading applies until end_frame().
    void begin_frame(const Camera &camera);
    // Renders the shadow map: the light camera fits the casters, render_casters() renders them with the given matrices
    // (the current shader is "flat", the color is not written).
    void render_shadow_map(const BoundingBoxf3 &casters, const std::function<void(const Transform3d &view, const Transform3d &projection)> &render_casters);
    // Ends the frame: the shaders used during it do not receive shadows anymore (thumbnails, picking).
    void end_frame();
    // Releases the OpenGL resources (the context must be current).
    void shutdown();

    bool per_pixel() const { return m_in_frame && m_quality != Quality::Classic; }
    bool shadows() const { return m_in_frame && m_shadow_map_valid; }

    // Sets the uniforms of the shading to the shader in use; receive_shadows binds the shadow map.
    void apply(GLShaderProgram &shader, bool receive_shadows);
    // The shader in use does not receive shadows anymore.
    void stop_shadows(GLShaderProgram &shader);
    // Shading of the toolpaths (libvgcode).
    void apply(libvgcode::Viewer &viewer) const;

    // The shading of the frame being rendered, nullptr out of a frame (used by the bed).
    static TismaShading *current() { return s_current; }

private:
    bool init_shadow_map();

    Quality      m_quality{ Quality::Classic };
    bool         m_in_frame{ false };
    unsigned int m_fbo{ 0 };
    unsigned int m_depth_texture{ 0 };
    int          m_size{ 0 };
    bool         m_shadow_map_valid{ false };
    Transform3d  m_camera_view{ Transform3d::Identity() };
    // From the eye space of the camera to the clip space of the light.
    Matrix4d     m_eye_to_shadow{ Matrix4d::Identity() };
    std::vector<GLShaderProgram*> m_shaders_with_shadows;

    static TismaShading *s_current;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_TismaShading_hpp_
