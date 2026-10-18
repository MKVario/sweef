package de.sweef.vario

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.ContentValues
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.PowerManager
import android.provider.MediaStore
import android.provider.Settings
import android.util.Base64
import android.webkit.GeolocationPermissions
import android.webkit.JavascriptInterface
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.core.content.ContextCompat
import androidx.webkit.WebViewAssetLoader
import org.json.JSONObject
import java.io.File

class MainActivity : Activity() {

    private lateinit var web: WebView
    private var dateiRueckruf: ValueCallback<Array<Uri>>? = null
    private var ortRueckruf: Pair<String, GeolocationPermissions.Callback>? = null

    companion object {
        private const val START = "https://appassets.androidplatform.net/assets/www/index.html"
        private const val ANFRAGE_RECHTE = 1
        private const val ANFRAGE_DATEI = 2
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        VarioBle.init(this)

        val lader = WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()

        web = WebView(this)
        setContentView(web)
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            @Suppress("DEPRECATION")
            databaseEnabled = true
            setGeolocationEnabled(true)
            allowFileAccess = false
            allowContentAccess = false
            mediaPlaybackRequiresUserGesture = false
        }
        web.addJavascriptInterface(Bruecke(), "SweefNative")
        web.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(v: WebView, r: WebResourceRequest): WebResourceResponse? =
                lader.shouldInterceptRequest(r.url)

