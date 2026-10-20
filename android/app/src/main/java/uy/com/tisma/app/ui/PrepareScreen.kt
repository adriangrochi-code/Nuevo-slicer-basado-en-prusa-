package uy.com.tisma.app.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.R
import uy.com.tisma.app.core.GcodeFlavor
import uy.com.tisma.app.core.Material
import uy.com.tisma.app.core.ModelInfo
import uy.com.tisma.app.core.PreviewMesh
import uy.com.tisma.app.core.SliceResult
import uy.com.tisma.app.core.SliceSettings
import uy.com.tisma.app.core.SliceSettingsStore
import uy.com.tisma.app.core.TismaCore
import java.io.File
import java.util.Locale

/** Prepare and slice: the model on the bed, the settings of printer, filament and quality, the result. */
@Composable
fun PrepareScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val store = remember { SliceSettingsStore(context) }
    var settings by remember { mutableStateOf(store.load()) }
    fun update(s: SliceSettings) {
        settings = s
        store.save(s)
    }
    var model by remember { mutableStateOf<File?>(null) }
    var info by remember { mutableStateOf<ModelInfo?>(null) }
    var mesh by remember { mutableStateOf<PreviewMesh?>(null) }
    var result by remember { mutableStateOf<SliceResult?>(null) }
    var progress by remember { mutableStateOf<Pair<Int, String>?>(null) }
    var error by remember { mutableStateOf<String?>(null) }

    val openModel = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        error = null
        result = null
        scope.launch {
            try {
                // The core reads files: the chosen document is copied to the cache, with its name (the extension
                // tells the format).
                val file = withContext(Dispatchers.IO) { copyToCache(context, uri) }
                model = file
                mesh = withContext(Dispatchers.Default) { runCatching { PreviewMesh.load(file) }.getOrNull() }
                info = if (TismaCore.available) withContext(Dispatchers.Default) { TismaCore.modelInfo(file.absolutePath) } else null
            } catch (e: Exception) {
                error = e.message ?: e.toString()
            }
        }
    }
    val saveGcode = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/x.gcode")) { uri: Uri? ->
        val gcode = result?.gcodePath ?: return@rememberLauncherForActivityResult
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch(Dispatchers.IO) {
            context.contentResolver.openOutputStream(uri)?.use { out -> File(gcode).inputStream().use { it.copyTo(out) } }
        }
    }

    Box(modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            // The bed and the model.
            Card(Modifier.fillMaxWidth()) {
                Box(Modifier.fillMaxWidth().height(300.dp)) {
                    ModelPreview(
                        mesh = mesh,
                        bedX = settings.bedX.toFloat(),
                        bedY = settings.bedY.toFloat(),
                        modelColor = MaterialTheme.colorScheme.primary,
                        bedColor = MaterialTheme.colorScheme.surfaceVariant,
                        gridColor = MaterialTheme.colorScheme.outlineVariant,
                        modifier = Modifier.fillMaxSize(),
                    )
                    val m = model
                    Column(Modifier.align(Alignment.TopStart).padding(12.dp)) {
                        if (m != null) {
                            Text(m.name, style = MaterialTheme.typography.titleSmall)
                            info?.let {
                                Text(stringResource(R.string.model_size, mm(it.sizeMm[0]), mm(it.sizeMm[1]), mm(it.sizeMm[2])),
                                    style = MaterialTheme.typography.bodySmall)
                            }
                            if (mesh == null)
                                Text(stringResource(R.string.no_preview), style = MaterialTheme.typography.bodySmall)
                        } else {
                            Text(stringResource(R.string.empty_bed), style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                    if (m != null)
                        Text(stringResource(R.string.drag_to_turn), style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.align(Alignment.BottomStart).padding(12.dp))
                }
            }
            if (!TismaCore.available)
                Text(stringResource(R.string.core_missing), color = MaterialTheme.colorScheme.error)

            PrinterSection(settings, ::update)
            FilamentSection(settings, ::update)
            QualitySection(settings, ::update)

            val problems = settings.problems()
            if (problems.isNotEmpty())
                Text(stringResource(R.string.settings_problem), color = MaterialTheme.colorScheme.error)
            Button(
                onClick = {
                    val source = model ?: return@Button
                    error = null
                    result = null
                    progress = 0 to ""
                    scope.launch {
                        try {
                            val ini = File(context.cacheDir, "settings.ini")
                            val out = File(context.cacheDir, source.nameWithoutExtension + ".gcode")
                            result = withContext(Dispatchers.Default) {
                                ini.writeText(settings.toIni())
                                TismaCore.slice(source.absolutePath, ini.absolutePath, out.absolutePath) { percent, text ->
                                    scope.launch(Dispatchers.Main) { progress = percent to text }
                                }
                            }
                        } catch (e: Exception) {
                            error = e.message ?: e.toString()
                        } finally {
                            progress = null
                        }
                    }
                },
                enabled = model != null && progress == null && TismaCore.available && problems.isEmpty(),
                modifier = Modifier.fillMaxWidth().height(52.dp),
            ) { Text(stringResource(R.string.slice), style = MaterialTheme.typography.titleMedium) }

            progress?.let { (percent, text) ->
                LinearProgressIndicator(progress = { percent / 100f }, modifier = Modifier.fillMaxWidth())
                Text(text, style = MaterialTheme.typography.bodySmall)
            }
            result?.let { r -> ResultCard(r, onSave = { saveGcode.launch(File(r.gcodePath).name) },
                onShare = { shareGcode(context, File(r.gcodePath)) }) }
            error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            // Room for the floating button.
            Box(Modifier.height(72.dp))
        }
        ExtendedFloatingActionButton(
            onClick = { openModel.launch(arrayOf("*/*")) },
            icon = { Icon(Icons.Filled.Add, contentDescription = null) },
            text = { Text(stringResource(if (model == null) R.string.open_model else R.string.change_model)) },
            modifier = Modifier.align(Alignment.BottomEnd).padding(16.dp),
        )
    }
}

/** A titled card with a one-line summary that opens to show its settings. */
@Composable
private fun Section(title: String, summary: String, content: @Composable () -> Unit) {
    var open by rememberSaveable(title) { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)) {
        Column(Modifier.padding(horizontal = 14.dp, vertical = 8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(title, style = MaterialTheme.typography.titleSmall)
                    Text(summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                IconButton(onClick = { open = !open }) {
                    Icon(if (open) Icons.Filled.KeyboardArrowUp else Icons.Filled.KeyboardArrowDown, contentDescription = null)
                }
            }
            AnimatedVisibility(open) {
                Column(Modifier.padding(bottom = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) { content() }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun <T> Choices(label: String, options: List<T>, selected: T, text: (T) -> String, onSelect: (T) -> Unit) {
    Text(label, style = MaterialTheme.typography.labelLarge)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        for (o in options)
            FilterChip(selected = o == selected, onClick = { onSelect(o) }, label = { Text(text(o)) })
    }
}

/** A number field that keeps what is typed and reports it only when it is a valid number. */
@Composable
private fun NumberField(label: String, value: Double, suffix: String, modifier: Modifier = Modifier, onValue: (Double) -> Unit) {
    var text by remember(value) { mutableStateOf(num(value)) }
    OutlinedTextField(
        value = text,
        onValueChange = {
            text = it
            it.replace(',', '.').toDoubleOrNull()?.let(onValue)
        },
        label = { Text(label) },
        suffix = { Text(suffix) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
        modifier = modifier,
    )
}

@Composable
private fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun PrinterSection(s: SliceSettings, update: (SliceSettings) -> Unit) {
    Section(stringResource(R.string.section_printer),
        stringResource(R.string.printer_summary, num(s.bedX), num(s.bedY), num(s.maxHeight), num(s.nozzle), s.flavor.label)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NumberField("X", s.bedX, "mm", Modifier.weight(1f)) { update(s.copy(bedX = it)) }
            NumberField("Y", s.bedY, "mm", Modifier.weight(1f)) { update(s.copy(bedY = it)) }
            NumberField("Z", s.maxHeight, "mm", Modifier.weight(1f)) { update(s.copy(maxHeight = it)) }
        }
        Choices(stringResource(R.string.nozzle), listOf(0.2, 0.4, 0.6, 0.8), s.nozzle, { "${num(it)} mm" }) { n ->
            // A layer the new nozzle can print.
            update(s.copy(nozzle = n, layerHeight = minOf(s.layerHeight, layerChoices(n).last())))
        }
        Choices(stringResource(R.string.firmware), GcodeFlavor.entries, s.flavor, { it.label }) { update(s.copy(flavor = it)) }
    }
}

@Composable
private fun FilamentSection(s: SliceSettings, update: (SliceSettings) -> Unit) {
    Section(stringResource(R.string.section_filament),
        stringResource(R.string.filament_summary, s.material.type, s.nozzleTemp, s.bedTemp)) {
        Choices(stringResource(R.string.material), Material.entries, s.material, { it.type }) { update(s.withMaterial(it)) }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NumberField(stringResource(R.string.nozzle_temp), s.nozzleTemp.toDouble(), "°C", Modifier.weight(1f)) {
                update(s.copy(nozzleTemp = it.toInt()))
            }
            NumberField(stringResource(R.string.bed_temp), s.bedTemp.toDouble(), "°C", Modifier.weight(1f)) {
                update(s.copy(bedTemp = it.toInt()))
            }
        }
        Text(stringResource(R.string.filament_hint), style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun QualitySection(s: SliceSettings, update: (SliceSettings) -> Unit) {
    Section(stringResource(R.string.section_quality),
        stringResource(R.string.quality_summary, num(s.layerHeight), s.infillPercent, s.perimeters)) {
        Choices(stringResource(R.string.layer_height), layerChoices(s.nozzle), s.layerHeight, { "${num(it)} mm" }) {
            update(s.copy(layerHeight = it))
        }
        Text(stringResource(R.string.infill, s.infillPercent), style = MaterialTheme.typography.labelLarge)
        Slider(value = s.infillPercent.toFloat(), onValueChange = { update(s.copy(infillPercent = (it / 5).toInt() * 5)) },
            valueRange = 0f..100f, steps = 19)
        Choices(stringResource(R.string.walls), listOf(1, 2, 3, 4, 5), s.perimeters, { "$it" }) { update(s.copy(perimeters = it)) }
        SwitchRow(stringResource(R.string.supports), s.supports) { update(s.copy(supports = it)) }
        SwitchRow(stringResource(R.string.brim), s.brim) { update(s.copy(brim = it)) }
    }
}

@Composable
private fun ResultCard(r: SliceResult, onSave: () -> Unit, onShare: () -> Unit) {
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(R.string.slice_done), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.slice_time, r.printTime))
            Text(stringResource(R.string.slice_filament, String.format(Locale.ROOT, "%.2f", r.filamentMm / 1000),
                String.format(Locale.ROOT, "%.1f", r.filamentGrams)))
            Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = onSave) { Text(stringResource(R.string.save_gcode)) }
                OutlinedButton(onClick = onShare) {
                    Icon(Icons.Filled.Share, contentDescription = null)
                    Box(Modifier.width(6.dp))
                    Text(stringResource(R.string.share_gcode))
                }
            }
        }
    }
}

/** Layer heights a nozzle prints well: up to three quarters of its diameter. */
private fun layerChoices(nozzle: Double): List<Double> =
    listOf(0.08, 0.12, 0.16, 0.2, 0.24, 0.28, 0.32, 0.4, 0.5, 0.6).filter { it <= 0.75 * nozzle + 1e-9 }.ifEmpty { listOf(0.08) }

private fun num(v: Double): String = String.format(Locale.ROOT, "%.2f", v).trimEnd('0').trimEnd('.')

private fun mm(v: Double) = String.format(Locale.ROOT, "%.1f", v)

/** The G-code to another app (a printer's app, a file manager, a message) through the share sheet. */
private fun shareGcode(context: Context, file: File) {
    val uri = FileProvider.getUriForFile(context, context.packageName + ".files", file)
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/x.gcode"
        putExtra(Intent.EXTRA_STREAM, uri)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, null))
}

internal fun copyToCache(context: Context, uri: Uri): File {
    var name = "model.stl"
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
        if (c.moveToFirst()) c.getString(0)?.let { name = it }
    }
    // Only the file name, never a path from the provider.
    name = File(name).name.ifEmpty { "model.stl" }
    val dir = File(context.cacheDir, "models").apply { mkdirs() }
    val file = File(dir, name)
    context.contentResolver.openInputStream(uri)!!.use { input -> file.outputStream().use { input.copyTo(it) } }
    return file
}
