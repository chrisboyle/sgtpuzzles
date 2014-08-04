package name.boyle.chris.sgtpuzzles;

import name.boyle.chris.sgtpuzzles.compat.ActionBarCompat;
import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;

@SuppressWarnings("WeakerAccess")
public class PrefsActivity extends PreferenceActivity implements OnSharedPreferenceChangeListener
{
	private PrefsSaver prefsSaver;

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		prefsSaver = PrefsSaver.get(this);
		addPreferencesFromResource(R.xml.preferences);
		updateSummary((ListPreference)findPreference(SGTPuzzles.ARROW_KEYS_KEY));
		if (ActionBarCompat.get(this) != null) {
			getPreferenceScreen().removePreference(findPreference("gameChooser"));
		} else {
			updateSummary((ListPreference)findPreference(GameChooser.CHOOSER_STYLE_KEY));
		}
	}

	@Override
	protected void onResume()
	{
		super.onResume();
		getPreferenceScreen().getSharedPreferences().registerOnSharedPreferenceChangeListener(this);
	}

	@Override
	protected void onPause()
	{
		super.onPause();
		getPreferenceScreen().getSharedPreferences().unregisterOnSharedPreferenceChangeListener(this);
		prefsSaver.backup();
	}

	@Override
	public void onSharedPreferenceChanged(SharedPreferences prefs, String key)
	{
		Preference p = findPreference(key);
		if (p != null && p instanceof ListPreference) updateSummary((ListPreference)p);
	}

	void updateSummary(ListPreference lp)
	{
		lp.setSummary(lp.getEntry());
		getListView().postInvalidate();
	}
}
