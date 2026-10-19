package de.sweef.vario

import android.annotation.SuppressLint
import android.content.Context
import android.hardware.GeomagneticField
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.location.LocationManager

/**
 * Kompass des Handys: Richtung der Handy-Oberkante (flach gehalten),
 * bezogen auf geografisch Nord (Missweisung wird ueber den Standort korrigiert).
 */
object Kompass : SensorEventListener {
    private var sm: SensorManager? = null
    @Volatile private var richtung = -1.0      // Grad, -1 = noch nichts
    @Volatile private var genau = 0            // SensorManager.SENSOR_STATUS_*
    private var missweisung = 0f
    private var glatt = Double.NaN

    @SuppressLint("MissingPermission")
    fun starten(ctx: Context): Boolean {
        val s = ctx.getSystemService(Context.SENSOR_SERVICE) as SensorManager
        val sensor = s.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR) ?: return false
        sm = s
        glatt = Double.NaN; richtung = -1.0
        s.registerListener(this, sensor, SensorManager.SENSOR_DELAY_UI)
        s.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD)?.let { s.registerListener(this, it, SensorManager.SENSOR_DELAY_NORMAL) }
        // Missweisung (Unterschied magnetisch / geografisch Nord) am Standort
        try {
            val lm = ctx.getSystemService(Context.LOCATION_SERVICE) as LocationManager
            val l = lm.getLastKnownLocation(LocationManager.GPS_PROVIDER) ?: lm.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
            if (l != null) missweisung = GeomagneticField(l.latitude.toFloat(), l.longitude.toFloat(), l.altitude.toFloat(), System.currentTimeMillis()).declination
        } catch (_: Exception) {}
        return true
    }

    fun stoppen() { sm?.unregisterListener(this); sm = null }

    /** {"grad":215,"genau":3} oder "" */
    fun stand(): String = if (richtung < 0) "" else "{\"grad\":${Math.round(richtung)},\"genau\":$genau}"

    override fun onSensorChanged(e: SensorEvent) {
        if (e.sensor.type == Sensor.TYPE_MAGNETIC_FIELD) return
        val m = FloatArray(9); val o = FloatArray(3)
        SensorManager.getRotationMatrixFromVector(m, e.values)
        SensorManager.getOrientation(m, o)
        val grad = ((Math.toDegrees(o[0].toDouble()) + missweisung) % 360 + 360) % 360
        // sanft glaetten (ueber 0/360 hinweg)
        glatt = if (glatt.isNaN()) grad else {
            var d = grad - glatt
            if (d > 180) d -= 360; if (d < -180) d += 360
            ((glatt + d * 0.25) % 360 + 360) % 360
        }
        richtung = glatt
    }

    override fun onAccuracyChanged(s: Sensor, a: Int) { if (s.type == Sensor.TYPE_MAGNETIC_FIELD) genau = a }
}
