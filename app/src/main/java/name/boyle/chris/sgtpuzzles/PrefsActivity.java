package name.boyle.chris.sgtpuzzles;

import android.app.backup.BackupManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.content.res.Configuration;
import android.os.Bundle;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.ActionBar;
import androidx.fragment.app.Fragment;
import androidx.preference.CheckBoxPreference;
import androidx.preference.ListPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceCategory;
import androidx.preference.PreferenceFragmentCompat;
import androidx.recyclerview.widget.RecyclerView;

import android.widget.Toast;

import java.text.MessageFormat;
import java.util.Objects;

public class PrefsActivity extends AppCompatActivity implements PreferenceFragmentCompat.OnPreferenceStartFragmentCallback
{
	private static final String TITLE_TAG = "prefsActivityTitle";

	@Override
	protected void onCreate(@Nullable Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		if (savedInstanceState == null) {
			getSupportFragmentManager().beginTransaction().replace(android.R.id.content, new PrefsMainFragment()).commit();
		} else {
			setTitle(savedInstanceState.getCharSequence(TITLE_TAG));
		}
		getSupportFragmentManager().addOnBackStackChangedListener(() -> {
				if (getSupportFragmentManager().getBackStackEntryCount() == 0) {
					setTitle(R.string.Settings);
				}
			});
		final ActionBar actionBar = getSupportActionBar();
		if (actionBar != null) actionBar.setDisplayHomeAsUpEnabled(true);
	}

	@Override
	public void onSaveInstanceState(@NonNull Bundle outState) {
		super.onSaveInstanceState(outState);
		outState.putCharSequence(TITLE_TAG, getTitle());
	}

	@Override
	public boolean onSupportNavigateUp() {
		if (getSupportFragmentManager().popBackStackImmediate()) {
			return true;
		}
		return super.onSupportNavigateUp();
	}

	@Override
	public boolean onPreferenceStartFragment(@NonNull PreferenceFragmentCompat caller, Preference pref) {
		final Bundle args = pref.getExtras();
		final Fragment fragment = getSupportFragmentManager().getFragmentFactory().instantiate(
				getClassLoader(),
				pref.getFragment());
		fragment.setArguments(args);
		fragment.setTargetFragment(caller, 0);
		getSupportFragmentManager().beginTransaction()
				.replace(android.R.id.content, fragment)
				.addToBackStack(null)
				.commit();
		setTitle(pref.getTitle());
		return true;
	}

	private static abstract class PrefsFragmentWithListPrefSummaries extends PreferenceFragmentCompat implements OnSharedPreferenceChangeListener {
		@Override
		public void onResume()
		{
			super.onResume();
			Objects.requireNonNull(getPreferenceScreen().getSharedPreferences()).registerOnSharedPreferenceChangeListener(this);
		}

		@Override
		public void onPause()
		{
			super.onPause();
			Objects.requireNonNull(getPreferenceScreen().getSharedPreferences()).unregisterOnSharedPreferenceChangeListener(this);
		}

		@Override
		public void onSharedPreferenceChanged(SharedPreferences prefs, String key)
		{
			Preference p = findPreference(key);
			if (p instanceof ListPreference) updateSummary((ListPreference)p);
		}

		void updateSummary(ListPreference lp)
		{
			lp.setSummary(lp.getEntry());
			final RecyclerView listView = getListView();
			if (listView != null) listView.postInvalidate();
		}
	}

	public static class PrefsMainFragment extends PrefsFragmentWithListPrefSummaries {
		static final String BACKEND_EXTRA = "backend";

		private BackupManager backupManager = null;

		@Override
		public void onCreate(@Nullable Bundle savedInstanceState) {
			super.onCreate(savedInstanceState);
			backupManager = new BackupManager(getContext());
		}

		@Override
		public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
			addPreferencesFromResource(R.xml.preferences);
			@Nullable final BackendName whichBackend = BackendName.byLowerCase(getActivity().getIntent().getStringExtra(BACKEND_EXTRA));
			final PreferenceCategory chooserCategory = findPreference("gameChooser");
			final PreferenceCategory thisGameCategory = findPreference("thisGame");
			if (whichBackend == null) {
				getPreferenceScreen().removePreference(thisGameCategory);
				updateSummary(findPreference(GameChooser.CHOOSER_STYLE_KEY));
			} else {
				getPreferenceScreen().removePreference(chooserCategory);
				final int nameId = getResources().getIdentifier("name_" + whichBackend, "string", getContext().getPackageName());
				thisGameCategory.setTitle(nameId);
				if (whichBackend != BackendName.BRIDGES) thisGameCategory.removePreference(findPreference("bridgesShowH"));
				if (whichBackend != BackendName.UNEQUAL) thisGameCategory.removePreference(findPreference("unequalShowH"));
				final Preference unavailablePref = findPreference("arrowKeysUnavailable");
				final int capabilityId = getResources().getIdentifier(
						whichBackend + "_arrows_capable", "bool", getContext().getPackageName());
				if (capabilityId <= 0 || getResources().getBoolean(capabilityId)) {
					thisGameCategory.removePreference(unavailablePref);
					final Configuration configuration = getResources().getConfiguration();
					final CheckBoxPreference arrowKeysPref = new CheckBoxPreference(getContext());
					arrowKeysPref.setOrder(-1);
					arrowKeysPref.setKey(GamePlay.getArrowKeysPrefName(whichBackend, configuration));
					arrowKeysPref.setDefaultValue(GamePlay.getArrowKeysDefault(whichBackend, getResources(), getContext().getPackageName()));
					arrowKeysPref.setTitle(MessageFormat.format(getString(R.string.arrowKeysIn), getString(nameId)));
					thisGameCategory.addPreference(arrowKeysPref);
				} else {
					unavailablePref.setSummary(MessageFormat.format(getString(R.string.arrowKeysUnavailableIn), getString(nameId)));
				}
			}
			updateSummary(findPreference(GamePlay.ORIENTATION_KEY));
			updateSummary(findPreference(NightModeHelper.NIGHT_MODE_KEY));
			final Preference aboutPref = findPreference("about_content");
			aboutPref.setSummary(
					String.format(getString(R.string.about_content), BuildConfig.VERSION_NAME));
			aboutPref.setOnPreferenceClickListener(preference -> {
				final ClipData data = ClipData.newPlainText(getString(R.string.version_copied_label), MessageFormat.format(getString(R.string.version_for_clipboard), BuildConfig.VERSION_NAME));
				((ClipboardManager) getContext().getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(data);
				Toast.makeText(getContext(), R.string.version_copied, Toast.LENGTH_SHORT).show();
				return true;
			});
			findPreference("send_feedback").setOnPreferenceClickListener(p -> { Utils.sendFeedbackDialog(getContext()); return true; });
		}

		@Override
		public void onPause() {
			super.onPause();
			backupManager.dataChanged();
		}
	}

	public static class PrefsDisplayAndInputFragment extends PrefsFragmentWithListPrefSummaries {
		@Override
		public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
			setPreferencesFromResource(R.xml.prefs_display_and_input, rootKey);
			updateSummary(findPreference(GamePlay.LIMIT_DPI_KEY));
			updateSummary(findPreference(GamePlay.MOUSE_LONG_PRESS_KEY));
		}
	}
}
