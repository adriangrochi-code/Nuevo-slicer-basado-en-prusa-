package uy.com.tisma.app.printers

import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.SocketTimeoutException
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import javax.net.ssl.SSLSocket
import kotlin.random.Random

/** The state of a Bambu Lab printer from its MQTT reports (the fields Tisma shows). */
data class BambuStatus(
    val serial: String = "",
    /** IDLE, PREPARE, RUNNING, PAUSE, FINISH, FAILED. */
    val gcodeState: String = "",
    val percent: Int = -1,
    val remainingMin: Int = -1,
    val nozzleTemp: Double = -1.0,
    val nozzleTarget: Double = -1.0,
    val bedTemp: Double = -1.0,
    val bedTarget: Double = -1.0,
    val layer: Int = -1,
    val totalLayers: Int = -1,
    val jobName: String = "",
    /** The RTSPS address of the camera (X1, P2S, H2), "disable", or empty (P1, A1: the camera on port 6000). */
    val rtspUrl: String = "",
) {
    /** The values of a report merged in (the printers send only what changed after the full state). */
    fun merge(json: String): BambuStatus {
        val p = runCatching { JSONObject(json).optJSONObject("print") }.getOrNull() ?: return this
        fun num(key: String, old: Double) = if (p.has(key) && !p.isNull(key)) p.optDouble(key, old) else old
        fun int(key: String, old: Int) = if (p.has(key) && !p.isNull(key)) p.optInt(key, old) else old
        return copy(
            gcodeState = p.optString("gcode_state", gcodeState).ifEmpty { gcodeState },
            percent = int("mc_percent", percent),
            remainingMin = int("mc_remaining_time", remainingMin),
            nozzleTemp = num("nozzle_temper", nozzleTemp),
            nozzleTarget = num("nozzle_target_temper", nozzleTarget),
            bedTemp = num("bed_temper", bedTemp),
            bedTarget = num("bed_target_temper", bedTarget),
            layer = int("layer_num", layer),
            totalLayers = int("total_layer_num", totalLayers),
            jobName = p.optString("subtask_name", jobName).ifEmpty { jobName },
            rtspUrl = p.optJSONObject("ipcam")?.optString("rtsp_url", rtspUrl) ?: rtspUrl,
        )
    }

    val printing: Boolean get() = gcodeState == "RUNNING" || gcodeState == "PREPARE" || gcodeState == "PAUSE"
}

/**
 * The job a Bambu Lab printer starts: a .gcode.3mf with the G-code in Metadata/plate_1.gcode, started with the
 * "project_file" command (the same as write_bambu_gcode_3mf() and bambu_project_file_command() of the desktop program).
 */
object BambuJob {
    /** Name on the SD card for an upload name ("my part.gcode" -> "my_part.gcode.3mf"). */
    fun name(uploadName: String): String {
        var n = File(uploadName).name
        for (suffix in listOf(".gcode", ".gco", ".g"))
            if (n.endsWith(suffix, ignoreCase = true)) {
                n = n.dropLast(suffix.length)
                break
            }
        return n.map { if (it == ' ' || it == '#' || it == '?' || it == '%') '_' else it }.joinToString("") + ".gcode.3mf"
    }

