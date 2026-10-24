package uy.com.tisma.app.ui

import android.content.Context
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.R
import uy.com.tisma.app.printers.BambuAccessCodeException
import uy.com.tisma.app.printers.BambuClient
import uy.com.tisma.app.printers.BambuTls
import uy.com.tisma.app.printers.CameraUnsupportedException
import uy.com.tisma.app.printers.DiscoveredPrinter
import uy.com.tisma.app.printers.HttpException
import uy.com.tisma.app.printers.MoonrakerClient
import uy.com.tisma.app.printers.NoCameraException
import uy.com.tisma.app.printers.OctoPrintClient
import uy.com.tisma.app.printers.PrinterKind
import uy.com.tisma.app.printers.PrinterLink
import uy.com.tisma.app.printers.PrinterState
import uy.com.tisma.app.printers.PrinterStore
import uy.com.tisma.app.printers.SavedPrinter
import uy.com.tisma.app.printers.discoverBambu
import java.util.Locale
import kotlin.coroutines.coroutineContext

private fun caPem(context: Context) = { context.assets.open("bambu_printer_ca.pem") }

/** The printers of the user over Wi-Fi: Bambu Lab (LAN mode), Klipper and OctoPrint; their camera, state and prints. */
@Composable
fun PrintersScreen(workspace: Workspace, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val store = remember { PrinterStore(context) }
    val printers = remember { mutableStateListOf<SavedPrinter>().apply { addAll(store.load()) } }
    var selected by remember { mutableStateOf<String?>(null) }
    var editing by remember { mutableStateOf<SavedPrinter?>(null) }
    fun save() = store.save(printers.toList())

    val current = printers.firstOrNull { it.id == selected }
    if (current != null) {
        PrinterDetail(workspace, current, onBack = { selected = null }, onEdit = { editing = current },
            onDelete = {
                printers.remove(current)
                save()
                selected = null
            }, modifier = modifier)
    } else {
        PrinterList(printers, onOpen = { selected = it.id }, onAdd = { editing = it }, modifier = modifier)
    }
    editing?.let { initial ->
        PrinterDialog(initial, onDismiss = { editing = null }, onSave = { p ->
            val i = printers.indexOfFirst { it.id == p.id }
            if (i >= 0) printers[i] = p else printers += p
            save()
            editing = null
            selected = p.id
        })
    }
}

