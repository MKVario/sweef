package de.sweef.vario

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Xml
import org.json.JSONArray
import org.json.JSONObject
import org.xmlpull.v1.XmlPullParser
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Startplaetze: eigene (von der Seite) und aus ParaglidingEarth (offline gespeichert).
 * Wenn das Vario verbunden ist und am Boden steht, fragt es am naechsten Startplatz,
 * ob dessen Hoehe uebernommen werden soll.
 */
object Startplaetze {
    data class Platz(val id: String, val name: String, val lat: Double, val lon: Double, val hoehe: Double, val eigen: Boolean, val richtungen: String = "")

    private lateinit var ctx: Context
    private val haupt = Handler(Looper.getMainLooper())
    @Volatile private var pge: List<Platz> = emptyList()
    @Volatile private var eigene: List<Platz> = emptyList()

    // Zustand des Varios aus den Live-Daten
    @Volatile var fliegt = true; private set
    @Volatile var nnJetzt = Double.NaN; private set
    @Volatile private var liveZeit = 0L
    @Volatile var frei = false            // von der Seite: entsperrt (PIN ok)

    private val gefragt = mutableSetOf<String>()
    private var verbindungNr = -1
    private var letztePruefung = 0L

    private fun datei() = File(ctx.filesDir, "startplaetze_pge.json")
    private fun prefs() = ctx.getSharedPreferences("sweef", Context.MODE_PRIVATE)

    fun init(c: Context) {
        if (::ctx.isInitialized) return
        ctx = c.applicationContext
        Thread { pge = lesen() }.start()
    }

    fun fragenAn(): Boolean = prefs().getBoolean("sp_frage", true)
    fun fragenSetzen(an: Boolean) = prefs().edit().putBoolean("sp_frage", an).apply()
    fun anzahl(): Int = pge.size
    fun stand(): Long = prefs().getLong("sp_stand", 0L)

    // ---------------- Live-Daten vom Vario ("v=..;h=..;nn=..;fl=0;...") ----------------
    fun live(text: String) {
        var fl: String? = null; var nn: String? = null
        for (teil in text.split(';')) {
            val i = teil.indexOf('=')
            if (i <= 0) continue
            when (teil.substring(0, i)) { "fl" -> fl = teil.substring(i + 1); "nn" -> nn = teil.substring(i + 1) }
        }
        if (fl != null) fliegt = fl == "1"
        nn?.toDoubleOrNull()?.let { nnJetzt = it }
        liveZeit = System.currentTimeMillis()
    }

    // ---------------- eigene Startplaetze (kommen von der Seite) ----------------
    fun eigeneSetzen(json: String) {
        try {
            val a = JSONArray(json)
            eigene = (0 until a.length()).mapNotNull { i ->
                val o = a.getJSONObject(i)
                if (o.isNull("lat") || o.isNull("lon")) null
                else Platz("e:" + o.optString("id"), o.optString("name"), o.getDouble("lat"), o.getDouble("lon"), o.getDouble("hoehe"), true)
            }
        } catch (_: Exception) {}
    }

    // ---------------- naechster Startplatz ----------------
    fun abstandM(lat1: Double, lon1: Double, lat2: Double, lon2: Double): Double {
        val r = Math.PI / 180
        val h = sin((lat2 - lat1) * r / 2).let { it * it } +
                cos(lat1 * r) * cos(lat2 * r) * sin((lon2 - lon1) * r / 2).let { it * it }
        return 12_742_000.0 * asin(sqrt(h))
    }

    /** eigene Startplaetze haben Vorrang, dann ParaglidingEarth */
    fun naechster(lat: Double, lon: Double, maxM: Double): Pair<Platz, Double>? {
        for (liste in listOf(eigene, pge)) {
            val best = liste.map { it to abstandM(lat, lon, it.lat, it.lon) }.minByOrNull { it.second }
            if (best != null && best.second <= maxM) return best
        }
        return null
    }

    fun naechsterJson(lat: Double, lon: Double, maxM: Double): String {
        val (p, m) = naechster(lat, lon, maxM) ?: return ""
        return JSONObject().put("id", p.id).put("name", p.name).put("lat", p.lat).put("lon", p.lon)
            .put("hoehe", p.hoehe).put("eigen", p.eigen).put("m", m).put("richtungen", p.richtungen).toString()
    }

    /** die naechsten Startplaetze aus der Liste, sortiert nach Entfernung (lat/lon NaN = nach Name) */
    fun listeJson(lat: Double, lon: Double, max: Int): String {
        val a = JSONArray()
        val sortiert = if (lat.isNaN()) pge.sortedBy { it.name.lowercase() }.map { it to -1.0 }
                       else pge.map { it to abstandM(lat, lon, it.lat, it.lon) }.sortedBy { it.second }
        sortiert.take(max).forEach { (p, m) ->
            a.put(JSONObject().put("id", p.id).put("name", p.name).put("lat", p.lat).put("lon", p.lon)
                .put("hoehe", p.hoehe).put("m", m).put("richtungen", p.richtungen))
        }
        return a.toString()
    }

