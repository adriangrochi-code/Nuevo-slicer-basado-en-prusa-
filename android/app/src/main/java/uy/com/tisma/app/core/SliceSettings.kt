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

/** The settings of the slice screen; [toIni] writes them as the options of Tisma's core. */
data class SliceSettings(
    // Printer
    val bedX: Double = 220.0,
    val bedY: Double = 220.0,
    val maxHeight: Double = 250.0,
    val nozzle: Double = 0.4,
    val flavor: GcodeFlavor = GcodeFlavor.MARLIN,
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
) {
    /** Another material, with its temperatures. */
    fun withMaterial(m: Material) = copy(material = m, nozzleTemp = m.nozzle, bedTemp = m.bed)

    /** Problems that make the settings unusable (empty: none). */
    fun problems(): List<String> = buildList {
        if (bedX < 50 || bedY < 50 || maxHeight < 20) add("bed")
        if (nozzle < 0.1 || nozzle > 1.2) add("nozzle")
        if (layerHeight > 0.75 * nozzle || layerHeight < 0.04) add("layer")
    }

    fun toIni(): String {
        val f = { v: Double -> String.format(Locale.ROOT, "%.3f", v).trimEnd('0').trimEnd('.') }
        // The first layer keeps the temperatures of the material relative to the others.
        val firstNozzle = nozzleTemp + (material.firstLayerNozzle - material.nozzle)
        val firstBed = (bedTemp + (material.firstLayerBed - material.bed)).coerceAtLeast(0)
        val lines = linkedMapOf(
            "bed_shape" to "0x0,${f(bedX)}x0,${f(bedX)}x${f(bedY)},0x${f(bedY)}",
            "max_print_height" to f(maxHeight),
            "nozzle_diameter" to f(nozzle),
            "gcode_flavor" to flavor.key,
            "filament_diameter" to "1.75",
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
        return lines.entries.joinToString("\n", postfix = "\n") { "${it.key} = ${it.value}" }
    }
}

/** The last settings, kept in the private storage of the app. */
class SliceSettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("slice", Context.MODE_PRIVATE)

    fun load(): SliceSettings {
        val d = SliceSettings()
        val material = runCatching { Material.valueOf(prefs.getString("material", d.material.name)!!) }.getOrDefault(d.material)
        return SliceSettings(
            bedX = prefs.getFloat("bed_x", d.bedX.toFloat()).exact(),
            bedY = prefs.getFloat("bed_y", d.bedY.toFloat()).exact(),
            maxHeight = prefs.getFloat("max_height", d.maxHeight.toFloat()).exact(),
            nozzle = prefs.getFloat("nozzle", d.nozzle.toFloat()).exact(),
            flavor = runCatching { GcodeFlavor.valueOf(prefs.getString("flavor", d.flavor.name)!!) }.getOrDefault(d.flavor),
            material = material,
            nozzleTemp = prefs.getInt("nozzle_temp", material.nozzle),
            bedTemp = prefs.getInt("bed_temp", material.bed),
            layerHeight = prefs.getFloat("layer_height", d.layerHeight.toFloat()).exact(),
            infillPercent = prefs.getInt("infill", d.infillPercent),
            perimeters = prefs.getInt("perimeters", d.perimeters),
            supports = prefs.getBoolean("supports", d.supports),
            brim = prefs.getBoolean("brim", d.brim),
        )
    }

    // Stored as floats: back to the value with 3 decimals, so that 0.4 is 0.4 again (and matches the choices).
    private fun Float.exact(): Double = Math.round(this * 1000.0) / 1000.0

    fun save(s: SliceSettings) {
        prefs.edit()
            .putFloat("bed_x", s.bedX.toFloat())
            .putFloat("bed_y", s.bedY.toFloat())
            .putFloat("max_height", s.maxHeight.toFloat())
            .putFloat("nozzle", s.nozzle.toFloat())
            .putString("flavor", s.flavor.name)
            .putString("material", s.material.name)
            .putInt("nozzle_temp", s.nozzleTemp)
            .putInt("bed_temp", s.bedTemp)
            .putFloat("layer_height", s.layerHeight.toFloat())
            .putInt("infill", s.infillPercent)
            .putInt("perimeters", s.perimeters)
            .putBoolean("supports", s.supports)
            .putBoolean("brim", s.brim)
            .apply()
    }
}