@Composable
private fun PrinterList(printers: List<SavedPrinter>, onOpen: (SavedPrinter) -> Unit, onAdd: (SavedPrinter) -> Unit,
                        modifier: Modifier) {
    val context = LocalContext.current
    val found = remember { mutableStateListOf<DiscoveredPrinter>() }
    // The Bambu Lab printers announce themselves every few seconds while this list is shown.
    LaunchedEffect(Unit) {
        withContext(Dispatchers.IO) {
            val job = coroutineContext.job
            runCatching {
                discoverBambu(context, stop = { !job.isActive }) { p ->
                    if (found.none { it.serial == p.serial }) found += p
                }
            }
        }
    }
    Box(modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(stringResource(R.string.my_printers), style = MaterialTheme.typography.titleMedium)
            if (printers.isEmpty())
                Text(stringResource(R.string.no_printers), color = MaterialTheme.colorScheme.onSurfaceVariant)
            for (p in printers)
                Card(Modifier.fillMaxWidth().clickable { onOpen(p) }) {
                    Column(Modifier.padding(14.dp)) {
                        Text(p.name, style = MaterialTheme.typography.titleSmall)
                        Text(kindLabel(p.kind) + " · " + p.host, style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            val newOnes = found.filter { f -> printers.none { it.serial.equals(f.serial, true) || it.host == f.ip } }
            Text(stringResource(R.string.on_the_network), style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(top = 8.dp))
            if (newOnes.isEmpty())
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CircularProgressIndicator(Modifier.height(16.dp).aspectRatio(1f), strokeWidth = 2.dp)
                    Text(stringResource(R.string.searching_printers), style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            for (f in newOnes)
                Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)) {
                    Row(Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(f.name, style = MaterialTheme.typography.titleSmall)
                            Text("Bambu Lab ${f.model} · ${f.ip}", style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        TextButton(onClick = {
                            onAdd(SavedPrinter(kind = PrinterKind.BAMBU, name = f.name, host = f.ip, serial = f.serial))
                        }) { Text(stringResource(R.string.add)) }
                    }
                }
            Box(Modifier.height(72.dp))
        }
        ExtendedFloatingActionButton(
            onClick = { onAdd(SavedPrinter(kind = PrinterKind.BAMBU, name = "", host = "")) },
            icon = { Icon(Icons.Filled.Add, contentDescription = null) },
            text = { Text(stringResource(R.string.add_printer)) },
            modifier = Modifier.align(Alignment.BottomEnd).padding(16.dp),
        )
    }
}

@Composable
private fun kindLabel(kind: PrinterKind) = when (kind) {
    PrinterKind.BAMBU -> "Bambu Lab"
    PrinterKind.KLIPPER -> "Klipper"
    PrinterKind.OCTOPRINT -> "OctoPrint"
}

/** Adding or changing a printer: tested before it is saved. */
@Composable
private fun PrinterDialog(initial: SavedPrinter, onDismiss: () -> Unit, onSave: (SavedPrinter) -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var kind by remember { mutableStateOf(initial.kind) }
    var name by remember { mutableStateOf(initial.name) }
    var host by remember { mutableStateOf(initial.host) }
    var secret by remember { mutableStateOf(initial.secret) }
    var testing by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val texts = ErrorTexts.of()
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(if (initial.name.isEmpty()) R.string.add_printer else R.string.edit_printer)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Choices(stringResource(R.string.printer_kind), PrinterKind.entries, kind, { kindLabel(it) }) { kind = it }
                OutlinedTextField(name, { name = it }, label = { Text(stringResource(R.string.printer_name)) }, singleLine = true)
                OutlinedTextField(host, { host = it.trim() }, singleLine = true,
                    label = { Text(stringResource(if (kind == PrinterKind.BAMBU) R.string.printer_ip else R.string.printer_address)) },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
                OutlinedTextField(secret, { secret = it.trim() }, singleLine = true,
                    label = { Text(stringResource(when (kind) {
                        PrinterKind.BAMBU -> R.string.access_code
                        PrinterKind.KLIPPER -> R.string.api_key_optional
                        PrinterKind.OCTOPRINT -> R.string.api_key
                    })) },
                    visualTransformation = PasswordVisualTransformation())
                Text(stringResource(when (kind) {
                    PrinterKind.BAMBU -> R.string.bambu_hint
                    PrinterKind.KLIPPER -> R.string.klipper_hint
                    PrinterKind.OCTOPRINT -> R.string.octoprint_hint
                }), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                if (testing) LinearProgressIndicator(Modifier.fillMaxWidth())
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
        },
        confirmButton = {
            Button(enabled = !testing && host.isNotEmpty() && (kind != PrinterKind.BAMBU || secret.isNotEmpty()) &&
                (kind != PrinterKind.OCTOPRINT || secret.isNotEmpty()), onClick = {
                testing = true
                error = null
                // A changed address or kind: the serial number is learnt again from the printer.
                val serial = if (kind == initial.kind && host == initial.host) initial.serial else ""
                val printer = initial.copy(kind = kind, name = name.ifBlank { host }, host = host, secret = secret, serial = serial)
                scope.launch {
                    try {
                        val learnt = withContext(Dispatchers.IO) { testPrinter(context, printer) }
                        onSave(printer.copy(serial = learnt.ifEmpty { printer.serial }))
                    } catch (e: Exception) {
                        error = texts.describe(e)
                    } finally {
                        testing = false
                    }
                }
            }) { Text(stringResource(R.string.test_and_save)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

/** Connects once; returns the serial number of a Bambu Lab printer (empty for the others). */
private fun testPrinter(context: Context, p: SavedPrinter): String = when (p.kind) {
    PrinterKind.BAMBU -> {
        val tls = caPem(context)().use { BambuTls(it, p.serial.ifEmpty { null }) }
        var serial = ""
        val started = System.currentTimeMillis()
        BambuClient(p.host, p.secret, tls).watch(stop = { serial.isNotEmpty() || System.currentTimeMillis() - started > 15_000 }) {
            serial = it.serial
        }
        if (serial.isEmpty()) throw java.io.IOException("no answer")
        serial
    }
    PrinterKind.KLIPPER -> "".also { MoonrakerClient(p.host, p.secret).status() }
    PrinterKind.OCTOPRINT -> "".also { OctoPrintClient(p.host, p.secret).status() }
}

/** Messages of the errors of the connections, in the language of the app. */
private class ErrorTexts(val accessCode: String, val apiKey: String, val unreachable: String, val rtsps: String,
                         val noCamera: String, val other: String) {
    fun describe(e: Throwable): String = when (e) {
        is BambuAccessCodeException -> accessCode
        is HttpException -> if (e.code == 401 || e.code == 403) apiKey else other.format(e.message)
        is CameraUnsupportedException -> rtsps
        is NoCameraException -> noCamera
        is java.net.ConnectException, is java.net.SocketTimeoutException, is java.net.UnknownHostException,
        is java.net.NoRouteToHostException -> unreachable
        else -> other.format(e.message ?: e.toString())
    }

    companion object {
        @Composable
        fun of() = ErrorTexts(
            stringResource(R.string.error_access_code), stringResource(R.string.error_api_key),
            stringResource(R.string.error_unreachable), stringResource(R.string.camera_rtsps),
            stringResource(R.string.camera_none), stringResource(R.string.error_other),
        )
    }
}

@Composable
internal fun PrinterDetail(workspace: Workspace, printer: SavedPrinter, onBack: () -> Unit, onEdit: () -> Unit,
                          onDelete: () -> Unit, modifier: Modifier) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val link = remember(printer) { PrinterLink(printer, caPem(context), context.cacheDir) }
    val texts = ErrorTexts.of()
    var state by remember(printer) { mutableStateOf<PrinterState?>(null) }
    var stateError by remember(printer) { mutableStateOf<String?>(null) }
    var frame by remember(printer) { mutableStateOf<ImageBitmap?>(null) }
    var cameraMessage by remember(printer) { mutableStateOf<String?>(null) }
    var confirmDelete by remember { mutableStateOf(false) }
    var confirmStop by remember { mutableStateOf(false) }

    // The state, again after an error while the screen is shown.
    LaunchedEffect(printer) {
        withContext(Dispatchers.IO) {
            val job = coroutineContext.job
            while (job.isActive) {
                try {
                    link.watch(stop = { !job.isActive }) {
                        state = it
                        stateError = null
                    }
                } catch (e: Exception) {
                    stateError = texts.describe(e)
                    if (e is BambuAccessCodeException) return@withContext
                }
                delay(5_000)
            }
        }
    }
    // The camera, the same way.
    LaunchedEffect(printer) {
        withContext(Dispatchers.IO) {
            val job = coroutineContext.job
            while (job.isActive) {
                try {
                    link.camera(stop = { !job.isActive }) { jpeg ->
                        BitmapFactory.decodeByteArray(jpeg, 0, jpeg.size)?.let {
                            frame = it.asImageBitmap()
                            cameraMessage = null
                        }
                    }
                } catch (e: Exception) {
                    cameraMessage = texts.describe(e)
                    if (e is CameraUnsupportedException || e is NoCameraException || e is BambuAccessCodeException) return@withContext
                }
                delay(5_000)
            }
        }
    }

    Column(modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.Filled.ArrowBack, contentDescription = null) }
            Column(Modifier.weight(1f)) {
                Text(printer.name, style = MaterialTheme.typography.titleMedium)
                Text(kindLabel(printer.kind) + " · " + printer.host, style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            IconButton(onClick = onEdit) { Icon(Icons.Filled.Edit, contentDescription = null) }
            IconButton(onClick = { confirmDelete = true }) { Icon(Icons.Filled.Delete, contentDescription = null) }
        }
        // Camera.
        Box(Modifier.fillMaxWidth().aspectRatio(16f / 9f).background(Color(0xFF15151C), RoundedCornerShape(12.dp)),
            contentAlignment = Alignment.Center) {
            val f = frame
            if (f != null)
                Image(f, contentDescription = null, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
            else if (cameraMessage != null)
                Text(cameraMessage!!, color = Color.White, modifier = Modifier.padding(16.dp))
            else
                CircularProgressIndicator()
        }
        // State.
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                val s = state
                when {
                    s == null && stateError != null -> Text(stateError!!, color = MaterialTheme.colorScheme.error)
                    s == null -> Text(stringResource(R.string.connecting))
                    else -> {
                        Text(stateLabel(s.state), style = MaterialTheme.typography.titleMedium)
                        if (s.jobName.isNotEmpty()) Text(s.jobName, style = MaterialTheme.typography.bodySmall)
                        if (s.active && s.percent >= 0) {
                            LinearProgressIndicator(progress = { s.percent / 100f }, modifier = Modifier.fillMaxWidth())
                            Text(buildString {
                                append("${s.percent} %")
                                if (s.remainingMin >= 0) append(" · " + remaining(s.remainingMin))
                                if (s.layer >= 0 && s.totalLayers > 0) append(" · ${s.layer}/${s.totalLayers}")
                            }, style = MaterialTheme.typography.bodySmall)
                        }
                        Text(stringResource(R.string.temperatures, temp(s.nozzleTemp, s.nozzleTarget), temp(s.bedTemp, s.bedTarget)))
                        stateError?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
                        if (s.active)
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 6.dp)) {
                                OutlinedButton(onClick = {
                                    scope.launch(Dispatchers.IO) {
                                        runCatching { if (s.state == "paused") link.resume() else link.pause() }
                                            .onFailure { stateError = texts.describe(it) }
                                    }
                                }) { Text(stringResource(if (s.state == "paused") R.string.resume else R.string.pause)) }
                                OutlinedButton(onClick = { confirmStop = true }) { Text(stringResource(R.string.stop)) }
                            }
                    }
                }
            }
        }
        SendCard(workspace, printer, link, texts)
        Box(Modifier.height(24.dp))
    }
    if (confirmDelete)
        AlertDialog(onDismissRequest = { confirmDelete = false },
            text = { Text(stringResource(R.string.delete_printer_question, printer.name)) },
            confirmButton = { TextButton(onClick = { confirmDelete = false; onDelete() }) { Text(stringResource(R.string.delete)) } },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text(stringResource(R.string.cancel)) } })
    if (confirmStop)
        AlertDialog(onDismissRequest = { confirmStop = false },
            text = { Text(stringResource(R.string.stop_question)) },
            confirmButton = {
                TextButton(onClick = {
                    confirmStop = false
                    scope.launch(Dispatchers.IO) { runCatching { link.stopPrint() }.onFailure { stateError = texts.describe(it) } }
                }) { Text(stringResource(R.string.stop)) }
            },
            dismissButton = { TextButton(onClick = { confirmStop = false }) { Text(stringResource(R.string.cancel)) } })
}

