package name.boyle.chris.sgtpuzzles.config

import android.content.Context
import android.util.Log
import androidx.preference.EditTextPreference
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.SwitchPreferenceCompat
import name.boyle.chris.sgtpuzzles.Utils.listFromSeparated

class ConfigPreferencesBuilder(private val category: PreferenceCategory, private val context: Context) : ConfigBuilder {

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
        })
    }

    override fun addBoolean(whichEvent: Int, kw: String, name: String, checked: Boolean) {
        addPreference(kw, name, SwitchPreferenceCompat(context).apply { isChecked = checked })
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
        })
    }

    private fun addPreference(kw: String, name: String, preference: Preference) {
        category.addPreference(preference.apply {
            key = kw
            isPersistent = false
            order = -1
            title = name
            isIconSpaceReserved = false
            setOnPreferenceChangeListener { _, newVal ->
                Log.d("Prefs", "$kw $newVal")
                true
            }
        })
    }
}