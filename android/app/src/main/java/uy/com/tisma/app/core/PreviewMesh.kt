package uy.com.tisma.app.core

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.util.Locale
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt

/**
 * Triangles of a model for the preview on screen: x, y, z of the 3 corners of each triangle in [vertices]. Models of
 * up to [MAX_TRIANGLES] are kept whole, as the desktop program shows them; larger ones are simplified by merging the
 * corners that fall in the same cell of a grid (vertex clustering), which keeps the surface closed: the model still
 * looks solid, only finer details than a cell go. Read here from STL and OBJ files; the slicing core reads the model
 * itself, this is only for showing it.
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

    // The triangle across each edge of each triangle (edge k joins corners k and k + 1), or -1.
    private val neighbors: IntArray by lazy { neighbors(vertices) }

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
            for (k in 0..2) {
                val m = neighbors[f * 3 + k]
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
        /** Triangles shown as they are; a phone draws this many smoothly (48 bytes each on the graphics card). */
        const val MAX_TRIANGLES = 500_000

        /** The preview of [file] by its extension, or null for formats it does not read (3MF, STEP). */
        fun load(file: File): PreviewMesh? = when (file.extension.lowercase(Locale.ROOT)) {
            // Mapped, not read into memory: an STL of millions of triangles is hundreds of megabytes.
            "stl" -> RandomAccessFile(file, "r").use { f ->
                if (f.length() > Int.MAX_VALUE) null
                else readStl(f.channel.map(FileChannel.MapMode.READ_ONLY, 0, f.length()))
            }
            "obj" -> readObj(file.readText())
            else -> null
        }

        fun readStl(bytes: ByteArray): PreviewMesh = readStl(ByteBuffer.wrap(bytes))

        fun readStl(buffer: ByteBuffer): PreviewMesh {
            val data = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
            // Binary: 80 bytes of header, the count, then 50 bytes per triangle. An ASCII file starts with "solid" but
            // so do some binary ones: the size decides.
            if (data.limit() >= 84) {
                val count = data.getInt(80).toLong() and 0xffffffffL
                if (84 + count * 50 == data.limit().toLong())
                    return fromSource(BinaryStl(data, count.toInt()))
            }
            val text = ByteArray(data.limit())
            data.position(0)
            data.get(text)
            return readAsciiStl(String(text, Charsets.US_ASCII))
        }

        private fun readAsciiStl(text: String): PreviewMesh {
            val corners = FloatList()
            var i = 0
            while (i < text.length) {
                var end = text.indexOf('\n', i)
                if (end < 0) end = text.length
                // "vertex x y z", with any spaces around.
                var p = i
                while (p < end && text[p].isWhitespace()) p++
                if (text.startsWith("vertex", p)) {
                    p += 6
                    repeat(3) {
                        while (p < end && text[p].isWhitespace()) p++
                        val start = p
                        while (p < end && !text[p].isWhitespace()) p++
                        corners += text.substring(start, p).toFloat()
                    }
                }
                i = end + 1
            }
            return fromArray(corners.toArray())
        }

        fun readObj(text: String): PreviewMesh {
            val points = FloatList()
            val corners = FloatList()
            fun corner(token: String) {
                // "v", "v/vt" or "v/vt/vn"; negative indices count from the end.
                val raw = token.substringBefore('/').toInt()
                val index = if (raw < 0) points.size / 3 + raw else raw - 1
                for (axis in 0..2) corners += points[index * 3 + axis]
            }
            val space = Regex("\\s+")
            for (line in text.lineSequence()) {
                val parts = line.trim().split(space)
                when (parts.firstOrNull()) {
                    "v" -> for (i in 1..3) points += parts[i].toFloat()
                    // Polygons as fans of triangles.
                    "f" -> for (i in 2 until parts.size - 1) {
                        corner(parts[1]); corner(parts[i]); corner(parts[i + 1])
                    }
                }
            }
            return fromArray(corners.toArray())
        }

        private fun fromArray(all: FloatArray): PreviewMesh =
            if (all.size / 9 <= MAX_TRIANGLES) PreviewMesh(all) else PreviewMesh(cluster(ArraySource(all), MAX_TRIANGLES))

        private fun fromSource(source: TriangleSource): PreviewMesh {
            if (source.count > MAX_TRIANGLES) return PreviewMesh(cluster(source, MAX_TRIANGLES))
            val out = FloatArray(source.count * 9)
            val corners = FloatArray(9)
            for (t in 0 until source.count) {
                source.read(t, corners)
                System.arraycopy(corners, 0, out, t * 9, 9)
            }
            return PreviewMesh(out)
        }
    }
}

/** Triangles read one by one: from an array, or straight from a mapped file. */
internal abstract class TriangleSource(val count: Int) {
    /** The 9 coordinates of the corners of triangle [t] into [out]. */
    abstract fun read(t: Int, out: FloatArray)
}

