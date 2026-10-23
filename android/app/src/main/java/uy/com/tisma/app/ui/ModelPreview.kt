package uy.com.tisma.app.ui

import android.graphics.Canvas
import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import uy.com.tisma.app.core.PreviewMesh
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Where the view looks from: turned about the vertical axis ([yaw]), tilted from straight above (pitch 0) to level
 * (pitch π/2), and the zoom. Shared by the preview and the buttons that choose a view.
 */
@Stable
class PreviewCamera {
    var yaw by mutableFloatStateOf(ISO_YAW)
    var pitch by mutableFloatStateOf(ISO_PITCH)
    var zoom by mutableFloatStateOf(1f)
    /** Shift of the view on the screen, in pixels. */
    var offsetX by mutableFloatStateOf(0f)
    var offsetY by mutableFloatStateOf(0f)

    fun look(yaw: Float, pitch: Float) {
        this.yaw = yaw
        this.pitch = pitch
        reset()
    }

    /** The whole bed in view. */
    fun reset() {
        zoom = 1f
        offsetX = 0f
        offsetY = 0f
    }

    /** Zooms by [factor] keeping the point [focusX], [focusY] (from the centre of the view, in pixels) in place. */
    fun zoomAt(factor: Float, focusX: Float = 0f, focusY: Float = 0f) {
        val z = (zoom * factor).coerceIn(MIN_ZOOM, MAX_ZOOM)
        val f = z / zoom
        offsetX = focusX - (focusX - offsetX) * f
        offsetY = focusY - (focusY - offsetY) * f
        zoom = z
    }

    /** The model filling the view (it stands on the centre of the bed). */
    fun fit(mesh: PreviewMesh?, bedX: Float, bedY: Float) {
        if (mesh == null || mesh.triangleCount == 0) return reset()
        val bed = extent(max(bedX, bedY) / 2, mesh.max[2] - mesh.min[2])
        val model = extent(max(mesh.max[0] - mesh.min[0], mesh.max[1] - mesh.min[1]) / 2, mesh.max[2] - mesh.min[2])
        offsetX = 0f
        offsetY = 0f
        zoom = (bed / model.coerceAtLeast(1e-3f)).coerceIn(1f, MAX_ZOOM)
    }

    // Views of the printer, whose front is at y = 0 (-Y).
    fun iso() = look(ISO_YAW, ISO_PITCH)
    fun top() = look(0f, 0f)
    fun front() = look(0f, LEVEL)
    fun back() = look(PI, LEVEL)
    fun left() = look(PI / 2, LEVEL)
    fun right() = look(-PI / 2, LEVEL)

    companion object {
        const val ISO_YAW = -0.6f
        const val ISO_PITCH = 0.95f
        private const val PI = Math.PI.toFloat()
        const val LEVEL = PI / 2
        const val MIN_ZOOM = 0.5f
        const val MAX_ZOOM = 20f
    }
}

/** Half the size on the screen (before scaling) of what spans [half] around the centre and [height] up. */
private fun extent(half: Float, height: Float) = sqrt(2f) * half + height * 0.7f

/** A point of the model shown over it: a load, with the direction of its force (pointing to the point). */
class PreviewMarker(val point: FloatArray, val direction: FloatArray?, val color: Color)

/** Where a tap hit the model: the triangle, the point and the normal of the triangle, in the coordinates of the mesh. */
class PreviewHit(val triangle: Int, val point: FloatArray, val normal: FloatArray)

/**
 * The view of the bed and of the model standing on its centre: the model is moved by (dx, dy, dz) from the
 * coordinates of its file. Turned about the vertical axis through the centre of the bed ([yaw]), then tilted
 * ([pitch]); parallel projection.
 */
