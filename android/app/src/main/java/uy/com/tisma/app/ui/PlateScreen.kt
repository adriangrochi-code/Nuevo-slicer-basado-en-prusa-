package uy.com.tisma.app.ui

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.R
import uy.com.tisma.app.core.PreviewMesh
import uy.com.tisma.app.core.SliceResult
import uy.com.tisma.app.core.TismaCore
import java.io.File
import java.util.Locale

/** The groups of tools of the plate, in the bar under it. */
enum class PlateCategory { FILE, VIEW, ENGINEERING, SLICE }

/** What the card over the plate shows. */
private enum class Panel { MODEL, FIX, LOAD, REQUIREMENTS, ANALYSIS, SLICE }

/** Slicing the model on the bed with the settings of the workspace. */
@Stable
class SliceController(private val workspace: Workspace, private val context: Context, private val scope: CoroutineScope) {
    var result by mutableStateOf<SliceResult?>(null)
    var progress by mutableStateOf<Pair<Int, String>?>(null)
        private set
    var error by mutableStateOf<String?>(null)

    val canSlice get() = workspace.model != null && progress == null && TismaCore.available && workspace.settings.problems().isEmpty()

    fun slice() {
        val source = workspace.model ?: return
        error = null
        result = null
        progress = 0 to ""
        scope.launch {
            try {
                val ini = File(context.cacheDir, "settings.ini")
                val out = File(context.cacheDir, source.nameWithoutExtension + ".gcode")
                val text = workspace.ini()
                result = withContext(Dispatchers.Default) {
                    ini.writeText(text)
                    TismaCore.slice(source.absolutePath, ini.absolutePath, out.absolutePath) { percent, text ->
                        scope.launch(Dispatchers.Main) { progress = percent to text }
                    }
                }
                workspace.lastGcode = File(result!!.gcodePath)
            } catch (e: Exception) {
                error = e.message ?: e.toString()
            } finally {
                progress = null
            }
        }
    }
}

/**
 * The plate: the bed and the model filling the screen, a strip of tools under it and the groups of tools at the
 * bottom (File, View, Engineering, Slice). In landscape the groups and the tools go to the left of the plate.
 * [onPage] opens a settings page (from the summary of the settings over the plate).
 */
