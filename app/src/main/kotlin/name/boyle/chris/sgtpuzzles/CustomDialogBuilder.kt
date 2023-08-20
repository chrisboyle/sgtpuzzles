package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.content.DialogInterface
import android.text.InputType.TYPE_CLASS_NUMBER
import android.text.InputType.TYPE_NUMBER_FLAG_DECIMAL
import android.text.InputType.TYPE_NUMBER_FLAG_SIGNED
import android.util.TypedValue
import android.util.TypedValue.COMPLEX_UNIT_DIP
import android.view.Gravity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TableLayout
import android.widget.TableRow
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.widget.AppCompatCheckBox
import androidx.appcompat.widget.AppCompatEditText
import androidx.appcompat.widget.AppCompatSpinner
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.AccessibilityDelegateCompat
import androidx.core.view.ViewCompat
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat
import name.boyle.chris.sgtpuzzles.GameLaunch.Companion.fromSeed
import name.boyle.chris.sgtpuzzles.GameLaunch.Companion.ofGameID
import name.boyle.chris.sgtpuzzles.GameLaunch.Companion.toGenerate
import java.util.StringTokenizer
import kotlin.math.sqrt

class CustomDialogBuilder @UsedByJNI constructor(
    context: Context,
    private val engine: EngineCallbacks,
    private val activity: ActivityCallbacks,
    private val dialogEvent: Int,
    title: String,
    private val backend: BackendName
) {
    private val dialogBuilder: AlertDialog.Builder
    private val dialogIds = ArrayList<String>()
    private val dialogLayout: TableLayout

    @UsedByJNI
    interface EngineCallbacks {
        fun configEvent(
            activityCallbacks: ActivityCallbacks,
            whichEvent: Int,
            context: Context,
            backendName: BackendName
        )

        fun configSetString(itemPtr: String, s: String)
        fun configSetBool(itemPtr: String, selected: Int)
        fun configSetChoice(itemPtr: String, selected: Int)
        fun configCancel()
        val fullGameIDFromDialog: String
        val fullSeedFromDialog: String
        fun configOK(): String
    }

    interface ActivityCallbacks {
        fun startGame(launch: GameLaunch)
        fun customDialogError(error: String?)
    }

    init {
        dialogBuilder = AlertDialog.Builder(context).apply {
            setTitle(title)
            dialogLayout = TableLayout(this.context).apply {
                val xPadding = context.resources.getDimensionPixelSize(R.dimen.dialog_padding_horizontal)
                val yPadding = context.resources.getDimensionPixelSize(R.dimen.dialog_padding_vertical)
                setPadding(xPadding, yPadding, xPadding, yPadding)
                setColumnShrinkable(0, true)
                setColumnShrinkable(1, true)
                setColumnStretchable(0, true)
                setColumnStretchable(1, true)
            }
            setView(ScrollView(this.context).apply { addView(dialogLayout) })
            setOnCancelListener { engine.configCancel() }
            setPositiveButton(android.R.string.ok) { dialog: DialogInterface, _: Int ->
                for (i in dialogIds) {
                    when (val v = dialogLayout.findViewWithTag<View>(i)) {
                        is EditText -> engine.configSetString(i, v.text.toString())
                        is CheckBox -> engine.configSetBool(i, if (v.isChecked) 1 else 0)
                        is Spinner -> engine.configSetChoice(i, v.selectedItemPosition)
                    }
                }
                try {
                    val launch: GameLaunch = when (dialogEvent) {
                        GamePlay.CFG_DESC -> ofGameID(
                            backend,
                            engine.fullGameIDFromDialog,
                            GameLaunch.Origin.CUSTOM_DIALOG
                        )
                        GamePlay.CFG_SEED -> fromSeed(
                            backend,
                            engine.fullSeedFromDialog
                        )
                        else -> toGenerate(
                            backend,
                            engine.configOK(),
                            GameLaunch.Origin.CUSTOM_DIALOG
                        )
                    }
                    activity.startGame(launch)
                    dialog.dismiss()
                } catch (e: IllegalArgumentException) {
                    activity.customDialogError(e.message)
                }
            }
            if (dialogEvent == GamePlay.CFG_SETTINGS) {
                setNegativeButton(R.string.Game_ID_) { _: DialogInterface?, _: Int ->
                    engine.configCancel()
                    engine.configEvent(
                        activity, GamePlay.CFG_DESC, context, backend
                    )
                }
                setNeutralButton(R.string.Seed_) { _: DialogInterface?, _: Int ->
                    engine.configCancel()
                    engine.configEvent(
                        activity, GamePlay.CFG_SEED, context, backend
                    )
                }
            }
        }
        dialogIds.clear()
    }

    private fun Context.dip(dip: Float) =
        TypedValue.applyDimension(COMPLEX_UNIT_DIP, dip, resources.displayMetrics).toInt()

    private fun attachLabel(labeled: View, label: AppCompatTextView) {
        ViewCompat.setAccessibilityDelegate(labeled, object : AccessibilityDelegateCompat() {
            override fun onInitializeAccessibilityNodeInfo(
                host: View,
                info: AccessibilityNodeInfoCompat
            ) {
                super.onInitializeAccessibilityNodeInfo(host, info)
                info.setLabeledBy(label)
            }
        })
    }

    @UsedByJNI
    fun dialogAddString(whichEvent: Int, name: String, value: String) {
        val context = dialogBuilder.context
        dialogIds.add(name)
        val et = AppCompatEditText(context).apply {
            // Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
            // Uglier temporary-er hack: Black Box must accept a range for ball count.
            if (whichEvent == GamePlay.CFG_SETTINGS && backend !== BLACKBOX) {
                inputType = (TYPE_CLASS_NUMBER or TYPE_NUMBER_FLAG_DECIMAL or TYPE_NUMBER_FLAG_SIGNED)
            }
            tag = name
            setText(value)
            width =
                context.resources.getDimensionPixelSize(if (whichEvent == GamePlay.CFG_SETTINGS) R.dimen.dialog_edit_text_width else R.dimen.dialog_long_edit_text_width)
            minHeight = context.dip(48f)
            setSelectAllOnFocus(true)
        }
        val tv = AppCompatTextView(context).apply {
            text = name
            setPadding(
                0,
                0,
                context.resources.getDimensionPixelSize(R.dimen.dialog_padding_horizontal),
                0
            )
            gravity = Gravity.END
        }
        attachLabel(et, tv)
        val tr = TableRow(context).apply {
            addView(tv)
            addView(et)
            gravity = Gravity.CENTER_VERTICAL
            dialogLayout.addView(this)
        }
        if (whichEvent == GamePlay.CFG_SEED && value.indexOf('#') == value.length - 1) {
            dialogLayout.addView(AppCompatTextView(context).apply { setText(R.string.seedWarning) })
        }
        when (name) {
            COLUMNS_OF_SUB_BLOCKS -> tv.tag = COLS_LABEL_TAG
            ROWS_OF_SUB_BLOCKS -> tr.tag = ROWS_ROW_TAG
        }
    }

    @UsedByJNI
    fun dialogAddBoolean(whichEvent: Int, name: String, selected: Boolean) {
        val context = dialogBuilder.context
        dialogIds.add(name)
        AppCompatCheckBox(context).apply {
            tag = name
            text = name
            isChecked = selected
            minimumHeight = context.dip(48f)
            if (backend === SOLO && name.startsWith("Jigsaw")) {
                jigsawHack(this)
                setOnClickListener { jigsawHack(this) }
            }
            dialogLayout.addView(this)
        }
    }

    private fun jigsawHack(jigsawCheckbox: AppCompatCheckBox) {
        val colTV = dialogLayout.findViewWithTag<View>(COLUMNS_OF_SUB_BLOCKS)
        val rowTV = dialogLayout.findViewWithTag<View>(ROWS_OF_SUB_BLOCKS)
        if (colTV is TextView && rowTV is TextView) {
            val cols = colTV.intValue ?: 3
            val rows = rowTV.intValue ?: 3
            if (jigsawCheckbox.isChecked) {
                colTV.intValue = cols * rows
                rowTV.intValue = 1
                rowTV.isEnabled = false
                dialogLayout.findViewWithTag<TextView>(COLS_LABEL_TAG).setText(R.string.Size_of_sub_blocks)
                dialogLayout.findViewWithTag<View>(ROWS_ROW_TAG).visibility = View.INVISIBLE
            } else {
                if (rows == 1) {
                    val size = sqrt(cols.toDouble()).toInt().coerceAtLeast(2)
                    colTV.intValue = size
                    rowTV.intValue = size
                }
                rowTV.isEnabled = true
                dialogLayout.findViewWithTag<TextView>(COLS_LABEL_TAG).setText(R.string.Columns_of_sub_blocks)
                dialogLayout.findViewWithTag<View>(ROWS_ROW_TAG).visibility = View.VISIBLE
            }
        }
    }

    @UsedByJNI
    fun dialogAddChoices(whichEvent: Int, name: String, value: String, selection: Int) {
        val context = dialogBuilder.context
        val st = StringTokenizer(value.substring(1), value.substring(0, 1))
        val choices = ArrayList<String>()
        while (st.hasMoreTokens()) choices.add(st.nextToken())
        dialogIds.add(name)
        val s = AppCompatSpinner(context).apply {
            tag = name
            adapter = ArrayAdapter(
                context,
                android.R.layout.simple_spinner_item, choices.toTypedArray()
            ).apply<ArrayAdapter<String>> {
                setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            }
            setSelection(selection)
            layoutParams = TableRow.LayoutParams(
                context.resources.getDimensionPixelSize(R.dimen.dialog_spinner_width),
                context.dip(48f)
            )
        }
        val tv = AppCompatTextView(context).apply {
            text = name
            setPadding(
                0,
                0,
                context.resources.getDimensionPixelSize(R.dimen.dialog_padding_horizontal),
                0
            )
            gravity = Gravity.END
        }
        attachLabel(s, tv)
        val tr = TableRow(context).apply {
            addView(tv)
            addView(s)
            gravity = Gravity.CENTER_VERTICAL
        }
        dialogLayout.addView(tr)
    }

    @UsedByJNI
    fun dialogShow() {
        dialogBuilder.create().show()
    }

    companion object {
        private const val COLUMNS_OF_SUB_BLOCKS = "Columns of sub-blocks"
        private const val ROWS_OF_SUB_BLOCKS = "Rows of sub-blocks"
        private const val COLS_LABEL_TAG = "colLabel"
        private const val ROWS_ROW_TAG = "rowsRow"
    }
}