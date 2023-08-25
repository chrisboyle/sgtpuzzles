package name.boyle.chris.sgtpuzzles.config

import android.content.Context
import android.util.TypedValue
import android.util.TypedValue.COMPLEX_UNIT_DIP
import android.view.Gravity
import android.view.View
import android.widget.ArrayAdapter
import android.widget.CheckBox
import android.widget.EditText
import android.widget.TableLayout
import android.widget.TableRow
import android.widget.TextView
import androidx.appcompat.widget.AppCompatCheckBox
import androidx.appcompat.widget.AppCompatEditText
import androidx.appcompat.widget.AppCompatSpinner
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.AccessibilityDelegateCompat
import androidx.core.view.ViewCompat
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat
import name.boyle.chris.sgtpuzzles.BackendName
import name.boyle.chris.sgtpuzzles.config.CustomDialogBuilder.Event.CFG_SETTINGS
import name.boyle.chris.sgtpuzzles.R
import name.boyle.chris.sgtpuzzles.UsedByJNI

/** Expresses midend's config_items as Views. */
abstract class ConfigViewsBuilder(
    private val themedContext: Context,
    private val engine: EngineCallbacks
) {
    @UsedByJNI
    interface EngineCallbacks {
        fun configEvent(
            activityCallbacks: CustomDialogBuilder.ActivityCallbacks,
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

    val table: TableLayout = TableLayout(themedContext).apply {
        val xPadding = context.resources.getDimensionPixelSize(R.dimen.dialog_padding_horizontal)
        val yPadding = context.resources.getDimensionPixelSize(R.dimen.dialog_padding_vertical)
        setPadding(xPadding, yPadding, xPadding, yPadding)
        setColumnShrinkable(0, true)
        setColumnShrinkable(1, true)
        setColumnStretchable(0, true)
        setColumnStretchable(1, true)
    }

    private val onApply = mutableListOf<() -> Unit>()

    fun configApply() {
        onApply.map { it() }
    }

    private fun Context.dip(dip: Float) =
        TypedValue.applyDimension(COMPLEX_UNIT_DIP, dip, resources.displayMetrics).toInt()

    private fun attachLabel(label: AppCompatTextView, labeled: View) {
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

    data class TextRowParts(val row: TableRow, val label: TextView, val editText: EditText)

    @UsedByJNI
    open fun addString(whichEvent: Int, name: String, value: String): TextRowParts {
        val editText = AppCompatEditText(themedContext).apply {
            setText(value)
            width =
                context.resources.getDimensionPixelSize(if (whichEvent == CFG_SETTINGS.jni) R.dimen.dialog_edit_text_width else R.dimen.dialog_long_edit_text_width)
            minHeight = context.dip(48f)
            setSelectAllOnFocus(true)
            onApply += { engine.configSetString(name, text.toString()) }
        }
        val label = AppCompatTextView(themedContext).apply {
            text = name
            setPadding(
                0,
                0,
                context.resources.getDimensionPixelSize(R.dimen.dialog_padding_horizontal),
                0
            )
            gravity = Gravity.END
        }
        attachLabel(label, editText)
        val tr = TableRow(themedContext).apply {
            addView(label)
            addView(editText)
            gravity = Gravity.CENTER_VERTICAL
            table.addView(this)
        }
        return TextRowParts(tr, label, editText)
    }

    @UsedByJNI
    open fun addBoolean(whichEvent: Int, name: String, selected: Boolean): CheckBox {
        return AppCompatCheckBox(themedContext).apply {
            text = name
            isChecked = selected
            minimumHeight = context.dip(48f)
            table.addView(this)
            onApply += { engine.configSetBool(name, if (isChecked) 1 else 0) }
        }
    }

    @UsedByJNI
    fun addChoices(whichEvent: Int, name: String, value: String, selection: Int) {
        val choices = value.drop(1).split(value.take(1))
        val s = AppCompatSpinner(themedContext).apply {
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
            onApply += { engine.configSetChoice(name, selectedItemPosition) }
        }
        val tv = AppCompatTextView(themedContext).apply {
            text = name
            setPadding(
                0,
                0,
                context.resources.getDimensionPixelSize(R.dimen.dialog_padding_horizontal),
                0
            )
            gravity = Gravity.END
        }
        attachLabel(tv, s)
        val tr = TableRow(themedContext).apply {
            addView(tv)
            addView(s)
            gravity = Gravity.CENTER_VERTICAL
        }
        table.addView(tr)
    }
}