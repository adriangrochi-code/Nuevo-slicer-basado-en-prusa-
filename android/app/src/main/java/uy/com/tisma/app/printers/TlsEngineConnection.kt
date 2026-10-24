package uy.com.tisma.app.printers

import java.io.Closeable
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.Socket
import java.nio.ByteBuffer
import javax.net.ssl.SSLEngine
import javax.net.ssl.SSLEngineResult
import javax.net.ssl.SSLSession

/**
 * TLS client over a plain socket through an SSLEngine, for implicit FTPS, whose server wants the data connection to
 * resume the TLS session of the control connection. Both connections go through engines of the same SSLContext:
 * - the engine of the data connection is made for the host and port of the control connection, so that the session
 *   cache finds the session (a layered socket looks it up by the port it is connected to, the data port);
 * - on Android (Conscrypt) SSLContext.createSSLEngine makes the native SSL with the context of the server sessions,
 *   whose session id context differs from the one of the client sockets: BoringSSL refuses to resume the session of
 *   a socket in an engine (ATTEMPT_TO_REUSE_SESSION_IN_DIFFERENT_CONTEXT), not the session of another engine.
 */
class TlsEngineConnection(private val engine: SSLEngine, private val socket: Socket) : Closeable {
    private val session: SSLSession = engine.session
    private var netOut = ByteBuffer.allocate(session.packetBufferSize)
    private var netIn = ByteBuffer.allocate(session.packetBufferSize)
    // Decrypted data not read yet, from 0 to its position.
    private var appIn = ByteBuffer.allocate(session.applicationBufferSize)
    private val raw = socket.getOutputStream()
    private val rawIn = socket.getInputStream()

    /** The session of the handshake (resumed or not). */
    val handshakeSession: SSLSession get() = engine.session

    fun handshake() {
        engine.useClientMode = true
        engine.beginHandshake()
        var status = engine.handshakeStatus
        while (status != SSLEngineResult.HandshakeStatus.FINISHED && status != SSLEngineResult.HandshakeStatus.NOT_HANDSHAKING)
            status = step(status)
    }

    private fun step(status: SSLEngineResult.HandshakeStatus): SSLEngineResult.HandshakeStatus = when (status) {
        SSLEngineResult.HandshakeStatus.NEED_WRAP -> {
            netOut.clear()
            val r = engine.wrap(ByteBuffer.allocate(0), netOut)
            flushNet()
            if (r.status == SSLEngineResult.Status.CLOSED) throw IOException("TLS: closed during the handshake")
            r.handshakeStatus
        }
        SSLEngineResult.HandshakeStatus.NEED_UNWRAP -> {
            val r = unwrap()
            if (r.status != SSLEngineResult.Status.OK) throw IOException("TLS: the server closed the connection")
            r.handshakeStatus
        }
        SSLEngineResult.HandshakeStatus.NEED_TASK -> {
            while (true) engine.delegatedTask?.run() ?: break
            engine.handshakeStatus
        }
        else -> status
    }

    /** Unwraps one record; BUFFER_UNDERFLOW when the connection ends in the middle of one. */
    private fun unwrap(): SSLEngineResult {
        while (true) {
            netIn.flip()
            val r = try {
                engine.unwrap(netIn, appIn)
            } finally {
                netIn.compact()
            }
            when (r.status) {
                SSLEngineResult.Status.BUFFER_UNDERFLOW -> {
                    if (!netIn.hasRemaining()) netIn = grow(netIn)
                    val n = rawIn.read(netIn.array(), netIn.arrayOffset() + netIn.position(), netIn.remaining())
                    if (n < 0) return r
                    netIn.position(netIn.position() + n)
                }
                SSLEngineResult.Status.BUFFER_OVERFLOW -> appIn = grow(appIn)
                else -> return r
            }
        }
    }

    private fun grow(b: ByteBuffer): ByteBuffer = ByteBuffer.allocate(b.capacity() * 2).also { b.flip(); it.put(b) }

    private fun flushNet() {
        netOut.flip()
        raw.write(netOut.array(), netOut.arrayOffset(), netOut.remaining())
        raw.flush()
        netOut.clear()
    }

    /** Reads records until some data comes; false at the end of the stream. */
    private fun fill(): Boolean {
        while (true) {
            val r = unwrap()
            if (r.status != SSLEngineResult.Status.OK) return false
            if (r.bytesProduced() > 0) return true
            var status = r.handshakeStatus
            while (status == SSLEngineResult.HandshakeStatus.NEED_TASK || status == SSLEngineResult.HandshakeStatus.NEED_WRAP)
                status = step(status)
        }
    }

    val input: InputStream = object : InputStream() {
        override fun read(): Int {
            val b = ByteArray(1)
            return if (read(b, 0, 1) < 0) -1 else b[0].toInt() and 0xFF
        }

        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (len == 0) return 0
            while (appIn.position() == 0) if (!fill()) return -1
            appIn.flip()
            val n = minOf(len, appIn.remaining())
            appIn.get(b, off, n)
            appIn.compact()
            return n
        }
    }

    val output: OutputStream = object : OutputStream() {
        override fun write(b: Int) = write(byteArrayOf(b.toByte()), 0, 1)

        override fun write(b: ByteArray, off: Int, len: Int) {
            val src = ByteBuffer.wrap(b, off, len)
            while (src.hasRemaining()) {
                netOut.clear()
                val r = engine.wrap(src, netOut)
                when (r.status) {
                    SSLEngineResult.Status.OK -> flushNet()
                    SSLEngineResult.Status.BUFFER_OVERFLOW -> netOut = ByteBuffer.allocate(netOut.capacity() * 2)
                    else -> throw IOException("TLS: ${r.status}")
                }
            }
        }

        override fun close() = this@TlsEngineConnection.close()
    }

    /** Ends the TLS stream (close_notify) and the connection: on a data connection the server then knows the file is complete. */
    override fun close() {
        try {
            engine.closeOutbound()
            while (!engine.isOutboundDone) {
                netOut.clear()
                engine.wrap(ByteBuffer.allocate(0), netOut)
                flushNet()
            }
        } finally {
            socket.close()
        }
    }
}
