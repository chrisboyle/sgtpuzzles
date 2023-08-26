package name.boyle.chris.sgtpuzzles.config

import android.content.Context
import androidx.preference.EditTextPreference
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreferenceCompat
import name.boyle.chris.sgtpuzzles.GameEngine
import name.boyle.chris.sgtpuzzles.Utils.listFromSeparated

class ConfigPreferencesBuilder(private val category: PreferenceCategory, private val context: Context, private val gameEngine: GameEngine) : ConfigBuilder {

    override fun setTitle(title: String) {
        // Ignore the title for the preferences screen as there's nowhere to put it
    }

    override fun dialogShow() {
        // The prefs screen is already shown
    }

    override fun addString(whichEvent: Int, kw: String, name: String, value: String) {
        addPreference(kw, name, EditTextPreference(context).apply {
            text = value
            summaryProvider = EditTextPreference.SimpleSummaryProvider.getInstance()
            setOnPreferenceChangeListener { _, newVal ->
                gameEngine.configSetString(name, newVal.toString(), true)
                gameEngine.savePrefs(context)
                true
            }
        })
    }

    override fun addBoolean(whichEvent: Int, kw: String, name: String, checked: Boolean) {
        addPreference(kw, name, SwitchPreferenceCompat(context).apply {
            isChecked = checked
            setOnPreferenceChangeListener { _, newVal ->
                gameEngine.configSetBool(name, newVal as Boolean, true)
                gameEngine.savePrefs(context)
                true
            }
        })
    }

    override fun addChoices(
        whichEvent: Int,
        kw: String,
        name: String,
        choiceList: String,
        choiceKWList: String,
        selection: Int
    ) {
        val choices = listFromSeparated(choiceList).toTypedArray()
        val choiceKWs = listFromSeparated(choiceKWList).toTypedArray()
        addPreference(kw, name, ListPreference(context).apply {
            entries = choices
            entryValues = choiceKWs
            value = choiceKWs[selection]
            summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
            setOnPreferenceChangeListener { _, newVal ->
                gameEngine.configSetChoice(name, entryValues.indexOf(newVal), true)
                gameEngine.savePrefs(context)
                true
            }
        })
    }

    private var orderCounter = 0

    private fun addPreference(kw: String, name: String, preference: Preference) {
        category.addPreference(preference.apply {
            key = kw
            isPersistent = false
            order = orderCounter++
            title = name
            isIconSpaceReserved = false
        })
    }
}