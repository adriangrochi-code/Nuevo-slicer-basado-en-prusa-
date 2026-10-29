package uy.com.tisma.app.core

import org.json.JSONArray
import org.json.JSONObject
import java.io.InputStream
import java.util.Locale

/** A printer preset of a vendor bundle: one nozzle of a printer model. */
class CatalogPreset(
    val name: String,
    val nozzle: String,
    /** Diameter of the filament the printer takes [mm]. */
    val filamentDiameter: Double,
    private val catalog: PrinterCatalog,
    private val pairs: IntArray,
) {
    /** The options of the printer and of its default print profile, as the vendor gives them. */
    val config: Map<String, String> by lazy {
        val out = LinkedHashMap<String, String>(pairs.size / 2)
        for (i in pairs.indices step 2) out[catalog.keys[pairs[i]]] = catalog.values[pairs[i + 1]]
        out
    }

    val nozzleMm: Double get() = nozzle.toDoubleOrNull() ?: 0.4
}

class CatalogModel(val vendor: String, val name: String, val presets: List<CatalogPreset>) {
    /** The nozzles of the model, smallest first, each once. */
    val nozzles: List<String> get() = presets.map { it.nozzle }.distinct()

    /**
     * The preset of [nozzle] most like [like]: the same name without its nozzle ("MK3S MMU2S Single" for "MK3S MMU2S
     * Single 0.6 nozzle"), else the first one.
     */
    fun presetFor(nozzle: String, like: CatalogPreset): CatalogPreset {
        val candidates = presets.filter { it.nozzle == nozzle }
        val base = withoutNozzle(like.name)
        return candidates.firstOrNull { withoutNozzle(it.name) == base } ?: candidates.firstOrNull() ?: like
    }

    /** The preset for a nozzle: the 0.4 mm one, the closest to it otherwise. */
    fun defaultPreset(): CatalogPreset =
        presets.minByOrNull { kotlin.math.abs(it.nozzleMm - 0.4) * 100 + (if (it.name.contains("MMU")) 1 else 0) }!!
}

private val NOZZLE_IN_NAME = Regex("""\(?\b\d+(\.\d+)?\s*(mm)?\s*(nozzle|HF nozzle)?\)?""", RegexOption.IGNORE_CASE)

private fun withoutNozzle(name: String) = name.replace(NOZZLE_IN_NAME, "").replace(Regex("\\s+"), " ").trim()

class CatalogVendor(val name: String, val models: List<CatalogModel>)

/**
 * The FFF printers of Tisma's vendor bundles (assets/printers.json, made by android/tools/printer_catalog.py when
 * the app is built): vendor, printer model and one preset per nozzle.
 */
class PrinterCatalog private constructor(val keys: Array<String>, val values: Array<String>, json: JSONArray) {
    val vendors: List<CatalogVendor>

    init {
        vendors = List(json.length()) { v ->
            val vendor = json.getJSONObject(v)
            val vendorName = vendor.getString("name")
            val models = vendor.getJSONArray("models")
            CatalogVendor(vendorName, List(models.length()) { m ->
                val model = models.getJSONObject(m)
                val presets = model.getJSONArray("presets")
                CatalogModel(vendorName, model.getString("name"), List(presets.length()) { p ->
                    val preset = presets.getJSONObject(p)
                    val config = preset.getJSONArray("config")
                    CatalogPreset(
                        name = preset.getString("name"),
                        nozzle = preset.getString("nozzle"),
                        filamentDiameter = preset.optString("filament_diameter", "1.75").toDoubleOrNull() ?: 1.75,
                        catalog = this,
                        pairs = IntArray(config.length()) { config.getInt(it) },
                    )
                })
            })
        }
    }

    fun find(vendor: String, model: String): CatalogModel? =
        vendors.firstOrNull { it.name == vendor }?.models?.firstOrNull { it.name == model }

    fun find(choice: CatalogPrinter): CatalogPreset? =
        find(choice.vendor, choice.model)?.presets?.firstOrNull { it.name == choice.preset }

    /** The models whose vendor and name contain every word of [query] (any case), in catalog order. */
    fun search(query: String): List<CatalogModel> {
        val words = query.lowercase(Locale.ROOT).split(Regex("\\s+")).filter { it.isNotEmpty() }
        return vendors.flatMap { v ->
            v.models.filter { m ->
                val text = (v.name + " " + m.name).lowercase(Locale.ROOT)
                words.all { it in text }
            }
        }
    }

    companion object {
        fun read(input: InputStream): PrinterCatalog {
            val json = JSONObject(input.bufferedReader(Charsets.UTF_8).use { it.readText() })
            val keys = json.getJSONArray("keys")
            val values = json.getJSONArray("values")
            return PrinterCatalog(
                Array(keys.length()) { keys.getString(it) },
                Array(values.length()) { values.getString(it) },
                json.getJSONArray("vendors"),
            )
        }
    }
}