            override fun shouldOverrideUrlLoading(v: WebView, r: WebResourceRequest): Boolean {
                if (r.url.host == "appassets.androidplatform.net") return false
                try { startActivity(Intent(Intent.ACTION_VIEW, r.url)) } catch (_: Exception) {}
                return true
            }
        }
        web.webChromeClient = object : WebChromeClient() {
            override fun onGeolocationPermissionsShowPrompt(origin: String, cb: GeolocationPermissions.Callback) {
                if (darf(Manifest.permission.ACCESS_FINE_LOCATION)) cb.invoke(origin, true, false)
                else { ortRueckruf = origin to cb; rechteAnfragen() }
            }

            override fun onShowFileChooser(v: WebView, cb: ValueCallback<Array<Uri>>, p: FileChooserParams): Boolean {
                dateiRueckruf?.onReceiveValue(null)
                dateiRueckruf = cb
                val i = Intent(Intent.ACTION_GET_CONTENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
                return try { startActivityForResult(i, ANFRAGE_DATEI); true } catch (e: Exception) {
                    dateiRueckruf = null; false
                }
            }
        }

        VarioBle.ereignis = { art, daten -> zurJs("_ereignis", JSONObject.quote(art), daten.toString()) }

        rechteAnfragen()
        web.loadUrl(START)
    }

    override fun onDestroy() {
        VarioBle.ereignis = null
        web.destroy()
        super.onDestroy()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        moveTaskToBack(true)        // App im Hintergrund weiterlaufen lassen
    }

    // ------------------------------------------------------------------
    // Rechte
    // ------------------------------------------------------------------
    private fun darf(p: String) = ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED

    private fun rechteAnfragen() {
        val noetig = mutableListOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION)
        if (Build.VERSION.SDK_INT >= 31) {
            noetig += Manifest.permission.BLUETOOTH_SCAN
            noetig += Manifest.permission.BLUETOOTH_CONNECT
        }
        if (Build.VERSION.SDK_INT >= 33) noetig += Manifest.permission.POST_NOTIFICATIONS
        val fehlt = noetig.filter { !darf(it) }
        if (fehlt.isNotEmpty()) requestPermissions(fehlt.toTypedArray(), ANFRAGE_RECHTE)
    }

    override fun onRequestPermissionsResult(code: Int, rechte: Array<out String>, ergebnis: IntArray) {
        super.onRequestPermissionsResult(code, rechte, ergebnis)
        ortRueckruf?.let { (o, cb) -> cb.invoke(o, darf(Manifest.permission.ACCESS_FINE_LOCATION), false) }
        ortRueckruf = null
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(code: Int, res: Int, daten: Intent?) {
        super.onActivityResult(code, res, daten)
        if (code == ANFRAGE_DATEI) {
            val uri = if (res == RESULT_OK) daten?.data else null
            dateiRueckruf?.onReceiveValue(if (uri != null) arrayOf(uri) else null)
            dateiRueckruf = null
        }
    }

    // ------------------------------------------------------------------
    // Antworten an die Seite
    // ------------------------------------------------------------------
    private fun zurJs(fn: String, vararg args: String) {
        val js = "window.SweefBridge && SweefBridge.$fn(${args.joinToString(",")})"
        runOnUiThread { try { web.evaluateJavascript(js, null) } catch (_: Exception) {} }
    }

    private fun fertig(id: Int, ok: Boolean, wert: String?) =
        zurJs("_fertig", id.toString(), ok.toString(), if (wert == null) "null" else JSONObject.quote(wert))

    private fun b64(v: ByteArray?) = Base64.encodeToString(v ?: ByteArray(0), Base64.NO_WRAP)

    // ------------------------------------------------------------------
    // Bruecke: wird von sweef-bridge.js aufgerufen
    // ------------------------------------------------------------------
    inner class Bruecke {
        @JavascriptInterface fun suchen(id: Int) = runOnUiThread {
            if (Build.VERSION.SDK_INT >= 31 && !darf(Manifest.permission.BLUETOOTH_SCAN)) {
                rechteAnfragen(); fertig(id, false, "Bitte Bluetooth-Zugriff erlauben und nochmal tippen"); return@runOnUiThread
            }
            VarioBle.suchen(4000, { it.startsWith("Sweef") }) { liste -> fertig(id, true, liste.toString()) }
        }

        @JavascriptInterface fun verbinden(id: Int, adresse: String, name: String) = runOnUiThread {
            VarioBle.verbinden(adresse, name) { ok, fehler -> fertig(id, ok, fehler) }
        }

        @JavascriptInterface fun trennen() = runOnUiThread { VarioBle.trennen() }

        @JavascriptInterface fun istVerbunden(): Boolean = VarioBle.bereit

        @JavascriptInterface fun aktuellesGeraet(): String =
            if (VarioBle.bereit) JSONObject().put("id", VarioBle.adresse).put("name", VarioBle.name ?: "Sweef").toString() else ""

        @JavascriptInterface fun hatKanal(id: Int, uuid: String) = runOnUiThread {
            fertig(id, VarioBle.hatKanal(uuid), "Kanal nicht vorhanden")
        }

        @JavascriptInterface fun schreiben(id: Int, uuid: String, daten: String) {
            VarioBle.schreiben(uuid, Base64.decode(daten, Base64.NO_WRAP), true) { ok, _, f -> fertig(id, ok, f) }
        }

        @JavascriptInterface fun lesen(id: Int, uuid: String) {
            VarioBle.lesen(uuid) { ok, v, f -> fertig(id, ok, if (ok) b64(v) else f) }
        }

        @JavascriptInterface fun melden(id: Int, uuid: String) {
            VarioBle.meldungenAn(uuid) { ok, _, f -> fertig(id, ok, f) }
        }

        @JavascriptInterface fun tempoAn(): Boolean = VarioService.tempoAn(this@MainActivity)
        @JavascriptInterface fun tempoSetzen(an: Boolean) = VarioService.tempoSetzen(this@MainActivity, an)
        @JavascriptInterface fun tempo(): Float = VarioService.letztesTempo
        @JavascriptInterface fun gpsErlaubt(): Boolean = darf(Manifest.permission.ACCESS_FINE_LOCATION)

        @JavascriptInterface fun appVersion(): String =
            try { packageManager.getPackageInfo(packageName, 0).versionName ?: "" } catch (e: Exception) { "" }

        @JavascriptInterface fun akkuFrei(): Boolean =
            (getSystemService(POWER_SERVICE) as PowerManager).isIgnoringBatteryOptimizations(packageName)

        @SuppressLint("BatteryLife")
        @JavascriptInterface fun akkuOptimierungAus() = runOnUiThread {
            try {
                startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
            } catch (e: Exception) {
                try { startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)) } catch (_: Exception) {}
            }
        }

        @JavascriptInterface fun neuesteFirmware(id: Int) = runOnUiThread {
            VarioUpdate.neuesteVersion { v -> fertig(id, v != null, v ?: "Keine Internetverbindung?") }
        }

        @JavascriptInterface fun updateStarten(variante: String) = runOnUiThread {
            VarioUpdate.starten(this@MainActivity, variante) { art, text, prozent ->
                zurJs("_ereignis", JSONObject.quote("update"),
                    JSONObject().put("art", art).put("text", text).put("p", prozent).toString())
            }
        }

        /** Neueste Versionen auf GitHub: {"app":5,"appUrl":"…","firmware":"2.10.19","eigene":4} */
        @JavascriptInterface fun versionenPruefen(id: Int) = runOnUiThread {
            AppUpdate.pruefen { j ->
                if (j == null) fertig(id, false, "Keine Internetverbindung?")
                else fertig(id, true, j.put("eigene", AppUpdate.eigeneNummer(this@MainActivity)).toString())
            }
        }

        @JavascriptInterface fun appInstallieren(url: String) = runOnUiThread {
            AppUpdate.installieren(this@MainActivity, url) { text, ende ->
                zurJs("_ereignis", JSONObject.quote("appupdate"),
                    JSONObject().put("text", text).put("ende", ende).toString())
            }
        }

        /** Datei in "Download/Sweef" speichern (Sicherung, CSV) */
        @JavascriptInterface fun speichern(name: String, inhalt: String, typ: String): Boolean {
            return try {
                val bytes = inhalt.toByteArray(Charsets.UTF_8)
                val mime = typ.substringBefore(";").ifBlank { "application/octet-stream" }
                if (Build.VERSION.SDK_INT >= 29) {
                    val werte = ContentValues().apply {
                        put(MediaStore.Downloads.DISPLAY_NAME, name)
                        put(MediaStore.Downloads.MIME_TYPE, mime)
                        put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/Sweef")
                    }
                    val uri = contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, werte) ?: return false
                    contentResolver.openOutputStream(uri)?.use { it.write(bytes) }
                } else {
                    val ordner = File(getExternalFilesDir(null), "Sweef").apply { mkdirs() }
                    File(ordner, name).writeBytes(bytes)
                }
                true
            } catch (e: Exception) { false }
        }
    }
}