    fun md5UpperHex(file: File): String {
        val md = MessageDigest.getInstance("MD5")
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val n = input.read(buffer)
                if (n < 0) break
                md.update(buffer, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02X".format(it) }
    }

    fun write3mf(gcode: File, out: File) {
        val contentTypes = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
 <Default Extension="png" ContentType="image/png"/>
 <Default Extension="gcode" ContentType="text/x.gcode"/>
</Types>
"""
        val rels = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""
        val model = """<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
 <metadata name="Application">TismaSlicer</metadata>
 <resources>
 </resources>
 <build/>
</model>
"""
        val sliceInfo = """<?xml version="1.0" encoding="UTF-8"?>
<config>
  <plate>
    <metadata key="index" value="1"/>
  </plate>
</config>
"""
        ZipOutputStream(out.outputStream().buffered()).use { zip ->
            fun add(name: String, text: String) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(text.toByteArray(Charsets.UTF_8))
                zip.closeEntry()
            }
            add("[Content_Types].xml", contentTypes)
            add("_rels/.rels", rels)
            add("3D/3dmodel.model", model)
            add("Metadata/slice_info.config", sliceInfo)
            add("Metadata/plate_1.gcode.md5", md5UpperHex(gcode))
            zip.putNextEntry(ZipEntry("Metadata/plate_1.gcode"))
            gcode.inputStream().use { it.copyTo(zip) }
            zip.closeEntry()
        }
    }

    fun projectFileCommand(fileName: String, useAms: Boolean, sequenceId: Int): String = JSONObject().put("print", JSONObject()
        .put("sequence_id", sequenceId.toString())
        .put("command", "project_file")
        .put("param", "Metadata/plate_1.gcode")
        .put("subtask_name", fileName)
        .put("url", "ftp://$fileName")
        .put("timelapse", false)
        .put("bed_leveling", true)
        .put("flow_cali", false)
        .put("vibration_cali", false)
        .put("layer_inspect", false)
        .put("use_ams", useAms)
        .put("profile_id", "0")
        .put("project_id", "0")
        .put("subtask_id", "0")
        .put("task_id", "0")).toString()

    /** Commands of the control of a print: "pause", "resume", "stop" (OpenBambuAPI, mqtt.md, print.*). */
    fun simpleCommand(command: String, sequenceId: Int): String =
        JSONObject().put("print", JSONObject().put("sequence_id", sequenceId.toString()).put("command", command).put("param", ""))
            .toString()
}

class BambuAccessCodeException : IOException("access code")

/**
 * A Bambu Lab printer in LAN mode (developer mode on), without the closed plugin of Bambu Lab: MQTT over TLS on port
 * 8883 for its state and the commands, implicit FTPS on port 990 for the files (OpenBambuAPI, mqtt.md and ftp.md; as
 * BambuLanClient of the desktop program).
 */
class BambuClient(
    val host: String,
    private val accessCode: String,
    private val tls: BambuTls,
    val mqttPort: Int = 8883,
    val ftpsPort: Int = 990,
) {
    private var sequence = Random.nextInt(1000, 9000)

    private inner class Session : AutoCloseable {
        val socket: SSLSocket = tls.connect(host, mqttPort)
        var serial: String = BambuTls.peerSerial(socket)
        private val input = socket.inputStream.buffered()
        private val output = socket.outputStream

        init {
            send(Mqtt.connect("tisma_" + Random.nextInt(1_000_000), "bblp", accessCode, 60))
            val ack = Mqtt.read(input)
            if (ack.type != Mqtt.CONNACK || ack.body.size < 2) throw IOException("MQTT: unexpected answer to CONNECT")
            when (val rc = ack.body[1].toInt() and 0xFF) {
                0 -> {}
                4, 5 -> throw BambuAccessCodeException()
                else -> throw IOException("MQTT: connection refused ($rc)")
            }
            // Without the serial number in the certificate, take it from the first report of any printer.
            send(Mqtt.subscribe(1, if (serial.isEmpty()) "device/+/report" else "device/$serial/report"))
        }

        fun send(bytes: ByteArray) {
            synchronized(output) {
                output.write(bytes)
                output.flush()
            }
        }

        fun publish(json: String) = send(Mqtt.publish("device/$serial/request", json))

        /** The next report (its payload), or null when nothing came before the read timeout. */
        fun nextReport(): String? {
            while (true) {
                val p = try {
                    Mqtt.read(input)
                } catch (_: SocketTimeoutException) {
                    return null
                }
                val (topic, payload) = Mqtt.parsePublish(p) ?: continue
                val m = Regex("device/([^/]+)/report").matchEntire(topic) ?: continue
                if (serial.isEmpty()) serial = m.groupValues[1]
                return payload
            }
        }

        override fun close() {
            runCatching { send(Mqtt.disconnect()) }
            runCatching { socket.close() }
        }
    }

    /**
     * Follows the state of the printer until [stop] returns true: [onStatus] gets the state after every report.
     * Throws [BambuAccessCodeException] or an IOException.
     */
    fun watch(stop: () -> Boolean, onStatus: (BambuStatus) -> Unit) {
        Session().use { s ->
            s.socket.soTimeout = 2_000
            var status = BambuStatus(serial = s.serial)
            var pushed = false
            if (s.serial.isNotEmpty()) {
                s.publish(PUSHALL)
                pushed = true
            }
            var lastPing = System.currentTimeMillis()
            while (!stop()) {
                val report = s.nextReport()
                if (report != null) {
                    if (!pushed) {
                        s.publish(PUSHALL)
                        pushed = true
                    }
                    status = status.copy(serial = s.serial).merge(report)
                    onStatus(status)
                }
                if (System.currentTimeMillis() - lastPing > 30_000) {
                    s.send(Mqtt.pingreq())
                    lastPing = System.currentTimeMillis()
                }
            }
        }
    }

    /** Publishes a command on device/<serial>/request. */
    fun sendCommand(json: String) {
        Session().use { s ->
            s.socket.soTimeout = 10_000
            if (s.serial.isEmpty()) s.nextReport() ?: throw IOException("No answer from the printer")
            s.publish(json)
            // Let the printer read it before closing.
            Thread.sleep(500)
        }
    }

    fun command(name: String) = sendCommand(BambuJob.simpleCommand(name, ++sequence))

    /** Uploads [file] as [name] to the root of the SD card. */
    fun upload(file: File, name: String, progress: (Long, Long) -> Boolean) {
        try {
            FtpsUpload(tls, host, ftpsPort).upload("bblp", accessCode, file, name, progress)
        } catch (e: FtpsException) {
            if (e.code == 530) throw BambuAccessCodeException()
            throw e
        }
    }

    /** Packs the G-code as a .gcode.3mf, uploads it and, with [start], starts it. Returns the name on the printer. */
    fun print(gcode: File, uploadName: String, start: Boolean, useAms: Boolean, workDir: File,
              progress: (Long, Long) -> Boolean): String {
        val name = BambuJob.name(uploadName)
        val job = File.createTempFile("tisma-bambu-", ".gcode.3mf", workDir)
        try {
            BambuJob.write3mf(gcode, job)
            upload(job, name, progress)
        } finally {
            job.delete()
        }
        if (start) sendCommand(BambuJob.projectFileCommand(name, useAms, ++sequence))
        return name
    }

    companion object {
        /** Makes the printer send its full state. */
        const val PUSHALL = """{"pushing":{"sequence_id":"1","command":"pushall","version":1,"push_target":1}}"""
    }
}