private class Projection(mesh: PreviewMesh?, val bedX: Float, val bedY: Float, yaw: Float, pitch: Float, size: Size,
                         fitModel: Boolean = false, zoom: Float = 1f, offset: Offset = Offset.Zero) {
    val dx: Float
    val dy: Float
    val dz: Float
    private val cy = cos(yaw)
    private val sy = sin(yaw)
    private val cp = cos(pitch)
    private val sp = sin(pitch)
    val scale: Float
    private val origin: Offset

    init {
        if (mesh != null && mesh.triangleCount > 0) {
            dx = bedX / 2 - (mesh.min[0] + mesh.max[0]) / 2
            dy = bedY / 2 - (mesh.min[1] + mesh.max[1]) / 2
            dz = -mesh.min[2]
        } else {
            dx = 0f; dy = 0f; dz = 0f
        }
        val modelHeight = if (mesh != null) mesh.max[2] - mesh.min[2] else 0f
        // The whole bed, or only the model (the bed around it is cut).
        val half = if (fitModel && mesh != null && mesh.triangleCount > 0)
            max(mesh.max[0] - mesh.min[0], mesh.max[1] - mesh.min[1]) / 2
        else max(bedX, bedY) / 2
        scale = zoom * 0.9f * min(size.width, size.height) / (2 * extent(half, modelHeight))
        origin = Offset(size.width / 2 + offset.x, size.height / 2 + modelHeight * scale * 0.25f + offset.y)
    }

    /** Screen x, screen y and distance from the viewer (larger: farther) of a point of the bed. */
    fun project(x: Float, y: Float, z: Float, out: FloatArray, at: Int) {
        val px = x - bedX / 2
        val py = y - bedY / 2
        val rx = px * cy - py * sy
        val ry = px * sy + py * cy
        out[at] = origin.x + rx * scale
        out[at + 1] = origin.y + (ry * cp - z * sp) * scale
        out[at + 2] = -ry * sp - z * cp
    }

    /** Screen corners and depths of all the triangles of [mesh], placed on the bed. */
    fun projectMesh(mesh: PreviewMesh): FloatArray {
        val v = mesh.vertices
        val screen = FloatArray(mesh.triangleCount * 9)
        for (i in 0 until mesh.triangleCount * 3)
            project(v[i * 3] + dx, v[i * 3 + 1] + dy, v[i * 3 + 2] + dz, screen, i * 3)
        return screen
    }

    /** The nearest triangle under a screen point, or null. */
    fun pick(mesh: PreviewMesh, at: Offset): PreviewHit? {
        val s = projectMesh(mesh)
        var best = -1
        var bestDepth = Float.MAX_VALUE
        var bu = 0f; var bv = 0f
        for (t in 0 until mesh.triangleCount) {
            val b = t * 9
            val x0 = s[b]; val y0 = s[b + 1]
            val e1x = s[b + 3] - x0; val e1y = s[b + 4] - y0
            val e2x = s[b + 6] - x0; val e2y = s[b + 7] - y0
            val det = e1x * e2y - e2x * e1y
            if (kotlin.math.abs(det) < 1e-9f) continue
            val px = at.x - x0; val py = at.y - y0
            val u = (px * e2y - e2x * py) / det
            val w = (e1x * py - px * e1y) / det
            if (u < 0f || w < 0f || u + w > 1f) continue
            val depth = s[b + 2] + u * (s[b + 5] - s[b + 2]) + w * (s[b + 8] - s[b + 2])
            if (depth < bestDepth) {
                bestDepth = depth
                best = t
                bu = u; bv = w
            }
        }
        if (best < 0) return null
        // Parallel projection: the same barycentric coordinates on the model.
        val v = mesh.vertices
        val b = best * 9
        val point = FloatArray(3) { v[b + it] + bu * (v[b + 3 + it] - v[b + it]) + bv * (v[b + 6 + it] - v[b + it]) }
        return PreviewHit(best, point, mesh.normal(best))
    }
}

