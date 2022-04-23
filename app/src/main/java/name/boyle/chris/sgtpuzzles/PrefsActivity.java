package name.boyle.chris.sgtpuzzles;

import android.content.res.Configuration;
import android.os.Bundle;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.app.ActionBar;
import androidx.fragment.app.Fragment;
import androidx.preference.Preference;
import androidx.preference.PreferenceCategory;
import androidx.preference.PreferenceFragmentCompat;
import androidx.preference.SwitchPreferenceCompat;

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
				Objects.requireNonNull(pref.getFragment()));
		fragment.setArguments(args);
		getSupportFragmentManager().beginTransaction()
				.replace(android.R.id.content, fragment)
				.addToBackStack(null)
				.commit();
		setTitle(pref.getTitle());
		return true;
	}

	public static class PrefsMainFragment extends PreferenceFragmentCompat {
		static final String BACKEND_EXTRA = "backend";

		@Override
		public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
			addPreferencesFromResource(R.xml.preferences);
			@Nullable final BackendName whichBackend = BackendName.byLowerCase(requireActivity().getIntent().getStringExtra(BACKEND_EXTRA));
			final PreferenceCategory chooserCategory = requirePreference(PrefsConstants.CATEGORY_CHOOSER);
			final PreferenceCategory thisGameCategory = requirePreference(PrefsConstants.CATEGORY_THIS_GAME);
			if (whichBackend == null) {
				getPreferenceScreen().removePreference(thisGameCategory);
			} else {
				getPreferenceScreen().removePreference(chooserCategory);
				final String packageName = requireContext().getPackageName();
				thisGameCategory.setTitle(whichBackend.getDisplayName());
				if (whichBackend != BackendName.BRIDGES) thisGameCategory.removePreference(requirePreference(PrefsConstants.BRIDGES_SHOW_H_KEY));
				if (whichBackend != BackendName.UNEQUAL) thisGameCategory.removePreference(requirePreference(PrefsConstants.UNEQUAL_SHOW_H_KEY));
				final Preference unavailablePref = requirePreference(PrefsConstants.PLACEHOLDER_NO_ARROWS);
				final int capabilityId = getResources().getIdentifier(
						whichBackend + "_arrows_capable", "bool", packageName);
				if (capabilityId <= 0 || getResources().getBoolean(capabilityId)) {
					thisGameCategory.removePreference(unavailablePref);
					final Configuration configuration = getResources().getConfiguration();
					final SwitchPreferenceCompat arrowKeysPref = new SwitchPreferenceCompat(requireContext());
					arrowKeysPref.setOrder(-1);
					arrowKeysPref.setIconSpaceReserved(false);
					arrowKeysPref.setKey(GamePlay.getArrowKeysPrefName(whichBackend, configuration));
					arrowKeysPref.setDefaultValue(GamePlay.getArrowKeysDefault(whichBackend, getResources(), packageName));
					arrowKeysPref.setTitle(MessageFormat.format(getString(R.string.arrowKeysIn), whichBackend.getDisplayName()));
					thisGameCategory.addPreference(arrowKeysPref);
				} else {
					unavailablePref.setSummary(MessageFormat.format(getString(R.string.arrowKeysUnavailableIn), whichBackend.getDisplayName()));
				}
			}
			final Preference aboutPref = requirePreference("about_content");
			aboutPref.setSummary(
					String.format(getString(R.string.about_content), BuildConfig.VERSION_NAME));
			aboutPref.setOnPreferenceClickListener(preference -> {
				Utils.copyVersionToClipboard(requireContext());
				return true;
			});
			requirePreference(PrefsConstants.PLACEHOLDER_SEND_FEEDBACK).setOnPreferenceClickListener(p -> { Utils.sendFeedbackDialog(getContext()); return true; });
		}

		@NonNull
		public <T extends Preference> T requirePreference(@NonNull CharSequence key) {
			return Objects.requireNonNull(findPreference(key));
		}
	}

	public static class PrefsDisplayAndInputFragment extends PreferenceFragmentCompat {
		@Override
		public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
			setPreferencesFromResource(R.xml.prefs_display_and_input, rootKey);
		}
	}
}
