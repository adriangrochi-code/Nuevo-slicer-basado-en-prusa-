package uy.com.tisma.app.cloud

import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Account of Tisma Cloud as the master reports it (GET /v1/me). */
data class CloudAccount(
    val email: String,
    val plan: String,
    val paid: Boolean,
    val paidUntil: String?,
    val hoursIncluded: Double,
    val hoursUsed: Double,
    val activeJobs: Int,
)

/** Why a call failed, for the screens (they pick the text). */
enum class CloudErrorKind { WRONG_LOGIN, NOT_VERIFIED, ALREADY_EXISTS, WRONG_CODE, TOO_MANY, NOT_LOGGED_IN, INVALID, NETWORK, SERVER }

class CloudException(val kind: CloudErrorKind, message: String) : Exception(message)

/**
 * The API of the Tisma Cloud master (server/README.md, "API") used by the app: accounts and plan. Blocking calls: run
 * them off the main thread. [token] is the session of this device (null: not logged in).
 */
class CloudClient(baseUrl: String, var token: String? = null, private val device: String = "Tisma Android") {
    private val base = baseUrl.trimEnd('/')

    /** Logs in with e-mail and password; returns the session token of this device. */
    fun login(email: String, password: String): String =
        post("/v1/auth/login", JSONObject().put("email", email).put("password", password).put("device", device))
            .getString("token").also { token = it }

    /** Creates an account; the master e-mails a 6-digit code to confirm it ([verify]). */
    fun register(email: String, password: String) {
        post("/v1/auth/register", JSONObject().put("email", email).put("password", password))
    }

    /** Confirms the e-mail with its code and logs in. */
    fun verify(email: String, code: String): String =
        post("/v1/auth/verify", JSONObject().put("email", email).put("code", code).put("device", device))
            .getString("token").also { token = it }

    fun resendCode(email: String) {
        post("/v1/auth/resend", JSONObject().put("email", email))
    }

    fun account(): CloudAccount {
        val json = request("GET", "/v1/me", null)
        return CloudAccount(
            email = json.getString("email"),
            plan = json.getString("plan"),
            paid = json.getBoolean("paid"),
            paidUntil = json.optString("paid_until").takeIf { it.isNotEmpty() && it != "null" },
            hoursIncluded = json.getDouble("hours_included"),
            hoursUsed = json.getDouble("hours_used"),
            activeJobs = json.getInt("active_jobs"),
        )
    }

    /** Ends the session of this device on the master (the token stops working). */
    fun logout() {
        try {
            request("POST", "/v1/auth/logout", JSONObject())
        } finally {
            token = null
        }
    }

    private fun post(path: String, body: JSONObject): JSONObject = request("POST", path, body)

    private fun request(method: String, path: String, body: JSONObject?): JSONObject {
        val connection = try {
            URL(base + path).openConnection() as HttpURLConnection
        } catch (e: IOException) {
            throw CloudException(CloudErrorKind.NETWORK, e.message ?: "network")
        }
        try {
            connection.requestMethod = method
            connection.connectTimeout = 15_000
            connection.readTimeout = 30_000
            connection.setRequestProperty("Accept", "application/json")
            token?.let { connection.setRequestProperty("Authorization", "Bearer $it") }
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toString().toByteArray()) }
            }
            val status = connection.responseCode
            val text = (if (status < 400) connection.inputStream else connection.errorStream)
                ?.bufferedReader()?.use { it.readText() } ?: ""
            if (status >= 400) throw error(status, text)
            return if (text.isBlank()) JSONObject() else JSONObject(text)
        } catch (e: IOException) {
            throw CloudException(CloudErrorKind.NETWORK, e.message ?: "network")
        } finally {
            connection.disconnect()
        }
    }

    private fun error(status: Int, text: String): CloudException {
        // FastAPI answers {"detail": "..."} (a list of problems for invalid fields).
        val detail = try {
            JSONObject(text).opt("detail")?.toString() ?: text
        } catch (_: Exception) {
            text
        }
        val kind = when (status) {
            400 -> CloudErrorKind.WRONG_CODE
            401 -> if (token != null && detail.contains("token", ignoreCase = true)) CloudErrorKind.NOT_LOGGED_IN else CloudErrorKind.WRONG_LOGIN
            403 -> if (detail.contains("verified", ignoreCase = true)) CloudErrorKind.NOT_VERIFIED else CloudErrorKind.SERVER
            409 -> CloudErrorKind.ALREADY_EXISTS
            422 -> CloudErrorKind.INVALID
            429 -> CloudErrorKind.TOO_MANY
            else -> CloudErrorKind.SERVER
        }
        return CloudException(kind, detail)
    }
}
