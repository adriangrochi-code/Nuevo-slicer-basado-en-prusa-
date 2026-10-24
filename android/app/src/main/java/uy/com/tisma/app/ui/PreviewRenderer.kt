package uy.com.tisma.app.ui

import android.content.Context
import android.graphics.PixelFormat
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.findViewTreeLifecycleOwner
import uy.com.tisma.app.core.PreviewMesh
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.opengles.GL10
import kotlin.math.roundToInt

/** What the preview shows: the model, its colours, the bed and where the camera is (colours ARGB). */
class PreviewScene(
    val mesh: PreviewMesh?,
    val triangleColors: IntArray?,
    val modelColor: Int,
    val bedX: Float,
    val bedY: Float,
    val bedColor: Int,
    val gridColor: Int,
    val background: Int,
    val yaw: Float,
    val pitch: Float,
    val zoom: Float,
    val offset: Offset,
    val fitModel: Boolean,
)

/**
 * The preview drawn by the graphics card, as the desktop program draws the plate: every triangle of the model with
 * a depth buffer, lit per pixel by the lights of the desktop shader (resources/shaders/140/gouraud.fs: a light from
 * the top left, one from the front right and an ambient one), on the bed with its 10 mm grid. Smoothed edges
 * (4 samples per pixel) when the phone has them. Drawn only when the scene changes.
 */
class PreviewSurface(context: Context) : GLSurfaceView(context) {
    private val renderer = PreviewRenderer()

    init {
        setEGLContextClientVersion(2)
        setEGLConfigChooser(ConfigChooser())
        holder.setFormat(PixelFormat.RGBA_8888)
        preserveEGLContextOnPause = true
        setRenderer(renderer)
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    fun show(scene: PreviewScene) {
        renderer.scene = scene
        requestRender()
    }

    // Paused with the screen: the drawing thread stops, what is on the graphics card stays for the return.
    private var lifecycle: Lifecycle? = null
    private val observer = LifecycleEventObserver { _, event ->
        when (event) {
            Lifecycle.Event.ON_RESUME -> onResume()
            Lifecycle.Event.ON_PAUSE -> onPause()
            else -> Unit
        }
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        lifecycle = findViewTreeLifecycleOwner()?.lifecycle
        lifecycle?.addObserver(observer)
    }

    override fun onDetachedFromWindow() {
        lifecycle?.removeObserver(observer)
        lifecycle = null
        super.onDetachedFromWindow()
    }
}

// 4 samples per pixel, else none; a depth buffer of 24 bits, else 16.
private class ConfigChooser : GLSurfaceView.EGLConfigChooser {
    override fun chooseConfig(egl: EGL10, display: EGLDisplay): EGLConfig {
        for (samples in intArrayOf(4, 0)) for (depth in intArrayOf(24, 16)) {
            val attributes = intArrayOf(
                EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_ALPHA_SIZE, 8,
                EGL10.EGL_DEPTH_SIZE, depth, EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL10.EGL_SAMPLE_BUFFERS, if (samples > 0) 1 else 0, EGL10.EGL_SAMPLES, samples,
                EGL10.EGL_NONE,
            )
            val configs = arrayOfNulls<EGLConfig>(1)
            val found = IntArray(1)
            if (egl.eglChooseConfig(display, attributes, configs, 1, found) && found[0] > 0) return configs[0]!!
        }
        throw IllegalStateException("No OpenGL ES 2 configuration")
    }

    private companion object {
        const val EGL_OPENGL_ES2_BIT = 4
    }
}

private class PreviewRenderer : GLSurfaceView.Renderer {
    @Volatile
    var scene: PreviewScene? = null

    private var width = 1
    private var height = 1
    private var lit = 0
    private var flat = 0
    private val buffers = IntArray(3)
    // What is on the graphics card: the model and its colours (by identity), the bed.
    private var meshOnCard: PreviewMesh? = null
    private var vertexCount = 0
    private var colorsOnCard: IntArray? = null
    private var colorOfModel = 0
    private var bedOnCard = Pair(-1f, -1f)
    private var gridVertices = 0

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        lit = program(LIT_VERTEX, LIT_FRAGMENT)
        flat = program(FLAT_VERTEX, FLAT_FRAGMENT)
        GLES20.glGenBuffers(3, buffers, 0)
        // A new context: nothing is on the card.
        meshOnCard = null
        colorsOnCard = null
        bedOnCard = Pair(-1f, -1f)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        this.width = width
        this.height = height
        GLES20.glViewport(0, 0, width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        val s = scene
        val bg = s?.background ?: 0
        GLES20.glClearColor(red(bg), green(bg), blue(bg), 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        if (s == null) return
        val mesh = s.mesh?.takeIf { it.triangleCount > 0 }
        upload(s, mesh)
        val proj = Projection(mesh, s.bedX, s.bedY, s.yaw, s.pitch, Size(width.toFloat(), height.toFloat()), s.fitModel,
            s.zoom, s.offset)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        GLES20.glDepthFunc(GLES20.GL_LEQUAL)

        // The bed, pushed back a little so that the grid and the bottom of the model on it win.
        GLES20.glUseProgram(flat)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(flat, "u_mvp"), 1, false, proj.clipMatrix(false), 0)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buffers[2])
        GLES20.glEnableVertexAttribArray(0)
        GLES20.glVertexAttribPointer(0, 3, GLES20.GL_FLOAT, false, 12, 0)
        val color = GLES20.glGetUniformLocation(flat, "u_color")
        uniformColor(color, s.bedColor)
        GLES20.glEnable(GLES20.GL_POLYGON_OFFSET_FILL)
        GLES20.glPolygonOffset(1f, 1f)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 6)
        GLES20.glDisable(GLES20.GL_POLYGON_OFFSET_FILL)
        uniformColor(color, s.gridColor)
        GLES20.glDrawArrays(GLES20.GL_LINES, 6, gridVertices)
        GLES20.glDisableVertexAttribArray(0)
        if (mesh == null) return

