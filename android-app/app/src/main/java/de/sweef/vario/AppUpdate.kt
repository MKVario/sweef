package de.sweef.vario

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import androidx.core.content.FileProvider
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

/**
 * Sucht auf GitHub nach einer neueren App-Version, laedt sie herunter
 * und oeffnet den Android-Installer (dort einmal "Installieren" tippen).
 */
object AppUpdate {
    private const val API = "https://api.github.com/repos/MKVario/sweef/releases/latest"
    private val handler = Handler(Looper.getMainLooper())

    /** Neueste Version auf GitHub: {app, appUrl, firmware} oder null */
    fun pruefen(fertig: (JSONObject?) -> Unit) {
        thread {
            val ergebnis = try {
                val c = URL(API).openConnection() as HttpURLConnection
                c.setRequestProperty("Accept", "application/vnd.github+json")
                c.connectTimeout = 15000; c.readTimeout = 15000
                val j = JSONObject(c.inputStream.use { String(it.readBytes()) })
                val tag = j.optString("tag_name")                     // z.B. v2.10.19-app5
                val fw = tag.removePrefix("v").substringBefore("-app")
                var nr = -1; var url = ""
                val assets = j.optJSONArray("assets")
                if (assets != null) for (i in 0 until assets.length()) {
                    val a = assets.getJSONObject(i)
                    val m = Regex("sweef_app_v1_0_(\\d+)\\.apk").matchEntire(a.optString("name")) ?: continue
                    nr = m.groupValues[1].toInt(); url = a.optString("browser_download_url")
                }
                JSONObject().put("app", nr).put("appUrl", url).put("firmware", fw)
            } catch (e: Exception) { null }
            handler.post { fertig(ergebnis) }
        }
    }

    fun eigeneNummer(ctx: Context): Long = try {
        val pi = ctx.packageManager.getPackageInfo(ctx.packageName, 0)
        if (Build.VERSION.SDK_INT >= 28) pi.longVersionCode else @Suppress("DEPRECATION") pi.versionCode.toLong()
    } catch (e: Exception) { 0 }

    /** Laedt die APK und startet den Installer. meldung(text, fertig) */
    fun installieren(ctx: Context, url: String, meldung: (String, Boolean) -> Unit) {
        if (Build.VERSION.SDK_INT >= 26 && !ctx.packageManager.canRequestPackageInstalls()) {
            meldung("Bitte erlauben, dass Sweef Apps installieren darf, dann nochmal tippen.", true)
            try {
                ctx.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + ctx.packageName))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            } catch (_: Exception) {}
            return
        }
        meldung("Lade neue App …", false)
        thread {
            try {
                val ordner = File(ctx.cacheDir, "updates").apply { mkdirs() }
                val datei = File(ordner, "sweef_app.apk")
                var u = URL(url)
                var bytes: ByteArray? = null
                for (i in 0 until 6) {
                    val c = u.openConnection() as HttpURLConnection
                    c.instanceFollowRedirects = false
                    c.connectTimeout = 15000; c.readTimeout = 60000
                    val code = c.responseCode
                    if (code in 300..399) { u = URL(u, c.getHeaderField("Location")); c.disconnect(); continue }
                    if (code != 200) throw Exception("Fehler $code")
                    bytes = c.inputStream.use { it.readBytes() }
                    break
                }
                datei.writeBytes(bytes ?: throw Exception("Zu viele Umleitungen"))
                handler.post {
                    val uri = FileProvider.getUriForFile(ctx, "de.sweef.vario.dateien", datei)
                    val i = Intent(Intent.ACTION_VIEW)
                        .setDataAndType(uri, "application/vnd.android.package-archive")
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
                    try { ctx.startActivity(i); meldung("Jetzt „Installieren“ tippen", true) }
                    catch (e: Exception) { meldung("Installer nicht gestartet: ${e.message}", true) }
                }
            } catch (e: Exception) {
                handler.post { meldung("Download fehlgeschlagen: ${e.message}", true) }
            }
        }
    }
}
