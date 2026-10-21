package de.sweef.vario

import kotlin.math.abs
import kotlin.math.acos
import kotlin.math.asin
import kotlin.math.atan2
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Wind aus dem Handy-GPS schaetzen.
 *
 * Bei gleicher Fluggeschwindigkeit durch die Luft liegen alle Geschwindigkeiten
 * ueber Grund (als Pfeile gezeichnet) auf einem Kreis. Der Mittelpunkt des
 * Kreises ist der Wind, der Radius die Fluggeschwindigkeit. Gebraucht werden
 * dafuer Punkte aus moeglichst vielen Richtungen: Kurven, Wenden am Hangende.
 */
object Wind {
    private class P(val t: Long, val e: Double, val n: Double)     // km/h Ost / Nord
    private val punkte = ArrayDeque<P>()
    private const val FENSTER_MS = 240_000L     // letzte 4 Minuten

    @Volatile var kmh = -1; private set         // -1 = keine Schaetzung
    @Volatile var aus = 0; private set          // Grad, aus dieser Richtung kommt der Wind
    @Volatile var zeit = 0L; private set
    private var letzteRechnung = 0L

    fun gueltig(): Boolean = kmh >= 0 && System.currentTimeMillis() - zeit < 90_000L

    @Synchronized fun punkt(t: Long, tempoKmh: Double, kursGrad: Double, fliegt: Boolean) {
        if (!fliegt) { punkte.clear(); return }
        if (tempoKmh < 4) return                // Kurs bei fast Stillstand unzuverlaessig
        val r = Math.toRadians(kursGrad)
        punkte.addLast(P(t, tempoKmh * sin(r), tempoKmh * cos(r)))
        while (punkte.isNotEmpty() && t - punkte.first().t > FENSTER_MS) punkte.removeFirst()
        if (t - letzteRechnung >= 5000) { letzteRechnung = t; rechnen(t) }
    }

    private fun rechnen(t: Long) {
        val n = punkte.size
        if (n < 30) return
        var w = Pair(punkte.sumOf { it.e } / n, punkte.sumOf { it.n } / n)
        var a = 35.0
        repeat(40) {
            var se = 0.0; var sn = 0.0; var sa = 0.0
            for (p in punkte) {
                val de = p.e - w.first; val dn = p.n - w.second
                val l = hypot(de, dn).coerceAtLeast(0.1)
                sa += l
                se += p.e - a * de / l; sn += p.n - a * dn / l
            }
            w = Pair(se / n, sn / n)
            a = sa / n
        }
        val winkel = punkte.map { Math.toDegrees(atan2(it.e - w.first, it.n - w.second)).let { x -> (x + 360) % 360 } }.sorted()
        var luecke = 360 - winkel.last() + winkel.first()
        for (i in 1 until winkel.size) luecke = maxOf(luecke, winkel[i] - winkel[i - 1])
        if (luecke > 200) return                // mind. ein Halbkreis (z. B. Wenden am Hang)
        // Plausibel? Fluggeschwindigkeit 18-65 km/h, Punkte nah am Kreis
        if (a < 18 || a > 65) return
        val streu = sqrt(punkte.sumOf { val d = hypot(it.e - w.first, it.n - w.second) - a; d * d } / n)
        if (streu > 8) return
        val ws = hypot(w.first, w.second)
        kmh = ws.roundToInt()
        aus = ((Math.toDegrees(atan2(-w.first, -w.second)) + 360) % 360).roundToInt()
        zeit = System.currentTimeMillis()
    }

    fun text(): String = if (!gueltig()) "" else "{\"kmh\":$kmh,\"aus\":$aus}"

    // ---------------- Sonnenuntergang (Unix-Sekunden), -1 = keiner ----------------
    fun sonnenuntergang(lat: Double, lon: Double, ms: Long): Long {
        val r = Math.PI / 180
        fun s(x: Double) = sin(x * r)
        fun c(x: Double) = cos(x * r)
        val n = ceil(ms / 86_400_000.0 + 2440587.5 - 2451545.0 + 0.0008)
        val js = n - lon / 360
        val m = (357.5291 + 0.98560028 * js) % 360
        val cc = 1.9148 * s(m) + 0.02 * s(2 * m) + 0.0003 * s(3 * m)
        val l = (m + cc + 180 + 102.9372) % 360
        val jt = 2451545.0 + js + 0.0053 * s(m) - 0.0069 * s(2 * l)
        val d = asin(s(l) * s(23.4397)) / r
        val x = (s(-0.833) - s(lat) * s(d)) / (c(lat) * c(d))
        if (abs(x) > 1) return -1
        return (((jt + acos(x) / r / 360) - 2440587.5) * 86400).roundToInt().toLong()
    }

    fun naechsterSonnenuntergang(lat: Double, lon: Double): Long {
        val jetzt = System.currentTimeMillis()
        var su = sonnenuntergang(lat, lon, jetzt)
        if (su > 0 && su * 1000 < jetzt - 3_600_000) su = sonnenuntergang(lat, lon, jetzt + 86_400_000)
        return su
    }
}