internal class ArraySource(private val v: FloatArray) : TriangleSource(v.size / 9) {
    override fun read(t: Int, out: FloatArray) = System.arraycopy(v, t * 9, out, 0, 9)
}

private class BinaryStl(private val data: ByteBuffer, count: Int) : TriangleSource(count) {
    // Each triangle: the normal (12 bytes, not used), 9 floats, 2 bytes of attributes.
    override fun read(t: Int, out: FloatArray) {
        var p = 84 + t * 50 + 12
        for (i in 0..8) {
            out[i] = data.getFloat(p)
            p += 4
        }
    }
}

/**
 * [source] with at most [target] triangles (vertex clustering): the corners in each cube of a grid merge into their
 * mean, the triangles left with two corners in one cube go. The grid is as fine as the target allows: the triangles
 * kept grow with the square of the cubes along the longest side, from which the next try is sized.
 */
internal fun cluster(source: TriangleSource, target: Int): FloatArray {
    val lo = FloatArray(3) { Float.MAX_VALUE }
    val hi = FloatArray(3) { -Float.MAX_VALUE }
    val corners = FloatArray(9)
    for (t in 0 until source.count) {
        source.read(t, corners)
        for (i in 0..8) {
            lo[i % 3] = min(lo[i % 3], corners[i])
            hi[i % 3] = max(hi[i % 3], corners[i])
        }
    }
    val longest = max(hi[0] - lo[0], max(hi[1] - lo[1], hi[2] - lo[2])).coerceAtLeast(1e-6f)
    // A flat plate of n × n cubes keeps about 2 n² triangles; a rounder shape keeps more, so this starts too fine.
    var cubes = sqrt(target / 2.0).toInt().coerceIn(2, MAX_CUBES)
    while (true) {
        val out = clusterOnce(source, lo, longest / cubes)
        val kept = out.size / 9
        if (kept <= target || cubes <= 2) return out
        cubes = (cubes * sqrt(target.toDouble() / kept) * 0.97).toInt().coerceIn(2, cubes - 1)
    }
}

// Cubes along the longest side at most: the 3 cube indices fit in 21 bits each of a Long key.
private const val MAX_CUBES = 1 shl 20

private fun clusterOnce(source: TriangleSource, lo: FloatArray, cube: Float): FloatArray {
    val ids = LongIntMap(1 shl 16)
    var sums = DoubleArray(3 * 4096)
    var counts = IntArray(4096)
    fun key(c: FloatArray, k: Int): Long {
        val ix = ((c[k * 3] - lo[0]) / cube).toLong().coerceIn(0, MAX_CUBES.toLong())
        val iy = ((c[k * 3 + 1] - lo[1]) / cube).toLong().coerceIn(0, MAX_CUBES.toLong())
        val iz = ((c[k * 3 + 2] - lo[2]) / cube).toLong().coerceIn(0, MAX_CUBES.toLong())
        return (ix shl 42) or (iy shl 21) or iz
    }
    // The cluster of each corner and the sum of its corners.
    val corners = FloatArray(9)
    for (t in 0 until source.count) {
        source.read(t, corners)
        for (k in 0..2) {
            val cell = key(corners, k)
            var id = ids.get(cell)
            if (id < 0) {
                id = ids.size
                ids.put(cell, id)
                if (id >= counts.size) {
                    counts = counts.copyOf(counts.size * 2)
                    sums = sums.copyOf(sums.size * 2)
                }
            }
            counts[id]++
            for (a in 0..2) sums[id * 3 + a] += corners[k * 3 + a].toDouble()
        }
    }
    val mean = FloatArray(ids.size * 3) { (sums[it] / counts[it / 3]).toFloat() }
    // The triangles whose corners are in three clusters, at the means of their clusters: once (several triangles of
    // the model can end on the same three), and not when the three means are on a line.
    val out = FloatList()
    val c = IntArray(3)
    val seen = LongIntMap(1 shl 16)
    val unique = ids.size < (1 shl 21)
    for (t in 0 until source.count) {
        source.read(t, corners)
        for (k in 0..2) c[k] = ids.get(key(corners, k))
        if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]) continue
        if (unique) {
            val lo = min(c[0], min(c[1], c[2])).toLong()
            val hi = max(c[0], max(c[1], c[2])).toLong()
            val mid = c[0] + c[1] + c[2] - lo - hi
            val triangle = (lo shl 42) or (mid shl 21) or hi
            if (seen.get(triangle) >= 0) continue
            seen.put(triangle, 0)
        }
        val ax = mean[c[1] * 3] - mean[c[0] * 3]; val ay = mean[c[1] * 3 + 1] - mean[c[0] * 3 + 1]; val az = mean[c[1] * 3 + 2] - mean[c[0] * 3 + 2]
        val bx = mean[c[2] * 3] - mean[c[0] * 3]; val by = mean[c[2] * 3 + 1] - mean[c[0] * 3 + 1]; val bz = mean[c[2] * 3 + 2] - mean[c[0] * 3 + 2]
        if (ay * bz - az * by == 0f && az * bx - ax * bz == 0f && ax * by - ay * bx == 0f) continue
        for (k in 0..2) for (a in 0..2) out += mean[c[k] * 3 + a]
    }
    return out.toArray()
}

