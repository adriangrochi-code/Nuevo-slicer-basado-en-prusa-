package uy.com.tisma.app.printers

import org.json.JSONObject
import java.io.DataOutputStream
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import kotlin.random.Random

/** The state of a printer driven over HTTP (Klipper or OctoPrint), in the terms the app shows. */
data class HttpPrinterStatus(
    /** printing, paused, standby, complete, error, cancelled, offline... as the printer says it. */
    val state: String = "",
    val percent: Int = -1,
    val nozzleTemp: Double = -1.0,
    val nozzleTarget: Double = -1.0,
    val bedTemp: Double = -1.0,
    val bedTarget: Double = -1.0,
    val jobName: String = "",
)

/** Small HTTP helpers for the printers of the local network (no library needed). */
internal object Http {
    fun get(url: String, headers: Map<String, String>): JSONObject {
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.connectTimeout = 8_000
        conn.readTimeout = 10_000
        headers.forEach { (k, v) -> conn.setRequestProperty(k, v) }
        try {
            val code = conn.responseCode
            val body = (if (code in 200..299) conn.inputStream else conn.errorStream)?.bufferedReader()?.use { it.readText() } ?: ""
            if (code !in 200..299) throw HttpException(code, body.take(200))
            return JSONObject(body)
        } finally {
            conn.disconnect()
        }
    }

    fun post(url: String, headers: Map<String, String>) {
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.requestMethod = "POST"
        conn.connectTimeout = 8_000
        conn.readTimeout = 10_000
        headers.forEach { (k, v) -> conn.setRequestProperty(k, v) }
        try {
            if (conn.responseCode !in 200..299) throw HttpException(conn.responseCode, "")
        } finally {
            conn.disconnect()
        }
    }

    /** multipart/form-data POST with text fields and one file, streamed. */
    fun upload(url: String, headers: Map<String, String>, fields: Map<String, String>, fileField: String, file: File,
               fileName: String, progress: (Long, Long) -> Boolean) {
        val boundary = "tisma" + Random.nextLong().toString(16)
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.requestMethod = "POST"
        conn.doOutput = true
        conn.connectTimeout = 10_000
        conn.readTimeout = 120_000
        conn.setRequestProperty("Content-Type", "multipart/form-data; boundary=$boundary")
        headers.forEach { (k, v) -> conn.setRequestProperty(k, v) }
        val head = buildString {
            for ((k, v) in fields)
                append("--$boundary\r\nContent-Disposition: form-data; name=\"$k\"\r\n\r\n$v\r\n")
            append("--$boundary\r\nContent-Disposition: form-data; name=\"$fileField\"; filename=\"${fileName.replace("\"", "_")}\"\r\n")
            append("Content-Type: application/octet-stream\r\n\r\n")
        }.toByteArray(Charsets.UTF_8)
        val tail = "\r\n--$boundary--\r\n".toByteArray(Charsets.UTF_8)
        conn.setFixedLengthStreamingMode(head.size + file.length() + tail.size)
        try {
            DataOutputStream(conn.outputStream).use { out ->
                out.write(head)
                val total = file.length()
                var sent = 0L
                file.inputStream().use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        out.write(buffer, 0, n)
                        sent += n
                        if (!progress(sent, total)) throw IOException("cancelled")
                    }
                }
                out.write(tail)
            }
            val code = conn.responseCode
            if (code !in 200..299)
                throw HttpException(code, conn.errorStream?.bufferedReader()?.use { it.readText() }?.take(200) ?: "")
        } finally {
            conn.disconnect()
        }
    }
}

class HttpException(val code: Int, body: String) : IOException("HTTP $code${if (body.isNotEmpty()) ": $body" else ""}")

/**
 * Klipper through Moonraker (Mainsail, Fluidd): state by /printer/objects/query, the webcams of
 * /server/webcams/list, upload by /server/files/upload (Moonraker documentation, docs/external_api; the same upload as
 * the desktop program, src/slic3r/Utils/Moonraker.cpp).
 */
class MoonrakerClient(baseUrl: String, private val apiKey: String = "") {
    val base = normalizeBase(baseUrl)
    private val headers get() = if (apiKey.isNotEmpty()) mapOf("X-Api-Key" to apiKey) else emptyMap()

    fun status(): HttpPrinterStatus {
        val r = Http.get("$base/printer/objects/query?print_stats&display_status&extruder&heater_bed", headers)
        val s = r.optJSONObject("result")?.optJSONObject("status") ?: JSONObject()
        val stats = s.optJSONObject("print_stats") ?: JSONObject()
        val extruder = s.optJSONObject("extruder") ?: JSONObject()
        val bed = s.optJSONObject("heater_bed") ?: JSONObject()
        val progress = s.optJSONObject("display_status")?.optDouble("progress", -1.0) ?: -1.0
        return HttpPrinterStatus(
            state = stats.optString("state", ""),
            percent = if (progress >= 0) (progress * 100).toInt() else -1,
            nozzleTemp = extruder.optDouble("temperature", -1.0),
            nozzleTarget = extruder.optDouble("target", -1.0),
            bedTemp = bed.optDouble("temperature", -1.0),
            bedTarget = bed.optDouble("target", -1.0),
            jobName = stats.optString("filename", ""),
        )
    }

