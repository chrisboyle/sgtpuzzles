package name.boyle.chris.sgtpuzzles

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import name.boyle.chris.sgtpuzzles.Utils.copyVersionToClipboard
import name.boyle.chris.sgtpuzzles.Utils.sendFeedbackDialog
import name.boyle.chris.sgtpuzzles.backend.BRIDGES
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.BackendName.Companion.byLowerCase
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder.Event.CFG_PREFS
import name.boyle.chris.sgtpuzzles.config.ConfigPreferencesBuilder
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.BRIDGES_SHOW_H_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CATEGORY_CHOOSER
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CATEGORY_THIS_GAME
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CATEGORY_THIS_GAME_DISPLAY_AND_INPUT
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.LATIN_SHOW_M_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.PLACEHOLDER_NO_ARROWS
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.PLACEHOLDER_SEND_FEEDBACK
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.UNEQUAL_SHOW_H_KEY
import java.text.MessageFormat

class PrefsActivity : AppCompatActivity(),
    PreferenceFragmentCompat.OnPreferenceStartFragmentCallback {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) {
            supportFragmentManager.beginTransaction()
                .replace(android.R.id.content, PrefsMainFragment())
                .commit()
        } else {
            title = savedInstanceState.getCharSequence(TITLE_TAG)
        }
        supportFragmentManager.addOnBackStackChangedListener {
            if (supportFragmentManager.backStackEntryCount == 0) {
                setTitle(R.string.Settings)
            }
        }
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
    }

    public override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putCharSequence(TITLE_TAG, title)
    }

    override fun onSupportNavigateUp(): Boolean {
        return supportFragmentManager.popBackStackImmediate() || super.onSupportNavigateUp()
    }

    override fun onPreferenceStartFragment(
        caller: PreferenceFragmentCompat,
        pref: Preference
    ): Boolean {
        val args = pref.extras
        val fragment = supportFragmentManager.fragmentFactory.instantiate(
            classLoader,
            pref.fragment!!
        )
        fragment.arguments = args
        supportFragmentManager.beginTransaction()
            .replace(android.R.id.content, fragment)
            .addToBackStack(null)
            .commit()
        title = pref.title
        return true
    }

    abstract class CurrentGamePrefFragment : PreferenceFragmentCompat() {
        fun onCreateCurrentGamePrefs(backend: BackendName?, thisGameCategory: PreferenceCategory) {
            if (backend == null) {
                preferenceScreen.removePreference(thisGameCategory)
            } else {
                thisGameCategory.title = backend.displayName
                with(GameEngineImpl.forPreferencesOnly(backend, requireContext())) {
                    configEvent(
                        CFG_PREFS.jni,
                        ConfigPreferencesBuilder(thisGameCategory, requireContext(), this)
                    )
                }
            }
        }

        protected fun <T : Preference> requirePreference(key: CharSequence): T {
            return findPreference(key)!!
        }
    }

    class PrefsMainFragment : CurrentGamePrefFragment() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            addPreferencesFromResource(R.xml.preferences)
            val backend = byLowerCase(requireActivity().intent.getStringExtra(BACKEND_EXTRA))
            val chooserCategory = requirePreference<PreferenceCategory>(CATEGORY_CHOOSER)
            val thisGameCategory = requirePreference<PreferenceCategory>(CATEGORY_THIS_GAME)
            onCreateCurrentGamePrefs(backend, thisGameCategory)
            if (backend != null) {
                preferenceScreen.removePreference(chooserCategory)
                addGameSpecificAndroidPreferences(backend, thisGameCategory)
            }
            requirePreference<Preference>("about_content").apply {
                summary = String.format(getString(R.string.about_content), BuildConfig.VERSION_NAME)
                setOnPreferenceClickListener {
                    copyVersionToClipboard(requireContext())
                    true
                }
            }
            requirePreference<Preference>(PLACEHOLDER_SEND_FEEDBACK).setOnPreferenceClickListener {
                sendFeedbackDialog(requireContext())
                true
            }
        }

        private fun addGameSpecificAndroidPreferences(
            whichBackend: BackendName,
            thisGameCategory: PreferenceCategory
        ) {
            if (!whichBackend.isLatin) thisGameCategory.removePreference(
                requirePreference(LATIN_SHOW_M_KEY)
            )
            if (whichBackend !== BRIDGES) thisGameCategory.removePreference(
                requirePreference(BRIDGES_SHOW_H_KEY)
            )
            if (whichBackend !== UNEQUAL) thisGameCategory.removePreference(
                requirePreference(UNEQUAL_SHOW_H_KEY)
            )
            val unavailablePref =
                requirePreference<Preference>(PLACEHOLDER_NO_ARROWS)
            if (whichBackend.isArrowsCapable) {
                thisGameCategory.removePreference(unavailablePref)
                SwitchPreferenceCompat(requireContext()).apply {
                    order = 1000  // after upstream prefs, before XML prefs
                    isIconSpaceReserved = false
                    key = GamePlay.getArrowKeysPrefName(whichBackend, resources.configuration)
                    setDefaultValue(GamePlay.getArrowKeysDefault(whichBackend, resources))
                    setTitle(R.string.showArrowKeys)
                    thisGameCategory.addPreference(this)
                }
            } else {
                unavailablePref.summary = MessageFormat.format(
                    getString(R.string.arrowKeysUnavailableIn),
                    whichBackend.displayName
                )
            }
        }

        companion object {
            const val BACKEND_EXTRA = "backend"
        }
    }

    class PrefsDisplayAndInputFragment : CurrentGamePrefFragment() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.prefs_display_and_input, rootKey)
            val backend = byLowerCase(requireActivity().intent.getStringExtra(PrefsMainFragment.BACKEND_EXTRA))
            val thisGameCategory = requirePreference<PreferenceCategory>(CATEGORY_THIS_GAME_DISPLAY_AND_INPUT)
            onCreateCurrentGamePrefs(backend, thisGameCategory)
            if (thisGameCategory.preferenceCount == 0) {
                preferenceScreen.removePreference(thisGameCategory)
            }
        }
    }

    companion object {
        private const val TITLE_TAG = "prefsActivityTitle"
    }
}
