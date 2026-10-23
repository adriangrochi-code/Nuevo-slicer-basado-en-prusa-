package uy.com.tisma.app.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import uy.com.tisma.app.BuildConfig
import uy.com.tisma.app.R
import uy.com.tisma.app.core.TismaCore

/** The sections of the top bar: the plate, the three groups of settings, and the rest. */
enum class Page { PLATE, PRINT, FILAMENT, PRINTER, MORE }

/** What "More" opens. */
enum class MorePage { PRINTERS, ACCOUNT, ABOUT }

/** The rest of the app: the printers over Wi-Fi, the Tisma Cloud account, about the app. */
@Composable
fun MoreScreen(workspace: Workspace, open: MorePage?, onOpen: (MorePage?) -> Unit, modifier: Modifier = Modifier) {
    if (open != null) {
        BackHandler { onOpen(null) }
        Column(modifier.fillMaxSize()) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(end = 16.dp)) {
                IconButton(onClick = { onOpen(null) }) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.back))
                }
                Text(stringResource(open.title()), style = MaterialTheme.typography.titleLarge)
            }
            when (open) {
                MorePage.PRINTERS -> PrintersScreen(workspace, Modifier.weight(1f))
                MorePage.ACCOUNT -> AccountScreen(Modifier.weight(1f))
                MorePage.ABOUT -> About()
            }
        }
        return
    }
    SettingsPage(modifier) {
        for (p in MorePage.entries) {
            ListItem(
                headlineContent = { Text(stringResource(p.title())) },
                supportingContent = { Text(stringResource(p.hint())) },
                leadingContent = { Icon(p.icon(), contentDescription = null) },
                trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null) },
                modifier = Modifier.clickable { onOpen(p) },
            )
            HorizontalDivider()
        }
    }
}

@Composable
private fun About() {
    SettingsPage {
        Text(stringResource(R.string.about_app, BuildConfig.VERSION_NAME), style = MaterialTheme.typography.titleMedium)
        Text(if (TismaCore.available) stringResource(R.string.core_version, TismaCore.version()) else stringResource(R.string.core_missing))
        Text(stringResource(R.string.about_license), style = MaterialTheme.typography.bodyMedium)
    }
}

private fun MorePage.title(): Int = when (this) {
    MorePage.PRINTERS -> R.string.more_printers
    MorePage.ACCOUNT -> R.string.cloud_title
    MorePage.ABOUT -> R.string.more_about
}

private fun MorePage.hint(): Int = when (this) {
    MorePage.PRINTERS -> R.string.more_printers_hint
    MorePage.ACCOUNT -> R.string.more_account_hint
    MorePage.ABOUT -> R.string.more_about_hint
}

@Composable
private fun MorePage.icon(): Painter = when (this) {
    MorePage.PRINTERS -> painterResource(R.drawable.ic_printer)
    MorePage.ACCOUNT -> rememberVectorPainter(Icons.Filled.AccountCircle)
    MorePage.ABOUT -> rememberVectorPainter(Icons.Filled.Info)
}
