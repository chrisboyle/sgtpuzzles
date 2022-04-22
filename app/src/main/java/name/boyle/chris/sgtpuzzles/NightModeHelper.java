package name.boyle.chris.sgtpuzzles;

import static android.content.res.Configuration.UI_MODE_NIGHT_MASK;
import static android.content.res.Configuration.UI_MODE_NIGHT_YES;

import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.Looper;
import androidx.annotation.NonNull;
import androidx.preference.PreferenceManager;
import android.util.Log;

public class NightModeHelper implements SensorEventListener, SharedPreferences.OnSharedPreferenceChangeListener {
	private static final float MAX_LUX_NIGHT = 3.4f;
	private static final float MIN_LUX_DAY = 15.0f;
	private static final long NIGHT_MODE_AUTO_DELAY = 2100;
	private static final String TAG = "NightModeHelper";

	private final Parent parent;
	private final SharedPreferences prefs;
	private final SharedPreferences state;
	private final Context context;
	private final SensorManager sensorManager;
	private Sensor lightSensor;

	enum NightMode { ON, AUTO, OFF }
	private NightMode nightMode = NightMode.OFF;
	private boolean darkNowSmoothed = false;
	private Float previousLux = null;
	private final Handler handler = new Handler(Looper.getMainLooper());

	public interface Parent {
		void refreshNightNow(boolean isNight, boolean alreadyStarted);
		int getUIMode();
	}

	NightModeHelper(@NonNull Context context, @NonNull Parent parent) {
		this.context = context;
		this.parent = parent;
		sensorManager = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
		if (sensorManager != null) {
			lightSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LIGHT);
		}
		if (lightSensor == null) {
			Log.w(TAG, "No light sensor available");
		}
		prefs = PreferenceManager.getDefaultSharedPreferences(context);
		prefs.registerOnSharedPreferenceChangeListener(this);
		state = context.getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, Context.MODE_PRIVATE);
		applyNightMode(false);
	}

	public boolean isNight() {
		return nightMode == NightMode.ON || (nightMode == NightMode.AUTO && darkNowSmoothed);
	}

	public void onSensorChanged(SensorEvent event) {
		if (event.values[0] <= MAX_LUX_NIGHT) {
			handler.removeCallbacks(stayedLight);
			if (previousLux == null) {
				stayedDark.run();
			} else if (previousLux > MAX_LUX_NIGHT) {
				handler.postDelayed(stayedDark, NIGHT_MODE_AUTO_DELAY);
			}
		} else if (event.values[0] >= MIN_LUX_DAY) {
			handler.removeCallbacks(stayedDark);
			if (previousLux == null) {
				stayedLight.run();
			} else if (previousLux < MIN_LUX_DAY) {
				handler.postDelayed(stayedLight, NIGHT_MODE_AUTO_DELAY);
			}
		} else {
			handler.removeCallbacks(stayedLight);
			handler.removeCallbacks(stayedDark);
		}
		previousLux = event.values[0];
	}

	public void onAccuracyChanged(Sensor sensor, int accuracy) {  // don't care
	}

	@Override
	public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
		if (key.equals(PrefsConstants.NIGHT_MODE_KEY)) {
			applyNightMode(true);
			long changed = state.getLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, 0);
			changed++;
			state.edit().putLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, changed).apply();
		}
	}

	private final Runnable stayedDark = new Runnable() {
		@Override
		public void run() {
			if (darkNowSmoothed) return;
			darkNowSmoothed = true;
			parent.refreshNightNow(isNight(), true);
			if (state.getLong(PrefsConstants.SEEN_NIGHT_MODE_SETTING, 0) < 1) {
				Utils.toastFirstFewTimes(context, state, PrefsConstants.SEEN_NIGHT_MODE, 3, R.string.night_mode_hint);
			}
		}
	};

	private final Runnable stayedLight = new Runnable() {
		@Override
		public void run() {
			if (!darkNowSmoothed) return;
			darkNowSmoothed = false;
			parent.refreshNightNow(isNight(), true);
		}
	};

	private void setStaticNightMode(final boolean night) {
		final boolean wasAuto = (nightMode == NightMode.AUTO);
		nightMode = night ? NightMode.ON : NightMode.OFF;
		darkNowSmoothed = night;
		handler.removeCallbacks(stayedLight);
		handler.removeCallbacks(stayedDark);
		previousLux = null;
		if (wasAuto && sensorManager != null) sensorManager.unregisterListener(this);
	}

	private void applyNightMode(final boolean alreadyStarted) {
		final String pref = prefs.getString(PrefsConstants.NIGHT_MODE_KEY, "system");
		if ("on".equals(pref)) {
			setStaticNightMode(true);
		}
		else if("system".equals(pref)) {
			// Rely on activity restart to get here again when it changes
			setStaticNightMode((parent.getUIMode() & UI_MODE_NIGHT_MASK) == UI_MODE_NIGHT_YES);
		}
		else if ("off".equals(pref) || lightSensor == null) {
			setStaticNightMode(false);
		}
		else if (nightMode != NightMode.AUTO) {
			nightMode = NightMode.AUTO;
			if (alreadyStarted && sensorManager != null) sensorManager.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL);
		}
		parent.refreshNightNow(isNight(), alreadyStarted);
	}

	public void onPause() {
		if (nightMode == NightMode.AUTO) {
			if (sensorManager != null) sensorManager.unregisterListener(this);
			handler.removeCallbacks(stayedLight);
			handler.removeCallbacks(stayedDark);
			previousLux = null;
		}
	}

	public void onResume() {
		if (nightMode == NightMode.AUTO) {
			if (sensorManager != null) sensorManager.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL);
		}
	}
}
