package uy.com.tisma.app.cloud

import android.content.Context

/**
 * The session of Tisma Cloud on this phone: the address of the master and the token of this device. Kept in the private
 * storage of the app (no other app can read it); logging out removes the token on the master too.
 */
class CloudStore(context: Context) {
    private val prefs = context.getSharedPreferences("cloud", Context.MODE_PRIVATE)

    var serverUrl: String
        get() = prefs.getString("server_url", DEFAULT_SERVER) ?: DEFAULT_SERVER
        set(value) = prefs.edit().putString("server_url", value.trim().trimEnd('/')).apply()

    var token: String?
        get() = prefs.getString("token", null)
        set(value) = prefs.edit().putString("token", value).apply()

    var email: String?
        get() = prefs.getString("email", null)
        set(value) = prefs.edit().putString("email", value).apply()

    fun client() = CloudClient(serverUrl, token)

    companion object {
        const val DEFAULT_SERVER = "https://tismaservidores.net"
    }
}
