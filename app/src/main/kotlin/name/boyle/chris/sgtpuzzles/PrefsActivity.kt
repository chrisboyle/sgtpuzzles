package name.boyle.chris.sgtpuzzles

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import name.boyle.chris.sgtpuzzles.BackendName.Companion.byLowerCase
import name.boyle.chris.sgtpuzzles.config.CustomDialogBuilder.Event.CFG_PREFS
import name.boyle.chris.sgtpuzzles.Utils.copyVersionToClipboard
import name.boyle.chris.sgtpuzzles.Utils.sendFeedbackDialog
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

    class PrefsMainFragment : PreferenceFragmentCompat() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            addPreferencesFromResource(R.xml.preferences)
            val whichBackend = byLowerCase(requireActivity().intent.getStringExtra(BACKEND_EXTRA))
            val chooserCategory =
                requirePreference<PreferenceCategory>(PrefsConstants.CATEGORY_CHOOSER)
            val thisGameCategory =
                requirePreference<PreferenceCategory>(PrefsConstants.CATEGORY_THIS_GAME)
            if (whichBackend == null) {
                preferenceScreen.removePreference(thisGameCategory)
            } else {
                preferenceScreen.removePreference(chooserCategory)
                thisGameCategory.title = whichBackend.displayName
                addGameSpecificAndroidPreferences(whichBackend, thisGameCategory)
                addNativePreferences(whichBackend, thisGameCategory)
            }
            requirePreference<Preference>("about_content").apply {
                summary = String.format(getString(R.string.about_content), BuildConfig.VERSION_NAME)
                setOnPreferenceClickListener {
                    copyVersionToClipboard(requireContext())
                    true
                }
            }
            requirePreference<Preference>(PrefsConstants.PLACEHOLDER_SEND_FEEDBACK).setOnPreferenceClickListener {
                sendFeedbackDialog(requireContext())
                true
            }
        }

        private fun addGameSpecificAndroidPreferences(
            whichBackend: BackendName,
            thisGameCategory: PreferenceCategory
        ) {
            if (!whichBackend.isLatin) thisGameCategory.removePreference(
                requirePreference(PrefsConstants.LATIN_SHOW_M_KEY)
            )
            if (whichBackend !== BRIDGES) thisGameCategory.removePreference(
                requirePreference(PrefsConstants.BRIDGES_SHOW_H_KEY)
            )
            if (whichBackend !== UNEQUAL) thisGameCategory.removePreference(
                requirePreference(PrefsConstants.UNEQUAL_SHOW_H_KEY)
            )
            val unavailablePref =
                requirePreference<Preference>(PrefsConstants.PLACEHOLDER_NO_ARROWS)
            if (whichBackend.isArrowsCapable) {
                thisGameCategory.removePreference(unavailablePref)
                SwitchPreferenceCompat(requireContext()).apply {
                    order = 0
                    isIconSpaceReserved = false
                    key = GamePlay.getArrowKeysPrefName(whichBackend, resources.configuration)
                    setDefaultValue(GamePlay.getArrowKeysDefault(whichBackend, resources))
                    title = MessageFormat.format(
                        getString(R.string.arrowKeysIn),
                        whichBackend.displayName
                    )
                    thisGameCategory.addPreference(this)
                }
            } else {
                unavailablePref.summary = MessageFormat.format(
                    getString(R.string.arrowKeysUnavailableIn),
                    whichBackend.displayName
                )
            }
        }

        private fun addNativePreferences(
            whichBackend: BackendName,
            thisGameCategory: PreferenceCategory
        ) {
            GameEngineImpl.forPreferencesOnly(whichBackend).configEvent(null, CFG_PREFS.jni, context, whichBackend)

//            thisGameCategory.addPreference(SwitchPreferenceCompat(requireContext()).apply {
//                title = "Hello world"
//                order = -1
//                isIconSpaceReserved = false
//                isPersistent = false
//                setOnPreferenceChangeListener { _, newVal ->
//                    Log.d("Prefs", "checked $newVal")
//                    true
//                }
//            })
        }

            private fun <T : Preference> requirePreference(key: CharSequence): T {
            return findPreference(key)!!
        }

        companion object {
            const val BACKEND_EXTRA = "backend"
        }
    }

    class PrefsDisplayAndInputFragment : PreferenceFragmentCompat() {
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            setPreferencesFromResource(R.xml.prefs_display_and_input, rootKey)
        }
    }

    companion object {
        private const val TITLE_TAG = "prefsActivityTitle"
    }
}