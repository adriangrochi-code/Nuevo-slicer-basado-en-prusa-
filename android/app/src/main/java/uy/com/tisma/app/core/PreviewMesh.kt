package uy.com.tisma.app.core

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale
import kotlin.math.cos
import kotlin.math.sqrt

/**
 * Triangles of a model for the preview on screen: x, y, z of the 3 corners of each triangle in [vertices], at most
 * [MAX_TRIANGLES] (larger models are sampled evenly). Read here from STL and OBJ files; the slicing core reads the
 * model itself, this is only for showing it.
 */
class PreviewMesh(val vertices: FloatArray) {
    val triangleCount get() = vertices.size / 9

    /** Smallest and largest x, y, z. */
    val min = FloatArray(3) { Float.MAX_VALUE }
    val max = FloatArray(3) { -Float.MAX_VALUE }

    init {
        for (i in vertices.indices) {
            val axis = i % 3
            min[axis] = minOf(min[axis], vertices[i])
            max[axis] = maxOf(max[axis], vertices[i])
        }
    }

    /** Unit normals of the triangles (by the order of their corners, outwards for a closed mesh): x, y, z of each. */
    val normals: FloatArray by lazy {
        val v = vertices
        val out = FloatArray(triangleCount * 3)
        for (t in 0 until triangleCount) {
            val b = t * 9
            val ax = v[b + 3] - v[b]; val ay = v[b + 4] - v[b + 1]; val az = v[b + 5] - v[b + 2]
            val bx = v[b + 6] - v[b]; val by = v[b + 7] - v[b + 1]; val bz = v[b + 8] - v[b + 2]
            val nx = ay * bz - az * by; val ny = az * bx - ax * bz; val nz = ax * by - ay * bx
            val len = sqrt(nx * nx + ny * ny + nz * nz).coerceAtLeast(1e-20f)
            out[t * 3] = nx / len; out[t * 3 + 1] = ny / len; out[t * 3 + 2] = nz / len
        }
        out
    }

    /** Unit normal of triangle [t]. */
    fun normal(t: Int): FloatArray = normals.copyOfRange(t * 3, t * 3 + 3)

    /** Centre of triangle [t]. */
    fun centroid(t: Int): FloatArray {
        val b = t * 9
        return FloatArray(3) { (vertices[b + it] + vertices[b + 3 + it] + vertices[b + 6 + it]) / 3 }
    }

    // Triangles sharing each edge, by the corners of the edge (the same point in two triangles is the same float).
    private val neighbors: Array<IntArray> by lazy {
        val byEdge = HashMap<List<Float>, MutableList<Int>>()
        for (t in 0 until triangleCount) for (k in 0..2) byEdge.getOrPut(edgeKey(t, k)) { ArrayList(2) } += t
        Array(triangleCount) { t -> IntArray(3) { k -> byEdge[edgeKey(t, k)]!!.firstOrNull { it != t } ?: -1 } }
    }

    private fun edgeKey(t: Int, k: Int): List<Float> {
        val a = t * 9 + k * 3
        val b = t * 9 + ((k + 1) % 3) * 3
        val p = listOf(vertices[a], vertices[a + 1], vertices[a + 2])
        val q = listOf(vertices[b], vertices[b + 1], vertices[b + 2])
        // The same edge from both triangles: the smaller corner first.
        return if (compare(p, q) <= 0) p + q else q + p
    }

    private fun compare(p: List<Float>, q: List<Float>): Int {
        for (i in 0..2) if (p[i] != q[i]) return p[i].compareTo(q[i])
        return 0
    }

    /**
     * The flat face around triangle [t]: the connected triangles whose normals are within 3° of its own, as the
     * desktop program picks a face (GLGizmoEngineering::pick_face()). The core picks the same face on the full mesh.
     */
    fun flatRegion(t: Int): IntArray {
        val n = normal(t)
        val limit = cos(Math.toRadians(3.0)).toFloat()
        val seen = BooleanArray(triangleCount)
        val out = ArrayList<Int>()
        val queue = ArrayDeque<Int>()
        queue += t
        seen[t] = true
        while (queue.isNotEmpty()) {
            val f = queue.removeFirst()
            out += f
            for (m in neighbors[f]) {
                if (m < 0 || seen[m]) continue
                if (normals[m * 3] * n[0] + normals[m * 3 + 1] * n[1] + normals[m * 3 + 2] * n[2] > limit) {
                    seen[m] = true
                    queue += m
                }
            }
        }
        return out.toIntArray()
    }

