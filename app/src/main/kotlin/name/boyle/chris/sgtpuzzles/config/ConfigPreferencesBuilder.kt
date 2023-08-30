package name.boyle.chris.sgtpuzzles.config

import android.content.Context
import androidx.preference.EditTextPreference
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreferenceCompat
import name.boyle.chris.sgtpuzzles.R
import name.boyle.chris.sgtpuzzles.Utils.listFromSeparated
import name.boyle.chris.sgtpuzzles.backend.GameEngine
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CATEGORY_THIS_GAME
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CATEGORY_THIS_GAME_DISPLAY_AND_INPUT

class ConfigPreferencesBuilder(
    private val category: PreferenceCategory,
    private val context: Context,
    private val gameEngine: GameEngine
) : ConfigBuilder {

    override fun setTitle(title: String) {
        // Ignore the title for the preferences screen as there's nowhere to put it
    }

    override fun dialogShow() {
        // The prefs screen is already shown
    }

    private fun isInThisCategory(kw: String) = ((CATEGORIES[kw] ?: CATEGORY_THIS_GAME) == category.key)

    override fun addString(whichEvent: Int, kw: String, name: String, value: String) {
        if (!isInThisCategory(kw)) return
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
        if (!isInThisCategory(kw)) return
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
        if (!isInThisCategory(kw)) return
        val choices = listFromSeparated(choiceList).toTypedArray()
        val choiceKWs = listFromSeparated(choiceKWList).toTypedArray()
        addPreference(kw, name, ListPreference(context).apply {
            entries = choices
            entryValues = choiceKWs
            value = choiceKWs[selection]
            dialogTitle = name
            summaryProvider = ListPreference.SimpleSummaryProvider.getInstance()
            setOnPreferenceChangeListener { _, newVal ->
                gameEngine.configSetChoice(name, entryValues.indexOf(newVal), true)
                gameEngine.savePrefs(context)
                true
            }
        })
        if (kw == "flash-type") {
            category.addPreference(Preference(context).apply {
                setSummary(R.string.flashTypeNote)
                isPersistent = false
                isSelectable = false
                isIconSpaceReserved = false
                order = orderCounter++
            })
        }
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

    companion object {
        private val CATEGORIES = mapOf("one-key-shortcuts" to CATEGORY_THIS_GAME_DISPLAY_AND_INPUT)
    }
}