        // The model: positions and normals (bytes) interleaved, colours of the corners or one colour.
        GLES20.glUseProgram(lit)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(lit, "u_mvp"), 1, false, proj.clipMatrix(true), 0)
        GLES20.glUniformMatrix3fv(GLES20.glGetUniformLocation(lit, "u_eye"), 1, false, proj.eyeRotation(), 0)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buffers[0])
        GLES20.glEnableVertexAttribArray(0)
        GLES20.glVertexAttribPointer(0, 3, GLES20.GL_FLOAT, false, 16, 0)
        GLES20.glEnableVertexAttribArray(1)
        GLES20.glVertexAttribPointer(1, 3, GLES20.GL_BYTE, true, 16, 12)
        if (colorsOnCard != null) {
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buffers[1])
            GLES20.glEnableVertexAttribArray(2)
            GLES20.glVertexAttribPointer(2, 4, GLES20.GL_UNSIGNED_BYTE, true, 4, 0)
        } else {
            GLES20.glDisableVertexAttribArray(2)
            GLES20.glVertexAttrib4f(2, red(s.modelColor), green(s.modelColor), blue(s.modelColor), 1f)
        }
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, vertexCount)
        GLES20.glDisableVertexAttribArray(0)
        GLES20.glDisableVertexAttribArray(1)
        GLES20.glDisableVertexAttribArray(2)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
    }

    private fun upload(s: PreviewScene, mesh: PreviewMesh?) {
        if (bedOnCard != Pair(s.bedX, s.bedY)) {
            bedOnCard = Pair(s.bedX, s.bedY)
            val bed = bedVertices(s.bedX, s.bedY)
            gridVertices = bed.size / 3 - 6
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buffers[2])
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, bed.size * 4, floats(bed), GLES20.GL_STATIC_DRAW)
        }
        if (mesh !== meshOnCard) {
            meshOnCard = mesh
            colorsOnCard = null
            vertexCount = 0
            if (mesh != null) {
                val data = modelVertices(mesh)
                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buffers[0])
                GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, data.capacity(), data, GLES20.GL_STATIC_DRAW)
                vertexCount = mesh.triangleCount * 3
            }
        }
        val colors = s.triangleColors?.takeIf { mesh != null && (it.size == mesh.triangleCount || it.size == vertexCount) }
        if (colors == null) {
            colorsOnCard = null
        } else if (colors !== colorsOnCard || s.modelColor != colorOfModel) {
            colorsOnCard = colors
            colorOfModel = s.modelColor
            val data = cornerColors(colors, vertexCount, s.modelColor)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buffers[1])
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, data.capacity(), data, GLES20.GL_STATIC_DRAW)
        }
    }

    private fun uniformColor(location: Int, argb: Int) =
        GLES20.glUniform4f(location, red(argb), green(argb), blue(argb), 1f)
}

/** The bed (two triangles), then the lines of its 10 mm grid and its edges: x, y, z of each vertex. */
internal fun bedVertices(bedX: Float, bedY: Float): FloatArray {
    val out = ArrayList<Float>()
    fun add(vararg p: Float) = p.forEach { out += it }
    add(0f, 0f, 0f, bedX, 0f, 0f, bedX, bedY, 0f, 0f, 0f, 0f, bedX, bedY, 0f, 0f, bedY, 0f)
    var g = 0f
    while (g < bedX - 0.01f) { add(g, 0f, 0f, g, bedY, 0f); g += 10f }
    add(bedX, 0f, 0f, bedX, bedY, 0f)
    g = 0f
    while (g < bedY - 0.01f) { add(0f, g, 0f, bedX, g, 0f); g += 10f }
    add(0f, bedY, 0f, bedX, bedY, 0f)
    return out.toFloatArray()
}

