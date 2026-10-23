package uy.com.tisma.app.printers

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** The kinds of printers the app talks to over the network. */
enum class PrinterKind { BAMBU, KLIPPER, OCTOPRINT }

/** A printer of the user. [secret] is the access code (Bambu Lab) or the API key, kept encrypted (see [PrinterStore]). */
data class SavedPrinter(
    val id: String = UUID.randomUUID().toString(),
    val kind: PrinterKind,
    val name: String,
    /** IP address or host (Bambu Lab), or the address of the web interface (Klipper, OctoPrint). */
    val host: String,
    /** Bambu Lab: the serial number, checked against the certificate of the printer once known. */
    val serial: String = "",
    val secret: String = "",
) {
    fun toJson(): JSONObject = JSONObject().put("id", id).put("kind", kind.name).put("name", name).put("host", host)
        .put("serial", serial)

    companion object {
        fun fromJson(o: JSONObject, secret: String): SavedPrinter? = runCatching {
            SavedPrinter(o.getString("id"), PrinterKind.valueOf(o.getString("kind")), o.getString("name"),
                o.getString("host"), o.optString("serial"), secret)
        }.getOrNull()
    }
}

/**
 * The printers of the user in the private storage of the app; the access codes and API keys encrypted with a key of
 * the Android Keystore (AES-GCM), which never leaves the phone.
 */
class PrinterStore(context: Context) {
    private val prefs = context.applicationContext.getSharedPreferences("printers", Context.MODE_PRIVATE)

    fun load(): List<SavedPrinter> {
        val list = runCatching { JSONArray(prefs.getString("list", "[]")) }.getOrDefault(JSONArray())
        return List(list.length()) { i -> list.getJSONObject(i) }.mapNotNull { o ->
            SavedPrinter.fromJson(o, decrypt(prefs.getString("secret_" + o.optString("id"), null)))
        }
    }

    fun save(printers: List<SavedPrinter>) {
        val edit = prefs.edit()
        // Secrets of removed printers go away with them.
        prefs.all.keys.filter { it.startsWith("secret_") }.forEach { edit.remove(it) }
        edit.putString("list", JSONArray(printers.map { it.toJson() }).toString())
        for (p in printers) if (p.secret.isNotEmpty()) edit.putString("secret_" + p.id, encrypt(p.secret))
        edit.apply()
    }

    private fun key(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .build())
        return gen.generateKey()
    }

    private fun encrypt(text: String): String {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key())
        val data = cipher.iv + cipher.doFinal(text.toByteArray(Charsets.UTF_8))
        return Base64.encodeToString(data, Base64.NO_WRAP)
    }

    private fun decrypt(stored: String?): String {
        if (stored.isNullOrEmpty()) return ""
        return runCatching {
            val data = Base64.decode(stored, Base64.NO_WRAP)
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, data, 0, 12))
            String(cipher.doFinal(data, 12, data.size - 12), Charsets.UTF_8)
        }.getOrDefault("")
    }

    private companion object {
        const val KEY_ALIAS = "tisma-printers"
    }
}
