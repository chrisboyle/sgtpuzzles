package name.boyle.chris.sgtpuzzles.config

import android.content.Context
import android.widget.EditText
import android.widget.TableRow
import android.widget.TextView
import name.boyle.chris.sgtpuzzles.BackendName
import name.boyle.chris.sgtpuzzles.GameLaunch
import name.boyle.chris.sgtpuzzles.UsedByJNI

interface ConfigBuilder {

    enum class Event(@JvmField val jni: Int) {
        CFG_SETTINGS(0), CFG_SEED(1), CFG_DESC(2), CFG_PREFS(3);
        companion object {
            fun fromJNI(jni: Int) = values().first { it.jni == jni }
        }
    }

    data class TextRowParts(val row: TableRow, val label: TextView, val editText: EditText)

    @UsedByJNI
    fun setTitle(title: String)

    @UsedByJNI
    fun addString(whichEvent: Int, kw: String, name: String, value: String)

    @UsedByJNI
    fun addBoolean(whichEvent: Int, kw: String, name: String, checked: Boolean)

    @UsedByJNI
    fun addChoices(whichEvent: Int, kw: String, name: String, choiceList: String, choiceKWList: String, selection: Int)

    @UsedByJNI
    fun dialogShow()

    interface ActivityCallbacks {
        fun startGame(launch: GameLaunch)
        fun customDialogError(error: String?)
    }

    @UsedByJNI
    interface EngineCallbacks {
        fun configEvent(
            activityCallbacks: ActivityCallbacks,
            whichEvent: Int,
            context: Context,
            backendName: BackendName
        )

        fun configSetString(itemPtr: String, s: String, isPrefs: Boolean)
        fun configSetBool(itemPtr: String, selected: Boolean, isPrefs: Boolean)
        fun configSetChoice(itemPtr: String, selected: Int, isPrefs: Boolean)
        fun configCancel()
        val fullGameIDFromDialog: String
        val fullSeedFromDialog: String
        fun configOK(): String
    }
}