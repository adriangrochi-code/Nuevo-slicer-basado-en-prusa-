package uy.com.tisma.app.cloud

import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import java.net.InetAddress
import java.net.ServerSocket
import kotlin.concurrent.thread

/** [CloudClient] against a fake master that answers like the FastAPI one (one HTTP/1.1 request per connection). */
class CloudClientTest {
    private lateinit var server: ServerSocket
    private val requests = mutableListOf<Triple<String, String?, JSONObject?>>() // path, Authorization, body
    private var answer: (String) -> Pair<Int, String> = { 200 to "{}" }

    @Before
    fun start() {
        server = ServerSocket(0, 10, InetAddress.getLoopbackAddress())
        thread(isDaemon = true) {
            while (!server.isClosed) {
                val socket = try { server.accept() } catch (_: Exception) { break }
                socket.use { s ->
                    val input = s.getInputStream().bufferedReader()
                    val path = input.readLine().split(" ")[1]
                    var auth: String? = null
                    var length = 0
                    while (true) {
                        val line = input.readLine()
                        if (line.isNullOrEmpty()) break
                        val (name, value) = line.split(":", limit = 2).map(String::trim)
                        if (name.equals("Authorization", true)) auth = value
                        if (name.equals("Content-Length", true)) length = value.toInt()
                    }
                    val text = CharArray(length).also { var n = 0; while (n < length) n += input.read(it, n, length - n) }
                    synchronized(requests) {
                        requests += Triple(path, auth, String(text).takeIf { it.isNotEmpty() }?.let { JSONObject(it) })
                    }
                    val (status, body) = answer(path)
                    val bytes = body.toByteArray()
                    s.getOutputStream().apply {
                        write(("HTTP/1.1 $status X\r\nContent-Type: application/json\r\nContent-Length: ${bytes.size}\r\n" +
                            "Connection: close\r\n\r\n").toByteArray())
                        write(bytes)
                        flush()
                    }
                }
            }
        }
    }

    @After
    fun stop() = server.close()

    private fun client(token: String? = null) = CloudClient("http://127.0.0.1:${server.localPort}/", token)

    private fun failure(block: () -> Unit): CloudErrorKind =
        try {
            block()
            throw AssertionError("no error")
        } catch (e: CloudException) {
            e.kind
        }

    @Test
    fun loginSendsDeviceAndKeepsToken() {
        answer = { 200 to """{"token":"t1","email":"a@b.uy"}""" }
        val c = client()
        assertEquals("t1", c.login("a@b.uy", "secret"))
        assertEquals("t1", c.token)
        val (path, auth, body) = requests.single()
        assertEquals("/v1/auth/login", path)
        assertNull(auth)
        assertEquals("secret", body!!.getString("password"))
        assertEquals("Tisma Android", body.getString("device"))
    }

    @Test
    fun accountReadsMeWithBearer() {
        answer = {
            200 to """{"email":"a@b.uy","plan":"pro","paid":true,"paid_until":"2026-11-30T00:00:00",
                "hours_included":20.0,"hours_used":3.5,"hours_left":16.5,"active_jobs":1,"max_concurrent":2}"""
        }
        val a = client("t1").account()
        assertEquals("Bearer t1", requests.single().second)
        assertEquals("/v1/me", requests.single().first)
        assertEquals(CloudAccount("a@b.uy", "pro", true, "2026-11-30T00:00:00", 20.0, 3.5, 1), a)
    }

    @Test
    fun accountWithoutPaymentDate() {
        answer = {
            200 to """{"email":"a@b.uy","plan":"trial","paid":false,"paid_until":null,
                "hours_included":1,"hours_used":0,"active_jobs":0}"""
        }
        val a = client("t1").account()
        assertFalse(a.paid)
        assertNull(a.paidUntil)
    }

    @Test
    fun errorsByStatus() {
        answer = { 401 to """{"detail":"Wrong e-mail or password"}""" }
        assertEquals(CloudErrorKind.WRONG_LOGIN, failure { client().login("a@b.uy", "x") })
        answer = { 401 to """{"detail":"Invalid access token"}""" }
        assertEquals(CloudErrorKind.NOT_LOGGED_IN, failure { client("old").account() })
        answer = { 403 to """{"detail":"E-mail not verified: enter the code we sent"}""" }
        assertEquals(CloudErrorKind.NOT_VERIFIED, failure { client().login("a@b.uy", "x") })
        answer = { 409 to """{"detail":"Account exists"}""" }
        assertEquals(CloudErrorKind.ALREADY_EXISTS, failure { client().register("a@b.uy", "12345678") })
        answer = { 400 to """{"detail":"Wrong code"}""" }
        assertEquals(CloudErrorKind.WRONG_CODE, failure { client().verify("a@b.uy", "123456") })
        answer = { 422 to """{"detail":[{"loc":["body","password"],"msg":"too short"}]}""" }
        assertEquals(CloudErrorKind.INVALID, failure { client().register("a@b.uy", "1") })
        answer = { 429 to """{"detail":"Too many attempts"}""" }
        assertEquals(CloudErrorKind.TOO_MANY, failure { client().resendCode("a@b.uy") })
        answer = { 500 to "Internal Server Error" }
        assertEquals(CloudErrorKind.SERVER, failure { client("t1").account() })
    }

    @Test
    fun logoutForgetsTokenEvenOnError() {
        answer = { 500 to "{}" }
        val c = client("t1")
        failure { c.logout() }
        assertNull(c.token)
        assertEquals("/v1/auth/logout", requests.single().first)
    }

    @Test
    fun unreachableServerIsNetworkError() {
        val port = server.localPort
        server.close()
        assertEquals(CloudErrorKind.NETWORK, failure { CloudClient("http://127.0.0.1:$port").account() })
    }
}
