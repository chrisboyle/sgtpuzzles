package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.content.DialogInterface
import android.text.InputType.TYPE_CLASS_NUMBER
import android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
import android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
import android.view.View
import android.widget.CheckBox
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.widget.AppCompatTextView
import name.boyle.chris.sgtpuzzles.CustomDialogBuilder.Event.CFG_DESC
import name.boyle.chris.sgtpuzzles.CustomDialogBuilder.Event.CFG_SEED
import name.boyle.chris.sgtpuzzles.CustomDialogBuilder.Event.CFG_SETTINGS
import name.boyle.chris.sgtpuzzles.GameLaunch.Companion.fromSeed
import name.boyle.chris.sgtpuzzles.GameLaunch.Companion.ofGameID
import name.boyle.chris.sgtpuzzles.GameLaunch.Companion.toGenerate
import name.boyle.chris.sgtpuzzles.GameLaunch.Origin
import kotlin.math.sqrt

class CustomDialogBuilder private constructor(
    private val dialogBuilder: AlertDialog.Builder,
    private val engine: EngineCallbacks,
    private val activity: ActivityCallbacks,
    private val dialogEvent: Event,
    title: String,
    private val backend: BackendName
) : ConfigBuilder(dialogBuilder.context, engine) {

    enum class Event(@JvmField val jni: Int) {
        CFG_SETTINGS(0), CFG_SEED(1), CFG_DESC(2), CFG_PREFS(3);
        companion object {
            fun fromJNI(jni: Int) = values().first { it.jni == jni }
        }
    }

    interface ActivityCallbacks {
        fun startGame(launch: GameLaunch)
        fun customDialogError(error: String?)
    }

    // Make dialogBuilder first in order to pass the themed context it creates to our superclass
    @UsedByJNI
    constructor(
        nonThemedContext: Context,
        engine: EngineCallbacks,
        activity: ActivityCallbacks,
        dialogEvent: Int,
        title: String,
        backend: BackendName
    ) : this(AlertDialog.Builder(nonThemedContext), engine, activity, Event.fromJNI(dialogEvent), title, backend)

    init {
        dialogBuilder.apply {
            setTitle(title)
            setView(ScrollView(this.context).apply { addView(table) })
            setOnCancelListener { engine.configCancel() }
            setPositiveButton(android.R.string.ok) { dialog: DialogInterface, _: Int ->
                configApply()
                try {
                    val launch: GameLaunch = when (dialogEvent) {
                        CFG_DESC -> ofGameID(
                            backend, engine.fullGameIDFromDialog, Origin.CUSTOM_DIALOG
                        )

                        CFG_SEED -> fromSeed(
                            backend, engine.fullSeedFromDialog
                        )

                        else -> toGenerate(
                            backend, engine.configOK(), Origin.CUSTOM_DIALOG
                        )
                    }
                    activity.startGame(launch)
                    dialog.dismiss()
                } catch (e: IllegalArgumentException) {
                    activity.customDialogError(e.message)
                }
            }
            if (dialogEvent == CFG_SETTINGS) {
                setNegativeButton(R.string.Game_ID_) { _: DialogInterface?, _: Int ->
                    engine.configCancel()
                    engine.configEvent(
                        activity, CFG_DESC.jni, context, backend
                    )
                }
                setNeutralButton(R.string.Seed_) { _: DialogInterface?, _: Int ->
                    engine.configCancel()
                    engine.configEvent(
                        activity, CFG_SEED.jni, context, backend
                    )
                }
            }
        }
    }

    @UsedByJNI
    override fun addString(whichEvent: Int, name: String, value: String): TextRowParts =
        super.addString(whichEvent, name, value).apply {
            if (dialogEvent == CFG_SEED && value.indexOf('#') == value.length - 1) {
                table.addView(AppCompatTextView(table.context).apply { setText(R.string.seedWarning) })
            }
            when (name) {
                COLUMNS_OF_SUB_BLOCKS -> {
                    label.tag = COLS_LABEL_TAG
                    editText.tag = COLS_EDITTEXT_TAG
                }
                ROWS_OF_SUB_BLOCKS -> {
                    row.tag = ROWS_ROW_TAG
                    editText.tag = ROWS_EDITTEXT_TAG
                }
            }
            // Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
            // Uglier temporary-er hack: Black Box must accept a range for ball count.
            if (whichEvent == CFG_SETTINGS.jni && backend !== BLACKBOX) {
                editText.inputType =
                    (TYPE_CLASS_NUMBER or TYPE_NUMBER_FLAG_DECIMAL or TYPE_NUMBER_FLAG_SIGNED)
            }
        }

    @UsedByJNI
    override fun addBoolean(whichEvent: Int, name: String, selected: Boolean): CheckBox =
        super.addBoolean(whichEvent, name, selected).apply {
            if (backend === SOLO && name.startsWith("Jigsaw")) {
                // FIXME not happening
                jigsawHack(this)
                setOnClickListener { jigsawHack(this) }
            }
        }

    private fun jigsawHack(jigsawCheckbox: CheckBox) {
        val colTV = table.findViewWithTag<View>(COLS_EDITTEXT_TAG)
        val rowTV = table.findViewWithTag<View>(ROWS_EDITTEXT_TAG)
        if (colTV is TextView && rowTV is TextView) {
            val cols = colTV.intValue ?: 3
            val rows = rowTV.intValue ?: 3
            if (jigsawCheckbox.isChecked) {
                colTV.intValue = cols * rows
                rowTV.intValue = 1
                rowTV.isEnabled = false
                table.findViewWithTag<TextView>(COLS_LABEL_TAG).setText(R.string.Size_of_sub_blocks)
                table.findViewWithTag<View>(ROWS_ROW_TAG).visibility = View.INVISIBLE
            } else {
                if (rows == 1) {
                    val size = sqrt(cols.toDouble()).toInt().coerceAtLeast(2)
                    colTV.intValue = size
                    rowTV.intValue = size
                }
                rowTV.isEnabled = true
                table.findViewWithTag<TextView>(COLS_LABEL_TAG).setText(R.string.Columns_of_sub_blocks)
                table.findViewWithTag<View>(ROWS_ROW_TAG).visibility = View.VISIBLE
            }
        }
    }

    @UsedByJNI
    fun dialogShow() {
        dialogBuilder.create().show()
    }

    companion object {
        private const val COLUMNS_OF_SUB_BLOCKS = "Columns of sub-blocks"
        private const val ROWS_OF_SUB_BLOCKS = "Rows of sub-blocks"
        private const val COLS_EDITTEXT_TAG = "colsEditText"
        private const val ROWS_EDITTEXT_TAG = "rowsEditText"
        private const val COLS_LABEL_TAG = "colLabel"
        private const val ROWS_ROW_TAG = "rowsRow"
    }
}