package uy.com.tisma.app.ui

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import uy.com.tisma.app.core.PreviewMesh
import kotlin.random.Random

class ProjectionTest {
    // A box 20 × 10 × 30 standing at (5, 5, 2) in the coordinates of its file: two triangles on top, two on the front.
    private val box = PreviewMesh(floatArrayOf(
        5f, 5f, 32f, 25f, 5f, 32f, 25f, 15f, 32f,
        5f, 5f, 32f, 25f, 15f, 32f, 5f, 15f, 32f,
        5f, 5f, 2f, 25f, 5f, 2f, 25f, 5f, 32f,
        5f, 5f, 2f, 25f, 5f, 32f, 5f, 5f, 32f,
    ))
    private val size = Size(1080f, 1600f)

    private fun projection(yaw: Float, pitch: Float, fit: Boolean = false) =
        Projection(box, 220f, 200f, yaw, pitch, size, fit, 1.7f, Offset(30f, -40f))

    // The point through a column-major 4 × 4 matrix, then from clip coordinates to the screen (y down) and the depth.
    private fun throughMatrix(m: FloatArray, x: Float, y: Float, z: Float): FloatArray {
        val c = FloatArray(4) { r -> m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r] }
        return floatArrayOf((c[0] / c[3] + 1) * size.width / 2, (1 - c[1] / c[3]) * size.height / 2, c[2] / c[3])
    }

    @Test
    fun theGraphicsCardDrawsWhereTapsAndMarkersAre() {
        val random = Random(7)
        for (view in 0 until 20) {
            val p = projection(random.nextFloat() * 6.3f, random.nextFloat() * PreviewCamera.LEVEL, view % 2 == 0)
            val bed = p.clipMatrix(false)
            val model = p.clipMatrix(true)
            val out = FloatArray(3)
            repeat(50) {
                val x = random.nextFloat() * 220f; val y = random.nextFloat() * 200f; val z = random.nextFloat() * 30f
                p.project(x, y, z, out, 0)
                val g = throughMatrix(bed, x, y, z)
                assertEquals(out[0], g[0], 0.05f)
                assertEquals(out[1], g[1], 0.05f)
                assertTrue(g[2] > -1f && g[2] < 1f)
                // A point of the model (in the coordinates of its file), moved onto the bed by (dx, dy, dz).
                val mx = 5f + random.nextFloat() * 20f; val my = 5f + random.nextFloat() * 10f; val mz = 2f + random.nextFloat() * 30f
                p.project(mx + p.dx, my + p.dy, mz + p.dz, out, 0)
                val h = throughMatrix(model, mx, my, mz)
                assertEquals(out[0], h[0], 0.05f)
                assertEquals(out[1], h[1], 0.05f)
                // Inside the range of depth of the card.
                assertTrue(h[2] > -1f && h[2] < 1f)
            }
            // Depth: the farther point (by project) is the farther one on the card.
            val a = FloatArray(3); val b = FloatArray(3)
            p.project(10f, 20f, 0f, a, 0); p.project(200f, 180f, 25f, b, 0)
            val ga = throughMatrix(bed, 10f, 20f, 0f); val gb = throughMatrix(bed, 200f, 180f, 25f)
            assertEquals(a[2] < b[2], ga[2] < gb[2])
        }
    }

    @Test
    fun aTrueViewNotAMirror() {
        val out = FloatArray(6)
        // From the front: x to the right, z up, the front of the bed (y = 0) nearer than its back.
        val front = projection(0f, PreviewCamera.LEVEL)
        front.project(0f, 0f, 0f, out, 0); front.project(100f, 0f, 0f, out, 3)
        assertTrue(out[3] > out[0])
        front.project(0f, 0f, 50f, out, 3)
        assertTrue(out[4] < out[1])
        front.project(0f, 150f, 0f, out, 3)
        assertTrue(out[2] < out[5])
        // From the top: x to the right, the front at the bottom of the screen, as on the desktop.
        val top = projection(0f, 0f)
        top.project(0f, 0f, 0f, out, 0); top.project(0f, 150f, 0f, out, 3)
        assertTrue(out[1] > out[4])
        top.project(100f, 0f, 0f, out, 3)
        assertTrue(out[3] > out[0])
        // Iso, as on the desktop: from the front left, so the front left corner is the nearest one.
        val iso = projection(PreviewCamera.ISO_YAW, PreviewCamera.ISO_PITCH)
        iso.project(0f, 0f, 0f, out, 0); iso.project(220f, 200f, 0f, out, 3)
        assertTrue(out[2] < out[5])
        iso.project(220f, 0f, 0f, out, 3)
        assertTrue(out[2] < out[5])
    }

    @Test
    fun eyeRotationIsARotationTowardsTheViewer() {
        val p = projection(0.9f, 0.6f)
        val m = p.eyeRotation()
        // Columns: the directions x, y, z of the bed in the eye. Rows orthonormal, right handed.
        val right = floatArrayOf(m[0], m[3], m[6]); val up = floatArrayOf(m[1], m[4], m[7]); val toward = floatArrayOf(m[2], m[5], m[8])
        fun dot(a: FloatArray, b: FloatArray) = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
        assertEquals(1f, dot(right, right), 1e-5f); assertEquals(1f, dot(up, up), 1e-5f); assertEquals(1f, dot(toward, toward), 1e-5f)
        assertEquals(0f, dot(right, up), 1e-5f); assertEquals(0f, dot(up, toward), 1e-5f)
        val cross = floatArrayOf(right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2], right[0] * up[1] - right[1] * up[0])
        assertEquals(1f, dot(cross, toward), 1e-5f)
        // Moving towards the viewer makes a point nearer.
        val a = FloatArray(3); val b = FloatArray(3)
        p.project(100f, 100f, 10f, a, 0)
        p.project(100f + toward[0] * 5, 100f + toward[1] * 5, 10f + toward[2] * 5, b, 0)
        assertEquals(a[2] - 5f, b[2], 1e-3f)
    }

    @Test
    fun aTapPicksTheNearestTriangle() {
        val p = projection(PreviewCamera.ISO_YAW, PreviewCamera.ISO_PITCH)
        // The middle of the top of the box, moved onto the bed.
        val out = FloatArray(3)
        p.project(15f + p.dx, 8f + p.dy, 32f + p.dz, out, 0)
        val hit = p.pick(box, Offset(out[0], out[1]))
        assertNotNull(hit)
        assertTrue(hit!!.triangle in 0..1)
        assertEquals(32f, hit.point[2], 1e-3f)
        assertEquals(1f, hit.normal[2], 1e-5f)
    }
}
