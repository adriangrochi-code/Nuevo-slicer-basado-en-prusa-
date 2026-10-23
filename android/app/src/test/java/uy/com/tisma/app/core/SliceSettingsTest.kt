package uy.com.tisma.app.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt
import java.nio.ByteBuffer
import java.nio.ByteOrder

class SliceSettingsTest {
    private fun ini(s: SliceSettings) = s.toIni().lines().filter { "=" in it }
        .associate { it.substringBefore('=').trim() to it.substringAfter('=').trim() }

    @Test
    fun defaultsAreTismaGenericPla() {
        val o = ini(SliceSettings())
        assertEquals("0x0,220x0,220x220,0x220", o["bed_shape"])
        assertEquals("PLA", o["filament_type"])
        assertEquals("210", o["temperature"])
        assertEquals("215", o["first_layer_temperature"])
        assertEquals("60", o["bed_temperature"])
        assertEquals("15%", o["fill_density"])
        assertEquals("0.2", o["layer_height"])
        assertEquals("0", o["support_material"])
        assertTrue(SliceSettings().problems().isEmpty())
    }

    @Test
    fun materialAndChangedTemperatures() {
        // PETG: the first layer stays 10 C below the other layers (Generic PETG), also when the user changes it.
        val s = SliceSettings().withMaterial(Material.PETG).copy(nozzleTemp = 245, bedTemp = 80, supports = true, brim = true)
        val o = ini(s)
        assertEquals("245", o["temperature"])
        assertEquals("235", o["first_layer_temperature"])
        assertEquals("80", o["bed_temperature"])
        assertEquals("75", o["first_layer_bed_temperature"])
        assertEquals("1", o["support_material"])
        assertEquals("5", o["brim_width"])
    }

    @Test
    fun nozzleAndLayers() {
        val fine = SliceSettings(nozzle = 0.2, layerHeight = 0.12)
        assertEquals("0.15", ini(fine)["first_layer_height"])
        assertTrue(fine.problems().isEmpty())
        assertEquals(listOf("layer"), SliceSettings(nozzle = 0.2, layerHeight = 0.2).problems())
        assertEquals("0.32", ini(SliceSettings(nozzle = 0.6, layerHeight = 0.32))["first_layer_height"])
        assertEquals("klipper", ini(SliceSettings(flavor = GcodeFlavor.KLIPPER))["gcode_flavor"])
    }
}

class PreviewMeshTest {
    private val cube = """
        solid t
        facet normal 0 0 1
         outer loop
          vertex 0 0 10
          vertex 20 0 10
          vertex 20 30 10
         endloop
        endfacet
        endsolid t
    """.trimIndent()

    @Test
    fun asciiStl() {
        val m = PreviewMesh.readStl(cube.toByteArray())
        assertEquals(1, m.triangleCount)
        assertEquals(20f, m.max[0]); assertEquals(30f, m.max[1]); assertEquals(10f, m.min[2])
    }

    @Test
    fun binaryStlEvenWhenItStartsWithSolid() {
        val count = 3
        val buffer = ByteBuffer.allocate(84 + 50 * count).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put("solid but binary".toByteArray()).position(80)
        buffer.putInt(count)
        for (t in 0 until count) {
            repeat(3) { buffer.putFloat(0f) }
            for (k in 0 until 9) buffer.putFloat((t * 10 + k).toFloat())
            buffer.putShort(0)
        }
        val m = PreviewMesh.readStl(buffer.array())
        assertEquals(3, m.triangleCount)
        assertEquals(28f, m.max[2])
    }

    @Test
    fun objPolygonsAndModelsUnderTheLimitWhole() {
        val obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1/1 2/2 3/3 4/4\n"
        assertEquals(2, PreviewMesh.readObj(obj).triangleCount)
        // Every triangle is kept (sampling some of them showed large models as dots).
        val big = buildString { for (i in 0 until 50_000) append("  vertex 0 0 $i\n\tvertex 1 0 $i\nvertex 0 1 $i\n") }
        val m = PreviewMesh.readStl(big.toByteArray())
        assertEquals(50_000, m.triangleCount)
        assertEquals(49_999f, m.max[2])
    }

    // A sphere of radius 50 with 2 · rings · (rings - 1) triangles or so.
    private fun sphere(rings: Int): FloatArray {
        val out = ArrayList<Float>()
        fun point(i: Int, j: Int): List<Float> {
            val theta = Math.PI * i / rings
            val phi = 2 * Math.PI * j / rings
            return listOf((50 * sin(theta) * cos(phi)).toFloat(), (50 * sin(theta) * sin(phi)).toFloat(), (50 * cos(theta)).toFloat())
        }
        for (i in 0 until rings) for (j in 0 until rings) {
            val a = point(i, j); val b = point(i + 1, j); val c = point(i + 1, j + 1); val d = point(i, j + 1)
            if (i > 0) out += a + b + d
            if (i < rings - 1) out += d + b + c
        }
        return out.toFloatArray()
    }

