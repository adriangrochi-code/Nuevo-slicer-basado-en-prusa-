package uy.com.tisma.app.printers

import org.conscrypt.Conscrypt
import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.nio.file.Files
import java.security.Provider
import java.util.zip.ZipFile

class MqttTest {
    @Test
    fun packetsRoundTrip() {
        val connect = Mqtt.read(ByteArrayInputStream(Mqtt.connect("tisma_1", "bblp", "12345678", 60)))
        assertEquals(Mqtt.CONNECT, connect.type)
        // Protocol name, level 4, user + password + clean session.
        assertEquals("MQTT", String(connect.body, 2, 4))
        assertEquals(4, connect.body[6].toInt())
        assertEquals(0xC2, connect.body[7].toInt() and 0xFF)
        // A payload over 127 bytes needs two bytes of length.
        val payload = "x".repeat(300)
        val p = Mqtt.read(ByteArrayInputStream(Mqtt.publish("device/SN/request", payload)))
        assertEquals("device/SN/request" to payload, Mqtt.parsePublish(p))
        val sub = Mqtt.read(ByteArrayInputStream(Mqtt.subscribe(1, "device/+/report")))
        assertEquals(Mqtt.SUBSCRIBE, sub.type)
        assertEquals(2, sub.flags)
        assertNull(Mqtt.parsePublish(sub))
    }
}

class BambuJobTest {
    @Test
    fun namesAndCommands() {
        assertEquals("my_part.gcode.3mf", BambuJob.name("dir/my part.gcode"))
        assertEquals("a_b_c.gcode.3mf", BambuJob.name("a#b?c.G"))
        val cmd = JSONObject(BambuJob.projectFileCommand("x.gcode.3mf", true, 7)).getJSONObject("print")
        assertEquals("project_file", cmd.getString("command"))
        assertEquals("Metadata/plate_1.gcode", cmd.getString("param"))
        assertEquals("ftp://x.gcode.3mf", cmd.getString("url"))
        assertEquals("7", cmd.getString("sequence_id"))
        assertTrue(cmd.getBoolean("use_ams"))
        val stop = JSONObject(BambuJob.simpleCommand("stop", 3)).getJSONObject("print")
        assertEquals("stop", stop.getString("command"))
        assertEquals("", stop.getString("param"))
    }

    @Test
    fun gcode3mf() {
        val dir = Files.createTempDirectory("bambu").toFile()
        val gcode = File(dir, "a.gcode").apply { writeText("G28\nG1 X10\n") }
        val out = File(dir, "a.gcode.3mf")
        BambuJob.write3mf(gcode, out)
        ZipFile(out).use { zip ->
            val names = zip.entries().toList().map { it.name }.toSet()
            assertEquals(setOf("[Content_Types].xml", "_rels/.rels", "3D/3dmodel.model", "Metadata/slice_info.config",
                "Metadata/plate_1.gcode.md5", "Metadata/plate_1.gcode"), names)
            assertEquals("G28\nG1 X10\n", zip.getInputStream(zip.getEntry("Metadata/plate_1.gcode")).reader().readText())
            // MD5 of "G28\nG1 X10\n", upper case.
            val md5 = zip.getInputStream(zip.getEntry("Metadata/plate_1.gcode.md5")).reader().readText()
            assertEquals(BambuJob.md5UpperHex(gcode), md5)
            assertEquals(md5.uppercase(), md5)
            assertEquals(32, md5.length)
        }
        dir.deleteRecursively()
    }

    @Test
    fun statusMergesTheReports() {
        var s = BambuStatus(serial = "SN")
        s = s.merge("""{"print":{"gcode_state":"RUNNING","mc_percent":12,"mc_remaining_time":34,"nozzle_temper":219.5,
            "nozzle_target_temper":220,"bed_temper":60,"layer_num":3,"total_layer_num":100,"subtask_name":"part",
            "ipcam":{"rtsp_url":"disable"}}}""")
        // The printers send only what changed afterwards.
        s = s.merge("""{"print":{"mc_percent":13}}""")
        s = s.merge("""{"info":{"command":"get_version"}}""")
        assertEquals("RUNNING", s.gcodeState)
        assertEquals(13, s.percent)
        assertEquals(34, s.remainingMin)
        assertEquals(219.5, s.nozzleTemp, 0.0)
        assertEquals("part", s.jobName)
        assertEquals("disable", s.rtspUrl)
        val state = PrinterState.of(s)
        assertEquals("printing", state.state)
        assertTrue(state.active)
        assertEquals("finished", PrinterState.of(BambuStatus(gcodeState = "FINISH")).state)
    }
}