@Composable
fun PlateScreen(workspace: Workspace, onPage: (Page) -> Unit, onSendToPrinter: () -> Unit, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val camera = remember { PreviewCamera() }
    val slicer = remember { SliceController(workspace, context, scope) }
    val eng = remember { EngineeringController(workspace, context, scope) }
    var category by rememberSaveable { mutableStateOf(PlateCategory.FILE) }
    var panel by remember { mutableStateOf<Panel?>(null) }
    var openError by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        workspace.loadCatalog()
        eng.loadMaterials()
    }

    val openModel = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        openError = null
        slicer.result = null
        scope.launch {
            try {
                // The core reads files: the chosen document is copied to the cache, with its name (the extension
                // tells the format).
                val file = withContext(Dispatchers.IO) { copyToCache(context, uri) }
                workspace.model = file
                workspace.engineering = EngineeringState()
                workspace.lastGcode = null
                workspace.info = null
                workspace.mesh = withContext(Dispatchers.Default) { runCatching { PreviewMesh.load(file) }.getOrNull() }
                workspace.info = if (TismaCore.available) withContext(Dispatchers.Default) { TismaCore.modelInfo(file.absolutePath) } else null
                camera.iso()
                panel = Panel.MODEL
            } catch (e: Exception) {
                openError = e.message ?: e.toString()
            }
        }
    }
    val saveGcode = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/x.gcode")) { uri: Uri? ->
        val gcode = slicer.result?.gcodePath ?: return@rememberLauncherForActivityResult
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch(Dispatchers.IO) {
            context.contentResolver.openOutputStream(uri)?.use { out -> File(gcode).inputStream().use { it.copyTo(out) } }
        }
    }
    val hasModel = workspace.model != null
    val mesh = workspace.mesh

    val tools: List<Tool> = when (category) {
        PlateCategory.FILE -> listOf(
            Tool(R.string.tool_open, painterResource(R.drawable.ic_folder)) { openModel.launch(arrayOf("*/*")) },
            Tool(R.string.tool_info, rememberVector(Icons.Filled.Info), enabled = hasModel, selected = panel == Panel.MODEL) {
                panel = if (panel == Panel.MODEL) null else Panel.MODEL
            },
            Tool(R.string.tool_remove, rememberVector(Icons.Filled.Delete), enabled = hasModel) {
                workspace.model = null
                workspace.mesh = null
                workspace.info = null
                workspace.lastGcode = null
                workspace.engineering = EngineeringState()
                slicer.result = null
                panel = null
            },
        )
        PlateCategory.VIEW -> listOf(
            Tool(R.string.view_iso, painterResource(R.drawable.ic_view)) { camera.iso() },
            Tool(R.string.view_top, painterResource(R.drawable.ic_view_top)) { camera.top() },
            Tool(R.string.view_front, painterResource(R.drawable.ic_view_front)) { camera.front() },
            Tool(R.string.view_left, painterResource(R.drawable.ic_view_left)) { camera.left() },
            Tool(R.string.view_right, painterResource(R.drawable.ic_view_right)) { camera.right() },
            Tool(R.string.view_back, painterResource(R.drawable.ic_view_back)) { camera.back() },
        )
        PlateCategory.ENGINEERING -> {
            val canTap = mesh != null
            listOf(
                Tool(R.string.tool_fix, painterResource(R.drawable.ic_fix), enabled = canTap,
                    selected = eng.tool == EngineeringTool.FIX && panel == Panel.FIX) {
                    eng.tool = EngineeringTool.FIX
                    panel = Panel.FIX
                },
                Tool(R.string.tool_load, painterResource(R.drawable.ic_load), enabled = canTap,
                    selected = eng.tool == EngineeringTool.LOAD && panel == Panel.LOAD) {
                    eng.tool = EngineeringTool.LOAD
                    panel = Panel.LOAD
                },
                Tool(R.string.tool_material, painterResource(R.drawable.ic_material), enabled = canTap,
                    selected = panel == Panel.REQUIREMENTS) { panel = if (panel == Panel.REQUIREMENTS) null else Panel.REQUIREMENTS },
                Tool(R.string.eng_analyze, rememberVector(Icons.Filled.PlayArrow), enabled = eng.ready) {
                    eng.run(false)
                    panel = Panel.ANALYSIS
                },
                Tool(R.string.eng_lightest, painterResource(R.drawable.ic_infill), enabled = eng.ready) {
                    eng.run(true)
                    panel = Panel.ANALYSIS
                },
                Tool(R.string.tool_result, rememberVector(Icons.Filled.Info),
                    enabled = workspace.engineering.answer != null || eng.progress != null || eng.error != null,
                    selected = panel == Panel.ANALYSIS) { panel = if (panel == Panel.ANALYSIS) null else Panel.ANALYSIS },
                Tool(R.string.eng_clear, rememberVector(Icons.Filled.Refresh), enabled = canTap) { eng.clear() },
            )
        }
        PlateCategory.SLICE -> listOf(
            Tool(R.string.slice, painterResource(R.drawable.ic_slice), enabled = slicer.canSlice, accent = true) {
                slicer.slice()
                panel = Panel.SLICE
            },
            Tool(R.string.tool_info, rememberVector(Icons.Filled.Info),
                enabled = slicer.result != null || slicer.progress != null || slicer.error != null, selected = panel == Panel.SLICE) {
                panel = if (panel == Panel.SLICE) null else Panel.SLICE
            },
            Tool(R.string.tool_save, painterResource(R.drawable.ic_save), enabled = slicer.result != null) {
                slicer.result?.let { saveGcode.launch(File(it.gcodePath).name) }
            },
            Tool(R.string.share_gcode, rememberVector(Icons.Filled.Share), enabled = slicer.result != null) {
                slicer.result?.let { shareGcode(context, File(it.gcodePath)) }
            },
            Tool(R.string.tool_send, rememberVector(Icons.AutoMirrored.Filled.Send), enabled = slicer.result != null) { onSendToPrinter() },
        )
    }
    // Each group has its own card: another group closes the one open (but not a running analysis or slice).
    fun selectCategory(c: PlateCategory) {
        category = c
        panel = when (c) {
            PlateCategory.FILE -> if (hasModel) panel.takeIf { it == Panel.MODEL } else null
            PlateCategory.VIEW -> null
            PlateCategory.ENGINEERING -> if (mesh == null) Panel.FIX else panel.takeIf { it in ENGINEERING_PANELS }
            PlateCategory.SLICE -> panel.takeIf { it == Panel.SLICE } ?: if (slicer.progress != null || slicer.result != null) Panel.SLICE else null
        }
    }

    // The plate, with the card of the tool open under it (not over it: the model stays in view to be tapped).
    val plate: @Composable (Modifier) -> Unit = { m ->
        BoxWithConstraints(m) {
            val panelMax = maxHeight * 0.5f
            Column(Modifier.fillMaxSize()) {
                Box(Modifier.weight(1f).fillMaxWidth().background(MaterialTheme.colorScheme.surfaceContainerLowest)) {
                    val engineering = category == PlateCategory.ENGINEERING && mesh != null
                    ModelPreview(
                        mesh = mesh,
                        bedX = workspace.settings.bedX.toFloat(),
                        bedY = workspace.settings.bedY.toFloat(),
                        modelColor = if (engineering) ENGINEERING_MODEL_COLOR else MaterialTheme.colorScheme.primary,
                        bedColor = MaterialTheme.colorScheme.surfaceVariant,
                        gridColor = MaterialTheme.colorScheme.outlineVariant,
                        modifier = Modifier.fillMaxSize(),
                        triangleColors = if (engineering) remember(mesh, workspace.engineering) { eng.colors(mesh!!) } else null,
                        markers = if (engineering) eng.markers() else emptyList(),
                        onTap = if (engineering) eng::tap else null,
                        fitModel = engineering,
                        camera = camera,
                    )
                    Column(Modifier.align(Alignment.TopStart).fillMaxWidth().padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        SettingsChips(workspace, onPage)
                        workspace.model?.let { model ->
                            Text(model.name, style = MaterialTheme.typography.titleSmall, maxLines = 1, overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(horizontal = 4.dp))
                        }
                        if (engineering)
                            Text(stringResource(if (eng.tool == EngineeringTool.FIX) R.string.eng_tap_fix else R.string.eng_tap_load),
                                style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(horizontal = 4.dp))
                    }
                    if (!hasModel)
                        EmptyPlate(onOpen = { openModel.launch(arrayOf("*/*")) }, error = openError, modifier = Modifier.align(Alignment.Center))
                    if (engineering && workspace.engineering.resultColors != null)
                        Legend(Modifier.align(Alignment.BottomStart).padding(8.dp))
                    ZoomButtons(onIn = { camera.zoomAt(1.5f) }, onOut = { camera.zoomAt(1 / 1.5f) },
                        onFit = { if (engineering) camera.reset() else camera.fit(mesh, workspace.settings.bedX.toFloat(), workspace.settings.bedY.toFloat()) },
                        canFit = mesh != null, modifier = Modifier.align(Alignment.CenterEnd).padding(8.dp))
                }
                panel?.let { p ->
                    PanelCard(title = stringResource(p.title()), onClose = { panel = null }, maxHeight = panelMax) {
                        when (p) {
                            Panel.MODEL -> ModelPanel(workspace)
                            Panel.FIX -> if (mesh == null) NoMeshText(workspace) else FixturesPanel(eng)
                            Panel.LOAD -> LoadsPanel(eng)
                            Panel.REQUIREMENTS -> RequirementsPanel(eng, workspace.settings.material.type)
                            Panel.ANALYSIS -> AnalysisPanel(eng, workspace)
                            Panel.SLICE -> SlicePanel(slicer, onSendToPrinter)
                        }
                    }
                }
            }
        }
    }

    BoxWithConstraints(modifier.fillMaxSize()) {
        if (maxWidth > maxHeight) {
            // Landscape: groups, then the tools in a column, then the plate.
            Row(Modifier.fillMaxSize()) {
                NavigationRail {
                    for (c in PlateCategory.entries)
                        NavigationRailItem(selected = c == category, onClick = { selectCategory(c) },
                            icon = { Icon(c.icon(), contentDescription = null) }, label = { Text(stringResource(c.title())) })
                }
                VerticalDivider()
                LazyColumn(Modifier.width(96.dp).fillMaxHeight().testTag("tools"), contentPadding = androidx.compose.foundation.layout.PaddingValues(4.dp)) {
                    items(tools) { ToolButton(it, Modifier.fillMaxWidth().height(80.dp)) }
                }
                VerticalDivider()
                plate(Modifier.weight(1f).fillMaxHeight())
            }
        } else {
            Column(Modifier.fillMaxSize()) {
                plate(Modifier.weight(1f).fillMaxWidth())
                HorizontalDivider()
                LazyRow(Modifier.fillMaxWidth().height(84.dp).testTag("tools"), contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 4.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    items(tools) { ToolButton(it, Modifier.width(84.dp).fillMaxHeight()) }
                }
                NavigationBar {
                    for (c in PlateCategory.entries)
                        NavigationBarItem(selected = c == category, onClick = { selectCategory(c) },
                            icon = { Icon(c.icon(), contentDescription = null) }, label = { Text(stringResource(c.title())) })
                }
            }
        }
    }
}