/**
 * The bed of the printer and the model standing on its centre, as the slicer will place it. Dragging turns the
 * view; two fingers zoom (towards them) and move it, a double tap zooms in on its point ([camera]); a tap on the
 * model calls [onTap]. Drawn on the canvas: flat shaded triangles sorted back to front (no depth
 * buffer needed for a preview). [triangleColors]: a colour per triangle, or per corner of each triangle (3 per
 * triangle, blended), instead of [modelColor] (ARGB, 0 = the model colour). [fitModel]: the view fills with the model instead of the whole bed.
 */
@Composable
fun ModelPreview(
    mesh: PreviewMesh?,
    bedX: Float,
    bedY: Float,
    modelColor: Color,
    bedColor: Color,
    gridColor: Color,
    modifier: Modifier = Modifier,
    triangleColors: IntArray? = null,
    markers: List<PreviewMarker> = emptyList(),
    onTap: ((PreviewHit) -> Unit)? = null,
    fitModel: Boolean = false,
    camera: PreviewCamera = remember { PreviewCamera() },
) {
    val paint = remember { Paint().apply { isAntiAlias = true } }
    val currentMesh by rememberUpdatedState(mesh)
    val currentTap by rememberUpdatedState(onTap)

    Canvas(
        modifier
            // Fitted to the model, the bed goes past the view.
            .clipToBounds()
            .pointerInput(camera) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    do {
                        val event = awaitPointerEvent()
                        val fingers = event.changes.count { it.pressed }
                        if (fingers >= 2) {
                            // Pinch: zoom towards the fingers, and move with them.
                            val c = event.calculateCentroid()
                            val pan = event.calculatePan()
                            camera.zoomAt(event.calculateZoom(), c.x - size.width / 2f, c.y - size.height / 2f)
                            camera.offsetX += pan.x
                            camera.offsetY += pan.y
                        } else if (fingers == 1) {
                            val d = event.changes.first { it.pressed }.positionChange()
                            camera.yaw += d.x * 0.01f
                            camera.pitch = (camera.pitch + d.y * 0.01f).coerceIn(0f, PreviewCamera.LEVEL)
                        }
                        event.changes.forEach { if (it.positionChange() != Offset.Zero) it.consume() }
                    } while (event.changes.any { it.pressed })
                }
            }
            .pointerInput(bedX, bedY, fitModel, camera) {
                detectTapGestures(
                    onDoubleTap = { at -> camera.zoomAt(2f, at.x - size.width / 2f, at.y - size.height / 2f) },
                    onTap = tap@{ at ->
                        val m = currentMesh ?: return@tap
                        val tap = currentTap ?: return@tap
                        val size = Size(this.size.width.toFloat(), this.size.height.toFloat())
                        Projection(m, bedX, bedY, camera.yaw, camera.pitch, size, fitModel, camera.zoom,
                            Offset(camera.offsetX, camera.offsetY)).pick(m, at)?.let(tap)
                    },
                )
            }
    ) {
        val proj = Projection(mesh, bedX, bedY, camera.yaw, camera.pitch, size, fitModel, camera.zoom, Offset(camera.offsetX, camera.offsetY))
        val cy = cos(camera.yaw); val sy = sin(camera.yaw)

        // Bed: the plate and a 10 mm grid.
        val p = FloatArray(12)
        proj.project(0f, 0f, 0f, p, 0); proj.project(bedX, 0f, 0f, p, 3); proj.project(bedX, bedY, 0f, p, 6); proj.project(0f, bedY, 0f, p, 9)
        drawIntoCanvas { canvas ->
            val bed = floatArrayOf(p[0], p[1], p[3], p[4], p[6], p[7], p[0], p[1], p[6], p[7], p[9], p[10])
            val c = bedColor.toArgb()
            canvas.nativeCanvas.drawVertices(Canvas.VertexMode.TRIANGLES, bed.size, bed, 0, null, 0,
                IntArray(6) { c }, 0, null, 0, 0, paint)
        }
        val line = FloatArray(6)
        var g = 0f
        while (g <= bedX + 0.01f) {
            proj.project(g, 0f, 0f, line, 0); proj.project(g, bedY, 0f, line, 3)
            drawLine(gridColor, Offset(line[0], line[1]), Offset(line[3], line[4]), strokeWidth = 1f)
            g += 10f
        }
        g = 0f
        while (g <= bedY + 0.01f) {
            proj.project(0f, g, 0f, line, 0); proj.project(bedX, g, 0f, line, 3)
            drawLine(gridColor, Offset(line[0], line[1]), Offset(line[3], line[4]), strokeWidth = 1f)
            g += 10f
        }
        if (mesh == null || mesh.triangleCount == 0) return@Canvas

        // Model: projected corners, a shade per triangle from its normal, back to front.
        val n = mesh.triangleCount
        val screen = proj.projectMesh(mesh)
        val depth = FloatArray(n) { screen[it * 9 + 2] + screen[it * 9 + 5] + screen[it * 9 + 8] }
        // Light from above and in front of the viewer, in world coordinates.
        val lx = sy * 0.4f; val ly = cy * 0.4f; val lz = 0.9f
        val order = (0 until n).sortedBy { -depth[it] }
        val xy = FloatArray(n * 6)
        val colors = IntArray(n * 3)
        for ((i, t) in order.withIndex()) {
            for (k in 0..2) {
                xy[i * 6 + k * 2] = screen[t * 9 + k * 3]
                xy[i * 6 + k * 2 + 1] = screen[t * 9 + k * 3 + 1]
            }
            val nn = mesh.normals
            val s = 0.35f + 0.65f * (nn[t * 3] * lx + nn[t * 3 + 1] * ly + nn[t * 3 + 2] * lz).coerceIn(0f, 1f)
            val perCorner = triangleColors != null && triangleColors.size == n * 3
            for (k in 0..2) {
                val custom = triangleColors?.getOrNull(if (perCorner) t * 3 + k else t) ?: 0
                val base = if (custom != 0) Color(custom) else modelColor
                colors[i * 3 + k] = Color(base.red * s, base.green * s, base.blue * s).toArgb()
            }
        }
        drawIntoCanvas { canvas ->
            canvas.nativeCanvas.drawVertices(Canvas.VertexMode.TRIANGLES, xy.size, xy, 0, null, 0, colors, 0, null, 0, 0, paint)
        }

        // Markers: a dot on the point and an arrow of the force towards it.
        val size3 = max(mesh.max[0] - mesh.min[0], max(mesh.max[1] - mesh.min[1], mesh.max[2] - mesh.min[2]))
        val q = FloatArray(6)
        for (m in markers) {
            proj.project(m.point[0] + proj.dx, m.point[1] + proj.dy, m.point[2] + proj.dz, q, 0)
            val tip = Offset(q[0], q[1])
            m.direction?.let { d ->
                val len = 0.35f * size3
                proj.project(m.point[0] - d[0] * len + proj.dx, m.point[1] - d[1] * len + proj.dy, m.point[2] - d[2] * len + proj.dz, q, 3)
                val tail = Offset(q[3], q[4])
                drawLine(m.color, tail, tip, strokeWidth = 6f)
                // Head: two short strokes back from the tip.
                val ax = tail.x - tip.x; val ay = tail.y - tip.y
                val al = sqrt(ax * ax + ay * ay).coerceAtLeast(1e-3f)
                val ux = ax / al * 28f; val uy = ay / al * 28f
                drawLine(m.color, tip, Offset(tip.x + ux * 0.87f - uy * 0.5f, tip.y + uy * 0.87f + ux * 0.5f), strokeWidth = 6f)
                drawLine(m.color, tip, Offset(tip.x + ux * 0.87f + uy * 0.5f, tip.y + uy * 0.87f - ux * 0.5f), strokeWidth = 6f)
            }
            drawCircle(m.color, radius = 10f, center = tip)
            drawCircle(Color.White, radius = 10f, center = tip, style = Stroke(width = 3f))
        }
    }
}
