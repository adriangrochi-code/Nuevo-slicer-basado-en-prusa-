package uy.com.tisma.app.core

import org.json.JSONObject

class TismaCoreException(message: String) : Exception(message)

data class ModelInfo(val objects: Int, val triangles: Long, val sizeMm: List<Double>)

data class SliceResult(
    val gcodePath: String,
    val printTime: String,
    val printTimeSeconds: Double,
    val filamentMm: Double,
    val filamentGrams: Double,
)

fun interface ProgressListener {
    fun onProgress(percent: Int, text: String)
}

/** A progress listener which can stop the work: the core asks [cancelled] while it runs. */
interface AnalysisListener : ProgressListener {
    fun cancelled(): Boolean
}

/** A material of the structural analysis (libtisma_fea/src/Materials.cpp). [linear]: the analysis applies to it. */
data class AnalysisMaterial(val key: String, val name: String, val linear: Boolean)

/** The slicing core of Tisma Slicer (C++, libtisma.so). Its calls block: run them off the main thread. */
object TismaCore {
    /** False when the app was built without the core (no Android deps): the interface still works. */
    val available: Boolean = try {
        System.loadLibrary("tisma")
        true
    } catch (_: UnsatisfiedLinkError) {
        false
    }

    fun version(): String = if (available) nativeVersion() else ""

    fun modelInfo(path: String): ModelInfo {
        val json = JSONObject(nativeModelInfo(path))
        val size = json.getJSONArray("size")
        return ModelInfo(
            objects = json.getInt("objects"),
            triangles = json.getLong("triangles"),
            sizeMm = List(size.length()) { size.getDouble(it) },
        )
    }

    /** Slices [modelPath] with the settings of [configIni] (empty: the defaults of the core) into [gcodePath]. */
    fun slice(modelPath: String, configIni: String, gcodePath: String, listener: ProgressListener?): SliceResult {
        val json = JSONObject(nativeSlice(modelPath, configIni, gcodePath, listener))
        return SliceResult(
            gcodePath = json.getString("gcode"),
            printTime = json.getString("time"),
            printTimeSeconds = json.getDouble("time_s"),
            filamentMm = json.getDouble("filament_mm"),
            filamentGrams = json.getDouble("filament_g"),
        )
    }

    /**
     * Structural analysis of the first object of [modelPath] with the settings of [configIni] (walls, infill, filament)
     * and [request]. The failure index of the voxels is written to [fieldPath] (read it with [FieldMap]).
     */
    fun analyze(modelPath: String, configIni: String, request: String, fieldPath: String, listener: AnalysisListener?): EngineeringAnswer =
        EngineeringAnswer.parse(nativeAnalyze(modelPath, configIni, request, fieldPath, listener))

    fun materials(): List<AnalysisMaterial> {
        val json = org.json.JSONArray(nativeMaterials())
        return List(json.length()) { i ->
            json.getJSONObject(i).let { AnalysisMaterial(it.getString("key"), it.getString("name"), it.getBoolean("linear")) }
        }
    }

    @JvmStatic private external fun nativeVersion(): String
    @JvmStatic private external fun nativeModelInfo(path: String): String
    @JvmStatic private external fun nativeSlice(modelPath: String, configIni: String, gcodePath: String, listener: ProgressListener?): String
    @JvmStatic private external fun nativeAnalyze(modelPath: String, configIni: String, request: String, fieldPath: String,
                                                  listener: ProgressListener?): String
    @JvmStatic private external fun nativeMaterials(): String
}