/** Sending the last sliced G-code to the printer. */
@Composable
private fun SendCard(workspace: Workspace, printer: SavedPrinter, link: PrinterLink, texts: ErrorTexts) {
    val scope = rememberCoroutineScope()
    val gcode = workspace.lastGcode
    var start by remember { mutableStateOf(true) }
    var useAms by remember { mutableStateOf(false) }
    var progress by remember { mutableStateOf<Float?>(null) }
    var message by remember { mutableStateOf<Pair<Boolean, String>?>(null) }
    val sentText = stringResource(R.string.sent)
    val startedText = stringResource(R.string.sent_and_started)
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.send_gcode), style = MaterialTheme.typography.titleSmall)
            if (gcode == null) {
                Text(stringResource(R.string.slice_first), color = MaterialTheme.colorScheme.onSurfaceVariant)
                return@Column
            }
            Text(gcode.name, style = MaterialTheme.typography.bodySmall)
            SwitchRow(stringResource(R.string.start_after_sending), start) { start = it }
            if (printer.kind == PrinterKind.BAMBU)
                SwitchRow(stringResource(R.string.use_ams), useAms) { useAms = it }
            progress?.let { LinearProgressIndicator(progress = { it }, modifier = Modifier.fillMaxWidth()) }
            message?.let { (ok, text) ->
                Text(text, color = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
            }
            Button(enabled = progress == null, modifier = Modifier.fillMaxWidth(), onClick = {
                progress = 0f
                message = null
                scope.launch {
                    try {
                        val name = withContext(Dispatchers.IO) {
                            link.send(gcode, gcode.name, start, useAms) { sent, total ->
                                progress = if (total > 0) sent.toFloat() / total else null
                                true
                            }
                        }
                        message = true to (if (start) startedText else sentText).format(name)
                    } catch (e: Exception) {
                        message = false to texts.describe(e)
                    } finally {
                        progress = null
                    }
                }
            }) { Text(stringResource(R.string.send)) }
        }
    }
}

@Composable
private fun stateLabel(state: String): String = when (state) {
    "printing" -> stringResource(R.string.state_printing)
    "paused" -> stringResource(R.string.state_paused)
    "preparing" -> stringResource(R.string.state_preparing)
    "idle" -> stringResource(R.string.state_idle)
    "finished" -> stringResource(R.string.state_finished)
    "failed" -> stringResource(R.string.state_failed)
    "offline" -> stringResource(R.string.state_offline)
    else -> state.ifEmpty { "—" }
}

private fun temp(actual: Double, target: Double): String =
    if (actual < 0) "—" else String.format(Locale.ROOT, "%.0f", actual) + (if (target > 0) String.format(Locale.ROOT, "/%.0f", target) else "") + " °C"

private fun remaining(min: Int) = if (min >= 60) "${min / 60} h ${min % 60} min" else "$min min"
