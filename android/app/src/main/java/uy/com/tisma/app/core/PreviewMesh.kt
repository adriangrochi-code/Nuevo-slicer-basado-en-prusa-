package uy.com.tisma.app.core

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Locale

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
