package uy.com.tisma.app.core

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.floor
import kotlin.math.sqrt

/** A face picked on the model: a point on it and its outward normal, in the coordinates of the file. */
data class PickedFace(val point: List<Double>, val normal: List<Double>)

/** A load: [force] in newtons (Z up, as on the bed), on a point of the face or spread over the whole flat face. */
data class EngineeringLoad(val at: PickedFace, val force: List<Double>, val wholeFace: Boolean) {
    val magnitude: Double get() = sqrt(force.sumOf { it * it })
}

/** Size of the analysis: voxels inside the part (the "Fast" and "Normal" qualities of the desktop program, and more). */
enum class AnalysisQuality(val voxels: Int) { FAST(20_000), NORMAL(60_000), HIGH(150_000) }

/**
 * What the structural analysis of the core needs (android/app/src/main/cpp/engineering.cpp): where the part is held,
 * the loads, the material (empty: the one of the filament), the ambient temperature and the safety factor required.
 */
data class EngineeringRequest(
    val bedFixture: Boolean = true,
    val fixtures: List<PickedFace> = emptyList(),
    val loads: List<EngineeringLoad> = emptyList(),
    val material: String = "",
    val temperature: Double = 23.0,
    val safetyFactor: Double = 2.0,
    val quality: AnalysisQuality = AnalysisQuality.FAST,
) {
    val ready: Boolean get() = (bedFixture || fixtures.isNotEmpty()) && loads.isNotEmpty()

    /** [lightest]: the lowest infill density which meets the requirements, instead of one analysis. */
    fun toJson(lightest: Boolean): String {
        fun vec(v: List<Double>) = JSONArray(v)
        fun face(f: PickedFace) = JSONObject().put("point", vec(f.point)).put("normal", vec(f.normal))
        return JSONObject()
            .put("mode", if (lightest) "lightest" else "analyze")
            .put("bed_fixture", bedFixture)
            .put("fixtures", JSONArray(fixtures.map(::face)))
            .put("loads", JSONArray(loads.map { face(it.at).put("force", vec(it.force)).put("face", it.wholeFace) }))
            .put("material", material)
            .put("temperature", temperature)
            .put("safety_factor", safetyFactor)
            .put("voxels", quality.voxels)
            .toString()
    }
}

enum class Verdict(val key: String) {
    HOLDS("holds"), LOW_MARGIN("low_margin"), OUT_OF_LOAD("out_of_load"), TOO_FLEXIBLE("too_flexible"),
    OUT_OF_TEMPERATURE("out_of_temperature"), NOT_APPLICABLE("not_applicable");

    companion object {
        fun of(key: String) = entries.firstOrNull { it.key == key } ?: HOLDS
    }
}

data class AnalysisResult(
    val verdict: Verdict,
    val safetyFactor: Double,
    /** Largest von Mises stress [MPa]. */
    val maxStress: Double,
    /** Largest displacement [mm]. */
    val maxDisplacement: Double,
    /** Estimated mass of the print and of the solid part [g]. */
    val mass: Double,
    val solidMass: Double,
    val voxels: Int,
    val voxelSize: Double,
    /** Materials that would hold, with their safety factor. */
    val alternatives: List<Pair<String, Double>>,
)

/** The answer of the core: the analysis, and for the lightest infill its density (null after a plain analysis). */
data class EngineeringAnswer(
    val material: String,
    val result: AnalysisResult,
    val lightestDensity: Double?,
    val feasible: Boolean,
) {
    companion object {
        fun parse(json: String): EngineeringAnswer {
            val o = JSONObject(json)
            val r = o.getJSONObject("result")
            val alt = r.optJSONArray("alternatives") ?: JSONArray()
            return EngineeringAnswer(
                material = o.getString("material"),
                result = AnalysisResult(
                    verdict = Verdict.of(r.getString("verdict")),
                    safetyFactor = r.getDouble("safety_factor"),
                    maxStress = r.getDouble("max_von_mises"),
                    maxDisplacement = r.getDouble("max_displacement"),
                    mass = r.getDouble("mass"),
                    solidMass = r.getDouble("solid_mass"),
                    voxels = r.getInt("voxels"),
                    voxelSize = r.getDouble("voxel_size"),
                    alternatives = List(alt.length()) { alt.getJSONArray(it).let { a -> a.getString(0) to a.getDouble(1) } },
                ),
                lightestDensity = if (o.has("density")) o.getDouble("density") else null,
                feasible = o.optBoolean("feasible", true),
            )
        }
    }
}

/**
 * The failure index (stress / strength, > 1 breaks) of the voxels of an analysis, as the core writes it: "TFEA",
 * version 1, origin (3 doubles), voxel edge (double), grid size (3 int32), count (int32), then the index of each voxel
 * (int32) and its failure index (float), little endian.
 */
class FieldMap(
    private val origin: DoubleArray,
    private val h: Double,
    private val size: IntArray,
    private val values: FloatArray,
) {
    /** The largest failure index of the voxels around a point (within one voxel), or null outside the part. */
    fun at(x: Double, y: Double, z: Double): Float? {
        val i = floor((x - origin[0]) / h).toInt()
        val j = floor((y - origin[1]) / h).toInt()
        val k = floor((z - origin[2]) / h).toInt()
        var best = -1f
        for (dk in -1..1) for (dj in -1..1) for (di in -1..1) {
            val a = i + di; val b = j + dj; val c = k + dk
            if (a < 0 || b < 0 || c < 0 || a >= size[0] || b >= size[1] || c >= size[2]) continue
            best = maxOf(best, values[a + size[0] * (b + size[1] * c)])
        }
        return if (best >= 0f) best else null
    }

    companion object {
        fun read(file: File): FieldMap = read(file.readBytes())

        fun read(bytes: ByteArray): FieldMap {
            val b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            val magic = ByteArray(4).also { b.get(it) }
            require(String(magic, Charsets.US_ASCII) == "TFEA" && b.int == 1) { "Not a field of the analysis" }
            val origin = DoubleArray(3) { b.double }
            val h = b.double
            val size = IntArray(3) { b.int }
            val values = FloatArray(size[0] * size[1] * size[2]) { -1f }
            repeat(b.int) {
                val index = b.int
                val value = b.float
                if (index in values.indices) values[index] = value
            }
            return FieldMap(origin, h, size, values)
        }
    }
}
