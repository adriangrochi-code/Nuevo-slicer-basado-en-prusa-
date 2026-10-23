package uy.com.tisma.app.ui

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.R
import uy.com.tisma.app.core.AnalysisListener
import uy.com.tisma.app.core.AnalysisMaterial
import uy.com.tisma.app.core.AnalysisQuality
import uy.com.tisma.app.core.EngineeringAnswer
import uy.com.tisma.app.core.EngineeringLoad
import uy.com.tisma.app.core.EngineeringRequest
import uy.com.tisma.app.core.FieldMap
import uy.com.tisma.app.core.PickedFace
import uy.com.tisma.app.core.PreviewMesh
import uy.com.tisma.app.core.TismaCore
import uy.com.tisma.app.core.Verdict
import java.io.File
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.ceil

enum class EngineeringTool { FIX, LOAD }

/** Directions of a new load; [INTO_FACE] pushes against the face that is touched. */
enum class LoadDirection(val vector: DoubleArray?) {
    DOWN(doubleArrayOf(0.0, 0.0, -1.0)), UP(doubleArrayOf(0.0, 0.0, 1.0)),
    X_PLUS(doubleArrayOf(1.0, 0.0, 0.0)), X_MINUS(doubleArrayOf(-1.0, 0.0, 0.0)),
    Y_PLUS(doubleArrayOf(0.0, 1.0, 0.0)), Y_MINUS(doubleArrayOf(0.0, -1.0, 0.0)),
    INTO_FACE(null),
}

private val FIXED_COLOR = Color(0xFF3B82F6)
private val LOAD_COLOR = Color(0xFFF97316)
// Neutral, so that the fixed faces, the loads and the result stand out.
internal val ENGINEERING_MODEL_COLOR = Color(0xFFB8B8C8)

/**
 * Structural analysis of the model on the phone, with the analysis of the desktop program (libtisma_fea): where the
 * part is held, the loads, the material; whether it holds, and the lightest infill that does. The setup and the
 * answer live in the workspace; this holds the tool in use and runs the analysis.
 */
@Stable
class EngineeringController(private val workspace: Workspace, private val context: Context, private val scope: CoroutineScope) {
    var tool by mutableStateOf(EngineeringTool.FIX)
    var force by mutableStateOf(20.0)
    var direction by mutableStateOf(LoadDirection.DOWN)
    var wholeFace by mutableStateOf(false)
    var progress by mutableStateOf<Int?>(null)
        private set
    var error by mutableStateOf<String?>(null)
    var materials by mutableStateOf<List<AnalysisMaterial>>(emptyList())
        private set
    private val cancel = AtomicBoolean(false)

    val request get() = workspace.engineering.request
    val ready get() = TismaCore.available && request.ready && progress == null

    suspend fun loadMaterials() {
        if (TismaCore.available && materials.isEmpty()) materials = withContext(Dispatchers.Default) { TismaCore.materials() }
    }

    fun setup(change: (EngineeringRequest) -> EngineeringRequest) {
        // A new setup: the result no longer applies.
        workspace.engineering = EngineeringState(change(request))
        error = null
    }

    /** A tap on the model: a fixed face or a load, as the tool says. */
    fun tap(hit: PreviewHit) {
        val face = PickedFace(hit.point.map { it.toDouble() }, hit.normal.map { it.toDouble() })
        when (tool) {
            EngineeringTool.FIX -> setup { it.copy(fixtures = it.fixtures + face) }
            EngineeringTool.LOAD -> {
                val dir = direction.vector ?: doubleArrayOf(-face.normal[0], -face.normal[1], -face.normal[2])
                setup { it.copy(loads = it.loads + EngineeringLoad(face, dir.map { d -> d * force }, wholeFace)) }
            }
        }
    }

    fun clear() = setup { EngineeringRequest(material = it.material, safetyFactor = it.safetyFactor,
        temperature = it.temperature, quality = it.quality) }

    fun cancel() = cancel.set(true)

