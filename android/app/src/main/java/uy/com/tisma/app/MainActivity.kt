package uy.com.tisma.app

import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.core.ModelInfo
import uy.com.tisma.app.core.SliceResult
import uy.com.tisma.app.core.TismaCore
import uy.com.tisma.app.ui.TismaTheme
import java.io.File
import java.util.Locale

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { TismaTheme { SliceScreen() } }
    }
}

/** First screen: open a model, slice it with the core on the phone and save the G-code. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SliceScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var model by remember { mutableStateOf<File?>(null) }
    var info by remember { mutableStateOf<ModelInfo?>(null) }
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
                info = withContext(Dispatchers.Default) { TismaCore.modelInfo(file.absolutePath) }
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

    Scaffold(topBar = { TopAppBar(title = { Text(stringResource(R.string.app_name)) }) }) { padding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(padding).padding(16.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (!TismaCore.available) {
                Text(stringResource(R.string.core_missing), color = MaterialTheme.colorScheme.error)
            } else {
                Text(stringResource(R.string.core_version, TismaCore.version()), style = MaterialTheme.typography.bodySmall)
            }

            Button(onClick = { openModel.launch(arrayOf("*/*")) }, enabled = TismaCore.available && progress == null,
                modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.open_model))
            }

            val currentInfo = info
            val currentModel = model
            if (currentModel != null && currentInfo != null) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Text(currentModel.name, style = MaterialTheme.typography.titleMedium)
                        Text(stringResource(R.string.model_size,
                            mm(currentInfo.sizeMm[0]), mm(currentInfo.sizeMm[1]), mm(currentInfo.sizeMm[2])))
                        Text(stringResource(R.string.model_objects, currentInfo.objects, currentInfo.triangles))
                    }
                }
                Button(
                    onClick = {
                        error = null
                        result = null
                        progress = 0 to ""
                        scope.launch {
                            try {
                                val out = File(context.cacheDir, currentModel.nameWithoutExtension + ".gcode")
                                result = withContext(Dispatchers.Default) {
                                    TismaCore.slice(currentModel.absolutePath, "", out.absolutePath) { percent, text ->
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
                    enabled = progress == null,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.slice)) }
            }

            progress?.let { (percent, text) ->
                LinearProgressIndicator(progress = { percent / 100f }, modifier = Modifier.fillMaxWidth())
                Text(text, style = MaterialTheme.typography.bodySmall)
            }

            result?.let { r ->
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp)) {
                        Text(stringResource(R.string.slice_done), style = MaterialTheme.typography.titleMedium)
                        Text(stringResource(R.string.slice_time, r.printTime))
                        Text(stringResource(R.string.slice_filament, String.format(Locale.ROOT, "%.2f", r.filamentMm / 1000),
                            String.format(Locale.ROOT, "%.1f", r.filamentGrams)))
                        Spacer(Modifier.height(8.dp))
                        Row {
                            OutlinedButton(onClick = { saveGcode.launch(File(r.gcodePath).name) }) {
                                Text(stringResource(R.string.save_gcode))
                            }
                        }
                    }
                }
            }

            error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
        }
    }
}

private fun mm(v: Double) = String.format(Locale.ROOT, "%.1f", v)

private fun copyToCache(context: android.content.Context, uri: Uri): File {
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