    private fun area(v: FloatArray): Double = (0 until v.size / 9).sumOf { t ->
        val b = t * 9
        val ax = v[b + 3] - v[b]; val ay = v[b + 4] - v[b + 1]; val az = v[b + 5] - v[b + 2]
        val bx = v[b + 6] - v[b]; val by = v[b + 7] - v[b + 1]; val bz = v[b + 8] - v[b + 2]
        0.5 * sqrt(((ay * bz - az * by) * (ay * bz - az * by) + (az * bx - ax * bz) * (az * bx - ax * bz) +
            (ax * by - ay * bx) * (ax * by - ay * bx)).toDouble())
    }

    @Test
    fun largeModelsAreSimplifiedAndStaySolid() {
        val full = sphere(300)
        assertTrue(full.size / 9 > 150_000)
        val simple = cluster(ArraySource(full), 20_000)
        val count = simple.size / 9
        assertTrue("$count triangles", count in 10_000..20_000)
        // The same closed surface: its area stays (sampling one triangle in k would keep 1 / k of it), every corner
        // stays on the sphere and no triangle is a line.
        assertEquals(area(full), area(simple), area(full) * 0.02)
        for (i in 0 until simple.size / 3) {
            val r = sqrt(simple[i * 3] * simple[i * 3] + simple[i * 3 + 1] * simple[i * 3 + 1] + simple[i * 3 + 2] * simple[i * 3 + 2])
            assertEquals(50f, r, 1f)
        }
        val m = PreviewMesh(simple)
        for (t in 0 until m.triangleCount) assertTrue(m.normals[t * 3] * m.normals[t * 3] + m.normals[t * 3 + 2] * m.normals[t * 3 + 2] + m.normals[t * 3 + 1] * m.normals[t * 3 + 1] > 0.99f)
    }

    @Test
    fun neighborsAcrossSharedEdges() {
        // Two triangles of a square share the edge from (1, 0) to (0, 1); the other edges have none.
        val v = floatArrayOf(0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f, 0f, 1f, 0f, 0f, 1f, 1f, 0f, 0f, 1f, 0f)
        assertEquals(listOf(-1, 1, -1, -1, -1, 0), neighbors(v).toList())
        assertEquals(listOf(0, 1, 2, 1, 4, 2), weld(v).toList())
    }

    @Test
    fun longIntMapGrows() {
        val map = LongIntMap(4)
        for (i in 0 until 100_000) map.put(i * 7_919L shl 21, i)
        assertEquals(100_000, map.size)
        for (i in 0 until 100_000) assertEquals(i, map.get(i * 7_919L shl 21))
        assertEquals(-1, map.get(3L))
    }
}

class AdvancedSettingsTest {
    private fun ini(s: SliceSettings) = s.toIni().lines().filter { "=" in it }
        .associate { it.substringBefore('=').trim() to it.substringAfter('=').trim() }

    @Test
    fun profileValuesAreNotWritten() {
        val o = ini(SliceSettings())
        for (k in listOf("top_solid_layers", "bottom_solid_layers", "fill_pattern", "seam_position", "support_material_style", "ironing"))
            assertTrue(k, k !in o)
    }

    @Test
    fun changedValuesAreWritten() {
        val o = ini(SliceSettings(topLayers = 6, bottomLayers = 4, infillPattern = InfillPattern.GYROID, seam = SeamPosition.REAR,
            supportStyle = SupportStyle.ORGANIC, supportsOnBedOnly = true, ironing = true))
        assertEquals("6", o["top_solid_layers"])
        assertEquals("4", o["bottom_solid_layers"])
        assertEquals("gyroid", o["fill_pattern"])
        assertEquals("rear", o["seam_position"])
        assertEquals("organic", o["support_material_style"])
        assertEquals("1", o["support_material_buildplate_only"])
        assertEquals("1", o["ironing"])
        assertEquals("top", o["ironing_type"])
    }

    @Test
    fun bedSizeOfShapes() {
        assertEquals(250.0 to 210.0, SliceSettings.bedSize("0x0,250x0,250x210,0x210"))
        assertEquals(200.0 to 200.0, SliceSettings.bedSize("-100x-100,100x-100,100x100,-100x100"))
        assertEquals(null, SliceSettings.bedSize("garbage"))
        // A typo of a vendor bundle (Anker M5), read as the core does: 235-0 is 235x0.
        assertEquals(235.0 to 235.0, SliceSettings.bedSize("0x0,235-0,235x235,0x235"))
    }
}