    /** Analyses the setup; [lightest] also looks for the lightest infill that holds. */
    fun run(lightest: Boolean, onDone: () -> Unit = {}) {
        val model = workspace.model ?: return
        val m = workspace.mesh ?: return
        val req = request
        error = null
        cancel.set(false)
        progress = 0
        scope.launch {
            try {
                val ini = File(context.cacheDir, "analysis.ini")
                val field = File(context.cacheDir, "analysis.field")
                val text = workspace.ini()
                val answer = withContext(Dispatchers.Default) {
                    ini.writeText(text)
                    TismaCore.analyze(model.absolutePath, ini.absolutePath, req.toJson(lightest), field.absolutePath,
                        object : AnalysisListener {
                            override fun onProgress(percent: Int, text: String) {
                                scope.launch(Dispatchers.Main) { progress = percent }
                            }
                            override fun cancelled() = cancel.get()
                        })
                }
                val colors = withContext(Dispatchers.Default) { resultColors(m, FieldMap.read(field), req.safetyFactor) }
                workspace.engineering = EngineeringState(req, answer, colors)
                onDone()
            } catch (e: Exception) {
                error = e.message ?: e.toString()
            } finally {
                progress = null
            }
        }
    }

    /** The colours of the model: the result, or the fixed faces and the loads. */
    fun colors(mesh: PreviewMesh): IntArray = workspace.engineering.resultColors ?: setupColors(mesh, request)

    /** The loads on a point, with an arrow of their force. */
    fun markers(): List<PreviewMarker> = request.loads.filter { !it.wholeFace }.map { l ->
        val mag = l.magnitude.coerceAtLeast(1e-9)
        PreviewMarker(l.at.point.map { it.toFloat() }.toFloatArray(), l.force.map { (it / mag).toFloat() }.toFloatArray(), LOAD_COLOR)
    }
}

/** Where the part is held: by the bed, by the faces tapped. */
@Composable
fun FixturesPanel(c: EngineeringController) {
    val req = c.request
    SwitchRow(stringResource(R.string.eng_bed_fixture), req.bedFixture) { v -> c.setup { it.copy(bedFixture = v) } }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.eng_fixed_faces, req.fixtures.size), Modifier.weight(1f))
        if (req.fixtures.isNotEmpty())
            TextButton(onClick = { c.setup { it.copy(fixtures = emptyList()) } }) { Text(stringResource(R.string.eng_clear)) }
    }
}

/** The loads there are, and the load the next tap adds. */
@Composable
fun LoadsPanel(c: EngineeringController) {
    val req = c.request
    req.loads.forEachIndexed { i, l ->
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.width(10.dp).height(10.dp).background(LOAD_COLOR, RoundedCornerShape(5.dp)))
            Text(stringResource(if (l.wholeFace) R.string.eng_load_face else R.string.eng_load_point, i + 1, num(l.magnitude)),
                Modifier.weight(1f).padding(start = 8.dp))
            IconButton(onClick = { c.setup { it.copy(loads = it.loads.filterIndexed { j, _ -> j != i }) } }) {
                Icon(Icons.Filled.Delete, contentDescription = stringResource(R.string.eng_remove_load))
            }
        }
    }
    NumberField(stringResource(R.string.eng_force), c.force, "N", Modifier.fillMaxWidth()) { c.force = it }
    Choices(stringResource(R.string.eng_direction), LoadDirection.entries, c.direction, { stringResource(it.label()) }) {
        c.direction = it
    }
    Choices(stringResource(R.string.eng_load_on), listOf(false, true), c.wholeFace,
        { stringResource(if (it) R.string.eng_whole_face else R.string.eng_point) }) { c.wholeFace = it }
}

/** Material, safety factor, temperature and the fineness of the analysis. */
@Composable
fun RequirementsPanel(c: EngineeringController, filament: String) {
    val req = c.request
    val fromFilament = stringResource(R.string.eng_material_filament, filament)
    Choices(stringResource(R.string.eng_material), listOf("") + c.materials.filter { it.linear }.map { it.key }, req.material,
        { key -> if (key.isEmpty()) fromFilament else c.materials.firstOrNull { it.key == key }?.name ?: key }) { k ->
        c.setup { it.copy(material = k) }
    }
    Choices(stringResource(R.string.eng_safety_factor), listOf(1.5, 2.0, 3.0, 4.0), req.safetyFactor, { num(it) }) { v ->
        c.setup { it.copy(safetyFactor = v) }
    }
    NumberField(stringResource(R.string.eng_temperature), req.temperature, "°C", Modifier.fillMaxWidth()) { v ->
        c.setup { it.copy(temperature = v) }
    }
    Choices(stringResource(R.string.eng_quality), AnalysisQuality.entries, req.quality, { stringResource(it.label()) }) { q ->
        c.setup { it.copy(quality = q) }
    }
}