    /** The faces resting on the bed: facing down at the lowest height (as the core finds them). */
    fun bedFaces(): IntArray = (0 until triangleCount).filter { t ->
        val b = t * 9
        normals[t * 3 + 2] < -0.999f && maxOf(vertices[b + 2], vertices[b + 5], vertices[b + 8]) < min[2] + 0.02f
    }.toIntArray()

    companion object {
        const val MAX_TRIANGLES = 20_000

        /** The preview of [file] by its extension, or null for formats it does not read (3MF, STEP). */
        fun load(file: File): PreviewMesh? = when (file.extension.lowercase(Locale.ROOT)) {
            "stl" -> readStl(file.readBytes())
            "obj" -> readObj(file.readText())
            else -> null
        }

        fun readStl(bytes: ByteArray): PreviewMesh {
            // Binary: 80 bytes of header, the count, then 50 bytes per triangle. An ASCII file starts with "solid" but
            // so do some binary ones: the size decides.
            if (bytes.size >= 84) {
                val count = ByteBuffer.wrap(bytes, 80, 4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xffffffffL
                if (84 + count * 50 == bytes.size.toLong())
                    return readBinaryStl(bytes, count.toInt())
            }
            return readAsciiStl(String(bytes, Charsets.US_ASCII))
        }

        private fun readBinaryStl(bytes: ByteArray, count: Int): PreviewMesh {
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val step = maxOf(1, (count + MAX_TRIANGLES - 1) / MAX_TRIANGLES)
            val out = FloatArray(((count + step - 1) / step) * 9)
            var o = 0
            var t = 0
            while (t < count) {
                // Skip the normal (12 bytes); 9 floats; 2 bytes of attributes.
                var p = 84 + t * 50 + 12
                repeat(9) {
                    out[o++] = buffer.getFloat(p)
                    p += 4
                }
                t += step
            }
            return PreviewMesh(out.copyOf(o))
        }

        private fun readAsciiStl(text: String): PreviewMesh {
            val corners = ArrayList<Float>()
            for (line in text.lineSequence()) {
                val parts = line.trim().split(Regex("\\s+"))
                if (parts.size >= 4 && parts[0] == "vertex")
                    for (i in 1..3) corners += parts[i].toFloat()
            }
            return PreviewMesh(sample(corners.toFloatArray()))
        }

        fun readObj(text: String): PreviewMesh {
            val points = ArrayList<Float>()
            val corners = ArrayList<Float>()
            fun corner(token: String) {
                // "v", "v/vt" or "v/vt/vn"; negative indices count from the end.
                val raw = token.substringBefore('/').toInt()
                val index = if (raw < 0) points.size / 3 + raw else raw - 1
                for (axis in 0..2) corners += points[index * 3 + axis]
            }
            for (line in text.lineSequence()) {
                val parts = line.trim().split(Regex("\\s+"))
                when (parts.firstOrNull()) {
                    "v" -> for (i in 1..3) points += parts[i].toFloat()
                    // Polygons as fans of triangles.
                    "f" -> for (i in 2 until parts.size - 1) {
                        corner(parts[1]); corner(parts[i]); corner(parts[i + 1])
                    }
                }
            }
            return PreviewMesh(sample(corners.toFloatArray()))
        }

        private fun sample(all: FloatArray): FloatArray {
            val count = all.size / 9
            if (count <= MAX_TRIANGLES) return all
            val step = (count + MAX_TRIANGLES - 1) / MAX_TRIANGLES
            val out = FloatArray(((count + step - 1) / step) * 9)
            var o = 0
            var t = 0
            while (t < count) {
                System.arraycopy(all, t * 9, out, o, 9)
                o += 9
                t += step
            }
            return out.copyOf(o)
        }
    }
}
