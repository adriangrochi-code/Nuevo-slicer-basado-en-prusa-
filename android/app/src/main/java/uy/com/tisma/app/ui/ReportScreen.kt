package uy.com.tisma.app.ui

import android.os.Build
import android.os.Process
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.BuildConfig
import uy.com.tisma.app.R
import uy.com.tisma.app.cloud.CloudErrorKind
import uy.com.tisma.app.cloud.CloudException
import uy.com.tisma.app.cloud.CloudReport
import uy.com.tisma.app.cloud.CloudStore
import uy.com.tisma.app.core.TismaCore

/** The kinds of report of the master (server reports.KINDS). */
private enum class ReportKind(val key: String) { BUG("bug"), CRASH("crash"), SUGGESTION("suggestion") }

/**
 * "Report a problem": what went wrong or a suggestion, sent to the Tisma team through Tisma Cloud (server/README.md,
 * "Problemas informados"), with the account when logged in. The master removes the personal data and groups the reports
 * of the same problem; the reports of the account show their state here.
 */
@Composable
fun ReportScreen(workspace: Workspace, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val store = remember { CloudStore(context) }
    val scope = rememberCoroutineScope()
    var kind by remember { mutableStateOf(ReportKind.BUG) }
    var title by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    var steps by remember { mutableStateOf("") }
    var expected by remember { mutableStateOf("") }
    var attach by remember { mutableStateOf(true) }
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf<String?>(null) }
    var mine by remember { mutableStateOf<List<CloudReport>>(emptyList()) }
    val loggedIn = store.token != null

    suspend fun loadMine() {
        if (store.token == null) return
        mine = runCatching { withContext(Dispatchers.IO) { store.client().myReports() } }.getOrDefault(mine)
    }
    LaunchedEffect(Unit) { loadMine() }

    fun send() {
        if (title.trim().length < 3) {
            message = context.getString(R.string.report_need_title)
            return
        }
        busy = true
        message = null
        scope.launch {
            try {
                val fields = mutableMapOf(
                    "app" to "android", "kind" to kind.key, "title" to title.trim(), "description" to description,
                    "steps" to steps, "expected" to expected, "app_version" to BuildConfig.VERSION_NAME,
                    "os" to "Android ${Build.VERSION.RELEASE}".take(80),
                )
                if (attach) fields["log"] = withContext(Dispatchers.IO) { attachment(workspace) }
                val similar = withContext(Dispatchers.IO) { store.client().report(fields) }
                message = context.getString(R.string.report_sent) +
                    if (similar > 0) "\n" + context.resources.getQuantityString(R.plurals.report_similar, similar, similar) else ""
                title = ""; description = ""; steps = ""; expected = ""
                loadMine()
            } catch (e: Exception) {
                message = when ((e as? CloudException)?.kind) {
                    CloudErrorKind.TOO_MANY -> context.getString(R.string.report_too_many)
                    CloudErrorKind.NETWORK -> context.getString(R.string.cloud_network)
                    CloudErrorKind.NOT_LOGGED_IN -> context.getString(R.string.cloud_session_ended)
                    else -> context.getString(R.string.cloud_server_error, e.message ?: "")
                }
            } finally {
                busy = false
            }
        }
    }

    SettingsPage(modifier) {
        Text(stringResource(R.string.report_intro), style = MaterialTheme.typography.bodyMedium)
        Choices(stringResource(R.string.report_kind), ReportKind.entries.toList(), kind, { k ->
            stringResource(when (k) {
                ReportKind.BUG -> R.string.report_kind_bug
                ReportKind.CRASH -> R.string.report_kind_crash
                ReportKind.SUGGESTION -> R.string.report_kind_suggestion
            })
        }) { kind = it }
        OutlinedTextField(title, { title = it.take(200) }, label = { Text(stringResource(R.string.report_title)) },
            placeholder = { Text(stringResource(R.string.report_title_hint)) }, singleLine = true,
            modifier = Modifier.fillMaxWidth())
        OutlinedTextField(description, { description = it.take(10_000) }, label = { Text(stringResource(R.string.report_description)) },
            minLines = 4, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(steps, { steps = it.take(10_000) }, label = { Text(stringResource(R.string.report_steps)) },
            minLines = 3, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(expected, { expected = it.take(10_000) }, label = { Text(stringResource(R.string.report_expected)) },
            modifier = Modifier.fillMaxWidth())
        SwitchRow(stringResource(R.string.report_attach), attach) { attach = it }
        Text(stringResource(if (loggedIn) R.string.report_with_account else R.string.report_without_account),
            style = MaterialTheme.typography.bodySmall)
        if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
        Button(onClick = { send() }, enabled = !busy, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.report_send))
        }
        message?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }
        if (mine.isNotEmpty()) {
            Group(stringResource(R.string.report_mine)) {
                for (r in mine) {
                    Card(Modifier.fillMaxWidth()) {
                        Text(r.title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(12.dp, 8.dp, 12.dp, 0.dp))
                        Text(statusText(r) + " · " + r.createdAt.take(10), style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(12.dp, 2.dp, 12.dp, 8.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun statusText(r: CloudReport): String {
    val status = stringResource(when (r.status) {
        "confirmed" -> R.string.report_status_confirmed
        "fixed" -> R.string.report_status_fixed
        "wontfix" -> R.string.report_status_wontfix
        "duplicate" -> R.string.report_status_duplicate
        else -> R.string.report_status_new
    })
    return if (r.rewardDays > 0) status + " · " + pluralStringResource(R.plurals.report_rewarded, r.rewardDays, r.rewardDays) else status
}

/** What is attached when the user agrees: the versions, the phone, the printer chosen and the log of the app. */
private fun attachment(workspace: Workspace): String {
    val out = StringBuilder()
    out.append("Tisma Android ").append(BuildConfig.VERSION_NAME)
    if (TismaCore.available) out.append(" (core ").append(TismaCore.version()).append(')')
    out.append('\n').append("Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
        .append("), ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append('\n')
    workspace.printerPreset()?.let { out.append("Printer: ").append(it.name).append('\n') }
    out.append("--- log of the app ---\n")
    // The log of this app only (an app may read its own lines of logcat).
    val log = runCatching {
        val process = ProcessBuilder("logcat", "-d", "-t", "400", "--pid=${Process.myPid()}").redirectErrorStream(true).start()
        process.inputStream.bufferedReader().use { it.readText() }.also { process.waitFor() }
    }.getOrDefault("")
    out.append(log.ifBlank { "(empty)\n" })
    return out.toString().takeLast(190_000)
}
