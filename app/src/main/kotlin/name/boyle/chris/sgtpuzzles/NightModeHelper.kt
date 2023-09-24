package name.boyle.chris.sgtpuzzles;

import static android.content.res.Configuration.UI_MODE_NIGHT_MASK;
import static android.content.res.Configuration.UI_MODE_NIGHT_YES;
import static androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM;
import static androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_NO;
import static androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_YES;

import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.AppCompatDelegate;
import androidx.preference.PreferenceManager;
import android.util.Log;

import java.util.Locale;

import name.boyle.chris.sgtpuzzles.config.PrefsConstants;

/** Switches the app in and out of night mode according to settings/sensors. To use, just extend ActivityWithNightMode. */
public class NightModeHelper extends Service implements SensorEventListener, SharedPreferences.OnSharedPreferenceChangeListener {

	/** Makes the service run while any relevant activity is started. */
	public static class ActivityWithNightMode extends AppCompatActivity {
		private final ServiceConnection _serviceConnection = new ServiceConnection() {
			@Override
			public void onServiceConnected(ComponentName name, IBinder service) {
			}

			@Override
			public void onServiceDisconnected(ComponentName name) {
			}
		};

		@Override
		protected void onStart() {
			super.onStart();
			bindService(new Intent(this, NightModeHelper.class), _serviceConnection, BIND_AUTO_CREATE);
		}

		@Override
		protected void onStop() {
			super.onStop();
			unbindService(_serviceConnection);
		}
	}

	private static final float MAX_LUX_NIGHT = 3.4f;
	private static final float MIN_LUX_DAY = 15.0f;
	private static final long NIGHT_MODE_AUTO_DELAY = 2100;
	private static final String TAG = "NightModeHelper";

	private final IBinder _binder = new LocalBinder();
	private SharedPreferences _prefs;
	private SharedPreferences _state;
	@Nullable private SensorManager _sensorManager;
	@Nullable private Sensor _lightSensor;

	/** Hack: we use unspecified to mean follow light sensor. */
	private enum NightMode {
		ON, AUTO, SYSTEM, OFF;
		@NonNull
		static NightMode fromPreference(final String pref) {
			if (pref == null) return SYSTEM;
			try {
				return NightMode.valueOf(pref.toUpperCase(Locale.ROOT));
			} catch (IllegalArgumentException e) {
				return SYSTEM;
			}
		}
	}
	private NightMode _mode = NightMode.SYSTEM;
	private boolean _darkNowSmoothed;
	private Float _previousLux = null;
	private final Handler _handler = new Handler(Looper.getMainLooper());

	@Override
	public void onCreate() {
		super.onCreate();
		_sensorManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
		if (_sensorManager != null) {
			_lightSensor = _sensorManager.getDefaultSensor(Sensor.TYPE_LIGHT);
		}
		if (_lightSensor == null) {
			Log.w(TAG, "No light sensor available");
		}
		_prefs = PreferenceManager.getDefaultSharedPreferences(this);
		_state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, Context.MODE_PRIVATE);
		_darkNowSmoothed = isNight(getResources().getConfiguration());
	}

	/** A binder that provides no communication; we just use binding to get one service as long as
	 *  any activity is open. Clients can see night status in Configuration.uiMode. */
	public static class LocalBinder extends Binder {
	}

	@Override
	public IBinder onBind(Intent intent) {
		onRebind(intent);
		return _binder;
	}

	public static boolean isNight(Configuration configuration) {
		return (configuration.uiMode & UI_MODE_NIGHT_MASK) == UI_MODE_NIGHT_YES;
	}

	public void onSensorChanged(SensorEvent event) {
		if (event.values[0] <= MAX_LUX_NIGHT) {
			_handler.removeCallbacks(this::stayedLight);
			if (_previousLux == null) {
				stayedDark();
			} else if (_previousLux > MAX_LUX_NIGHT) {
				_handler.postDelayed(this::stayedDark, NIGHT_MODE_AUTO_DELAY);
			}
		} else if (event.values[0] >= MIN_LUX_DAY) {
			_handler.removeCallbacks(this::stayedDark);
			if (_previousLux == null) {
				stayedLight();
			} else if (_previousLux < MIN_LUX_DAY) {
				_handler.postDelayed(this::stayedLight, NIGHT_MODE_AUTO_DELAY);
			}
		} else {
			_handler.removeCallbacks(this::stayedLight);
			_handler.removeCallbacks(this::stayedDark);
		}
		_previousLux = event.values[0];
	}

	public void onAccuracyChanged(Sensor sensor, int accuracy) {  // don't care
	}

	@Override
	public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
		if (key == null || key.equals(PrefsConstants.NIGHT_MODE_KEY)) {
			applyNightMode();
			long changed = _state.getLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, 0);
			changed++;
			_state.edit().putLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, changed).apply();
		}
	}

	private void stayedDark() {
		if (_darkNowSmoothed) return;
		_darkNowSmoothed = true;
		if (_state.getLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, 0) < 1) {
			Utils.toastFirstFewTimes(NightModeHelper.this, _state, PrefsConstants.SEEN_NIGHT_MODE, 3, R.string.night_mode_hint);
		}
		AppCompatDelegate.setDefaultNightMode(MODE_NIGHT_YES);
	}

	private void stayedLight () {
		if (!_darkNowSmoothed) return;
		_darkNowSmoothed = false;
		AppCompatDelegate.setDefaultNightMode(MODE_NIGHT_NO);
	}

	private void applyNightMode() {
		NightMode newMode = NightMode.fromPreference(_prefs.getString(PrefsConstants.NIGHT_MODE_KEY, "system"));
		if (newMode == NightMode.AUTO && _lightSensor == null) {
			newMode = NightMode.OFF;
		}
		if (_mode != NightMode.AUTO && newMode == NightMode.AUTO) {
			_previousLux = null;
			_darkNowSmoothed = (_mode == NightMode.ON || (_mode == NightMode.SYSTEM && isNight(getResources().getConfiguration())));
			if (_sensorManager != null) _sensorManager.registerListener(this, _lightSensor, SensorManager.SENSOR_DELAY_NORMAL);
		}
		if (_mode == NightMode.AUTO && newMode != NightMode.AUTO) {
			_handler.removeCallbacks(this::stayedLight);
			_handler.removeCallbacks(this::stayedDark);
			_previousLux = null;
			if (_sensorManager != null) _sensorManager.unregisterListener(this);
		}
		_mode = newMode;
		AppCompatDelegate.setDefaultNightMode(_mode == NightMode.ON || (_mode == NightMode.AUTO && _darkNowSmoothed) ? MODE_NIGHT_YES :
				_mode == NightMode.SYSTEM ? MODE_NIGHT_FOLLOW_SYSTEM :
				MODE_NIGHT_NO);
	}

	@Override
	public boolean onUnbind(Intent intent) {
		_prefs.unregisterOnSharedPreferenceChangeListener(this);
		if (_mode == NightMode.AUTO) {
			if (_sensorManager != null) _sensorManager.unregisterListener(this);
			_handler.removeCallbacks(this::stayedLight);
			_handler.removeCallbacks(this::stayedDark);
			_previousLux = null;
		}
		return true;
	}

	@Override
	public void onRebind(Intent intent) {
		_prefs.registerOnSharedPreferenceChangeListener(this);
		applyNightMode();
	}
}
