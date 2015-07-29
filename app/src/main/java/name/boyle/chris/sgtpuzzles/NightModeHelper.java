package name.boyle.chris.sgtpuzzles;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Handler;
import android.preference.PreferenceManager;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

public class NightModeHelper implements SensorEventListener, SharedPreferences.OnSharedPreferenceChangeListener {
	static final String NIGHT_MODE_KEY = "nightMode";
	private static final String SEEN_NIGHT_MODE = "seenNightMode";
	private static final String SEEN_NIGHT_MODE_SETTING = "seenNightModeSetting";
	private static final float MAX_LUX_NIGHT = 3.4f;
	private static final float MIN_LUX_DAY = 15.0f;
	private static final long NIGHT_MODE_AUTO_DELAY = 2100;

	private final Parent parent;
	private final SharedPreferences prefs;
	private final SharedPreferences state;
	private final PrefsSaver prefsSaver;
	private Context context;
	private SensorManager sensorManager;
	private Sensor lightSensor;

	enum NightMode { ON, AUTO, OFF }
	private NightMode nightMode = NightMode.OFF;
	private boolean darkNowSmoothed = false;
	private Float previousLux = null;
	private Handler handler = new Handler();

	public interface Parent {
		void refreshNightNow(boolean isNight, boolean alreadyStarted);
	}

	public NightModeHelper(Context context, Parent parent) {
		this.context = context;
		this.parent = parent;
		sensorManager = (SensorManager)context.getSystemService(Context.SENSOR_SERVICE);
		lightSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LIGHT);
		prefs = PreferenceManager.getDefaultSharedPreferences(context);
		prefs.registerOnSharedPreferenceChangeListener(this);
		state = context.getSharedPreferences(GamePlay.STATE_PREFS_NAME, Context.MODE_PRIVATE);
		prefsSaver = PrefsSaver.get(context);
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
	@SuppressLint("CommitPrefEdits")
	public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
		if (key.equals(NIGHT_MODE_KEY)) {
			applyNightMode(true);
			long changed = state.getLong(SEEN_NIGHT_MODE_SETTING, 0);
			changed++;
			SharedPreferences.Editor ed = state.edit();
			ed.putLong(SEEN_NIGHT_MODE_SETTING, changed);
			prefsSaver.save(ed);
		}
	}

	private final Runnable stayedDark = new Runnable() {
		@Override
		public void run() {
			if (darkNowSmoothed) return;
			darkNowSmoothed = true;
			parent.refreshNightNow(isNight(), true);
			if (state.getLong(SEEN_NIGHT_MODE_SETTING, 0) < 1) {
				Utils.toastFirstFewTimes(context, state, prefsSaver, SEEN_NIGHT_MODE, 3, R.string.night_mode_hint);
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

	private void applyNightMode(final boolean alreadyStarted) {
		final boolean wasAuto = (nightMode == NightMode.AUTO);
		final String pref = prefs.getString(NIGHT_MODE_KEY, "auto");
		if ("on".equals(pref)) {
			nightMode = NightMode.ON;
			darkNowSmoothed = true;
			handler.removeCallbacks(stayedLight);
			handler.removeCallbacks(stayedDark);
			previousLux = null;
			if (wasAuto) sensorManager.unregisterListener(this);
		}
		else if ("off".equals(pref) || lightSensor == null) {
			nightMode = NightMode.OFF;
			darkNowSmoothed = false;
			handler.removeCallbacks(stayedLight);
			handler.removeCallbacks(stayedDark);
			previousLux = null;
			if (wasAuto) sensorManager.unregisterListener(this);
		}
		else if (!wasAuto) {
			nightMode = NightMode.AUTO;
			if (alreadyStarted) sensorManager.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL);
		}
		parent.refreshNightNow(isNight(), alreadyStarted);
	}

	public void onPause() {
		if (nightMode == NightMode.AUTO) {
			sensorManager.unregisterListener(this);
			handler.removeCallbacks(stayedLight);
			handler.removeCallbacks(stayedDark);
			previousLux = null;
		}
	}

	public void onResume() {
		if (nightMode == NightMode.AUTO) {
			sensorManager.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL);
		}
	}
}
