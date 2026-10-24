package uy.com.tisma.app.printers

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream

/**
 * The few MQTT 3.1.1 packets a Bambu Lab printer needs (CONNECT, SUBSCRIBE, PUBLISH with QoS 0, PINGREQ, DISCONNECT),
 * as BambuMqtt of the desktop program (src/slic3r/Utils/BambuLanClient.cpp).
 */
object Mqtt {
    const val CONNECT = 1
    const val CONNACK = 2
    const val PUBLISH = 3
    const val SUBSCRIBE = 8
    const val SUBACK = 9
    const val PINGREQ = 12
    const val PINGRESP = 13
    const val DISCONNECT = 14

    class Packet(val type: Int, val flags: Int, val body: ByteArray)

    private fun string(s: String): ByteArray {
        val b = s.toByteArray(Charsets.UTF_8)
        return byteArrayOf((b.size shr 8).toByte(), b.size.toByte()) + b
    }

    private fun packet(type: Int, flags: Int, body: ByteArray): ByteArray {
        val out = ByteArrayOutputStream()
        out.write((type shl 4) or flags)
        // Remaining length: 7 bits per byte, the high bit set while more bytes follow.
        var n = body.size
        do {
            var b = n % 128
            n /= 128
            if (n > 0) b = b or 0x80
            out.write(b)
        } while (n > 0)
        out.write(body)
        return out.toByteArray()
    }

    fun connect(clientId: String, user: String, password: String, keepAliveS: Int): ByteArray {
        // Protocol "MQTT" level 4; flags: user name, password, clean session.
        val header = string("MQTT") + byteArrayOf(4, 0xC2.toByte(), (keepAliveS shr 8).toByte(), keepAliveS.toByte())
        return packet(CONNECT, 0, header + string(clientId) + string(user) + string(password))
    }

    fun subscribe(packetId: Int, topic: String): ByteArray =
        packet(SUBSCRIBE, 2, byteArrayOf((packetId shr 8).toByte(), packetId.toByte()) + string(topic) + byteArrayOf(0))

    fun publish(topic: String, payload: String): ByteArray = packet(PUBLISH, 0, string(topic) + payload.toByteArray(Charsets.UTF_8))

    fun pingreq(): ByteArray = packet(PINGREQ, 0, ByteArray(0))

    fun disconnect(): ByteArray = packet(DISCONNECT, 0, ByteArray(0))

    /** Reads one packet; throws IOException at the end of the stream or on a malformed length. */
    fun read(input: InputStream): Packet {
        val first = input.read()
        if (first < 0) throw IOException("The printer closed the connection")
        var length = 0
        var multiplier = 1
        for (i in 0 until 4) {
            val b = input.read()
            if (b < 0) throw IOException("The printer closed the connection")
            length += (b and 0x7F) * multiplier
            if (b and 0x80 == 0) {
                val body = ByteArray(length)
                var read = 0
                while (read < length) {
                    val n = input.read(body, read, length - read)
                    if (n < 0) throw IOException("The printer closed the connection")
                    read += n
                }
                return Packet(first shr 4, first and 0x0F, body)
            }
            multiplier *= 128
        }
        throw IOException("MQTT: malformed length")
    }

    /** Topic and payload of a PUBLISH packet (any QoS), or null. */
    fun parsePublish(p: Packet): Pair<String, String>? {
        if (p.type != PUBLISH || p.body.size < 2) return null
        val n = ((p.body[0].toInt() and 0xFF) shl 8) or (p.body[1].toInt() and 0xFF)
        if (2 + n > p.body.size) return null
        val topic = String(p.body, 2, n, Charsets.UTF_8)
        // QoS 1 and 2 have a packet identifier after the topic.
        val start = 2 + n + if ((p.flags shr 1) and 3 != 0) 2 else 0
        if (start > p.body.size) return null
        return topic to String(p.body, start, p.body.size - start, Charsets.UTF_8)
    }
}
