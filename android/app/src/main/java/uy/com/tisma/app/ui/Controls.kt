package uy.com.tisma.app.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import java.io.File
import java.util.Locale

/** A page of settings: scrolls, with the groups one under the other ([Group]). */
@Composable
internal fun SettingsPage(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
        content = content,
    )
}

/** A group of a settings page: a big title, then its settings. */
@Composable
internal fun Group(title: String, subtitle: String? = null, content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().padding(top = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.primary)
        subtitle?.let {
            Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        content()
        HorizontalDivider(Modifier.padding(top = 8.dp))
    }
}

/** A titled card with a one-line summary that opens to show its settings. */
@Composable
internal fun Section(title: String, summary: String, content: @Composable () -> Unit) {
    var open by rememberSaveable(title) { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow)) {
        Column(Modifier.padding(horizontal = 14.dp, vertical = 8.dp)) {
            // The whole header opens and closes the section, not only the arrow.
            Row(Modifier.clickable { open = !open }, verticalAlignment = Alignment.CenterVertically) {
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
internal fun <T> Choices(label: String, options: List<T>, selected: T, text: @Composable (T) -> String, onSelect: (T) -> Unit) {
    Text(label, style = MaterialTheme.typography.labelLarge)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        for (o in options)
            FilterChip(selected = o == selected, onClick = { onSelect(o) }, label = { Text(text(o)) })
    }
}

/**
 * A number chosen from the usual values (chips) or typed with the keyboard in the field beside the label, any value of
 * [range]. [none]: a chip for no value (the profile's), also chosen by clearing the field. A typed number outside the
 * range (or not whole when [whole]) is not taken: the field shows the error.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun NumberChoices(
    label: String,
    options: List<Double>,
    value: Double?,
    suffix: String,
    range: ClosedFloatingPointRange<Double>,
    whole: Boolean = false,
    none: String? = null,
    onValue: (Double?) -> Unit,
) {
    var text by remember(value) { mutableStateOf(value?.let(::num) ?: "") }
    val typed = text.replace(',', '.').trim().toDoubleOrNull()
    val valid = typed != null && typed in range && (!whole || typed % 1.0 == 0.0)
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.labelLarge, modifier = Modifier.weight(1f))
        OutlinedTextField(
            value = text,
            onValueChange = {
                text = it
                val v = it.replace(',', '.').trim().toDoubleOrNull()
                when {
                    it.isBlank() && none != null -> onValue(null)
                    v != null && v in range && (!whole || v % 1.0 == 0.0) -> onValue(v)
                }
            },
            placeholder = none?.let { { Text(it) } },
            suffix = { Text(suffix) },
            singleLine = true,
            isError = text.isNotBlank() && !valid,
            keyboardOptions = KeyboardOptions(keyboardType = if (whole) KeyboardType.Number else KeyboardType.Decimal),
            modifier = Modifier.width(132.dp),
        )
    }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        if (none != null)
            FilterChip(selected = value == null, onClick = { onValue(null) }, label = { Text(none) })
        for (o in options)
            FilterChip(selected = value != null && kotlin.math.abs(o - value) < 1e-9, onClick = { onValue(o) },
                label = { Text(num(o) + if (suffix == "%") " %" else "") })
    }
}

/** A number field that keeps what is typed and reports it only when it is a valid number. */
@Composable
internal fun NumberField(label: String, value: Double, suffix: String, modifier: Modifier = Modifier, onValue: (Double) -> Unit) {
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
internal fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

internal fun num(v: Double): String = String.format(Locale.ROOT, "%.2f", v).trimEnd('0').trimEnd('.')

internal fun mm(v: Double) = String.format(Locale.ROOT, "%.1f", v)

/** The G-code to another app (a printer's app, a file manager, a message) through the share sheet. */
internal fun shareGcode(context: Context, file: File) {
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
