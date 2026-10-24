package uy.com.tisma.app.printers

import java.io.BufferedReader
import java.io.File
import java.io.IOException
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket

class FtpsException(val code: Int, message: String) : IOException(message)

/**
 * Upload of one file by implicit FTPS (TLS from the start, port 990 on the printers), what a Bambu Lab printer needs:
 * USER/PASS, TYPE I, PBSZ 0, PROT P, PASV, STOR.
 *
 * The FTP server of the printers (vsftpd with require_ssl_reuse) refuses a data connection that does not resume the TLS
 * session of the control connection ("522 SSL connection failed: session reuse required"). Both connections go through
 * SSLEngines made for the host and port of the control connection, so that the data connection resumes its session
 * (see [TlsEngineConnection]). PASV answers 0.0.0.0 on the printers: the address of the control connection is used.
 */
class FtpsUpload(private val tls: BambuTls, private val host: String, private val port: Int, private val timeoutMs: Int = 15_000) {
    private lateinit var control: TlsEngineConnection
    private lateinit var reader: BufferedReader
    private lateinit var writer: OutputStream

    private fun reply(): Pair<Int, String> {
        // A multi-line reply starts with "ddd-" and ends with "ddd ".
        var line = reader.readLine() ?: throw IOException("The printer closed the FTP connection")
        val code = line.take(3).toIntOrNull() ?: throw IOException("FTP: bad reply $line")
        if (line.length > 3 && line[3] == '-')
            while (true) {
                line = reader.readLine() ?: throw IOException("The printer closed the FTP connection")
                if (line.startsWith("$code ")) break
            }
        return code to line
    }

    private fun command(cmd: String, vararg ok: Int): Pair<Int, String> {
        writer.write("$cmd\r\n".toByteArray(Charsets.UTF_8))
        writer.flush()
        val r = reply()
        if (r.first !in ok) throw FtpsException(r.first, "FTP ${cmd.substringBefore(' ')}: ${r.second}")
        return r
    }

    /**
     * Sends [file] as [name] to the root of the SD card; [progress] gets (bytes sent, total) and returns false to
     * cancel. Throws FtpsException 530 for a wrong access code.
     */
    fun upload(user: String, password: String, file: File, name: String, progress: (Long, Long) -> Boolean) {
        val socket = Socket()
        try {
            socket.connect(InetSocketAddress(host, port), timeoutMs)
            socket.soTimeout = timeoutMs
        } catch (e: IOException) {
            runCatching { socket.close() }
            throw e
        }
        control = tls.ftps(socket, host, port)
        try {
            control.handshake()
            reader = BufferedReader(InputStreamReader(control.input, Charsets.UTF_8))
            writer = control.output
            val greeting = reply()
            if (greeting.first != 220) throw FtpsException(greeting.first, "FTP: ${greeting.second}")
            command("USER $user", 331, 230)
            command("PASS $password", 230)
            command("TYPE I", 200)
            command("PBSZ 0", 200)
            command("PROT P", 200)
            val pasv = command("PASV", 227).second
            val numbers = Regex("""(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)""").find(pasv)?.groupValues
                ?: throw IOException("FTP: bad PASV reply $pasv")
            val dataPort = numbers[5].toInt() * 256 + numbers[6].toInt()
            val plain = Socket()
            plain.connect(InetSocketAddress(host, dataPort), timeoutMs)
            plain.soTimeout = timeoutMs
            writer.write("STOR $name\r\n".toByteArray(Charsets.UTF_8))
            writer.flush()
            // For the host and port of the control connection: it resumes the TLS session of the control connection.
            val data = tls.ftps(plain, host, port)
            // The handshake first: some servers answer 150 only once the data connection is secured, others before.
            try {
                data.handshake()
            } catch (e: IOException) {
                runCatching { plain.close() }
                val r = runCatching { reply() }.getOrNull()
                // An error reply of the server explains more than the failed handshake; 150 does not.
                throw if (r != null && r.first >= 400) FtpsException(r.first, "FTP STOR: ${r.second}")
                      else IOException("FTPS data connection: ${e.message}", e)
            }
            val opening = reply()
            if (opening.first != 150 && opening.first != 125) {
                runCatching { plain.close() }
                throw FtpsException(opening.first, "FTP STOR: ${opening.second}")
            }
            val total = file.length()
            var sent = 0L
            data.output.use { out ->
                file.inputStream().use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buffer)
                        if (n < 0) break
                        out.write(buffer, 0, n)
                        sent += n
                        if (!progress(sent, total)) throw FtpsException(0, "cancelled")
                    }
                }
            }
            val done = reply()
            if (done.first != 226 && done.first != 250) throw FtpsException(done.first, "FTP STOR: ${done.second}")
            runCatching { command("QUIT", 221) }
        } finally {
            runCatching { control.close() }
        }
    }
}