class CameraTest {
    @Test
    fun bambuAuthAndFrames() {
        val auth = BambuCamera.authPacket("bblp", "12345678")
        assertEquals(80, auth.size)
        assertArrayEquals(byteArrayOf(0x40, 0, 0, 0, 0, 0x30, 0, 0), auth.copyOf(8))
        assertEquals("bblp", String(auth, 16, 4))
        assertEquals(0, auth[20].toInt())
        assertEquals("12345678", String(auth, 48, 8))
        val jpeg = byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 1, 2, 3, 0xFF.toByte(), 0xD9.toByte())
        val header = byteArrayOf(jpeg.size.toByte(), 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0)
        val input = ByteArrayInputStream(header + jpeg + header + jpeg)
        assertArrayEquals(jpeg, BambuCamera.readFrame(input))
        assertArrayEquals(jpeg, BambuCamera.readFrame(input))
        assertNull(BambuCamera.readFrame(input))
    }

    @Test
    fun mjpegParts() {
        val jpeg = byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 9, 0xFF.toByte(), 0x00, 0xFF.toByte(), 0xD9.toByte())
        val part = "--boundary\r\nContent-Type: image/jpeg\r\nContent-Length: 7\r\n\r\n".toByteArray()
        val input = ByteArrayInputStream(part + jpeg + "\r\n".toByteArray() + part + jpeg)
        assertArrayEquals(jpeg, Mjpeg.readFrame(input))
        assertArrayEquals(jpeg, Mjpeg.readFrame(input))
        assertNull(Mjpeg.readFrame(input))
    }

    @Test
    fun announcements() {
        // As captured from an X1C (Bambu Lab forum, "Use SSDP standards").
        val packet = "NOTIFY * HTTP/1.1\r\nHost: 239.255.255.250:1990\r\nServer: Buildroot/2018.02-rc3 UPnP/1.0 ssdpd/1.8\r\n" +
            "Location: 192.168.1.64\r\nNT: urn:bambulab-com:device:3dprinter:1\r\nNTS: ssdp:alive\r\nUSN: 00M09A000000000\r\n" +
            "Cache-Control: max-age=1800\r\nDevModel.bambu.com: 3DPrinter-X1-Carbon\r\nDevName.bambu.com: MrSlinky\r\n\r\n"
        assertEquals(DiscoveredPrinter("192.168.1.64", "00M09A000000000", "MrSlinky", "3DPrinter-X1-Carbon"),
            BambuDiscovery.parse(packet))
        assertNull(BambuDiscovery.parse("NOTIFY * HTTP/1.1\r\nNT: upnp:rootdevice\r\nLocation: http://1.2.3.4/\r\n"))
    }

    @Test
    fun addresses() {
        assertEquals("http://192.168.1.5", normalizeBase("192.168.1.5/"))
        assertEquals("http://p.lan:7125", normalizeBase(" http://p.lan:7125 "))
        assertEquals("http://p.lan/webcam/?action=stream", absolute("http://p.lan:7125", "/webcam/?action=stream"))
        assertEquals("http://cam.lan/x", absolute("http://p.lan", "http://cam.lan/x"))
        assertEquals("idle", PrinterState.of(HttpPrinterStatus(state = "standby")).state)
        assertEquals("finished", PrinterState.of(HttpPrinterStatus(state = "complete")).state)
    }
}

/**
 * End to end against tools/bambu/fake_printer.py (on the desktop branches): TLS with a test CA, MQTT, implicit FTPS
 * that requires TLS session reuse, the camera of the A1 and P1. Runs with TISMA_BAMBU_FAKE=<its --dir>:
 *   python3 tools/bambu/fake_printer.py --dir /tmp/fake --ssdp-port 11990 &
 *   TISMA_BAMBU_FAKE=/tmp/fake ./gradlew :app:testDebugUnitTest --tests '*BambuFakeTest*'
 */
