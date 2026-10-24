package uy.com.tisma.app.printers

import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL

/**
 * The camera of the Bambu Lab printers A1 and P1: JPEG images over TLS on port 6000 (OpenBambuAPI, video.md). The
 * X1, P2S and H2 serve RTSPS on port 322 instead, which this does not read.
 */
object BambuCamera {
    const val PORT = 6000

    /** The 80 bytes sent after connecting: sizes and type, then the user and the access code padded to 32 bytes. */
    fun authPacket(user: String, accessCode: String): ByteArray {
        val out = ByteArray(80)
        fun int(at: Int, v: Int) {
            for (i in 0..3) out[at + i] = (v shr (8 * i)).toByte()
        }
        int(0, 0x40)
        int(4, 0x3000)
        user.toByteArray(Charsets.US_ASCII).copyInto(out, 16, 0, minOf(user.length, 32))
        accessCode.toByteArray(Charsets.US_ASCII).copyInto(out, 48, 0, minOf(accessCode.length, 32))
        return out
    }

    /** The next JPEG: a 16-byte header (little-endian payload size first), then the image. Null at the end. */
    fun readFrame(input: InputStream): ByteArray? {
        val header = ByteArray(16)
        if (!readFully(input, header)) return null
        val size = (header[0].toInt() and 0xFF) or ((header[1].toInt() and 0xFF) shl 8) or
            ((header[2].toInt() and 0xFF) shl 16) or ((header[3].toInt() and 0xFF) shl 24)
        if (size <= 0 || size > 16 shl 20) throw IOException("Camera: bad frame size $size")
        val image = ByteArray(size)
        if (!readFully(input, image)) return null
        return image
    }

    private fun readFully(input: InputStream, buf: ByteArray): Boolean {
        var read = 0
        while (read < buf.size) {
            val n = input.read(buf, read, buf.size - read)
            if (n < 0) return false
            read += n
        }
        return true
    }

    /** Streams the images to [onFrame] until [stop] returns true or the printer ends the connection. */
    fun stream(tls: BambuTls, host: String, accessCode: String, port: Int = PORT, stop: () -> Boolean, onFrame: (ByteArray) -> Unit) {
        tls.connect(host, port).use { socket ->
            socket.outputStream.write(authPacket("bblp", accessCode))
            socket.outputStream.flush()
            // A timeout lets [stop] be checked; the printer sends about one image per second.
            socket.soTimeout = 2_000
            val input = BufferedInputStream(socket.inputStream)
            while (!stop()) {
                val frame = try {
                    readFrame(input)
                } catch (_: SocketTimeoutException) {
                    continue
                } ?: throw IOException("The camera closed the connection (wrong access code, or another viewer)")
                onFrame(frame)
            }
        }
    }
}

/**
 * MJPEG over HTTP (multipart/x-mixed-replace), the stream of the webcams of Klipper (crowsnest, mjpg-streamer) and
 * OctoPrint: the JPEG of every part.
 */
object Mjpeg {
    /** The next JPEG of a multipart stream: whatever lies between the next FF D8 and the FF D9 that ends it. */
    fun readFrame(input: InputStream): ByteArray? {
        var prev = -1
        while (true) {
            val b = input.read()
            if (b < 0) return null
            if (prev == 0xFF && b == 0xD8) break
            prev = b
        }
        val out = ByteArrayOutputStream(64 * 1024)
        out.write(0xFF)
        out.write(0xD8)
        prev = -1
        while (true) {
            val b = input.read()
            if (b < 0) return null
            out.write(b)
            if (prev == 0xFF && b == 0xD9) return out.toByteArray()
            prev = b
            if (out.size() > 16 shl 20) throw IOException("Camera: image too large")
        }
    }

    fun stream(url: String, headers: Map<String, String> = emptyMap(), stop: () -> Boolean, onFrame: (ByteArray) -> Unit) {
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.connectTimeout = 10_000
        conn.readTimeout = 15_000
        headers.forEach { (k, v) -> conn.setRequestProperty(k, v) }
        try {
            if (conn.responseCode !in 200..299) throw IOException("Camera: HTTP ${conn.responseCode}")
            val input = BufferedInputStream(conn.inputStream)
            val single = conn.contentType?.startsWith("image/") == true
            while (!stop()) {
                val frame = readFrame(input) ?: break
                onFrame(frame)
                // A snapshot address gives one image: ask again, about twice a second.
                if (single) return
            }
        } finally {
            conn.disconnect()
        }
    }
}

/** A Bambu Lab printer announcing itself on the local network. */
data class DiscoveredPrinter(val ip: String, val serial: String, val name: String, val model: String)

/**
 * The announcements of the Bambu Lab printers: SSDP style NOTIFY messages to 239.255.255.250 on ports 1990 and 2021
 * (not the standard 1900), with the address in Location, the serial number in USN and Bambu headers such as
 * DevName.bambu.com and DevModel.bambu.com (packets captured in the Bambu Lab forum, "Use SSDP standards").
 */
object BambuDiscovery {
    val PORTS = listOf(1990, 2021)
    const val GROUP = "239.255.255.250"

    fun parse(packet: String): DiscoveredPrinter? {
        val headers = packet.lineSequence().mapNotNull { line ->
            val i = line.indexOf(':')
            if (i <= 0) null else line.substring(0, i).trim().lowercase() to line.substring(i + 1).trim()
        }.toMap()
        if (headers["nt"]?.startsWith("urn:bambulab-com:device:3dprinter") != true) return null
        val ip = headers["location"]?.removePrefix("http://")?.substringBefore('/')?.substringBefore(':') ?: return null
        val serial = headers["usn"] ?: return null
        if (ip.isEmpty() || serial.isEmpty()) return null
        return DiscoveredPrinter(ip, serial, headers["devname.bambu.com"] ?: serial, headers["devmodel.bambu.com"] ?: "")
    }
}