/** The analysis running, its error, or its answer. */
@Composable
fun AnalysisPanel(c: EngineeringController, workspace: Workspace) {
    if (!TismaCore.available)
        Text(stringResource(R.string.core_missing), color = MaterialTheme.colorScheme.error)
    else if (!c.request.ready)
        Text(stringResource(R.string.eng_not_ready), color = MaterialTheme.colorScheme.onSurfaceVariant)
    c.progress?.let { p ->
        Text(stringResource(R.string.eng_running, p), style = MaterialTheme.typography.bodyMedium)
        LinearProgressIndicator(progress = { p / 100f }, modifier = Modifier.fillMaxWidth())
        TextButton(onClick = { c.cancel() }) { Text(stringResource(R.string.eng_cancel)) }
    }
    c.error?.let { Text(errorText(it), color = MaterialTheme.colorScheme.error) }
    workspace.engineering.answer?.let { a ->
        AnswerCard(a, c.request.safetyFactor, onUseInfill = { percent ->
            workspace.update(workspace.settings.copy(infillPercent = percent))
        }, currentInfill = workspace.settings.infillPercent)
    }
}

@Composable
private fun AnswerCard(a: EngineeringAnswer, required: Double, currentInfill: Int, onUseInfill: (Int) -> Unit) {
    val r = a.result
    val good = r.verdict == Verdict.HOLDS
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(
        containerColor = if (good) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.errorContainer)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(stringResource(r.verdict.label()), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.eng_safety, fmt(r.safetyFactor, 2), num(required)))
            Text(stringResource(R.string.eng_stress, fmt(r.maxStress, 1)))
            Text(stringResource(R.string.eng_displacement, fmt(r.maxDisplacement, 2)))
            Text(stringResource(R.string.eng_mass, fmt(r.mass, 1), a.material))
            if (r.alternatives.isNotEmpty())
                Text(stringResource(R.string.eng_alternatives, r.alternatives.joinToString(", ") { "${it.first} (${fmt(it.second, 1)})" }),
                    style = MaterialTheme.typography.bodySmall)
            a.lightestDensity?.let { d ->
                if (a.feasible) {
                    // Whole percents, rounded up so that the part still holds.
                    val percent = ceil(d * 100 - 1e-6).toInt().coerceIn(0, 100)
                    Text(stringResource(R.string.eng_lightest_result, percent), style = MaterialTheme.typography.titleSmall,
                        modifier = Modifier.padding(top = 6.dp))
                    if (percent != currentInfill)
                        Button(onClick = { onUseInfill(percent) }) { Text(stringResource(R.string.eng_use_infill, percent)) }
                    else
                        Text(stringResource(R.string.eng_infill_in_use), style = MaterialTheme.typography.bodySmall)
                } else
                    Text(stringResource(R.string.eng_not_feasible), modifier = Modifier.padding(top = 6.dp))
            }
            Text(stringResource(R.string.eng_voxels, r.voxels, fmt(r.voxelSize, 2)), style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
internal fun Legend(modifier: Modifier) {
    Column(modifier.background(MaterialTheme.colorScheme.surface.copy(alpha = 0.85f), RoundedCornerShape(8.dp)).padding(8.dp)) {
        Box(Modifier.width(160.dp).height(10.dp).background(Brush.horizontalGradient(SCALE.map { Color(it.second) }),
            RoundedCornerShape(5.dp)))
        Row(Modifier.width(160.dp)) {
            Text(stringResource(R.string.eng_legend_low), style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            Text(stringResource(R.string.eng_legend_high), style = MaterialTheme.typography.labelSmall)
        }
    }
}

// Colour scale of the result on (failure index × safety factor required): blue (little stress), green, yellow at
// the safety factor required, red where the part breaks.
private val SCALE = listOf(0.0 to 0xFF2563EB.toInt(), 0.5 to 0xFF22C55E.toInt(), 1.0 to 0xFFFACC15.toInt(), 1.5 to 0xFFF97316.toInt(),
    2.0 to 0xFFDC2626.toInt())

private fun scaleColor(x: Double): Int {
    if (x <= SCALE.first().first) return SCALE.first().second
    for (i in 1 until SCALE.size) {
        val (x1, c1) = SCALE[i]
        val (x0, c0) = SCALE[i - 1]
        if (x <= x1) {
            val t = ((x - x0) / (x1 - x0)).toFloat()
            val a = Color(c0); val b = Color(c1)
            return Color(a.red + (b.red - a.red) * t, a.green + (b.green - a.green) * t, a.blue + (b.blue - a.blue) * t).toArgb()
        }
    }
    return SCALE.last().second
}

/** The colour of each corner of the triangles of the preview for the failure index around it. */
internal fun resultColors(mesh: PreviewMesh, field: FieldMap, required: Double): IntArray = IntArray(mesh.triangleCount * 3) { i ->
    val v = mesh.vertices
    val fi = field.at(v[i * 3].toDouble(), v[i * 3 + 1].toDouble(), v[i * 3 + 2].toDouble()) ?: return@IntArray 0
    // 1 / required is the limit: scale so that it is 1 (yellow) and breaking (fi = 1) is red.
    val x = fi * required
    scaleColor(if (x <= 1.0) x else 1.0 + (x - 1.0) / maxOf(required - 1.0, 1e-6))
}

/** Fixed faces (and the faces on the bed) in blue, faces with a load in orange. */
private fun setupColors(mesh: PreviewMesh, req: EngineeringRequest): IntArray {
    val out = IntArray(mesh.triangleCount)
    val fixed = FIXED_COLOR.toArgb()
    if (req.bedFixture) for (t in mesh.bedFaces()) out[t] = fixed
    fun nearest(face: PickedFace): Int? {
        var best = -1
        var bestD = Double.MAX_VALUE
        for (t in 0 until mesh.triangleCount) {
            val n = mesh.normals
            if (n[t * 3] * face.normal[0] + n[t * 3 + 1] * face.normal[1] + n[t * 3 + 2] * face.normal[2] < 0.9) continue
            val c = mesh.centroid(t)
            val d = (c[0] - face.point[0]).let { it * it } + (c[1] - face.point[1]).let { it * it } + (c[2] - face.point[2]).let { it * it }
            if (d < bestD) { bestD = d; best = t }
        }
        return best.takeIf { it >= 0 }
    }
    for (f in req.fixtures) nearest(f)?.let { t -> for (x in mesh.flatRegion(t)) out[x] = fixed }
    val load = LOAD_COLOR.toArgb()
    for (l in req.loads.filter { it.wholeFace }) nearest(l.at)?.let { t -> for (x in mesh.flatRegion(t)) out[x] = load }
    return out
}

private fun LoadDirection.label(): Int = when (this) {
    LoadDirection.DOWN -> R.string.eng_dir_down
    LoadDirection.UP -> R.string.eng_dir_up
    LoadDirection.X_PLUS -> R.string.eng_dir_x_plus
    LoadDirection.X_MINUS -> R.string.eng_dir_x_minus
    LoadDirection.Y_PLUS -> R.string.eng_dir_y_plus
    LoadDirection.Y_MINUS -> R.string.eng_dir_y_minus
    LoadDirection.INTO_FACE -> R.string.eng_dir_into_face
}

private fun AnalysisQuality.label(): Int = when (this) {
    AnalysisQuality.FAST -> R.string.eng_quality_fast
    AnalysisQuality.NORMAL -> R.string.eng_quality_normal
    AnalysisQuality.HIGH -> R.string.eng_quality_high
}

private fun Verdict.label(): Int = when (this) {
    Verdict.HOLDS -> R.string.eng_verdict_holds
    Verdict.LOW_MARGIN -> R.string.eng_verdict_low_margin
    Verdict.OUT_OF_LOAD -> R.string.eng_verdict_out_of_load
    Verdict.TOO_FLEXIBLE -> R.string.eng_verdict_too_flexible
    Verdict.OUT_OF_TEMPERATURE -> R.string.eng_verdict_out_of_temperature
    Verdict.NOT_APPLICABLE -> R.string.eng_verdict_not_applicable
}

@Composable
private fun errorText(e: String): String = when (e) {
    "no_fixture" -> stringResource(R.string.eng_error_no_fixture)
    "no_load" -> stringResource(R.string.eng_error_no_load)
    "Cancelled" -> stringResource(R.string.eng_cancelled)
    else -> e
}

private fun fmt(v: Double, decimals: Int) = String.format(Locale.ROOT, "%.${decimals}f", v)
