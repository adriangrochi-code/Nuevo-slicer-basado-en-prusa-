package uy.com.tisma.app.printers

import android.content.Context
import android.net.wifi.WifiManager
import java.net.DatagramPacket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.MulticastSocket
import java.net.SocketTimeoutException

/**
 * Listens to the announcements of the Bambu Lab printers on the Wi-Fi network (see [BambuDiscovery]) until [stop]
 * returns true. Android drops multicast packets unless a multicast lock is held.
 */
fun discoverBambu(context: Context, stop: () -> Boolean, onFound: (DiscoveredPrinter) -> Unit) {
    val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    val lock = wifi.createMulticastLock("tisma-printers").apply { setReferenceCounted(false); acquire() }
    val sockets = BambuDiscovery.PORTS.mapNotNull { port ->
        runCatching {
            MulticastSocket(null as InetSocketAddress?).apply {
                reuseAddress = true
                bind(InetSocketAddress(port))
                soTimeout = 500
                // Without the group (a network that refuses it) the packets sent to this address still arrive.
                @Suppress("DEPRECATION")
                runCatching { joinGroup(InetAddress.getByName(BambuDiscovery.GROUP)) }
            }
        }.getOrNull()
    }
    try {
        val buffer = ByteArray(2048)
        while (!stop() && sockets.isNotEmpty()) {
            for (s in sockets) {
                val packet = DatagramPacket(buffer, buffer.size)
                try {
                    s.receive(packet)
                } catch (_: SocketTimeoutException) {
                    continue
                }
                BambuDiscovery.parse(String(packet.data, 0, packet.length, Charsets.UTF_8))?.let(onFound)
            }
        }
    } finally {
        sockets.forEach { runCatching { it.close() } }
        lock.release()
    }
}