/** 16 bytes per corner: x, y, z (floats), then the normal of its triangle in signed bytes and one byte of padding. */
internal fun modelVertices(mesh: PreviewMesh): ByteBuffer {
    val v = mesh.vertices
    val n = mesh.normals
    val out = ByteBuffer.allocateDirect(mesh.triangleCount * 3 * 16).order(ByteOrder.nativeOrder())
    for (t in 0 until mesh.triangleCount) {
        val nx = (n[t * 3] * 127f).roundToInt().toByte()
        val ny = (n[t * 3 + 1] * 127f).roundToInt().toByte()
        val nz = (n[t * 3 + 2] * 127f).roundToInt().toByte()
        for (k in 0..2) {
            val b = t * 9 + k * 3
            out.putFloat(v[b]).putFloat(v[b + 1]).putFloat(v[b + 2])
            out.put(nx).put(ny).put(nz).put(0)
        }
    }
    out.flip()
    return out
}

/** RGBA bytes of each corner from a colour per triangle or per corner (ARGB; 0 = [model]). */
internal fun cornerColors(colors: IntArray, corners: Int, model: Int): ByteBuffer {
    val out = ByteBuffer.allocateDirect(corners * 4)
    val perCorner = colors.size == corners
    for (c in 0 until corners) {
        val argb = colors[if (perCorner) c else c / 3].takeIf { it != 0 } ?: model
        out.put((argb shr 16).toByte()).put((argb shr 8).toByte()).put(argb.toByte()).put(-1)
    }
    out.flip()
    return out
}

private fun floats(values: FloatArray): ByteBuffer {
    val out = ByteBuffer.allocateDirect(values.size * 4).order(ByteOrder.nativeOrder())
    out.asFloatBuffer().put(values)
    return out
}

private fun red(argb: Int) = ((argb shr 16) and 0xff) / 255f
private fun green(argb: Int) = ((argb shr 8) and 0xff) / 255f
private fun blue(argb: Int) = (argb and 0xff) / 255f

private fun program(vertex: String, fragment: String): Int {
    fun shader(type: Int, source: String): Int {
        val s = GLES20.glCreateShader(type)
        GLES20.glShaderSource(s, source)
        GLES20.glCompileShader(s)
        val ok = IntArray(1)
        GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0)
        check(ok[0] != 0) { "Preview shader: " + GLES20.glGetShaderInfoLog(s) }
        return s
    }
    val p = GLES20.glCreateProgram()
    GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, vertex))
    GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, fragment))
    // The attributes at fixed places: the position at 0 in both programs.
    GLES20.glBindAttribLocation(p, 0, "a_position")
    GLES20.glBindAttribLocation(p, 1, "a_normal")
    GLES20.glBindAttribLocation(p, 2, "a_color")
    GLES20.glLinkProgram(p)
    val ok = IntArray(1)
    GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0)
    check(ok[0] != 0) { "Preview program: " + GLES20.glGetProgramInfoLog(p) }
    return p
}

private const val FLAT_VERTEX = """
uniform mat4 u_mvp;
attribute vec3 a_position;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
"""

private const val FLAT_FRAGMENT = """
precision mediump float;
uniform vec4 u_color;
void main() {
    gl_FragColor = u_color;
}
"""

private const val LIT_VERTEX = """
uniform mat4 u_mvp;
uniform mat3 u_eye;
attribute vec3 a_position;
attribute vec3 a_normal;
attribute vec4 a_color;
varying vec3 v_normal;
varying vec4 v_color;
void main() {
    v_normal = u_eye * a_normal;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
"""

// The lights of the desktop program (resources/shaders/140/gouraud.fs), in the coordinates of the eye; the view is a
// parallel projection, so the eye looks along -z everywhere. Both sides of a triangle are lit, as a mesh may be open.
private const val LIT_FRAGMENT = """
precision mediump float;
const float INTENSITY_CORRECTION = 0.6;
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
const float LIGHT_TOP_DIFFUSE = 0.8 * INTENSITY_CORRECTION;
const float LIGHT_TOP_SPECULAR = 0.125 * INTENSITY_CORRECTION;
const float LIGHT_TOP_SHININESS = 20.0;
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
const float LIGHT_FRONT_DIFFUSE = 0.3 * INTENSITY_CORRECTION;
const float INTENSITY_AMBIENT = 0.3;
varying vec3 v_normal;
varying vec4 v_color;
void main() {
    vec3 n = normalize(v_normal);
    if (n.z < 0.0)
        n = -n;
    float diffuse = INTENSITY_AMBIENT + max(dot(n, LIGHT_TOP_DIR), 0.0) * LIGHT_TOP_DIFFUSE
        + max(dot(n, LIGHT_FRONT_DIR), 0.0) * LIGHT_FRONT_DIFFUSE;
    float specular = LIGHT_TOP_SPECULAR * pow(max(reflect(-LIGHT_TOP_DIR, n).z, 0.0), LIGHT_TOP_SHININESS);
    gl_FragColor = vec4(vec3(specular) + v_color.rgb * diffuse, 1.0);
}
"""
