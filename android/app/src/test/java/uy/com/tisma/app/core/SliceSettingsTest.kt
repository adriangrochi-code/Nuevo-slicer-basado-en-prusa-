package uy.com.tisma.app.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
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
    fun objPolygonsAndLargeModelsSampled() {
        val obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1/1 2/2 3/3 4/4\n"
        assertEquals(2, PreviewMesh.readObj(obj).triangleCount)
        val big = buildString { for (i in 0 until 50_000) append("vertex 0 0 $i\nvertex 1 0 $i\nvertex 0 1 $i\n") }
        val m = PreviewMesh.readStl(big.toByteArray())
        assertTrue(m.triangleCount <= PreviewMesh.MAX_TRIANGLES)
        assertTrue(m.triangleCount > PreviewMesh.MAX_TRIANGLES / 2)
    }
}
