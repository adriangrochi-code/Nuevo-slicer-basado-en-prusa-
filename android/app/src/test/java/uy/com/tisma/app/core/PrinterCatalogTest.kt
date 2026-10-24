package uy.com.tisma.app.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class PrinterCatalogTest {
    // Two models of one vendor, as android/tools/printer_catalog.py writes them.
    private val json = """
        {"keys":["bed_shape","max_print_height","nozzle_diameter","gcode_flavor","perimeters","start_gcode"],
         "values":["0x0,250x0,250x210,0x210","220","0.4","0.6","marlin2","3","G28\\nG1 Z5","0x0,300x0,300x300,0x300"],
         "vendors":[{"name":"Acme","models":[
           {"name":"Acme One","presets":[
             {"name":"Acme One 0.4","nozzle":"0.4","config":[0,0,1,1,2,2,3,4,4,5,5,6]},
             {"name":"Acme One MMU 0.4","nozzle":"0.4","config":[0,0,1,1,2,2,3,4]},
             {"name":"Acme One 0.6","nozzle":"0.6","config":[0,0,1,1,2,3,3,4]},
             {"name":"Acme One MMU 0.6","nozzle":"0.6","config":[0,0,1,1,2,3,3,4]}]},
           {"name":"Acme Big","presets":[
             {"name":"Acme Big","nozzle":"0.6","config":[0,7,2,3],"filament_diameter":"2.85"}]}]}]}
    """.trimIndent()

    private fun catalog() = PrinterCatalog.read(json.byteInputStream())

    @Test
    fun readsVendorsModelsAndConfig() {
        val c = catalog()
        val one = c.find("Acme", "Acme One")!!
        assertEquals(listOf("0.4", "0.6"), one.nozzles)
        val p = one.presets[0]
        assertEquals("0x0,250x0,250x210,0x210", p.config["bed_shape"])
        // Kept as in the INI file (new lines escaped).
        assertEquals("G28\\nG1 Z5", p.config["start_gcode"])
        assertEquals(1.75, p.filamentDiameter, 0.0)
        assertEquals(2.85, c.find("Acme", "Acme Big")!!.presets[0].filamentDiameter, 0.0)
        assertNotNull(c.find(CatalogPrinter("Acme", "Acme One", "Acme One MMU 0.6")))
        assertNull(c.find(CatalogPrinter("Acme", "Acme One", "Gone")))
    }

    @Test
    fun searchAndNozzleChange() {
        val c = catalog()
        assertEquals(listOf("Acme Big"), c.search("acme BIG").map { it.name })
        assertEquals(2, c.search("acme").size)
        assertTrue(c.search("zzz").isEmpty())
        val one = c.find("Acme", "Acme One")!!
        assertEquals("Acme One 0.4", one.defaultPreset().name)
        // The other nozzle keeps the variant (with or without the multi-material unit).
        assertEquals("Acme One MMU 0.6", one.presetFor("0.6", one.presets[1]).name)
        assertEquals("Acme One 0.6", one.presetFor("0.6", one.presets[0]).name)
    }

    @Test
    fun catalogPrinterInTheIni() {
        val c = catalog()
        val preset = c.find("Acme", "Acme Big")!!.presets[0]
        val s = SliceSettings(layerHeight = 0.32).withPrinter("Acme", "Acme Big", preset)
        assertEquals(300.0, s.bedX, 0.0)
        assertEquals(0.6, s.nozzle, 0.0)
        assertEquals(0.32, s.layerHeight, 0.0)
        val lines = s.toIni(preset.config).lines().filter { "=" in it }
        val keys = lines.map { it.substringBefore('=').trim() }
        // Each option once, the app's values after the preset's.
        assertEquals(keys.size, keys.toSet().size)
        val o = lines.associate { it.substringBefore('=').trim() to it.substringAfter('=').trim() }
        assertEquals("0x0,300x0,300x300,0x300", o["bed_shape"])
        assertEquals("2.85", o["filament_diameter"])
        assertEquals("2", o["perimeters"])
        assertFalse("seam_position" in o)
        val custom = s.withCustomPrinter()
        assertNull(custom.printer)
        assertEquals("1.75", custom.toIni().lines().first { it.startsWith("filament_diameter") }.substringAfter("= "))
    }

    @Test
    fun realCatalog() {
        // Made from resources/profiles by the printerCatalog task of the build.
        val path = System.getProperty("tisma.printerCatalog") ?: return
        val c = File(path).inputStream().use { PrinterCatalog.read(it) }
        assertTrue(c.vendors.size > 100)
        val mk4 = c.search("Prusa MK4").firstOrNull()
        assertNotNull(mk4)
        // The bed and the nozzle, when a preset gives them (a few leave them to the defaults of the core), are usable.
        for (v in c.vendors) for (m in v.models) for (p in m.presets) {
            p.config["bed_shape"]?.let { assertNotNull("${p.name}: bed", SliceSettings.bedSize(it)) }
            val s = SliceSettings().withPrinter(v.name, m.name, p)
            assertTrue("${p.name}: ${s.problems()}", s.problems().isEmpty())
        }
        // Every catalog printer gives an INI the core can read: one value per option.
        val p = mk4!!.defaultPreset()
        val keys = SliceSettings().withPrinter(mk4.vendor, mk4.name, p).toIni(p.config).lines()
            .filter { "=" in it }.map { it.substringBefore('=').trim() }
        assertEquals(keys.size, keys.toSet().size)
    }
}
