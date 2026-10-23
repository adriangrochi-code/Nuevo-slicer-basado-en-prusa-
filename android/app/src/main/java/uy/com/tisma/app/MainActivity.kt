package uy.com.tisma.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.Build
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import uy.com.tisma.app.core.TismaCore
import uy.com.tisma.app.ui.AccountScreen
import uy.com.tisma.app.ui.EngineeringScreen
import uy.com.tisma.app.ui.PrepareScreen
import uy.com.tisma.app.ui.PrintersScreen
import uy.com.tisma.app.ui.TismaTheme
import uy.com.tisma.app.ui.Workspace

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { TismaTheme { TismaApp() } }
    }
}

private enum class Tab { SLICE, ENGINEERING, PRINTERS, ACCOUNT }

/** The tabs of the app: slicing and structural analysis on the phone, the printers over Wi-Fi, the Tisma Cloud account. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TismaApp() {
    var tab by rememberSaveable { mutableStateOf(Tab.SLICE) }
    val context = LocalContext.current
    val workspace = remember { Workspace(context) }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                actions = {
                    if (TismaCore.available)
                        Text(stringResource(R.string.core_version, TismaCore.version()),
                            style = MaterialTheme.typography.labelSmall,
                            modifier = Modifier.padding(end = 12.dp))
                },
            )
        },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(selected = tab == Tab.SLICE, onClick = { tab = Tab.SLICE },
                    icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                    label = { Text(stringResource(R.string.tab_slice)) })
                NavigationBarItem(selected = tab == Tab.ENGINEERING, onClick = { tab = Tab.ENGINEERING },
                    icon = { Icon(painterResource(R.drawable.ic_engineering), contentDescription = null) },
                    label = { Text(stringResource(R.string.tab_engineering)) })
                NavigationBarItem(selected = tab == Tab.PRINTERS, onClick = { tab = Tab.PRINTERS },
                    icon = { Icon(painterResource(R.drawable.ic_printer), contentDescription = null) },
                    label = { Text(stringResource(R.string.tab_printers)) })
                NavigationBarItem(selected = tab == Tab.ACCOUNT, onClick = { tab = Tab.ACCOUNT },
                    icon = { Icon(Icons.Filled.AccountCircle, contentDescription = null) },
                    label = { Text(stringResource(R.string.tab_account)) })
            }
        },
    ) { padding ->
        // The slice screen stays composed, so switching tabs keeps the model and the slice result (and a slice running).
        Box(Modifier.fillMaxSize().padding(padding)) {
            PrepareScreen(workspace, onSendToPrinter = { tab = Tab.PRINTERS }, Modifier.hiddenIf(tab != Tab.SLICE))
            if (tab == Tab.ENGINEERING) EngineeringScreen(workspace)
            if (tab == Tab.PRINTERS) PrintersScreen(workspace)
            if (tab == Tab.ACCOUNT) AccountScreen()
        }
    }
}

/** Keeps a screen composed (its state survives) but neither drawn nor touchable. */
private fun Modifier.hiddenIf(hidden: Boolean) =
    if (!hidden) this else layout { measurable, constraints ->
        measurable.measure(constraints)
        layout(0, 0) {}
    }
