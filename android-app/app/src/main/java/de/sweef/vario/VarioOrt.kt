package de.sweef.vario

import android.content.Context
import org.json.JSONObject

/**
 * "Wo ist mein Vario?": merkt sich, wo das Handy war, als die
 * Verbindung zum Vario zuletzt abgerissen ist.
 */
object VarioOrt {
    @Volatile private var lat = 0.0
    @Volatile private var lon = 0.0
    @Volatile private var genau = 0f
    @Volatile private var zeit = 0L

    /** jede gute GPS-Position, solange das Vario verbunden ist */
    fun fix(la: Double, lo: Double, g: Float, t: Long) { lat = la; lon = lo; genau = g; zeit = t }

    /** beim Abreissen der Verbindung aufrufen */
    fun merken(ctx: Context, name: String?) {
        if (zeit == 0L || System.currentTimeMillis() - zeit > 3 * 60 * 1000L) return   // kein frisches GPS
        val j = JSONObject().put("lat", lat).put("lon", lon).put("genau", genau.toDouble())
            .put("zeit", System.currentTimeMillis()).put("name", name ?: "Sweef")
        ctx.getSharedPreferences("sweef", Context.MODE_PRIVATE).edit().putString("zuletzt", j.toString()).apply()
    }

    fun lesen(ctx: Context): String =
        ctx.getSharedPreferences("sweef", Context.MODE_PRIVATE).getString("zuletzt", "") ?: ""
}
