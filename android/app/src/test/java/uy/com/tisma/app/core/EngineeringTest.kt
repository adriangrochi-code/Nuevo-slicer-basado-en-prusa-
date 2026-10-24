package uy.com.tisma.app.core

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class EngineeringTest {
    // A box 100 x 10 x 10 mm (12 triangles, outward normals).
    private fun bar(): PreviewMesh {
        val v = listOf(
            floatArrayOf(0f, 0f, 0f), floatArrayOf(100f, 0f, 0f), floatArrayOf(100f, 10f, 0f), floatArrayOf(0f, 10f, 0f),
            floatArrayOf(0f, 0f, 10f), floatArrayOf(100f, 0f, 10f), floatArrayOf(100f, 10f, 10f), floatArrayOf(0f, 10f, 10f))
        val f = listOf(0 to 2 to 1, 0 to 3 to 2, 4 to 5 to 6, 4 to 6 to 7, 0 to 1 to 5, 0 to 5 to 4, 1 to 2 to 6, 1 to 6 to 5,
            2 to 3 to 7, 2 to 7 to 6, 3 to 0 to 4, 3 to 4 to 7)
        val out = FloatArray(f.size * 9)
        f.forEachIndexed { t, (ab, c) ->
            listOf(ab.first, ab.second, c).forEachIndexed { k, i -> for (a in 0..2) out[t * 9 + k * 3 + a] = v[i][a] }
        }
        return PreviewMesh(out)
    }

    @Test
    fun flatFacesOfTheMesh() {
        val m = bar()
        // Bottom: the two triangles facing down.
        assertEquals(setOf(0, 1), m.bedFaces().toSet())
        assertEquals(setOf(2, 3), m.flatRegion(2).toSet())
        assertEquals(setOf(10, 11), m.flatRegion(11).toSet())
        assertEquals(-1f, m.normal(10)[0], 1e-6f)
    }

    @Test
    fun requestAsJson() {
        val load = EngineeringLoad(PickedFace(listOf(99.0, 5.0, 10.0), listOf(0.0, 0.0, 1.0)), listOf(0.0, 0.0, -20.0), false)
        val r = EngineeringRequest(bedFixture = false, fixtures = listOf(PickedFace(listOf(0.0, 5.0, 5.0), listOf(-1.0, 0.0, 0.0))),
            loads = listOf(load), safetyFactor = 3.0, quality = AnalysisQuality.NORMAL)
        assertTrue(r.ready)
        assertFalse(EngineeringRequest(bedFixture = false, loads = listOf(load)).ready)
        assertFalse(EngineeringRequest().ready)
        val o = JSONObject(r.toJson(lightest = true))
        assertEquals("lightest", o.getString("mode"))
        assertEquals(60000, o.getInt("voxels"))
        assertEquals(-20.0, o.getJSONArray("loads").getJSONObject(0).getJSONArray("force").getDouble(2), 0.0)
        assertEquals(-1.0, o.getJSONArray("fixtures").getJSONObject(0).getJSONArray("normal").getDouble(0), 0.0)
        assertEquals(20.0, load.magnitude, 1e-9)
    }

    @Test
    fun answerOfTheCore() {
        // As the core answered for a cantilever of PLA (android/app/src/main/cpp/engineering.cpp).
        val a = EngineeringAnswer.parse("""{"material":"PLA","feasible":true,"density":0.05,"analyses":2,"result":{"verdict":"holds",
            "safety_factor":2.46626,"max_von_mises":19.9898,"max_displacement":4.67344,"mass":4.62493,"solid_mass":13.2023,
            "critical":[1.18742,9.7622,9.7622],"voxels":21294,"voxel_size":0.793701,"alternatives":[["PETG",2.1]]}}""")
        assertEquals(Verdict.HOLDS, a.result.verdict)
        assertEquals(0.05, a.lightestDensity!!, 0.0)
        assertEquals(21294, a.result.voxels)
        assertEquals("PETG" to 2.1, a.result.alternatives[0])
        val b = EngineeringAnswer.parse("""{"material":"ABS","result":{"verdict":"out_of_load","safety_factor":0.5,"max_von_mises":40,
            "max_displacement":1,"mass":3,"solid_mass":5,"critical":[0,0,0],"voxels":10,"voxel_size":1,"alternatives":[]}}""")
        assertEquals(Verdict.OUT_OF_LOAD, b.result.verdict)
        assertNull(b.lightestDensity)
    }

    @Test
    fun fieldOfTheCore() {
        // Grid 4 x 3 x 2 of 2 mm voxels from (10, 0, 0); voxels 0 and 23 (the last one).
        val b = ByteBuffer.allocate(4 + 4 + 24 + 8 + 12 + 4 + 2 * 8).order(ByteOrder.LITTLE_ENDIAN)
        b.put("TFEA".toByteArray()).putInt(1)
        b.putDouble(10.0).putDouble(0.0).putDouble(0.0).putDouble(2.0)
        b.putInt(4).putInt(3).putInt(2).putInt(2)
        b.putInt(0).putFloat(0.25f).putInt(23).putFloat(0.9f)
        val f = FieldMap.read(b.array())
        assertEquals(0.25f, f.at(11.0, 1.0, 1.0)!!, 0f)
        // Next to a voxel: the largest around.
        assertEquals(0.25f, f.at(13.0, 1.0, 1.0)!!, 0f)
        assertEquals(0.9f, f.at(17.0, 5.0, 3.0)!!, 0f)
        assertNull(f.at(14.5, 5.0, 1.0)?.takeIf { it > 1f })
        assertNull(f.at(100.0, 100.0, 100.0))
    }
}
