package de.sweef.vario

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale
import java.util.UUID

/**
 * Die Bluetooth-Verbindung zum Vario. Lebt unabhaengig von der Oberflaeche,
 * damit die Verbindung (und das GPS-Tempo) auch im Hintergrund weiterlaeuft.
 * Alle Ablaeufe laufen auf dem Haupt-Thread (handler), Bluetooth-Rueckrufe
 * werden dorthin weitergereicht.
 */
@SuppressLint("MissingPermission")
object VarioBle {
    private const val TAG = "SweefBle"
    val DIENST: UUID = UUID.fromString("7e5e0001-b5a3-f393-e0a9-e50e24dcca9e")
    val BEFEHL: UUID = UUID.fromString("7e5e0003-b5a3-f393-e0a9-e50e24dcca9e")
    private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Meldungen an die Oberflaeche ("verbunden", "getrennt", "notify", ...) */
    var ereignis: ((String, JSONObject) -> Unit)? = null

    private lateinit var ctx: Context
    private val handler = Handler(Looper.getMainLooper())

    var adresse: String? = null; private set
    var name: String? = null; private set
    private var gatt: BluetoothGatt? = null
    private var dienst: BluetoothGattService? = null
    @Volatile var bereit = false; private set
    private var verbindet = false
    /** true = Nutzer hat getrennt (kein automatisches Wiederverbinden) */
    var absichtlich = true
    /** waehrend eines Firmware-Updates keine eigenen Verbindungen */
    var gesperrt = false

    private val wartende = mutableListOf<(Boolean, String?) -> Unit>()
    private val verbindenZeitlimit = Runnable { verbindenFehlgeschlagen("Zeitüberschreitung") }

    fun init(c: Context) {
        ctx = c.applicationContext
        if (adresse == null) adresse = ctx.getSharedPreferences("sweef", Context.MODE_PRIVATE).getString("letztes_vario", null)
    }

    /** Adresse des zuletzt verbundenen Varios (fuer ein abgebrochenes Update) */
    fun letzteAdresse(): String? = adresse
        ?: ctx.getSharedPreferences("sweef", Context.MODE_PRIVATE).getString("letztes_vario", null)

    val DFU_DIENST: UUID = UUID.fromString("00001530-1212-efde-1523-785feabcd123")

