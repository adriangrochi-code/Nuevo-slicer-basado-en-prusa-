package uy.com.tisma.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import uy.com.tisma.app.R
import uy.com.tisma.app.core.CatalogModel
import uy.com.tisma.app.core.PrinterCatalog

/**
 * Full screen list of the printers of the catalog: the vendors, then the models of the one chosen, or the models
 * matching a search. [onCustom] picks the printer typed by hand.
 */
@Composable
fun PrinterPicker(catalog: PrinterCatalog, onPick: (CatalogModel) -> Unit, onCustom: () -> Unit, onDismiss: () -> Unit) {
    var query by rememberSaveable { mutableStateOf("") }
    var vendor by rememberSaveable { mutableStateOf<String?>(null) }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize()) {
            Column {
                Row(Modifier.padding(start = 16.dp, end = 4.dp, top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.choose_printer), style = MaterialTheme.typography.titleLarge,
                        modifier = Modifier.weight(1f))
                    IconButton(onClick = onDismiss) { Icon(Icons.Filled.Close, contentDescription = null) }
                }
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                    placeholder = { Text(stringResource(R.string.search_printer)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
                )
                val models = remember(query) { if (query.isBlank()) null else catalog.search(query) }
                val v = vendor
                when {
                    models != null -> ModelList(models, showVendor = true, onPick)
                    v != null -> {
                        TextButton(onClick = { vendor = null }, modifier = Modifier.padding(horizontal = 8.dp)) {
                            Text("‹ " + v)
                        }
                        ModelList(catalog.vendors.firstOrNull { it.name == v }?.models.orEmpty(), showVendor = false, onPick)
                    }
                    else -> LazyColumn {
                        item {
                            Row(Modifier.fillMaxWidth().clickable(onClick = onCustom).padding(16.dp)) {
                                Column {
                                    Text(stringResource(R.string.custom_printer), style = MaterialTheme.typography.titleSmall)
                                    Text(stringResource(R.string.custom_printer_hint), style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                                }
                            }
                            HorizontalDivider()
                        }
                        items(catalog.vendors, key = { it.name }) { item ->
                            Row(Modifier.fillMaxWidth().clickable { vendor = item.name }.padding(horizontal = 16.dp, vertical = 12.dp)) {
                                Text(item.name, Modifier.weight(1f))
                                Text("${item.models.size}", color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ModelList(models: List<CatalogModel>, showVendor: Boolean, onPick: (CatalogModel) -> Unit) {
    if (models.isEmpty()) {
        Text(stringResource(R.string.no_printers_found), Modifier.padding(16.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        return
    }
    LazyColumn {
        items(models, key = { it.vendor + "\u0000" + it.name }) { m ->
            Column(Modifier.fillMaxWidth().clickable { onPick(m) }.padding(horizontal = 16.dp, vertical = 10.dp)) {
                Text(m.name)
                Text((if (showVendor) m.vendor + " · " else "") + m.nozzles.joinToString(" · ") { "$it mm" },
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}
