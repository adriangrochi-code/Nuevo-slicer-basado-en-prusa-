package uy.com.tisma.app.printers

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.ConnectException

/** The state of any printer, as the app shows it. */
data class PrinterState(
    /** printing, paused, preparing, idle, finished, failed, offline, or what the printer says. */
    val state: String = "",
    val percent: Int = -1,
    val remainingMin: Int = -1,
    val nozzleTemp: Double = -1.0,
    val nozzleTarget: Double = -1.0,
    val bedTemp: Double = -1.0,
    val bedTarget: Double = -1.0,
    val layer: Int = -1,
    val totalLayers: Int = -1,
    val jobName: String = "",
    /** Bambu Lab: the serial number the printer gave. */
    val serial: String = "",
) {
    val active: Boolean get() = state == "printing" || state == "paused" || state == "preparing"

    companion object {
        fun of(s: BambuStatus) = PrinterState(
            state = when (s.gcodeState) {
                "RUNNING" -> "printing"
                "PAUSE" -> "paused"
                "PREPARE", "SLICING" -> "preparing"
                "IDLE" -> "idle"
                "FINISH" -> "finished"
                "FAILED" -> "failed"
                else -> s.gcodeState.lowercase()
            },
            percent = s.percent, remainingMin = s.remainingMin, nozzleTemp = s.nozzleTemp, nozzleTarget = s.nozzleTarget,
            bedTemp = s.bedTemp, bedTarget = s.bedTarget, layer = s.layer, totalLayers = s.totalLayers, jobName = s.jobName,
            serial = s.serial,
        )

        fun of(s: HttpPrinterStatus) = PrinterState(
            state = when (s.state) {
                "printing" -> "printing"
                "paused", "pausing" -> "paused"
                "standby", "operational", "ready" -> "idle"
                "complete" -> "finished"
                "error", "cancelled" -> "failed"
                "offline", "closed" -> "offline"
                else -> s.state
            },
            percent = s.percent, nozzleTemp = s.nozzleTemp, nozzleTarget = s.nozzleTarget, bedTemp = s.bedTemp,
            bedTarget = s.bedTarget, jobName = s.jobName,
        )
    }
}

/** The camera of the printer cannot be shown by the app (the RTSPS video of the Bambu Lab X1, P2S and H2). */
class CameraUnsupportedException(message: String) : IOException(message)

/** No camera set up on the printer. */
class NoCameraException : IOException("no camera")

/**
 * One printer of the user, whatever its kind: its state, its camera, sending a G-code and the control of the print.
 * The calls block (network): run them off the main thread.
 */
class PrinterLink(private val printer: SavedPrinter, private val caPem: () -> InputStream, private val workDir: File) {
    private fun bambu(): BambuClient =
        BambuClient(printer.host, printer.secret, caPem().use { BambuTls(it, printer.serial.ifEmpty { null }) })

    private val klipper by lazy { MoonrakerClient(printer.host, printer.secret) }
    private val octoprint by lazy { OctoPrintClient(printer.host, printer.secret) }

    /** Follows the state until [stop]: the reports of a Bambu Lab printer, the state of the others every 2 s. */
    fun watch(stop: () -> Boolean, onState: (PrinterState) -> Unit) {
        when (printer.kind) {
            PrinterKind.BAMBU -> bambu().watch(stop) { onState(PrinterState.of(it)) }
            PrinterKind.KLIPPER, PrinterKind.OCTOPRINT -> while (!stop()) {
                val s = if (printer.kind == PrinterKind.KLIPPER) klipper.status() else octoprint.status()
                onState(PrinterState.of(s))
                var waited = 0
                while (waited < 2_000 && !stop()) {
                    Thread.sleep(200)
                    waited += 200
                }
            }
        }
    }

    /** Streams the images of the camera to [onFrame] (JPEG) until [stop]. */
    fun camera(stop: () -> Boolean, onFrame: (ByteArray) -> Unit) {
        when (printer.kind) {
            PrinterKind.BAMBU -> {
                val tls = caPem().use { BambuTls(it, printer.serial.ifEmpty { null }) }
                try {
                    BambuCamera.stream(tls, printer.host, printer.secret, stop = stop, onFrame = onFrame)
                } catch (_: ConnectException) {
                    // Port 6000 closed: a printer with the RTSPS camera (X1, P2S, H2).
                    throw CameraUnsupportedException("rtsps")
                }
            }
            PrinterKind.KLIPPER, PrinterKind.OCTOPRINT -> {
                val url = (if (printer.kind == PrinterKind.KLIPPER) klipper.webcamUrl() else octoprint.webcamUrl())
                    ?: throw NoCameraException()
                while (!stop()) {
                    Mjpeg.stream(url, stop = stop, onFrame = onFrame)
                    // A snapshot address: the next image in half a second.
                    Thread.sleep(500)
                }
            }
        }
    }

    /** Sends a G-code and, with [start], prints it. [useAms]: Bambu Lab, the filament of the AMS. Returns its name there. */
    fun send(gcode: File, name: String, start: Boolean, useAms: Boolean, progress: (Long, Long) -> Boolean): String =
        when (printer.kind) {
            PrinterKind.BAMBU -> bambu().print(gcode, name, start, useAms, workDir, progress)
            PrinterKind.KLIPPER -> name.also { klipper.upload(gcode, it, start, progress) }
            PrinterKind.OCTOPRINT -> name.also { octoprint.upload(gcode, it, start, progress) }
        }

    fun pause() = when (printer.kind) {
        PrinterKind.BAMBU -> bambu().command("pause")
        PrinterKind.KLIPPER -> klipper.pause()
        PrinterKind.OCTOPRINT -> octoprint.pause()
    }

    fun resume() = when (printer.kind) {
        PrinterKind.BAMBU -> bambu().command("resume")
        PrinterKind.KLIPPER -> klipper.resume()
        PrinterKind.OCTOPRINT -> octoprint.resume()
    }

    fun stopPrint() = when (printer.kind) {
        PrinterKind.BAMBU -> bambu().command("stop")
        PrinterKind.KLIPPER -> klipper.cancel()
        PrinterKind.OCTOPRINT -> octoprint.cancel()
    }
}