    private fun adapter(): BluetoothAdapter? =
        (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    // ------------------------------------------------------------------
    // Suchen
    // ------------------------------------------------------------------
    private var suchCallback: ScanCallback? = null

    fun suchen(dauerMs: Long, nurName: (String) -> Boolean, fertig: (JSONArray) -> Unit) =
        suchen(dauerMs, false, nurName, fertig)

    fun suchen(dauerMs: Long, alle: Boolean, nurName: (String) -> Boolean, fertig: (JSONArray) -> Unit) {
        val scanner = adapter()?.bluetoothLeScanner
        if (scanner == null) { fertig(JSONArray()); return }
        val gefunden = LinkedHashMap<String, JSONObject>()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, r: ScanResult) {
                val n = r.scanRecord?.deviceName ?: r.device.name
                if (!alle && (n == null || !nurName(n))) return
                val dfu = r.scanRecord?.serviceUuids?.any { it.uuid == DFU_DIENST } == true
                handler.post {
                    gefunden[r.device.address] = JSONObject()
                        .put("id", r.device.address).put("name", n ?: "").put("rssi", r.rssi).put("dfu", dfu)
                }
            }
        }
        suchStopp()
        suchCallback = cb
        val einst = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try { scanner.startScan(null, einst, cb) } catch (e: Exception) { fertig(JSONArray()); return }
        handler.postDelayed({
            suchStopp()
            val a = JSONArray()
            gefunden.values.sortedByDescending { it.optInt("rssi") }.forEach { a.put(it) }
            fertig(a)
        }, dauerMs)
    }

    private fun suchStopp() {
        val cb = suchCallback ?: return
        suchCallback = null
        try { adapter()?.bluetoothLeScanner?.stopScan(cb) } catch (_: Exception) {}
    }

    // ------------------------------------------------------------------
    // Verbinden / Trennen
    // ------------------------------------------------------------------
    fun verbinden(adr: String, n: String?, fertig: (Boolean, String?) -> Unit) {
        if (gesperrt) { fertig(false, "Firmware-Update läuft"); return }
        absichtlich = false
        if (adr == adresse && bereit) { fertig(true, null); return }
        if (adr != adresse) {
            schliessen()
            adresse = adr
        }
        if (n != null) name = n
        wartende.add(fertig)
        if (!verbindet) starten()
    }

    private fun starten() {
        val adr = adresse ?: return
        val dev: BluetoothDevice = try { adapter()?.getRemoteDevice(adr) } catch (e: Exception) { null }
            ?: run { verbindenFehlgeschlagen("Bluetooth aus?"); return }
        verbindet = true
        handler.removeCallbacks(verbindenZeitlimit)
        handler.postDelayed(verbindenZeitlimit, 15000)
        gatt = dev.connectGatt(ctx, false, callback, BluetoothDevice.TRANSPORT_LE)
    }

    private fun verbindenFehlgeschlagen(grund: String) {
        handler.removeCallbacks(verbindenZeitlimit)
        verbindet = false
        try { gatt?.close() } catch (_: Exception) {}
        gatt = null; dienst = null; bereit = false
        val w = wartende.toList(); wartende.clear()
        w.forEach { it(false, grund) }
        wiederverbindenPlanen()
    }

    fun trennen() {
        absichtlich = true
        handler.removeCallbacks(wiederverbinden)
        val g = gatt
        if (g == null) { schliessen(); return }
        try { g.disconnect() } catch (_: Exception) {}
        // falls kein Rueckruf mehr kommt
        handler.postDelayed({ if (gatt === g) getrennt(g) }, 1500)
    }

    private fun schliessen() {
        handler.removeCallbacks(verbindenZeitlimit)
        try { gatt?.close() } catch (_: Exception) {}
        gatt = null; dienst = null; bereit = false; verbindet = false
        warteschlangeLeeren("getrennt")
    }

    private val wiederverbinden = Runnable {
        if (!absichtlich && !gesperrt && !bereit && !verbindet && adresse != null) starten()
    }

    private fun wiederverbindenPlanen() {
        if (absichtlich || gesperrt || adresse == null) return
        handler.removeCallbacks(wiederverbinden)
        handler.postDelayed(wiederverbinden, 4000)
    }

    private fun getrennt(g: BluetoothGatt) {
        if (g !== gatt) { try { g.close() } catch (_: Exception) {}; return }
        val warBereit = bereit
        schliessen()
        val w = wartende.toList(); wartende.clear()
        w.forEach { it(false, "Verbindung abgebrochen") }
        if (warBereit) VarioOrt.merken(ctx, name)
        if (warBereit) ereignis?.invoke("getrennt", JSONObject().put("absichtlich", absichtlich))
        if (absichtlich) VarioService.stoppen(ctx) else wiederverbindenPlanen()
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, neu: Int) {
            handler.post {
                if (g !== gatt) { if (neu == BluetoothProfile.STATE_DISCONNECTED) try { g.close() } catch (_: Exception) {}; return@post }
                if (neu == BluetoothProfile.STATE_CONNECTED) {
                    if (!g.requestMtu(247)) g.discoverServices()
                } else if (neu == BluetoothProfile.STATE_DISCONNECTED) {
                    getrennt(g)
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            handler.post { if (g === gatt && dienst == null) g.discoverServices() }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            handler.post {
                if (g !== gatt) return@post
                val d = g.getService(DIENST)
                if (status != BluetoothGatt.GATT_SUCCESS || d == null) {
                    try { g.disconnect() } catch (_: Exception) {}
                    verbindenFehlgeschlagen("Kein Sweef-Dienst gefunden")
                    return@post
                }
                handler.removeCallbacks(verbindenZeitlimit)
                dienst = d; bereit = true; verbindet = false
                ctx.getSharedPreferences("sweef", Context.MODE_PRIVATE).edit().putString("letztes_vario", adresse).apply()
                val w = wartende.toList(); wartende.clear()
                w.forEach { it(true, null) }
                ereignis?.invoke("verbunden", JSONObject().put("id", adresse))
                VarioService.starten(ctx)
            }
        }

        @Deprecated("alt")
        override fun onCharacteristicRead(g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int) {
            @Suppress("DEPRECATION") val v = ch.value ?: ByteArray(0)
            handler.post { opFertig(status == BluetoothGatt.GATT_SUCCESS, v, "Lesen fehlgeschlagen") }
        }

        override fun onCharacteristicRead(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray, status: Int) {
            handler.post { opFertig(status == BluetoothGatt.GATT_SUCCESS, value, "Lesen fehlgeschlagen") }
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, ch: BluetoothGattCharacteristic, status: Int) {
            handler.post { opFertig(status == BluetoothGatt.GATT_SUCCESS, null, "Schreiben fehlgeschlagen") }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            handler.post { opFertig(status == BluetoothGatt.GATT_SUCCESS, null, "Meldungen nicht möglich") }
        }

        @Deprecated("alt")
        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION") val v = ch.value ?: return
            meldung(ch.uuid, v.copyOf())
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray) {
            meldung(ch.uuid, value)
        }
    }

    private fun meldung(uuid: UUID, v: ByteArray) {
        handler.post {
            ereignis?.invoke("notify", JSONObject()
                .put("u", uuid.toString())
                .put("d", Base64.encodeToString(v, Base64.NO_WRAP)))
        }
    }

    fun hatKanal(uuid: String): Boolean =
        dienst?.getCharacteristic(UUID.fromString(uuid)) != null

    // ------------------------------------------------------------------
    // Warteschlange: Android kann immer nur einen Vorgang gleichzeitig
    // ------------------------------------------------------------------
    private class Op(
        val art: Int,                 // 0 = schreiben, 1 = lesen, 2 = Meldungen an
        val uuid: UUID,
        val daten: ByteArray?,
        val mitAntwort: Boolean,
        val fertig: (Boolean, ByteArray?, String?) -> Unit
    )

    private val schlange = ArrayDeque<Op>()
    private var laufend: Op? = null
    private val opZeitlimit = Runnable { opFertig(false, null, "Zeitüberschreitung") }

    fun schreiben(uuid: String, daten: ByteArray, mitAntwort: Boolean, fertig: (Boolean, ByteArray?, String?) -> Unit) =
        einreihen(Op(0, UUID.fromString(uuid), daten, mitAntwort, fertig))

    fun lesen(uuid: String, fertig: (Boolean, ByteArray?, String?) -> Unit) =
        einreihen(Op(1, UUID.fromString(uuid), null, true, fertig))

    fun meldungenAn(uuid: String, fertig: (Boolean, ByteArray?, String?) -> Unit) =
        einreihen(Op(2, UUID.fromString(uuid), null, true, fertig))

    private fun einreihen(op: Op) {
        handler.post {
            if (!bereit) { op.fertig(false, null, "Nicht verbunden"); return@post }
            schlange.addLast(op)
            naechste()
        }
    }

    private fun naechste() {
        if (laufend != null) return
        val op = schlange.removeFirstOrNull() ?: return
        laufend = op
        val g = gatt
        val ch = dienst?.getCharacteristic(op.uuid)
        if (g == null || ch == null) { opFertig(false, null, "Kanal fehlt"); return }
        val ok = try {
            when (op.art) {
                0 -> schreibenRoh(g, ch, op.daten ?: ByteArray(0), op.mitAntwort)
                1 -> g.readCharacteristic(ch)
                else -> {
                    g.setCharacteristicNotification(ch, true)
                    val d = ch.getDescriptor(CCCD)
                    if (d == null) false else descriptorSchreiben(g, d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                }
            }
        } catch (e: Exception) { Log.w(TAG, "Vorgang", e); false }
        if (!ok) { opFertig(false, null, "Bluetooth beschäftigt"); return }
        handler.postDelayed(opZeitlimit, 6000)
    }

    @Suppress("DEPRECATION")
    private fun schreibenRoh(g: BluetoothGatt, ch: BluetoothGattCharacteristic, v: ByteArray, mitAntwort: Boolean): Boolean {
        val typ = if (mitAntwort) BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT else BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        return if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(ch, v, typ) == BluetoothStatusCodes.SUCCESS
        } else {
            ch.writeType = typ
            ch.value = v
            g.writeCharacteristic(ch)
        }
    }

    @Suppress("DEPRECATION")
    private fun descriptorSchreiben(g: BluetoothGatt, d: BluetoothGattDescriptor, v: ByteArray): Boolean {
        return if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(d, v) == BluetoothStatusCodes.SUCCESS
        } else {
            d.value = v
            g.writeDescriptor(d)
        }
    }

    private fun opFertig(ok: Boolean, wert: ByteArray?, fehler: String) {
        handler.removeCallbacks(opZeitlimit)
        val op = laufend ?: return
        laufend = null
        try { op.fertig(ok, wert, if (ok) null else fehler) } catch (e: Exception) { Log.w(TAG, "Rueckruf", e) }
        naechste()
    }

    private fun warteschlangeLeeren(grund: String) {
        handler.removeCallbacks(opZeitlimit)
        val alle = listOfNotNull(laufend) + schlange.toList()
        laufend = null; schlange.clear()
        alle.forEach { try { it.fertig(false, null, grund) } catch (_: Exception) {} }
    }

    // ------------------------------------------------------------------
    // GPS-Tempo ans Vario (vom Hintergrund-Dienst jede Sekunde)
    // ------------------------------------------------------------------
    fun tempoSenden(kmh: Float) {
        handler.post {
            if (!bereit || schlange.size > 3) return@post
            val t = String.format(Locale.US, "gs=%.1f", kmh)
            schlange.addLast(Op(0, BEFEHL, t.toByteArray(), false) { _, _, _ -> })
            naechste()
        }
    }
}
