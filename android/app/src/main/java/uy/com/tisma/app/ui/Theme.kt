package uy.com.tisma.app.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

// Tisma's violet (the accent of the desktop program and of the account page).
private val TismaViolet = Color(0xFF7C3AED)
private val TismaVioletLight = Color(0xFFC4B5FD)

private val LightColors = lightColorScheme(primary = TismaViolet, secondary = Color(0xFF5B21B6))
private val DarkColors = darkColorScheme(primary = TismaVioletLight, secondary = Color(0xFFA78BFA))

@Composable
fun TismaTheme(dynamicColor: Boolean = false, content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val colors = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (dark) dynamicDarkColorScheme(LocalContext.current) else dynamicLightColorScheme(LocalContext.current)
        dark -> DarkColors
        else -> LightColors
    }
    MaterialTheme(colorScheme = colors, content = content)
}
