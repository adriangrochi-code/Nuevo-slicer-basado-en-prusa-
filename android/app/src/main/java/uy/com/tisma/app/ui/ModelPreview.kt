package uy.com.tisma.app.ui

import android.graphics.Canvas
import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import uy.com.tisma.app.core.PreviewMesh
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * The bed of the printer and the model standing on its centre, as the slicer will place it. Dragging turns the
 * view. Drawn on the canvas: flat shaded triangles sorted back to front (no depth buffer needed for a preview).
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
) {
    var yaw by remember { mutableFloatStateOf(-0.6f) }
    var pitch by remember { mutableFloatStateOf(0.95f) }
    val paint = remember { Paint().apply { isAntiAlias = true } }

    Canvas(
        modifier.pointerInput(Unit) {
            detectDragGestures { change, drag ->
                change.consume()
                yaw += drag.x * 0.01f
                pitch = (pitch + drag.y * 0.01f).coerceIn(0.1f, 1.5f)
            }
        }
    ) {
        // Model: centred on the bed, standing on it.
        val dx: Float
        val dy: Float
        val dz: Float
        if (mesh != null && mesh.triangleCount > 0) {
            dx = bedX / 2 - (mesh.min[0] + mesh.max[0]) / 2
            dy = bedY / 2 - (mesh.min[1] + mesh.max[1]) / 2
            dz = -mesh.min[2]
        } else {
            dx = 0f; dy = 0f; dz = 0f
        }
        val modelHeight = if (mesh != null) mesh.max[2] - mesh.min[2] else 0f
        // View: turn about the vertical axis through the bed centre, then tilt.
        val cy = cos(yaw); val sy = sin(yaw); val cp = cos(pitch); val sp = sin(pitch)
        val half = max(bedX, bedY) / 2
        val extent = sqrt(2f) * half + modelHeight * 0.7f
        val scale = 0.9f * min(size.width, size.height) / (2 * extent)
        val origin = Offset(size.width / 2, size.height / 2 + modelHeight * scale * 0.25f)
        // Screen x, screen y and distance from the viewer (larger: farther) of a world point.
        fun project(x: Float, y: Float, z: Float, out: FloatArray, at: Int) {
            val px = x - bedX / 2
            val py = y - bedY / 2
            val rx = px * cy - py * sy
            val ry = px * sy + py * cy
            out[at] = origin.x + rx * scale
            out[at + 1] = origin.y + (ry * cp - z * sp) * scale
            out[at + 2] = -ry * sp - z * cp
        }

        // Bed: the plate and a 10 mm grid.
        val p = FloatArray(12)
        project(0f, 0f, 0f, p, 0); project(bedX, 0f, 0f, p, 3); project(bedX, bedY, 0f, p, 6); project(0f, bedY, 0f, p, 9)
        drawIntoCanvas { canvas ->
            val bed = floatArrayOf(p[0], p[1], p[3], p[4], p[6], p[7], p[0], p[1], p[6], p[7], p[9], p[10])
            val c = bedColor.toArgb()
            canvas.nativeCanvas.drawVertices(Canvas.VertexMode.TRIANGLES, bed.size, bed, 0, null, 0,
                IntArray(6) { c }, 0, null, 0, 0, paint)
        }
        val line = FloatArray(6)
        var g = 0f
        while (g <= bedX + 0.01f) {
            project(g, 0f, 0f, line, 0); project(g, bedY, 0f, line, 3)
            drawLine(gridColor, Offset(line[0], line[1]), Offset(line[3], line[4]), strokeWidth = 1f)
            g += 10f
        }
        g = 0f
        while (g <= bedY + 0.01f) {
            project(0f, g, 0f, line, 0); project(bedX, g, 0f, line, 3)
            drawLine(gridColor, Offset(line[0], line[1]), Offset(line[3], line[4]), strokeWidth = 1f)
            g += 10f
        }
        if (mesh == null || mesh.triangleCount == 0) return@Canvas

        // Model: projected corners, a shade per triangle from its normal, back to front.
        val v = mesh.vertices
        val n = mesh.triangleCount
        val screen = FloatArray(n * 9)
        val depth = FloatArray(n)
        val shade = FloatArray(n)
        // Light from above and in front of the viewer, in world coordinates.
        val lx = sy * 0.4f; val ly = cy * 0.4f; val lz = 0.9f
        for (t in 0 until n) {
            val b = t * 9
            for (k in 0..2)
                project(v[b + k * 3] + dx, v[b + k * 3 + 1] + dy, v[b + k * 3 + 2] + dz, screen, b + k * 3)
            depth[t] = screen[b + 2] + screen[b + 5] + screen[b + 8]
            val ax = v[b + 3] - v[b]; val ay = v[b + 4] - v[b + 1]; val az = v[b + 5] - v[b + 2]
            val bx = v[b + 6] - v[b]; val by = v[b + 7] - v[b + 1]; val bz = v[b + 8] - v[b + 2]
            val nx = ay * bz - az * by; val ny = az * bx - ax * bz; val nz = ax * by - ay * bx
            val len = sqrt(nx * nx + ny * ny + nz * nz).coerceAtLeast(1e-12f)
            shade[t] = 0.35f + 0.65f * ((nx * lx + ny * ly + nz * lz) / len).coerceIn(0f, 1f)
        }
        // Farthest first.
        val order = (0 until n).sortedBy { -depth[it] }
        val xy = FloatArray(n * 6)
        val colors = IntArray(n * 3)
        val base = modelColor
        for ((i, t) in order.withIndex()) {
            for (k in 0..2) {
                xy[i * 6 + k * 2] = screen[t * 9 + k * 3]
                xy[i * 6 + k * 2 + 1] = screen[t * 9 + k * 3 + 1]
            }
            val s = shade[t]
            val c = Color(base.red * s, base.green * s, base.blue * s).toArgb()
            colors[i * 3] = c; colors[i * 3 + 1] = c; colors[i * 3 + 2] = c
        }
        drawIntoCanvas { canvas ->
            canvas.nativeCanvas.drawVertices(Canvas.VertexMode.TRIANGLES, xy.size, xy, 0, null, 0, colors, 0, null, 0, 0, paint)
        }
    }
}
