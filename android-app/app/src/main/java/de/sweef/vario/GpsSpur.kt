package de.sweef.vario

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Schreibt die GPS-Positionen mit, solange das Vario verbunden ist
 * (eine Datei pro Tag, 14 Tage aufbewahrt). Die Seite holt sich daraus
 * beim Laden der Fluege den passenden Abschnitt (nach Uhrzeit).
 * Zeile: zeit_ms,breite,laenge,gps_hoehe_m
 */
object GpsSpur {
    private const val TAGE = 14
    private val puffer = StringBuilder()
    private var anzahl = 0
    private var ordner: File? = null
    private val tagFormat = SimpleDateFormat("yyyyMMdd", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }

    @Synchronized fun init(ctx: Context) {
        if (ordner != null) return
        ordner = File(ctx.filesDir, "spur").apply { mkdirs() }
        aufraeumen()
    }

    @Synchronized fun punkt(zeit: Long, lat: Double, lon: Double, hoehe: Double) {
        if (ordner == null) return
        puffer.append(zeit).append(',')
            .append(String.format(Locale.US, "%.6f,%.6f,%.0f", lat, lon, hoehe)).append('\n')
        if (++anzahl >= 15) schreiben()
    }

    @Synchronized fun schreiben() {
        val o = ordner ?: return
        if (puffer.isEmpty()) return
        try {
            // nach Tagen aufteilen (um Mitternacht UTC)
            val zeilen = puffer.toString().trimEnd().split('\n')
            zeilen.groupBy { tagFormat.format(Date(it.substringBefore(',').toLongOrNull() ?: 0L)) }
                .forEach { (tag, z) -> File(o, "$tag.csv").appendText(z.joinToString("\n", postfix = "\n")) }
        } catch (_: Exception) {}
        puffer.setLength(0); anzahl = 0
    }

    /** alle Punkte zwischen von und bis (ms), als "zeit,lat,lon,hoehe;..." */
    @Synchronized fun abschnitt(von: Long, bis: Long): String {
        schreiben()
        val o = ordner ?: return ""
        val sb = StringBuilder()
        var tag = von - 86_400_000L
        val tage = mutableSetOf<String>()
        while (tag <= bis + 86_400_000L) { tage += tagFormat.format(Date(tag)); tag += 86_400_000L }
        for (t in tage.sorted()) {
            val f = File(o, "$t.csv")
            if (!f.exists()) continue
            try {
                f.forEachLine { z ->
                    val ms = z.substringBefore(',').toLongOrNull() ?: return@forEachLine
                    if (ms in von..bis) sb.append(z).append(';')
                }
            } catch (_: Exception) {}
        }
        return sb.toString().trimEnd(';')
    }

    private fun aufraeumen() {
        val o = ordner ?: return
        val grenze = tagFormat.format(Date(System.currentTimeMillis() - TAGE * 86_400_000L))
        o.listFiles()?.forEach { if (it.name.removeSuffix(".csv") < grenze) it.delete() }
    }
}
