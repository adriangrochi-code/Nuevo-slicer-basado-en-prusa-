package uy.com.tisma.app.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class PreviewCameraTest {
    @Test
    fun zoomKeepsThePointUnderTheFingers() {
        val c = PreviewCamera()
        // A point 100 px right of the centre, with the view not shifted: on the screen it is at 100 + offset.
        c.zoomAt(2f, 100f, 0f)
        assertEquals(2f, c.zoom, 1e-6f)
        // What was at 100 px (bed point at 100 / zoom 1) is now at 100 / 1 * 2 + offset = 100.
        assertEquals(100f, 100f * 2f + c.offsetX, 1e-4f)
        c.zoomAt(1000f)
        assertEquals(PreviewCamera.MAX_ZOOM, c.zoom, 1e-6f)
        c.top()
        assertEquals(1f, c.zoom, 1e-6f)
        assertEquals(0f, c.offsetX, 1e-6f)
        assertEquals(0f, c.pitch, 1e-6f)
    }
}