private val ENGINEERING_PANELS = setOf(Panel.FIX, Panel.LOAD, Panel.REQUIREMENTS, Panel.ANALYSIS)

/** One tool of the strip: an icon over its name. [accent]: the main action of its group. */
private class Tool(val title: Int, val icon: Painter, val enabled: Boolean = true, val selected: Boolean = false,
                   val accent: Boolean = false, val onClick: () -> Unit)

@Composable
private fun rememberVector(v: androidx.compose.ui.graphics.vector.ImageVector) =
    androidx.compose.ui.graphics.vector.rememberVectorPainter(v)

@Composable
private fun ToolButton(t: Tool, modifier: Modifier) {
    val colors = MaterialTheme.colorScheme
    val tint = when {
        !t.enabled -> colors.onSurface.copy(alpha = 0.38f)
        t.accent || t.selected -> colors.primary
        else -> colors.onSurfaceVariant
    }
    Column(
        modifier.padding(4.dp)
            .background(if (t.selected) colors.secondaryContainer else if (t.accent && t.enabled) colors.primaryContainer.copy(alpha = 0.5f)
                else colors.surface.copy(alpha = 0f), RoundedCornerShape(16.dp))
            .clickable(enabled = t.enabled, onClick = t.onClick)
            .padding(4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(t.icon, contentDescription = null, tint = tint, modifier = Modifier.size(28.dp))
        Text(stringResource(t.title), style = MaterialTheme.typography.labelMedium, color = tint, textAlign = TextAlign.Center,
            maxLines = 2, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(top = 4.dp))
    }
}

/** Zoom in and out by steps, and frame the part: easier than a pinch with one hand. */
@Composable
private fun ZoomButtons(onIn: () -> Unit, onOut: () -> Unit, onFit: () -> Unit, canFit: Boolean, modifier: Modifier) {
    Column(modifier.background(MaterialTheme.colorScheme.surface.copy(alpha = 0.9f), RoundedCornerShape(24.dp)).padding(4.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp)) {
        IconButton(onClick = onIn) { Icon(painterResource(R.drawable.ic_zoom_in), contentDescription = stringResource(R.string.zoom_in)) }
        IconButton(onClick = onOut) { Icon(painterResource(R.drawable.ic_zoom_out), contentDescription = stringResource(R.string.zoom_out)) }
        IconButton(onClick = onFit, enabled = canFit) {
            Icon(painterResource(R.drawable.ic_zoom_fit), contentDescription = stringResource(R.string.zoom_fit))
        }
    }
}

/** The settings in use, over the plate; each one opens its page. */
@Composable
private fun SettingsChips(workspace: Workspace, onPage: (Page) -> Unit) {
    val s = workspace.settings
    val preset = workspace.printerPreset()
    val printer = if (preset != null) stringResource(R.string.catalog_printer_summary, s.printer!!.model, preset.nozzle)
        else stringResource(R.string.custom_printer)
    LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        item { SettingChip(printer, R.drawable.ic_printer) { onPage(Page.PRINTER) } }
        item { SettingChip(s.material.type, R.drawable.ic_filament) { onPage(Page.FILAMENT) } }
        item { SettingChip(stringResource(R.string.print_chip, num(s.layerHeight), s.infillPercent), R.drawable.ic_print) { onPage(Page.PRINT) } }
    }
}

