package name.boyle.chris.sgtpuzzles;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

import android.content.Intent;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.net.Uri;
import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceScreen;

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
		updateSummary((ListPreference)findPreference(SGTPuzzles.ORIENTATION_KEY));
		findPreference("about_content").setSummary(
				String.format(getString(R.string.about_content),
						SGTPuzzles.getVersion(this)));
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

	@Override
	public boolean onPreferenceTreeClick(PreferenceScreen preferenceScreen, Preference preference) {
		if (preference.getKey().equals("show_website")) {
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(
					getString(R.string.website_url))));
			return true;
		} else if (preference.getKey().equals("send_feedback")) {
			SGTPuzzles.tryEmailAuthor(this);
			return true;
		}
		return super.onPreferenceTreeClick(preferenceScreen, preference);
	}
}
