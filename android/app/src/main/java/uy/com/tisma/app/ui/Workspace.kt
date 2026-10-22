package uy.com.tisma.app.ui

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import uy.com.tisma.app.core.CatalogPreset
import uy.com.tisma.app.core.ModelInfo
import uy.com.tisma.app.core.PreviewMesh
import uy.com.tisma.app.core.PrinterCatalog
import uy.com.tisma.app.core.SliceSettings
import uy.com.tisma.app.core.SliceSettingsStore
import java.io.File

/** What the tabs share: the model on the bed, the settings and the printer catalog. */
class Workspace(context: Context) {
    private val app = context.applicationContext
    private val store = SliceSettingsStore(app)

    var settings by mutableStateOf(store.load())
        private set
    var model by mutableStateOf<File?>(null)
    var info by mutableStateOf<ModelInfo?>(null)
    var mesh by mutableStateOf<PreviewMesh?>(null)

    /** The catalog of printers, once [loadCatalog] read it (null before, or when the app was built without it). */
    var catalog by mutableStateOf<PrinterCatalog?>(null)
        private set

    fun update(s: SliceSettings) {
        settings = s
        store.save(s)
    }

    suspend fun loadCatalog() {
        if (catalog != null) return
        catalog = withContext(Dispatchers.IO) {
            runCatching { app.assets.open("printers.json").use { PrinterCatalog.read(it) } }.getOrNull()
        }
    }

    /** The preset of the chosen printer of the catalog, or null (custom printer, or no longer in the catalog). */
    fun printerPreset(): CatalogPreset? = settings.printer?.let { catalog?.find(it) }

    /** The options for the core, with the preset of the chosen printer. */
    fun ini(): String = settings.toIni(printerPreset()?.config)
}