/** The triangle across each edge of each triangle (edge k joins corners k and k + 1), or -1. */
internal fun neighbors(v: FloatArray): IntArray {
    val id = weld(v)
    val count = v.size / 9
    val edges = LongIntMap(count * 2)
    val out = IntArray(count * 3) { -1 }
    for (t in 0 until count) for (k in 0..2) {
        val a = id[t * 3 + k]
        val b = id[t * 3 + (k + 1) % 3]
        if (a == b) continue
        val key = (min(a, b).toLong() shl 32) or max(a, b).toLong()
        val first = edges.get(key)
        if (first < 0) {
            edges.put(key, t * 3 + k)
        } else {
            // The second triangle of the edge (a third one of a non-manifold edge sees the first one only).
            if (out[first] < 0) out[first] = t
            out[t * 3 + k] = first / 3
        }
    }
    return out
}

/** For each corner, the first corner at the same point (the same floats: shared corners of a mesh file). */
internal fun weld(v: FloatArray): IntArray {
    val corners = v.size / 3
    val table = IntArray(Integer.highestOneBit(maxOf(corners, 2) - 1) shl 2) { -1 }
    val mask = table.size - 1
    val out = IntArray(corners)
    for (c in 0 until corners) {
        // + 0 makes -0 into 0: the same point, with other bits.
        val x = v[c * 3] + 0f; val y = v[c * 3 + 1] + 0f; val z = v[c * 3 + 2] + 0f
        var h = mix((x.toRawBits() * 31 + y.toRawBits()) * 31 + z.toRawBits()) and mask
        while (true) {
            val e = table[h]
            if (e < 0) {
                table[h] = c
                out[c] = c
                break
            }
            if (v[e * 3] == x && v[e * 3 + 1] == y && v[e * 3 + 2] == z) {
                out[c] = e
                break
            }
            h = (h + 1) and mask
        }
    }
    return out
}

private fun mix(value: Int): Int {
    var h = value
    h = h xor (h ushr 16)
    h *= 0x7feb352d
    h = h xor (h ushr 15)
    h *= -0x7b935975
    return h xor (h ushr 16)
}

/** Long keys to Int values (not negative), without boxing: millions of entries take tens of megabytes, not hundreds. */
internal class LongIntMap(expected: Int) {
    private var keys = LongArray(capacity(expected)) { EMPTY }
    private var values = IntArray(keys.size)
    var size = 0
        private set

    /** The value of [key], or -1. */
    fun get(key: Long): Int {
        val mask = keys.size - 1
        var h = slot(key) and mask
        while (true) {
            val k = keys[h]
            if (k == key) return values[h]
            if (k == EMPTY) return -1
            h = (h + 1) and mask
        }
    }

    fun put(key: Long, value: Int) {
        if ((size + 1) * 2 > keys.size) grow()
        val mask = keys.size - 1
        var h = slot(key) and mask
        while (keys[h] != EMPTY && keys[h] != key) h = (h + 1) and mask
        if (keys[h] == EMPTY) size++
        keys[h] = key
        values[h] = value
    }

    private fun grow() {
        val oldKeys = keys
        val oldValues = values
        keys = LongArray(oldKeys.size * 2) { EMPTY }
        values = IntArray(keys.size)
        size = 0
        for (i in oldKeys.indices) if (oldKeys[i] != EMPTY) put(oldKeys[i], oldValues[i])
    }

    private fun slot(key: Long) = mix((key xor (key ushr 32)).toInt())

    private companion object {
        const val EMPTY = Long.MIN_VALUE
        fun capacity(expected: Int) = Integer.highestOneBit(maxOf(expected, 4) * 2 - 1) shl 1
    }
}

/** A growing array of floats (an ArrayList<Float> boxes each one). */
internal class FloatList {
    private var data = FloatArray(1024)
    var size = 0
        private set

    operator fun plusAssign(value: Float) {
        if (size == data.size) data = data.copyOf(data.size * 2)
        data[size++] = value
    }

    operator fun get(index: Int): Float {
        if (index !in 0 until size) throw IndexOutOfBoundsException("$index of $size")
        return data[index]
    }

    fun toArray(): FloatArray = data.copyOf(size)
}