    // ---------------- automatisch fragen (aus dem GPS im Dienst) ----------------
    fun pruefen(lat: Double, lon: Double, genau: Float) {
        if (!::ctx.isInitialized || !fragenAn() || !VarioBle.bereit || !frei) return
        val jetzt = System.currentTimeMillis()
        if (jetzt - letztePruefung < 10_000) return
        letztePruefung = jetzt
        if (jetzt - liveZeit > 5000 || fliegt || genau > 40f) return
        if (VarioBle.verbindungNr != verbindungNr) { verbindungNr = VarioBle.verbindungNr; gefragt.clear() }
        val (p, _) = naechster(lat, lon, 300.0) ?: return
        if (p.id in gefragt) return
        gefragt += p.id
        if (!nnJetzt.isNaN() && kotlin.math.abs(nnJetzt - p.hoehe) <= 5) return    // passt schon
        val name = p.name.replace(Regex("[|;=\\n\\r]"), " ").trim().take(24)
        VarioBle.befehlSenden(String.format(Locale.US, "spfrage=%d|%s", Math.round(p.hoehe).toInt(), name))
    }

    // ---------------- ParaglidingEarth laden ----------------
    /** laedt Startplaetze im Umkreis (km) und merkt sie sich; fertig(anzahl gesamt) oder null bei Fehler */
    fun laden(lat: Double, lon: Double, km: Int, fertig: (Int?, String?) -> Unit) {
        Thread {
            try {
                val url = URL(String.format(Locale.US,
                    "https://www.paragliding.earth/api/getAroundLatLngSites.php?lat=%.5f&lng=%.5f&distance=%d&limit=2000", lat, lon, km))
                val c = url.openConnection() as HttpURLConnection
                c.connectTimeout = 15000; c.readTimeout = 30000
                c.setRequestProperty("User-Agent", "Sweef-Vario-App")
                val neu = c.inputStream.use { parsen(it) }
                if (neu.isEmpty()) { haupt.post { fertig(null, "Keine Startplätze gefunden") }; return@Thread }
                val alle = LinkedHashMap<String, Platz>()
                pge.forEach { alle[it.id] = it }
                neu.forEach { alle[it.id] = it }
                pge = alle.values.toList()
                schreiben(pge)
                prefs().edit().putLong("sp_stand", System.currentTimeMillis()).apply()
                haupt.post { fertig(pge.size, null) }
            } catch (e: Exception) {
                haupt.post { fertig(null, "Keine Internetverbindung?") }
            }
        }.start()
    }

    private fun parsen(ein: java.io.InputStream): List<Platz> {
        val xp = Xml.newPullParser()
        xp.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
        xp.setInput(ein, null)
        val liste = mutableListOf<Platz>()
        var feld: MutableMap<String, String>? = null
        var richt: StringBuilder? = null
        var tag = ""
        var tiefe = 0; var startTiefe = -1
        while (true) {
            when (xp.next()) {
                XmlPullParser.START_TAG -> {
                    tiefe++
                    tag = xp.name
                    if (tag == "takeoff" && feld == null) { feld = mutableMapOf(); richt = StringBuilder(); startTiefe = tiefe }
                }
                XmlPullParser.TEXT -> {
                    val f = feld
                    if (f != null && !f.containsKey(tag)) {
                        val t = xp.text?.trim() ?: ""
                        if (t.isNotEmpty()) {
                            f[tag] = t
                            // Windrichtungen: <N>2</N> ... 2 = gut, 1 = moeglich
                            if (tag in RICHTUNGEN && (t == "1" || t == "2")) richt?.append(tag)?.append(if (t == "2") "+" else "")?.append(' ')
                        }
                    }
                }
                XmlPullParser.END_TAG -> {
                    if (xp.name == "takeoff" && tiefe == startTiefe && feld != null) {
                        val f = feld!!
                        val lat = f["lat"]?.toDoubleOrNull(); val lon = f["lng"]?.toDoubleOrNull()
                        val h = f["takeoff_altitude"]?.toDoubleOrNull()
                        if (lat != null && lon != null && h != null && h > -100) {
                            val id = "p:" + (f["pge_site_id"] ?: String.format(Locale.US, "%.5f,%.5f", lat, lon))
                            liste += Platz(id, f["name"] ?: "Startplatz", lat, lon, h, false, richt.toString().trim())
                        }
                        feld = null; richt = null
                    }
                    tiefe--
                    tag = ""
                }
                XmlPullParser.END_DOCUMENT -> return liste
            }
        }
    }
    private val RICHTUNGEN = setOf("N", "NE", "E", "SE", "S", "SW", "W", "NW")

    private fun schreiben(l: List<Platz>) {
        val a = JSONArray()
        l.forEach { a.put(JSONObject().put("i", it.id).put("n", it.name).put("a", it.lat).put("o", it.lon).put("h", it.hoehe).put("r", it.richtungen)) }
        try { datei().writeText(a.toString()) } catch (_: Exception) {}
    }

    private fun lesen(): List<Platz> = try {
        val a = JSONArray(datei().readText())
        (0 until a.length()).map { i ->
            val o = a.getJSONObject(i)
            Platz(o.getString("i"), o.getString("n"), o.getDouble("a"), o.getDouble("o"), o.getDouble("h"), false, o.optString("r"))
        }
    } catch (_: Exception) { emptyList() }
}
