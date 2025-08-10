package name.boyle.chris.sgtpuzzles

import android.app.Service
import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.content.SharedPreferences
import android.content.SharedPreferences.OnSharedPreferenceChangeListener
import android.content.res.Configuration
import android.content.res.Configuration.UI_MODE_NIGHT_MASK
import android.content.res.Configuration.UI_MODE_NIGHT_YES
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Binder
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.Utils.toastFirstFewTimes
import name.boyle.chris.sgtpuzzles.config.PrefsConstants

/** Switches the app in and out of night mode according to settings/sensors. To use, just extend ActivityWithNightMode. */
class NightModeHelper : Service(), SensorEventListener, OnSharedPreferenceChangeListener {

    /** Makes the service run while any relevant activity is started. */
    open class ActivityWithNightMode : AppCompatActivity() {
        private val serviceConnection: ServiceConnection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName, service: IBinder) {}
            override fun onServiceDisconnected(name: ComponentName) {}
        }

        override fun onStart() {
            super.onStart()
            bindService(
                Intent(this, NightModeHelper::class.java),
                serviceConnection,
                BIND_AUTO_CREATE
            )
        }

        override fun onStop() {
            super.onStop()
            unbindService(serviceConnection)
        }
    }

    private val binder: IBinder = LocalBinder()
    private lateinit var prefs: SharedPreferences
    private lateinit var state: SharedPreferences
    private var sensorManager: SensorManager? = null
    private var lightSensor: Sensor? = null

    /** Hack: we use unspecified to mean follow light sensor.  */
    private enum class NightMode {
        ON, AUTO, SYSTEM, OFF;

        companion object {
            fun fromPreference(pref: String?): NightMode {
                return if (pref == null) SYSTEM else try {
                    valueOf(pref.uppercase())
                } catch (_: IllegalArgumentException) {
                    SYSTEM
                }
            }
        }
    }

    private var mode = NightMode.SYSTEM
    private var darkNowSmoothed = false
    private var previousLux: Float? = null
    private val handler = Handler(Looper.getMainLooper())
    override fun onCreate() {
        super.onCreate()
        sensorManager = (getSystemService(SENSOR_SERVICE) as SensorManager?)?.apply {
            lightSensor = getDefaultSensor(Sensor.TYPE_LIGHT)
        }
        if (lightSensor == null) {
            Log.w(TAG, "No light sensor available")
        }
        prefs = PreferenceManager.getDefaultSharedPreferences(this)
        state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE)
        darkNowSmoothed = isNight(resources.configuration)
    }

    /** A binder that provides no communication; we just use binding to get one service as long as
     * any activity is open. Clients can see night status in Configuration.uiMode.  */
    class LocalBinder : Binder()

    override fun onBind(intent: Intent): IBinder {
        onRebind(intent)
        return binder
    }

    override fun onSensorChanged(event: SensorEvent) {
        // It's important to use a single Runnable instance for a callback so that removal works
        if (event.values[0] <= MAX_LUX_NIGHT) {
            handler.removeCallbacks(stayedLightRunnable)
            if (previousLux == null) {
                stayedDark()
            } else if (previousLux!! > MAX_LUX_NIGHT) {
                handler.postDelayed(stayedDarkRunnable, NIGHT_MODE_AUTO_DELAY)
            }
        } else if (event.values[0] >= MIN_LUX_DAY) {
            handler.removeCallbacks(stayedDarkRunnable)
            if (previousLux == null) {
                stayedLight()
            } else if (previousLux!! < MIN_LUX_DAY) {
                handler.postDelayed(stayedLightRunnable, NIGHT_MODE_AUTO_DELAY)
            }
        } else {
            handler.removeCallbacks(stayedLightRunnable)
            handler.removeCallbacks(stayedDarkRunnable)
        }
        previousLux = event.values[0]
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {  // don't care
    }

    override fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String?) {
        if (key == null || key == PrefsConstants.NIGHT_MODE_KEY) {
            applyNightMode()
            var changed = state.getLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, 0)
            changed++
            state.edit { putLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, changed) }
        }
    }

    private fun stayedDark() {
        if (darkNowSmoothed) return
        darkNowSmoothed = true
        if (state.getLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, 0) < 1) {
            toastFirstFewTimes(
                this@NightModeHelper,
                state,
                PrefsConstants.SEEN_NIGHT_MODE,
                3,
                R.string.night_mode_hint
            )
        }
        AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES)
    }
    private val stayedDarkRunnable = Runnable { stayedDark() }

    private fun stayedLight() {
        if (!darkNowSmoothed) return
        darkNowSmoothed = false
        AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_NO)
    }
    private val stayedLightRunnable = Runnable { stayedLight() }

    private fun applyNightMode() {
        var newMode = NightMode.fromPreference(
            prefs.getString(PrefsConstants.NIGHT_MODE_KEY, "system")
        )
        if (newMode == NightMode.AUTO && lightSensor == null) {
            newMode = NightMode.OFF
        }
        if (mode != NightMode.AUTO && newMode == NightMode.AUTO) {
            previousLux = null
            darkNowSmoothed =
                mode == NightMode.ON || mode == NightMode.SYSTEM && isNight(resources.configuration)
            sensorManager?.registerListener(
                this,
                lightSensor,
                SensorManager.SENSOR_DELAY_NORMAL
            )
        }
        if (mode == NightMode.AUTO && newMode != NightMode.AUTO) {
            handler.removeCallbacks(stayedLightRunnable)
            handler.removeCallbacks(stayedDarkRunnable)
            previousLux = null
            sensorManager?.unregisterListener(this)
        }
        mode = newMode
        AppCompatDelegate.setDefaultNightMode(if (mode == NightMode.ON || mode == NightMode.AUTO && darkNowSmoothed) AppCompatDelegate.MODE_NIGHT_YES else if (mode == NightMode.SYSTEM) AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM else AppCompatDelegate.MODE_NIGHT_NO)
    }

    override fun onUnbind(intent: Intent): Boolean {
        prefs.unregisterOnSharedPreferenceChangeListener(this)
        if (mode == NightMode.AUTO) {
            sensorManager?.unregisterListener(this)
            handler.removeCallbacks(stayedLightRunnable)
            handler.removeCallbacks(stayedDarkRunnable)
            previousLux = null
        }
        return true
    }

    override fun onRebind(intent: Intent) {
        prefs.registerOnSharedPreferenceChangeListener(this)
        applyNightMode()
    }

    companion object {
        private const val MAX_LUX_NIGHT = 3.4f
        private const val MIN_LUX_DAY = 15.0f
        private const val NIGHT_MODE_AUTO_DELAY: Long = 2100
        private const val TAG = "NightModeHelper"
        @JvmStatic
		fun isNight(configuration: Configuration): Boolean {
            return configuration.uiMode and UI_MODE_NIGHT_MASK == UI_MODE_NIGHT_YES
        }
    }
}