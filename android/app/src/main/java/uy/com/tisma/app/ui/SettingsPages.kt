package uy.com.tisma.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import uy.com.tisma.app.R
import uy.com.tisma.app.core.GcodeFlavor
import uy.com.tisma.app.core.InfillPattern
import uy.com.tisma.app.core.Material
import uy.com.tisma.app.core.SeamPosition
import uy.com.tisma.app.core.SliceSettings
import uy.com.tisma.app.core.SupportStyle

/** The printer: one of the catalog (brand, model, nozzle, variant) or a custom one typed by hand. */
@Composable
fun PrinterPage(workspace: Workspace, modifier: Modifier = Modifier) {
    val s = workspace.settings
    val update = workspace::update
    val catalog = workspace.catalog
    val preset = workspace.printerPreset()
    val model = s.printer?.let { catalog?.find(it.vendor, it.model) }
    var picking by remember { mutableStateOf(false) }
    SettingsPage(modifier) {
        Group(stringResource(R.string.section_printer)) {
            // The printer in use; a tap opens the catalog.
            Card(onClick = { if (catalog != null) picking = true }, modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
                Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(if (preset != null) s.printer!!.model else stringResource(R.string.custom_printer),
                            style = MaterialTheme.typography.titleMedium)
                        Text(if (preset != null) s.printer!!.vendor else stringResource(R.string.custom_printer_hint),
                            style = MaterialTheme.typography.bodySmall)
                    }
                    if (catalog != null) Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = stringResource(R.string.choose_printer))
                }
            }
            if (preset != null && model != null) {
                Choices(stringResource(R.string.nozzle), model.nozzles, preset.nozzle, { "$it mm" }) { n ->
                    update(s.withPrinter(model.vendor, model.name, model.presetFor(n, preset)))
                }
                // Variants of the same nozzle (with or without a multi-material unit, input shaper...).
                val sameNozzle = model.presets.filter { it.nozzle == preset.nozzle }
                if (sameNozzle.size > 1)
                    Choices(stringResource(R.string.printer_variant), sameNozzle, preset, { it.name }) {
                        update(s.withPrinter(model.vendor, model.name, it))
                    }
                Text(stringResource(R.string.printer_bed, num(s.bedX), num(s.bedY), num(s.maxHeight)),
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            } else {
                if (s.printer != null && catalog != null)
                    Text(stringResource(R.string.printer_not_in_catalog, s.printer.model), color = MaterialTheme.colorScheme.error)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    NumberField("X", s.bedX, "mm", Modifier.weight(1f)) { update(s.copy(bedX = it)) }
                    NumberField("Y", s.bedY, "mm", Modifier.weight(1f)) { update(s.copy(bedY = it)) }
                    NumberField("Z", s.maxHeight, "mm", Modifier.weight(1f)) { update(s.copy(maxHeight = it)) }
                }
                Choices(stringResource(R.string.nozzle), listOf(0.2, 0.4, 0.6, 0.8), s.nozzle, { "${num(it)} mm" }) { n ->
                    // A layer the new nozzle can print.
                    update(s.copy(nozzle = n, layerHeight = minOf(s.layerHeight, SliceSettings.layerChoices(n).last())))
                }
                Choices(stringResource(R.string.firmware), GcodeFlavor.entries, s.flavor, { it.label }) { update(s.copy(flavor = it)) }
            }
        }
    }
    if (picking && catalog != null)
        PrinterPicker(
            catalog,
            onPick = { m ->
                update(s.withPrinter(m.vendor, m.name, m.defaultPreset()))
                picking = false
            },
            onCustom = {
                update(s.withCustomPrinter())
                picking = false
            },
            onDismiss = { picking = false },
        )
}

/** The filament: material and temperatures. */
@Composable
fun FilamentPage(workspace: Workspace, modifier: Modifier = Modifier) {
    val s = workspace.settings
    val update = workspace::update
    SettingsPage(modifier) {
        Group(stringResource(R.string.section_filament), stringResource(R.string.filament_hint)) {
            Choices(stringResource(R.string.material), Material.entries, s.material, { it.type }) { update(s.withMaterial(it)) }
        }
        Group(stringResource(R.string.group_temperatures)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumberField(stringResource(R.string.nozzle_temp), s.nozzleTemp.toDouble(), "°C", Modifier.weight(1f)) {
                    update(s.copy(nozzleTemp = it.toInt()))
                }
                NumberField(stringResource(R.string.bed_temp), s.bedTemp.toDouble(), "°C", Modifier.weight(1f)) {
                    update(s.copy(bedTemp = it.toInt()))
                }
            }
        }
    }
}

