package uy.com.tisma.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import uy.com.tisma.app.ui.FilamentPage
import uy.com.tisma.app.ui.MorePage
import uy.com.tisma.app.ui.MoreScreen
import uy.com.tisma.app.ui.Page
import uy.com.tisma.app.ui.PlateScreen
import uy.com.tisma.app.ui.PrintPage
import uy.com.tisma.app.ui.PrinterPage
import uy.com.tisma.app.ui.TismaTheme
import uy.com.tisma.app.ui.Workspace

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { TismaTheme { TismaApp() } }
    }
}

/**
 * The app: the sections in a bar at the top (at the side in landscape), as in the mobile slicers: the plate, the
 * print, filament and printer settings, and the rest (printers over Wi-Fi, Tisma Cloud account, about).
 */
@Composable
fun TismaApp(workspace: Workspace = LocalContext.current.let { remember { Workspace(it) } }) {
    var page by rememberSaveable { mutableStateOf(Page.PLATE) }
    var more by rememberSaveable { mutableStateOf<MorePage?>(null) }
    // Back from a section returns to the plate.
    BackHandler(enabled = page != Page.PLATE && more == null) { page = Page.PLATE }

    val content: @Composable (Modifier) -> Unit = { m ->
        // The plate stays composed, so switching sections keeps the model, the view and the slice (and a slice running).
        Box(m) {
            PlateScreen(workspace, onPage = { page = it }, onSendToPrinter = {
                more = MorePage.PRINTERS
                page = Page.MORE
            }, Modifier.hiddenIf(page != Page.PLATE))
            val pageModifier = Modifier.navigationBarsPadding()
            when (page) {
                Page.PLATE -> {}
                Page.PRINT -> PrintPage(workspace, pageModifier)
                Page.FILAMENT -> FilamentPage(workspace, pageModifier)
                Page.PRINTER -> PrinterPage(workspace, pageModifier)
                Page.MORE -> MoreScreen(workspace, more, onOpen = { more = it }, pageModifier)
            }
        }
    }
    Surface(Modifier.fillMaxSize()) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            if (maxWidth > maxHeight) {
                // The tools of the plate take the left side: the sections go to the right.
                Row(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
                    content(Modifier.weight(1f))
                    NavigationRail {
                        for (p in Page.entries)
                            NavigationRailItem(selected = p == page, onClick = { page = p },
                                icon = { Icon(p.icon(), contentDescription = null) }, label = { Text(stringResource(p.title())) })
                    }
                }
            } else {
                androidx.compose.foundation.layout.Column(Modifier.fillMaxSize()) {
                    NavigationBar(windowInsets = WindowInsets.statusBars) {
                        for (p in Page.entries)
                            NavigationBarItem(selected = p == page, onClick = { page = p },
                                icon = { Icon(p.icon(), contentDescription = null) }, label = { Text(stringResource(p.title()), maxLines = 1) })
                    }
                    content(Modifier.weight(1f))
                }
            }
        }
    }
}

private fun Page.title(): Int = when (this) {
    Page.PLATE -> R.string.page_plate
    Page.PRINT -> R.string.page_print
    Page.FILAMENT -> R.string.page_filament
    Page.PRINTER -> R.string.page_printer
    Page.MORE -> R.string.page_more
}

@Composable
private fun Page.icon(): Painter = when (this) {
    Page.PLATE -> painterResource(R.drawable.ic_plate)
    Page.PRINT -> painterResource(R.drawable.ic_print)
    Page.FILAMENT -> painterResource(R.drawable.ic_filament)
    Page.PRINTER -> painterResource(R.drawable.ic_printer)
    Page.MORE -> rememberVectorPainter(Icons.Filled.MoreVert)
}

/** Keeps a screen composed (its state survives) but neither drawn, touchable nor read by accessibility services. */
private fun Modifier.hiddenIf(hidden: Boolean) =
    if (!hidden) this else clearAndSetSemantics {}.layout { measurable, constraints ->
        measurable.measure(constraints)
        layout(0, 0) {}
    }
