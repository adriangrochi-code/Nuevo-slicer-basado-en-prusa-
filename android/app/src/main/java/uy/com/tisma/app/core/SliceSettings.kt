package uy.com.tisma.app.core

import android.content.Context
import java.util.Locale

/**
 * A filament family with the values of Tisma's generic filaments (resources/profiles/Templates.ini: Generic PLA,
 * PETG, ABS and FLEX), the starting point the user can change.
 */
enum class Material(
    val type: String,
    val nozzle: Int,
    val firstLayerNozzle: Int,
    val bed: Int,
    val firstLayerBed: Int,
    val density: Double,
    val fanAlwaysOn: Boolean,
    val maxFan: Int,
) {
    PLA("PLA", 210, 215, 60, 60, 1.24, true, 100),
    PETG("PETG", 240, 230, 90, 85, 1.27, true, 50),
    ABS("ABS", 255, 255, 100, 100, 1.04, false, 15),
    FLEX("FLEX", 240, 240, 50, 50, 1.22, false, 90),
}

enum class GcodeFlavor(val key: String, val label: String) {
    MARLIN("marlin2", "Marlin"),
    KLIPPER("klipper", "Klipper"),
    REPRAP("reprapfirmware", "RepRapFirmware"),
}

/** Values of fill_pattern of the core (s_keys_map_InfillPattern, src/libslic3r/PrintConfig.cpp). */
enum class InfillPattern(val key: String) {
    GRID("grid"), GYROID("gyroid"), CUBIC("cubic"), ADAPTIVE_CUBIC("adaptivecubic"), HONEYCOMB("honeycomb"),
    HONEYCOMB_3D("3dhoneycomb"), TRIANGLES("triangles"), RECTILINEAR("rectilinear"), LIGHTNING("lightning"),
}

/** Values of seam_position of the core (s_keys_map_SeamPosition). */
enum class SeamPosition(val key: String) { ALIGNED("aligned"), NEAREST("nearest"), REAR("rear"), RANDOM("random") }

/** Values of support_material_style of the core (s_keys_map_SupportMaterialStyle). */
enum class SupportStyle(val key: String) { GRID("grid"), SNUG("snug"), ORGANIC("organic") }

/** A printer of the catalog: the preset of a printer model of a vendor bundle. */
data class CatalogPrinter(val vendor: String, val model: String, val preset: String)

/**
 * The settings of the slice screen; [toIni] writes them as the options of Tisma's core.
 *
 * With a printer of the catalog ([printer]), the bed, height and nozzle below are copied from its preset (for the
 * preview and the checks) and the core gets the options of the preset itself. The advanced settings left null keep
 * the value of the print profile of the printer (or the default of the core).
 */
