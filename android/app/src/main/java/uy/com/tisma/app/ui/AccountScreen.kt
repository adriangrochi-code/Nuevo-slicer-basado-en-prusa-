package uy.com.tisma.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import uy.com.tisma.app.R
import uy.com.tisma.app.cloud.CloudAccount
import uy.com.tisma.app.cloud.CloudErrorKind
import uy.com.tisma.app.cloud.CloudException
import uy.com.tisma.app.cloud.CloudStore
import java.util.Locale

private enum class Mode { LOGIN, REGISTER, CODE }

/** Tisma Cloud account: log in or create an account (confirmed with a code by e-mail), then the plan and its hours. */
@Composable
fun AccountScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val store = remember { CloudStore(context) }
    val scope = rememberCoroutineScope()
    var token by remember { mutableStateOf(store.token) }
    var account by remember { mutableStateOf<CloudAccount?>(null) }
    var mode by remember { mutableStateOf(Mode.LOGIN) }
    var email by remember { mutableStateOf(store.email ?: "") }
    var password by remember { mutableStateOf("") }
    var code by remember { mutableStateOf("") }
    var server by remember { mutableStateOf(store.serverUrl) }
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf<String?>(null) }

    fun errorText(e: Exception): String = when ((e as? CloudException)?.kind) {
        CloudErrorKind.WRONG_LOGIN -> context.getString(R.string.cloud_wrong_login)
        CloudErrorKind.NOT_VERIFIED -> context.getString(R.string.cloud_not_verified)
        CloudErrorKind.ALREADY_EXISTS -> context.getString(R.string.cloud_already_exists)
        CloudErrorKind.WRONG_CODE -> context.getString(R.string.cloud_wrong_code)
        CloudErrorKind.TOO_MANY -> context.getString(R.string.cloud_too_many)
        CloudErrorKind.NOT_LOGGED_IN -> context.getString(R.string.cloud_session_ended)
        CloudErrorKind.INVALID -> context.getString(R.string.cloud_invalid)
        CloudErrorKind.NETWORK -> context.getString(R.string.cloud_network)
        else -> context.getString(R.string.cloud_server_error, e.message ?: "")
    }

    // Runs a call of the master off the main thread, with the progress bar and the error message.
    fun call(block: suspend () -> Unit) {
        busy = true
        message = null
        scope.launch {
            try {
                block()
            } catch (e: Exception) {
                if ((e as? CloudException)?.kind == CloudErrorKind.NOT_VERIFIED) mode = Mode.CODE
                if ((e as? CloudException)?.kind == CloudErrorKind.NOT_LOGGED_IN) {
                    store.token = null
                    token = null
                }
                message = errorText(e)
            } finally {
                busy = false
            }
        }
    }

    fun loggedIn(newToken: String) {
        store.token = newToken
        store.email = email.trim().lowercase(Locale.ROOT)
        token = newToken
        password = ""
        code = ""
    }

    LaunchedEffect(token) {
        if (token != null) call { account = withContext(Dispatchers.IO) { store.client().account() } }
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp).verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(stringResource(R.string.cloud_title), style = MaterialTheme.typography.titleLarge)
        Text(stringResource(R.string.cloud_intro), style = MaterialTheme.typography.bodyMedium)

        val current = account
        if (token != null) {
            if (current != null) {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(current.email, style = MaterialTheme.typography.titleMedium)
                        Text(stringResource(R.string.cloud_plan, current.plan))
                        Text(
                            if (current.paid) stringResource(R.string.cloud_paid, current.paidUntil?.take(10) ?: "")
                            else stringResource(R.string.cloud_not_paid),
                            color = if (current.paid) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                        )
                        Text(stringResource(R.string.cloud_hours,
                            String.format(Locale.ROOT, "%.1f", current.hoursUsed),
                            String.format(Locale.ROOT, "%.1f", current.hoursIncluded)))
                    }
                }
            }
            OutlinedButton(
                onClick = { call { account = withContext(Dispatchers.IO) { store.client().account() } } },
                enabled = !busy, modifier = Modifier.fillMaxWidth(),
            ) { Text(stringResource(R.string.cloud_refresh)) }
            OutlinedButton(
                onClick = {
                    call {
                        // The session ends here even if the master cannot be reached.
                        try {
                            withContext(Dispatchers.IO) { store.client().logout() }
                        } finally {
                            store.token = null
                            token = null
                            account = null
                        }
                    }
                },
                enabled = !busy, modifier = Modifier.fillMaxWidth(),
            ) { Text(stringResource(R.string.cloud_logout)) }
        } else {
            OutlinedTextField(email, { email = it }, label = { Text(stringResource(R.string.cloud_email)) },
                singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email),
                modifier = Modifier.fillMaxWidth())
            if (mode == Mode.CODE) {
                Text(stringResource(R.string.cloud_code_sent, email))
                OutlinedTextField(code, { code = it.filter(Char::isDigit).take(6) },
                    label = { Text(stringResource(R.string.cloud_code)) }, singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth())
                Button(
                    onClick = { call { loggedIn(withContext(Dispatchers.IO) { store.client().verify(email.trim(), code) }) } },
                    enabled = !busy && code.length == 6, modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.cloud_confirm)) }
                TextButton(onClick = { call { withContext(Dispatchers.IO) { store.client().resendCode(email.trim()) } } },
                    enabled = !busy) { Text(stringResource(R.string.cloud_resend)) }
            } else {
                OutlinedTextField(password, { password = it }, label = { Text(stringResource(R.string.cloud_password)) },
                    singleLine = true, visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    modifier = Modifier.fillMaxWidth())
                if (mode == Mode.REGISTER) Text(stringResource(R.string.cloud_password_rule),
                    style = MaterialTheme.typography.bodySmall)
                Button(
                    onClick = {
                        call {
                            if (mode == Mode.LOGIN) {
                                loggedIn(withContext(Dispatchers.IO) { store.client().login(email.trim(), password) })
                            } else {
                                withContext(Dispatchers.IO) { store.client().register(email.trim(), password) }
                                mode = Mode.CODE
                            }
                        }
                    },
                    enabled = !busy && email.isNotBlank() && password.length >= (if (mode == Mode.REGISTER) 8 else 1),
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(if (mode == Mode.LOGIN) R.string.cloud_login else R.string.cloud_register)) }
            }
            TextButton(onClick = { mode = if (mode == Mode.LOGIN) Mode.REGISTER else Mode.LOGIN; message = null }) {
                Text(stringResource(if (mode == Mode.LOGIN) R.string.cloud_to_register else R.string.cloud_to_login))
            }
            // The address of the master: tismaservidores.net, or a server of the local network for tests.
            OutlinedTextField(server, { server = it; store.serverUrl = it },
                label = { Text(stringResource(R.string.cloud_server)) }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri), modifier = Modifier.fillMaxWidth())
        }

        if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
        message?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    }
}