/** How to print: quality, then the settings most prints leave to the profile. */
@Composable
fun PrintPage(workspace: Workspace, modifier: Modifier = Modifier) {
    val s = workspace.settings
    val update = workspace::update
    SettingsPage(modifier) {
        Group(stringResource(R.string.section_quality)) {
            Choices(stringResource(R.string.layer_height), SliceSettings.layerChoices(s.nozzle), s.layerHeight, { "${num(it)} mm" }) {
                update(s.copy(layerHeight = it))
            }
            Text(stringResource(R.string.infill, s.infillPercent), style = MaterialTheme.typography.labelLarge)
            Slider(value = s.infillPercent.toFloat(), onValueChange = { update(s.copy(infillPercent = (it / 5).toInt() * 5)) },
                valueRange = 0f..100f, steps = 19)
            Choices(stringResource(R.string.walls), listOf(1, 2, 3, 4, 5), s.perimeters, { "$it" }) { update(s.copy(perimeters = it)) }
        }
        Group(stringResource(R.string.group_supports)) {
            SwitchRow(stringResource(R.string.supports), s.supports) { update(s.copy(supports = it)) }
            SwitchRow(stringResource(R.string.brim), s.brim) { update(s.copy(brim = it)) }
            if (s.supports) {
                val profile = stringResource(R.string.as_profile)
                Choices(stringResource(R.string.support_style), listOf<SupportStyle?>(null) + SupportStyle.entries, s.supportStyle,
                    { it?.let { p -> stringResource(p.label()) } ?: profile }) { update(s.copy(supportStyle = it)) }
                SwitchRow(stringResource(R.string.supports_on_bed_only), s.supportsOnBedOnly) { update(s.copy(supportsOnBedOnly = it)) }
            }
        }
        AdvancedGroup(s, update)
        if (s.problems().isNotEmpty())
            Text(stringResource(R.string.settings_problem), color = MaterialTheme.colorScheme.error)
    }
}

/** Settings most prints leave to the profile: each one can go back to "As the profile". */
@Composable
private fun AdvancedGroup(s: SliceSettings, update: (SliceSettings) -> Unit) {
    val profile = stringResource(R.string.as_profile)
    val changed = listOfNotNull(s.topLayers, s.bottomLayers, s.infillPattern, s.seam, s.supportStyle).size +
        (if (s.supportsOnBedOnly) 1 else 0) + (if (s.ironing) 1 else 0)
    Group(stringResource(R.string.section_advanced),
        if (changed == 0) stringResource(R.string.advanced_summary_none) else stringResource(R.string.advanced_summary, changed)) {
        Choices(stringResource(R.string.top_layers), listOf(null, 3, 4, 5, 6, 8), s.topLayers, { it?.toString() ?: profile }) {
            update(s.copy(topLayers = it))
        }
        Choices(stringResource(R.string.bottom_layers), listOf(null, 3, 4, 5, 6, 8), s.bottomLayers, { it?.toString() ?: profile }) {
            update(s.copy(bottomLayers = it))
        }
        Choices(stringResource(R.string.infill_pattern), listOf<InfillPattern?>(null) + InfillPattern.entries, s.infillPattern,
            { it?.let { p -> stringResource(p.label()) } ?: profile }) { update(s.copy(infillPattern = it)) }
        Choices(stringResource(R.string.seam), listOf<SeamPosition?>(null) + SeamPosition.entries, s.seam,
            { it?.let { p -> stringResource(p.label()) } ?: profile }) { update(s.copy(seam = it)) }
        SwitchRow(stringResource(R.string.ironing), s.ironing) { update(s.copy(ironing = it)) }
    }
}

private fun InfillPattern.label(): Int = when (this) {
    InfillPattern.GRID -> R.string.pattern_grid
    InfillPattern.GYROID -> R.string.pattern_gyroid
    InfillPattern.CUBIC -> R.string.pattern_cubic
    InfillPattern.ADAPTIVE_CUBIC -> R.string.pattern_adaptive_cubic
    InfillPattern.HONEYCOMB -> R.string.pattern_honeycomb
    InfillPattern.HONEYCOMB_3D -> R.string.pattern_honeycomb_3d
    InfillPattern.TRIANGLES -> R.string.pattern_triangles
    InfillPattern.RECTILINEAR -> R.string.pattern_rectilinear
    InfillPattern.LIGHTNING -> R.string.pattern_lightning
}

private fun SeamPosition.label(): Int = when (this) {
    SeamPosition.ALIGNED -> R.string.seam_aligned
    SeamPosition.NEAREST -> R.string.seam_nearest
    SeamPosition.REAR -> R.string.seam_rear
    SeamPosition.RANDOM -> R.string.seam_random
}

private fun SupportStyle.label(): Int = when (this) {
    SupportStyle.GRID -> R.string.support_grid
    SupportStyle.SNUG -> R.string.support_snug
    SupportStyle.ORGANIC -> R.string.support_organic
}