data class SliceSettings(
    // Printer
    val printer: CatalogPrinter? = null,
    val bedX: Double = 220.0,
    val bedY: Double = 220.0,
    val maxHeight: Double = 250.0,
    val nozzle: Double = 0.4,
    val flavor: GcodeFlavor = GcodeFlavor.MARLIN,
    val filamentDiameter: Double = 1.75,
    // Filament
    val material: Material = Material.PLA,
    val nozzleTemp: Int = Material.PLA.nozzle,
    val bedTemp: Int = Material.PLA.bed,
    // Quality
    val layerHeight: Double = 0.2,
    val infillPercent: Int = 15,
    val perimeters: Int = 2,
    val supports: Boolean = false,
    val brim: Boolean = false,
    // Advanced (null: the print profile's)
    val topLayers: Int? = null,
    val bottomLayers: Int? = null,
    val infillPattern: InfillPattern? = null,
    val seam: SeamPosition? = null,
    val supportStyle: SupportStyle? = null,
    val supportsOnBedOnly: Boolean = false,
    val ironing: Boolean = false,
) {
    /** Another material, with its temperatures. */
    fun withMaterial(m: Material) = copy(material = m, nozzleTemp = m.nozzle, bedTemp = m.bed)

    /** A printer of the catalog: its bed, height, nozzle and filament diameter, and a layer its nozzle can print. */
    fun withPrinter(vendor: String, model: String, preset: CatalogPreset): SliceSettings {
        val c = preset.config
        // A few vendor presets leave the bed or the nozzle to the defaults of the core (200 x 200 x 200 mm, 0.4 mm).
        val (x, y) = bedSize(c["bed_shape"]) ?: (200.0 to 200.0)
        val n = c["nozzle_diameter"]?.split(',')?.firstOrNull()?.trim()?.toDoubleOrNull() ?: 0.4
        return copy(
            printer = CatalogPrinter(vendor, model, preset.name),
            bedX = x, bedY = y,
            maxHeight = c["max_print_height"]?.toDoubleOrNull() ?: 200.0,
            nozzle = n,
            filamentDiameter = preset.filamentDiameter,
            layerHeight = closestLayer(n),
        )
    }

    /** Back to the printer typed by hand (the fields above). */
    fun withCustomPrinter() = copy(printer = null, filamentDiameter = 1.75)

    /** The layer height closest to the current one that [n] prints. */
    fun closestLayer(n: Double): Double = layerChoices(n).minBy { kotlin.math.abs(it - layerHeight) }

    /** Problems that make the settings unusable (empty: none). */
    fun problems(): List<String> = buildList {
        // A printer typed by hand; a catalog printer has the values of its vendor (half a bed in ditto mode, nozzles
        // of 1.6 mm for chocolate).
        if (printer == null && (bedX < 50 || bedY < 50 || maxHeight < 20)) add("bed")
        if (printer == null && (nozzle < 0.1 || nozzle > 1.2)) add("nozzle")
        if (layerHeight > 0.75 * nozzle || layerHeight < 0.04) add("layer")
    }

    /**
     * The options for the core: those of [printerConfig] (the preset of the catalog printer, from
     * [PrinterCatalog]) first, then the settings of the app, which replace them. Each option once (the INI reader of
     * the core does not accept a key twice).
     */
    fun toIni(printerConfig: Map<String, String>? = null): String {
        val f = { v: Double -> String.format(Locale.ROOT, "%.3f", v).trimEnd('0').trimEnd('.') }
        // The first layer keeps the temperatures of the material relative to the others.
        val firstNozzle = nozzleTemp + (material.firstLayerNozzle - material.nozzle)
        val firstBed = (bedTemp + (material.firstLayerBed - material.bed)).coerceAtLeast(0)
        val lines = LinkedHashMap<String, String>()
        if (printerConfig != null)
            lines.putAll(printerConfig)
        else {
            lines["bed_shape"] = "0x0,${f(bedX)}x0,${f(bedX)}x${f(bedY)},0x${f(bedY)}"
            lines["max_print_height"] = f(maxHeight)
            lines["nozzle_diameter"] = f(nozzle)
            lines["gcode_flavor"] = flavor.key
        }
        lines += linkedMapOf(
            "filament_diameter" to f(filamentDiameter),
            "filament_type" to material.type,
            "filament_density" to f(material.density),
            "temperature" to "$nozzleTemp",
            "first_layer_temperature" to "$firstNozzle",
            "bed_temperature" to "$bedTemp",
            "first_layer_bed_temperature" to "$firstBed",
            "fan_always_on" to if (material.fanAlwaysOn) "1" else "0",
            "max_fan_speed" to "${material.maxFan}",
            "layer_height" to f(layerHeight),
            // Not thicker than the nozzle allows, not thinner than the other layers.
            "first_layer_height" to f(maxOf(layerHeight, minOf(0.2, 0.75 * nozzle))),
            "fill_density" to "$infillPercent%",
            "perimeters" to "$perimeters",
            "support_material" to if (supports) "1" else "0",
            "support_material_auto" to "1",
            "brim_width" to if (brim) "5" else "0",
        )
        topLayers?.let { lines["top_solid_layers"] = "$it" }
        bottomLayers?.let { lines["bottom_solid_layers"] = "$it" }
        infillPattern?.let { lines["fill_pattern"] = it.key }
        seam?.let { lines["seam_position"] = it.key }
        supportStyle?.let { lines["support_material_style"] = it.key }
        if (supportsOnBedOnly) lines["support_material_buildplate_only"] = "1"
        if (ironing) {
            lines["ironing"] = "1"
            lines["ironing_type"] = "top"
        }
        return lines.entries.joinToString("\n", postfix = "\n") { "${it.key} = ${it.value}" }
    }

    companion object {
        /** Layer heights a nozzle prints well: up to three quarters of its diameter. */
        fun layerChoices(nozzle: Double): List<Double> =
            listOf(0.08, 0.12, 0.16, 0.2, 0.24, 0.28, 0.32, 0.4, 0.5, 0.6).filter { it <= 0.75 * nozzle + 1e-9 }
                .ifEmpty { listOf(0.08) }

        private val LEADING_NUMBER = Regex("""^\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?""")

        /**
         * Width and depth of the bounding box of a bed_shape ("0x0,250x0,250x210,0x210"), or null. Read as the core
         * reads it (ConfigOptionPoints::deserialize): the number at the start of each coordinate, 0 without one.
         */
        fun bedSize(shape: String?): Pair<Double, Double>? {
            fun coordinate(s: String?) = s?.let { LEADING_NUMBER.find(it)?.value?.trim()?.toDoubleOrNull() } ?: 0.0
            val points = shape?.split(',')?.filter { it.isNotBlank() }?.map { p ->
                val xy = p.split('x')
                coordinate(xy.getOrNull(0)) to coordinate(xy.getOrNull(1))
            } ?: return null
            if (points.size < 3) return null
            val w = points.maxOf { it.first } - points.minOf { it.first }
            val d = points.maxOf { it.second } - points.minOf { it.second }
            return if (w > 0 && d > 0) w to d else null
        }
    }
}

