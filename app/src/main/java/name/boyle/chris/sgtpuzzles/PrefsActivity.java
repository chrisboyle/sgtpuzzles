package name.boyle.chris.sgtpuzzles;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.os.Build;
import android.os.Bundle;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.support.v4.app.NavUtils;
import android.view.MenuItem;

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
		updateSummary((ListPreference)findPreference(GamePlay.ARROW_KEYS_KEY));
		updateSummary((ListPreference)findPreference(GamePlay.ORIENTATION_KEY));
		findPreference("about_content").setSummary(
				String.format(getString(R.string.about_content),
						GamePlay.getVersion(this)));
		// getSupportActionBar() not available from PreferenceActivity
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
			getActionBar().setDisplayHomeAsUpEnabled(true);
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
	public boolean onOptionsItemSelected(MenuItem item) {
		switch (item.getItemId()) {
			case android.R.id.home:
				NavUtils.navigateUpFromSameTask(this);
				return true;
		}
		return super.onOptionsItemSelected(item);
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