@Composable
private fun SettingChip(text: String, icon: Int, onClick: () -> Unit) {
    AssistChip(onClick = onClick, label = { Text(text, maxLines = 1) },
        leadingIcon = { Icon(painterResource(icon), contentDescription = null, modifier = Modifier.size(18.dp)) },
        colors = androidx.compose.material3.AssistChipDefaults.assistChipColors(
            containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.9f)))
}

@Composable
private fun EmptyPlate(onOpen: () -> Unit, error: String?, modifier: Modifier) {
    Card(modifier.padding(24.dp), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh)) {
        EmptyPlateContent(onOpen, error)
    }
}

@Composable
private fun EmptyPlateContent(onOpen: () -> Unit, error: String?) {
    Column(Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.empty_bed), style = MaterialTheme.typography.titleMedium, textAlign = TextAlign.Center)
        Button(onClick = onOpen) {
            Icon(painterResource(R.drawable.ic_folder), contentDescription = null, modifier = Modifier.size(18.dp))
            Text(stringResource(R.string.open_model), modifier = Modifier.padding(start = 8.dp))
        }
        if (!TismaCore.available)
            Text(stringResource(R.string.core_missing), color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center,
                style = MaterialTheme.typography.bodySmall)
        error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    }
}

/** The card of the tool open, under the plate: as high as its content up to [maxHeight], with a title and a close button. */
@Composable
private fun PanelCard(title: String, onClose: () -> Unit, maxHeight: Dp, content: @Composable ColumnScope.() -> Unit) {
    Surface(Modifier.fillMaxWidth().heightIn(max = maxHeight), color = MaterialTheme.colorScheme.surfaceContainer,
        shape = RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp), shadowElevation = 6.dp) {
        Column {
            Row(Modifier.padding(start = 16.dp, end = 4.dp, top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                IconButton(onClick = onClose) { Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.close)) }
            }
            Column(Modifier.verticalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, bottom = 16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp), content = content)
        }
    }
}