/** The last settings, kept in the private storage of the app. */
class SliceSettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("slice", Context.MODE_PRIVATE)

    fun load(): SliceSettings {
        val d = SliceSettings()
        val material = runCatching { Material.valueOf(prefs.getString("material", d.material.name)!!) }.getOrDefault(d.material)
        val vendor = prefs.getString("printer_vendor", null)
        val model = prefs.getString("printer_model", null)
        val preset = prefs.getString("printer_preset", null)
        return SliceSettings(
            printer = if (vendor != null && model != null && preset != null) CatalogPrinter(vendor, model, preset) else null,
            bedX = prefs.getFloat("bed_x", d.bedX.toFloat()).exact(),
            bedY = prefs.getFloat("bed_y", d.bedY.toFloat()).exact(),
            maxHeight = prefs.getFloat("max_height", d.maxHeight.toFloat()).exact(),
            nozzle = prefs.getFloat("nozzle", d.nozzle.toFloat()).exact(),
            flavor = enumOr("flavor", d.flavor),
            filamentDiameter = prefs.getFloat("filament_diameter", d.filamentDiameter.toFloat()).exact(),
            material = material,
            nozzleTemp = prefs.getInt("nozzle_temp", material.nozzle),
            bedTemp = prefs.getInt("bed_temp", material.bed),
            layerHeight = prefs.getFloat("layer_height", d.layerHeight.toFloat()).exact(),
            infillPercent = prefs.getInt("infill", d.infillPercent),
            perimeters = prefs.getInt("perimeters", d.perimeters),
            supports = prefs.getBoolean("supports", d.supports),
            brim = prefs.getBoolean("brim", d.brim),
            topLayers = prefs.getInt("top_layers", -1).takeIf { it >= 0 },
            bottomLayers = prefs.getInt("bottom_layers", -1).takeIf { it >= 0 },
            infillPattern = enumOrNull<InfillPattern>("infill_pattern"),
            seam = enumOrNull<SeamPosition>("seam"),
            supportStyle = enumOrNull<SupportStyle>("support_style"),
            supportsOnBedOnly = prefs.getBoolean("supports_on_bed_only", d.supportsOnBedOnly),
            ironing = prefs.getBoolean("ironing", d.ironing),
        )
    }

    private inline fun <reified T : Enum<T>> enumOr(key: String, default: T): T = enumOrNull<T>(key) ?: default

    private inline fun <reified T : Enum<T>> enumOrNull(key: String): T? =
        prefs.getString(key, null)?.let { name -> enumValues<T>().firstOrNull { it.name == name } }

    // Stored as floats: back to the value with 3 decimals, so that 0.4 is 0.4 again (and matches the choices).
    private fun Float.exact(): Double = Math.round(this * 1000.0) / 1000.0

    fun save(s: SliceSettings) {
        prefs.edit()
            .putString("printer_vendor", s.printer?.vendor)
            .putString("printer_model", s.printer?.model)
            .putString("printer_preset", s.printer?.preset)
            .putFloat("bed_x", s.bedX.toFloat())
            .putFloat("bed_y", s.bedY.toFloat())
            .putFloat("max_height", s.maxHeight.toFloat())
            .putFloat("nozzle", s.nozzle.toFloat())
            .putString("flavor", s.flavor.name)
            .putFloat("filament_diameter", s.filamentDiameter.toFloat())
            .putString("material", s.material.name)
            .putInt("nozzle_temp", s.nozzleTemp)
            .putInt("bed_temp", s.bedTemp)
            .putFloat("layer_height", s.layerHeight.toFloat())
            .putInt("infill", s.infillPercent)
            .putInt("perimeters", s.perimeters)
            .putBoolean("supports", s.supports)
            .putBoolean("brim", s.brim)
            .putInt("top_layers", s.topLayers ?: -1)
            .putInt("bottom_layers", s.bottomLayers ?: -1)
            .putString("infill_pattern", s.infillPattern?.name)
            .putString("seam", s.seam?.name)
            .putString("support_style", s.supportStyle?.name)
            .putBoolean("supports_on_bed_only", s.supportsOnBedOnly)
            .putBoolean("ironing", s.ironing)
            .apply()
    }
}
