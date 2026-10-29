package uy.com.tisma.app.printers

import java.io.InputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.security.KeyStore
import java.security.Provider
import java.security.cert.CertificateException
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

/**
 * TLS to a Bambu Lab printer: its certificate must be issued by the CA of Bambu Lab (resources/cert/
 * bambu_printer_ca.pem, as the desktop program) and its common name is the serial number of the printer
 * (OpenBambuAPI, tls.md). The name is checked against [expectedSerial] when it is known. [provider] chooses the TLS
 * implementation and its certificate checks (the tests run with the ones of Android); the default ones when null.
 */
class BambuTls(caPem: InputStream, private val expectedSerial: String? = null, private val provider: Provider? = null) {
    private val trustManager: X509TrustManager

    init {
        val factory = CertificateFactory.getInstance("X.509")
        val store = KeyStore.getInstance(KeyStore.getDefaultType()).apply { load(null, null) }
        factory.generateCertificates(caPem).forEachIndexed { i, c -> store.setCertificateEntry("bambu-ca-$i", c) }
        val algorithm = TrustManagerFactory.getDefaultAlgorithm()
        val tmf = (if (provider != null) TrustManagerFactory.getInstance("PKIX", provider) else TrustManagerFactory.getInstance(algorithm))
            .apply { init(store) }
        val chain = tmf.trustManagers.filterIsInstance<X509TrustManager>().first()
        trustManager = object : X509TrustManager {
            override fun getAcceptedIssuers(): Array<X509Certificate> = chain.acceptedIssuers
            override fun checkClientTrusted(certs: Array<X509Certificate>, authType: String) = chain.checkClientTrusted(certs, authType)
            override fun checkServerTrusted(certs: Array<X509Certificate>, authType: String) {
                chain.checkServerTrusted(certs, authType)
                val name = commonName(certs[0])
                if (!expectedSerial.isNullOrEmpty() && !name.equals(expectedSerial, ignoreCase = true))
                    throw CertificateException("The printer is $name, not $expectedSerial")
            }
        }
    }

    private fun context(protocol: String): SSLContext =
        (if (provider != null) SSLContext.getInstance(protocol, provider) else SSLContext.getInstance(protocol))
            .apply { init(null, arrayOf(trustManager), null) }

    fun factory(): SSLSocketFactory = context("TLS").socketFactory

    fun connect(host: String, port: Int, timeoutMs: Int = 10_000): SSLSocket {
        val plain = Socket()
        plain.connect(InetSocketAddress(host, port), timeoutMs)
        plain.soTimeout = timeoutMs
        val socket = factory().createSocket(plain, host, port, true) as SSLSocket
        socket.startHandshake()
        return socket
    }

    // One context for all the FTPS connections of a client, so that the data connections find the session of the
    // control connection in its cache.
    private val context12: SSLContext by lazy { context("TLSv1.2") }

    /**
     * A TLS 1.2 connection for FTPS over [plain], already connected, through an engine made for [host] and [port]: the
     * address of the control connection, whose session the data connections resume (see [TlsEngineConnection]).
     * TLS 1.2 as the desktop program: the FTP server of the printers resumes the session of the handshake. The
     * handshake is left to the caller.
     */
    fun ftps(plain: Socket, host: String, port: Int): TlsEngineConnection {
        val engine = context12.createSSLEngine(host, port).apply { enabledProtocols = arrayOf("TLSv1.2") }
        return TlsEngineConnection(engine, plain)
    }

    companion object {
        fun commonName(cert: X509Certificate): String =
            Regex("""(?:^|,)\s*CN=([^,]+)""").find(cert.subjectX500Principal.name)?.groupValues?.get(1)?.trim() ?: ""

        /** The serial number of the printer at the other end. */
        fun peerSerial(socket: SSLSocket): String =
            (socket.session.peerCertificates.firstOrNull() as? X509Certificate)?.let(::commonName) ?: ""
    }
}
