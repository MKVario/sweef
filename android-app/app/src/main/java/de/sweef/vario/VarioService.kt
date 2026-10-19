package de.sweef.vario

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import androidx.core.content.ContextCompat

/**
 * Laeuft im Vordergrund, solange das Vario verbunden ist:
 * haelt die App am Leben (Bluetooth bleibt verbunden) und schickt
 * jede Sekunde die GPS-Geschwindigkeit ueber Grund ans Vario.
 */
class VarioService : Service() {

    companion object {
        private const val KANAL = "sweef_verbindung"
        private const val NR = 7
        @Volatile var laeuft = false; private set
        /** letzte Geschwindigkeit in km/h (fuer die Oberflaeche), -1 = keine */
        @Volatile var letztesTempo = -1f; private set

        fun starten(ctx: Context) {
            val i = Intent(ctx, VarioService::class.java)
            try { ContextCompat.startForegroundService(ctx, i) } catch (_: Exception) {}
        }

        fun stoppen(ctx: Context) {
            try { ctx.stopService(Intent(ctx, VarioService::class.java)) } catch (_: Exception) {}
        }

        fun tempoAn(ctx: Context): Boolean =
            ctx.getSharedPreferences("sweef", MODE_PRIVATE).getBoolean("tempo_an", true)

        fun tempoSetzen(ctx: Context, an: Boolean) {
            ctx.getSharedPreferences("sweef", MODE_PRIVATE).edit().putBoolean("tempo_an", an).apply()
        }
    }

    private var lm: LocationManager? = null
    private var wach: PowerManager.WakeLock? = null

    private val gps = object : LocationListener {
        override fun onLocationChanged(l: Location) {
            val alt = (SystemClock.elapsedRealtimeNanos() - l.elapsedRealtimeNanos) / 1_000_000_000.0
            if (alt <= 3 && (!l.hasAccuracy() || l.accuracy < 50)) {
                if (VarioBle.bereit) VarioOrt.fix(l.latitude, l.longitude, l.accuracy, System.currentTimeMillis())
                Startplaetze.pruefen(l.latitude, l.longitude, if (l.hasAccuracy()) l.accuracy else 99f)
            }
            if (alt <= 3 && (!l.hasAccuracy() || l.accuracy < 50))
                GpsSpur.punkt(l.time, l.latitude, l.longitude, if (l.hasAltitude()) l.altitude else -9999.0)
            if (!l.hasSpeed() || alt > 3) { letztesTempo = -1f; return }
            val kmh = l.speed * 3.6f
            letztesTempo = kmh
            if (tempoAn(this@VarioService)) VarioBle.tempoSenden(kmh)
        }
        @Deprecated("alt") override fun onStatusChanged(p: String?, s: Int, e: Bundle?) {}
        override fun onProviderEnabled(p: String) {}
        override fun onProviderDisabled(p: String) { letztesTempo = -1f }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        VarioBle.init(this)
        GpsSpur.init(this)
        Startplaetze.init(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(KANAL, "Verbindung zum Vario", NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun darf(p: String) = ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED

    @SuppressLint("MissingPermission", "WakelockTimeout")
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val oeffnen = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE
        )
        val n: Notification = Notification.Builder(this, KANAL)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Sweef verbunden")
            .setContentText(if (tempoAn(this)) "Vario verbunden · GPS-Tempo wird gesendet" else "Vario verbunden")
            .setContentIntent(oeffnen)
            .setOngoing(true)
            .build()

        val gpsErlaubt = darf(Manifest.permission.ACCESS_FINE_LOCATION)
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                var typ = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
                if (gpsErlaubt) typ = typ or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
                startForeground(NR, n, typ)
            } else {
                startForeground(NR, n)
            }
        } catch (e: Exception) {
            stopSelf(); return START_NOT_STICKY
        }
        laeuft = true

        if (wach == null) {
            wach = (getSystemService(POWER_SERVICE) as PowerManager)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "sweef:verbindung").also { it.acquire(8 * 3600 * 1000L) }
        }
        if (gpsErlaubt && lm == null) {
            lm = getSystemService(LOCATION_SERVICE) as LocationManager
            try {
                lm?.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, gps, Looper.getMainLooper())
            } catch (_: Exception) {}
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        laeuft = false
        letztesTempo = -1f
        try { lm?.removeUpdates(gps) } catch (_: Exception) {}
        GpsSpur.schreiben()
        lm = null
        try { wach?.let { if (it.isHeld) it.release() } } catch (_: Exception) {}
        wach = null
        super.onDestroy()
    }
}
