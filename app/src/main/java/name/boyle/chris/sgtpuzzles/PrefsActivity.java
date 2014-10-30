package name.boyle.chris.sgtpuzzles;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

import android.annotation.SuppressLint;
import android.app.ActionBar;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.preference.CheckBoxPreference;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceCategory;
import android.support.v4.app.NavUtils;
import android.view.MenuItem;

import java.text.MessageFormat;

@SuppressWarnings("WeakerAccess")
public class PrefsActivity extends PreferenceActivity implements OnSharedPreferenceChangeListener
{
	static final String BACKEND_EXTRA = "backend";
	private PrefsSaver prefsSaver;

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		prefsSaver = PrefsSaver.get(this);
		addPreferencesFromResource(R.xml.preferences);
		final String whichBackend = getIntent().getStringExtra(BACKEND_EXTRA);
		if (whichBackend == null) throw new RuntimeException("PrefsActivity requires extra: " + BACKEND_EXTRA);
		final int nameId = getResources().getIdentifier("name_" + whichBackend, "string", getPackageName());
		final PreferenceCategory thisGameCategory = (PreferenceCategory) findPreference("thisGame");
		thisGameCategory.setTitle(nameId);
		if (!whichBackend.equals("bridges")) thisGameCategory.removePreference(findPreference("bridgesShowH"));
		final Preference unavailablePref = findPreference("arrowKeysUnavailable");
		final int capabilityId = getResources().getIdentifier(
				whichBackend + "_arrows_capable", "bool", getPackageName());
		if (capabilityId <= 0 || getResources().getBoolean(capabilityId)) {
			thisGameCategory.removePreference(unavailablePref);
			final Configuration configuration = getResources().getConfiguration();
			final CheckBoxPreference arrowKeysPref = new CheckBoxPreference(this);
			arrowKeysPref.setOrder(-1);
			arrowKeysPref.setKey(GamePlay.getArrowKeysPrefName(whichBackend, configuration));
			arrowKeysPref.setDefaultValue(GamePlay.getArrowKeysDefault(whichBackend, getResources(), getPackageName()));
			arrowKeysPref.setTitle(MessageFormat.format(getString(R.string.arrowKeysIn), getString(nameId)));
			thisGameCategory.addPreference(arrowKeysPref);
		} else {
			unavailablePref.setSummary(MessageFormat.format(getString(R.string.arrowKeysUnavailableIn), getString(nameId)));
		}
		updateSummary((ListPreference) findPreference(GamePlay.ORIENTATION_KEY));
		findPreference("about_content").setSummary(
				String.format(getString(R.string.about_content),
						Utils.getVersion(this)));
		// getSupportActionBar() not available from PreferenceActivity
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
			@SuppressLint("AppCompatMethod") final ActionBar actionBar = getActionBar();
			if (actionBar != null) {
				actionBar.setDisplayHomeAsUpEnabled(true);
			}
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