@Composable
private fun ModelPanel(workspace: Workspace) {
    val info = workspace.info
    workspace.model?.let { Text(it.name, style = MaterialTheme.typography.bodyLarge) }
    if (info != null) {
        Text(stringResource(R.string.model_size, mm(info.sizeMm[0]), mm(info.sizeMm[1]), mm(info.sizeMm[2])))
        Text(stringResource(R.string.model_objects, info.objects, info.triangles))
    } else workspace.mesh?.let { m ->
        Text(stringResource(R.string.model_size, mm((m.max[0] - m.min[0]).toDouble()), mm((m.max[1] - m.min[1]).toDouble()),
            mm((m.max[2] - m.min[2]).toDouble())))
    }
    if (workspace.mesh == null) Text(stringResource(R.string.no_preview), style = MaterialTheme.typography.bodySmall)
    Text(stringResource(R.string.drag_to_turn), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun NoMeshText(workspace: Workspace) =
    Text(stringResource(if (workspace.model == null) R.string.eng_no_model else R.string.eng_no_preview))

@Composable
private fun SlicePanel(slicer: SliceController, onSendToPrinter: () -> Unit) {
    if (!TismaCore.available) Text(stringResource(R.string.core_missing), color = MaterialTheme.colorScheme.error)
    slicer.progress?.let { (percent, text) ->
        Text(stringResource(R.string.slicing, percent), style = MaterialTheme.typography.bodyMedium)
        LinearProgressIndicator(progress = { percent / 100f }, modifier = Modifier.fillMaxWidth())
        if (text.isNotEmpty()) Text(text, style = MaterialTheme.typography.bodySmall)
    }
    slicer.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    slicer.result?.let { r ->
        Text(stringResource(R.string.slice_done), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
        Text(stringResource(R.string.slice_time, r.printTime))
        Text(stringResource(R.string.slice_filament, String.format(Locale.ROOT, "%.2f", r.filamentMm / 1000),
            String.format(Locale.ROOT, "%.1f", r.filamentGrams)))
        Button(onClick = onSendToPrinter, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.send_to_printer)) }
    }
}

private fun Panel.title(): Int = when (this) {
    Panel.MODEL -> R.string.panel_model
    Panel.FIX -> R.string.eng_tool_fix
    Panel.LOAD -> R.string.eng_tool_load
    Panel.REQUIREMENTS -> R.string.eng_requirements
    Panel.ANALYSIS -> R.string.panel_analysis
    Panel.SLICE -> R.string.panel_slice
}

private fun PlateCategory.title(): Int = when (this) {
    PlateCategory.FILE -> R.string.cat_file
    PlateCategory.VIEW -> R.string.cat_view
    PlateCategory.ENGINEERING -> R.string.tab_engineering
    PlateCategory.SLICE -> R.string.slice
}

@Composable
private fun PlateCategory.icon(): Painter = when (this) {
    PlateCategory.FILE -> painterResource(R.drawable.ic_folder)
    PlateCategory.VIEW -> painterResource(R.drawable.ic_view)
    PlateCategory.ENGINEERING -> painterResource(R.drawable.ic_engineering)
    PlateCategory.SLICE -> painterResource(R.drawable.ic_slice)
}