class BambuFakeTest {
    private val dir = System.getenv("TISMA_BAMBU_FAKE")?.let(::File)

    private fun tls(serial: String? = null, provider: Provider? = null) =
        File(dir, "ca.pem").inputStream().use { BambuTls(it, serial, provider) }

    private fun client(code: String = "12345678", serial: String? = null, provider: Provider? = null) =
        BambuClient("127.0.0.1", code, tls(serial, provider), mqttPort = 18883, ftpsPort = 18990)

    @Test
    fun statusUploadPrintCamera() {
        assumeTrue("TISMA_BAMBU_FAKE not set", dir != null)
        // State: the serial number from the certificate, then the reports.
        var status: BambuStatus? = null
        client().watch(stop = { status != null }) { status = it }
        assertEquals("01P00A000000001", status!!.serial)
        assertEquals("IDLE", status!!.gcodeState)
        assertEquals(24.5, status!!.bedTemp, 0.0)

        // Print: the .gcode.3mf on the SD card (with TLS session reuse on the data connection), then project_file.
        val work = Files.createTempDirectory("tisma").toFile()
        val gcode = File(work, "part.gcode").apply { writeText("G28\n".repeat(50_000)) }
        var last = 0L
        val name = client(serial = "01P00A000000001").print(gcode, "my part.gcode", start = true, useAms = false, workDir = work) { sent, _ ->
            last = sent
            true
        }
        assertEquals("my_part.gcode.3mf", name)
        val uploaded = File(dir, "sdcard/my_part.gcode.3mf")
        assertTrue(uploaded.isFile)
        assertEquals(uploaded.length(), last)
        ZipFile(uploaded).use { assertEquals(gcode.readText(), it.getInputStream(it.getEntry("Metadata/plate_1.gcode")).reader().readText()) }
        val command = JSONObject(File(dir, "last_command.json").readText()).getJSONObject("print")
        assertEquals("project_file", command.getString("command"))
        assertEquals("my_part.gcode.3mf", command.getString("subtask_name"))

        // Camera: three images.
        val frames = mutableListOf<ByteArray>()
        BambuCamera.stream(tls(), "127.0.0.1", "12345678", port = 16000, stop = { frames.size >= 3 }) { frames += it }
        assertEquals(3, frames.size)
        assertTrue(frames.all { it[0] == 0xFF.toByte() && it[1] == 0xD8.toByte() })
        work.deleteRecursively()
    }

    @Test
    fun uploadWithTheTlsOfAndroid() {
        assumeTrue("TISMA_BAMBU_FAKE not set", dir != null)
        // Conscrypt (BoringSSL) as on Android: the data connection must resume the session there too.
        val f = Files.createTempFile("x", ".txt").toFile().apply { writeText("conscrypt\n".repeat(10_000)) }
        client(provider = Conscrypt.newProvider()).upload(f, "conscrypt.txt") { _, _ -> true }
        assertEquals(f.readText(), File(dir, "sdcard/conscrypt.txt").readText())
        var status: BambuStatus? = null
        client(provider = Conscrypt.newProvider()).watch(stop = { status != null }) { status = it }
        assertEquals("01P00A000000001", status!!.serial)
        f.delete()
    }

    @Test
    fun wrongAccessCodeAndWrongPrinter() {
        assumeTrue("TISMA_BAMBU_FAKE not set", dir != null)
        try {
            client(code = "00000000").watch(stop = { false }) {}
            throw AssertionError("connected with a wrong access code")
        } catch (_: BambuAccessCodeException) {
        }
        val f = Files.createTempFile("x", ".txt").toFile().apply { writeText("x") }
        try {
            client(code = "00000000").upload(f, "x.txt") { _, _ -> true }
            throw AssertionError("uploaded with a wrong access code")
        } catch (_: BambuAccessCodeException) {
        }
        // Another serial number than the one saved: refused (the certificate names another printer).
        var refused = false
        try {
            client(serial = "OTHER").watch(stop = { true }) {}
        } catch (e: javax.net.ssl.SSLException) {
            refused = true
        }
        assertTrue(refused)
        assertFalse(f.delete().not())
    }
}