    /** The address of the first enabled MJPEG webcam (relative addresses on the printer), or null. */
    fun webcamUrl(): String? {
        val r = Http.get("$base/server/webcams/list", headers)
        val cams = (r.optJSONObject("result") ?: r).optJSONArray("webcams") ?: return null
        for (i in 0 until cams.length()) {
            val c = cams.getJSONObject(i)
            if (!c.optBoolean("enabled", true)) continue
            val url = c.optString("stream_url").ifEmpty { c.optString("snapshot_url") }
            if (url.isNotEmpty()) return absolute(base, url)
        }
        return null
    }

    fun upload(file: File, name: String, start: Boolean, progress: (Long, Long) -> Boolean) =
        Http.upload("$base/server/files/upload", headers, linkedMapOf("root" to "gcodes", "print" to start.toString()),
            "file", file, name, progress)

    fun pause() = Http.post("$base/printer/print/pause", headers)
    fun resume() = Http.post("$base/printer/print/resume", headers)
    fun cancel() = Http.post("$base/printer/print/cancel", headers)
}

/**
 * OctoPrint: /api/job and /api/printer for the state, webcam.streamUrl of /api/settings, upload to /api/files/local
 * (OctoPrint REST API; the same upload as the desktop program, src/slic3r/Utils/OctoPrint.cpp).
 */
class OctoPrintClient(baseUrl: String, private val apiKey: String) {
    val base = normalizeBase(baseUrl)
    private val headers get() = mapOf("X-Api-Key" to apiKey)

    fun status(): HttpPrinterStatus {
        val job = Http.get("$base/api/job", headers)
        val printer = try {
            Http.get("$base/api/printer", headers)
        } catch (e: HttpException) {
            // 409: the printer is not connected to OctoPrint.
            if (e.code == 409) JSONObject() else throw e
        }
        val temps = printer.optJSONObject("temperature") ?: JSONObject()
        val tool = temps.optJSONObject("tool0") ?: JSONObject()
        val bed = temps.optJSONObject("bed") ?: JSONObject()
        val completion = job.optJSONObject("progress")?.optDouble("completion", -1.0) ?: -1.0
        return HttpPrinterStatus(
            state = job.optString("state", "").lowercase(),
            percent = if (completion >= 0) completion.toInt() else -1,
            nozzleTemp = tool.optDouble("actual", -1.0),
            nozzleTarget = tool.optDouble("target", -1.0),
            bedTemp = bed.optDouble("actual", -1.0),
            bedTarget = bed.optDouble("target", -1.0),
            jobName = job.optJSONObject("job")?.optJSONObject("file")?.optString("name", "") ?: "",
        )
    }

    fun webcamUrl(): String? {
        val webcam = Http.get("$base/api/settings", headers).optJSONObject("webcam") ?: return null
        val url = webcam.optString("streamUrl").ifEmpty { webcam.optString("snapshotUrl") }
        return if (url.isEmpty()) null else absolute(base, url)
    }

    fun upload(file: File, name: String, start: Boolean, progress: (Long, Long) -> Boolean) =
        Http.upload("$base/api/files/local", headers, linkedMapOf("print" to start.toString()), "file", file, name, progress)

    private fun jobCommand(json: String) {
        val conn = URL("$base/api/job").openConnection() as HttpURLConnection
        conn.requestMethod = "POST"
        conn.doOutput = true
        conn.setRequestProperty("Content-Type", "application/json")
        headers.forEach { (k, v) -> conn.setRequestProperty(k, v) }
        try {
            conn.outputStream.use { it.write(json.toByteArray()) }
            if (conn.responseCode !in 200..299) throw HttpException(conn.responseCode, "")
        } finally {
            conn.disconnect()
        }
    }

    fun pause() = jobCommand("""{"command":"pause","action":"pause"}""")
    fun resume() = jobCommand("""{"command":"pause","action":"resume"}""")
    fun cancel() = jobCommand("""{"command":"cancel"}""")
}

/** "192.168.1.5", "192.168.1.5:7125/" or "http://host" as "http://host[:port]" without the last slash. */
internal fun normalizeBase(address: String): String {
    var a = address.trim().trimEnd('/')
    if (!a.startsWith("http://") && !a.startsWith("https://")) a = "http://$a"
    return a
}

/** An address of the printer: absolute as it is, relative to the host of [base] otherwise ("/webcam/?action=stream"). */
internal fun absolute(base: String, url: String): String {
    if (url.startsWith("http://") || url.startsWith("https://")) return url
    val u = URL(base)
    val host = u.protocol + "://" + u.host
    // The webcams are served by the web server of the printer (port 80), not by Moonraker's port.
    return if (url.startsWith("/")) host + url else "$host/$url"
}
