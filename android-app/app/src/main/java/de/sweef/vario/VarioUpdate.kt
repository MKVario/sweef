package de.sweef.vario

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import no.nordicsemi.android.dfu.DfuBaseService
import no.nordicsemi.android.dfu.DfuProgressListenerAdapter
import no.nordicsemi.android.dfu.DfuServiceInitiator
import no.nordicsemi.android.dfu.DfuServiceListenerHelper
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

/** Dienst der Nordic-Bibliothek, der die Firmware uebertraegt. */
class DfuService : DfuBaseService() {
    override fun getNotificationTarget(): Class<out Activity> = MainActivity::class.java
    override fun isDebug(): Boolean = false
}

/**
 * Firmware-Update per Bluetooth:
 * 1. Firmware von GitHub laden
 * 2. dem Vario "dfu" schicken -> es startet den Bootloader
 * 3. den Bootloader suchen und die Firmware mit der Nordic-Bibliothek aufspielen
 */
object VarioUpdate {
    const val QUELLE = "https://github.com/MKVario/sweef/releases/latest/download/"
    private val handler = Handler(Looper.getMainLooper())
    @Volatile var laeuft = false; private set

    /** Neueste verfuegbare Firmware-Version (Textdatei im GitHub-Release) */
    fun neuesteVersion(fertig: (String?) -> Unit) {
        thread {
            val v = try { String(laden(QUELLE + "version.txt")).trim() } catch (e: Exception) { null }
            handler.post { fertig(v) }
        }
    }

    private fun laden(url: String): ByteArray {
        var u = URL(url)
        repeat(6) {
            val c = u.openConnection() as HttpURLConnection
            c.instanceFollowRedirects = false
            c.connectTimeout = 15000; c.readTimeout = 30000
            val code = c.responseCode
            if (code in 300..399) {
                val ziel = c.getHeaderField("Location") ?: throw Exception("Umleitung ohne Ziel")
                c.disconnect()
                u = URL(u, ziel)
                return@repeat
            }
            if (code != 200) throw Exception("Download-Fehler $code")
            return c.inputStream.use { it.readBytes() }
        }
        throw Exception("Zu viele Umleitungen")
    }

    /**
     * variante: "g" (grosses Display) oder "k" (kleines Display)
     * meldung(art, text, prozent): Fortschritt fuer die Oberflaeche
     */
    fun starten(ctx: Context, variante: String, meldung: (String, String, Int) -> Unit) {
        if (laeuft) return
        val adr = VarioBle.adresse
        if (adr == null || !VarioBle.bereit) { meldung("fehler", "Erst mit dem Vario verbinden", 0); return }
        laeuft = true
        val datei = File(ctx.cacheDir, "sweef_firmware.zip")
        meldung("info", "Lade Firmware …", 0)
        thread {
            try {
                val name = if (variante == "k") "sweef_firmware_klein.zip" else "sweef_firmware_gross.zip"
                datei.writeBytes(laden(QUELLE + name))
            } catch (e: Exception) {
                handler.post { laeuft = false; meldung("fehler", "Firmware nicht geladen: ${e.message}", 0) }
                return@thread
            }
            handler.post { bootloaderStarten(ctx, adr, datei, meldung) }
        }
    }

    private fun bootloaderStarten(ctx: Context, adr: String, datei: File, meldung: (String, String, Int) -> Unit) {
        meldung("info", "Vario wechselt in den Update-Modus …", 0)
        VarioBle.schreiben(VarioBle.BEFEHL.toString(), "dfu".toByteArray(), true) { ok, _, fehler ->
            if (!ok) {
                laeuft = false
                meldung("fehler", "Vario antwortet nicht ($fehler)", 0)
                return@schreiben
            }
            VarioBle.gesperrt = true
            handler.postDelayed({
                VarioBle.trennen()
                handler.postDelayed({ bootloaderSuchen(ctx, adr, datei, meldung) }, 1500)
            }, 1200)
        }
    }

    /** Der Bootloader meldet sich unter derselben oder der naechsthoeheren Adresse */
    private fun naechsteAdresse(adr: String): String {
        val teile = adr.split(":").map { it.toInt(16) }.toMutableList()
        var i = teile.size - 1
        while (i >= 0) { teile[i] = (teile[i] + 1) and 0xFF; if (teile[i] != 0) break; i-- }
        return teile.joinToString(":") { String.format("%02X", it) }
    }

    private fun bootloaderSuchen(ctx: Context, adr: String, datei: File, meldung: (String, String, Int) -> Unit) {
        meldung("info", "Suche Vario im Update-Modus …", 0)
        val plus1 = naechsteAdresse(adr)
        VarioBle.suchen(8000, { n -> true }) { liste ->
            var ziel: JSONObject? = null
            for (i in 0 until liste.length()) {
                val d = liste.getJSONObject(i)
                val id = d.getString("id").uppercase()
                val n = d.optString("name")
                if (id == adr.uppercase() || id == plus1 || n.contains("DFU", true)) { ziel = d; break }
            }
            if (ziel == null) {
                // nicht gesehen: trotzdem mit der alten Adresse versuchen
                ziel = JSONObject().put("id", adr).put("name", "Sweef")
            }
            aufspielen(ctx, ziel.getString("id"), ziel.optString("name"), adr, datei, meldung)
        }
    }

    private fun aufspielen(ctx: Context, ziel: String, name: String, alteAdr: String, datei: File,
                           meldung: (String, String, Int) -> Unit) {
        val hoerer = object : DfuProgressListenerAdapter() {
            override fun onDfuProcessStarting(a: String) { meldung("info", "Verbinde …", 0) }
            override fun onProgressChanged(a: String, prozent: Int, v: Float, va: Float, teil: Int, teile: Int) {
                meldung("fortschritt", "Übertrage … $prozent %", prozent)
            }
            override fun onDfuCompleted(a: String) {
                ende(ctx, this, alteAdr)
                meldung("fertig", "Update fertig! Das Vario startet neu.", 100)
            }
            override fun onDfuAborted(a: String) {
                ende(ctx, this, alteAdr)
                meldung("fehler", "Update abgebrochen", 0)
            }
            override fun onError(a: String, fehler: Int, typ: Int, text: String?) {
                ende(ctx, this, alteAdr)
                meldung("fehler", "Update-Fehler: ${text ?: fehler}. Einfach nochmal versuchen.", 0)
            }
        }
        DfuServiceListenerHelper.registerProgressListener(ctx, hoerer)
        DfuServiceInitiator.createDfuNotificationChannel(ctx)
        DfuServiceInitiator(ziel)
            .setDeviceName(name)
            .setKeepBond(false)
            .setForeground(true)
            .setPacketsReceiptNotificationsEnabled(true)
            .setPacketsReceiptNotificationsValue(8)
            .setZip(datei.absolutePath)
            .start(ctx, DfuService::class.java)
    }

    private fun ende(ctx: Context, hoerer: DfuProgressListenerAdapter, alteAdr: String) {
        DfuServiceListenerHelper.unregisterProgressListener(ctx, hoerer)
        laeuft = false
        VarioBle.gesperrt = false
        // nach dem Neustart des Varios automatisch wieder verbinden
        handler.postDelayed({
            VarioBle.verbinden(alteAdr, null) { _, _ -> }
        }, 6000)
    }
}
