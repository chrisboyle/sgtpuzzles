package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.widget.AppCompatCheckBox;
import androidx.appcompat.widget.AppCompatEditText;
import androidx.appcompat.widget.AppCompatSpinner;
import androidx.appcompat.widget.AppCompatTextView;
import androidx.core.view.AccessibilityDelegateCompat;
import androidx.core.view.ViewCompat;
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat;

import java.util.ArrayList;
import java.util.Locale;
import java.util.StringTokenizer;

public class CustomDialogBuilder {
    private static final String COLUMNS_OF_SUB_BLOCKS = "Columns of sub-blocks";
    private static final String ROWS_OF_SUB_BLOCKS = "Rows of sub-blocks";
    private static final String COLS_LABEL_TAG = "colLabel";
    private static final String ROWS_ROW_TAG = "rowsRow";

    private final BackendName _currentBackend;
    private final AlertDialog.Builder _dialogBuilder;
    private final int _dialogEvent;
    private final ArrayList<String> _dialogIds = new ArrayList<>();
    private final TableLayout _dialogLayout;
    private final EngineCallbacks _engineCallbacks;
    private final ActivityCallbacks _activityCallbacks;

    @UsedByJNI
    public interface EngineCallbacks {
        void configEvent(ActivityCallbacks activityCallbacks, int whichEvent, Context context, BackendName backendName);
        void configSetString(String item_ptr, String s);
        void configSetBool(String item_ptr, int selected);
        void configSetChoice(String item_ptr, int selected);
        void configCancel();
        String getFullGameIDFromDialog();
        String getFullSeedFromDialog();
        String configOK();
    }

    public interface ActivityCallbacks {
        void startGame(GameLaunch launch);
        void customDialogError(String error);
    }

    @UsedByJNI
    public CustomDialogBuilder(final Context context, final EngineCallbacks engineCallbacks, final ActivityCallbacks activityCallbacks, final int whichEvent, final String title, final BackendName backendName)
    {
        _engineCallbacks = engineCallbacks;
        _activityCallbacks = activityCallbacks;
        _currentBackend = backendName;
        _dialogEvent = whichEvent;
        _dialogBuilder = new AlertDialog.Builder(context)
                .setTitle(title)
                .setOnCancelListener(dialog1 -> engineCallbacks.configCancel())
                .setPositiveButton(android.R.string.ok, (d, whichButton) -> {
                    // do nothing - but must have listener to ensure button is shown
                    // (listener is overridden in dialogShow to prevent dismiss)
                });
        if (whichEvent == GamePlay.CFG_SETTINGS) {
            _dialogBuilder.setNegativeButton(R.string.Game_ID_, (dialog, which) -> {
                engineCallbacks.configCancel();
                engineCallbacks.configEvent(activityCallbacks, GamePlay.CFG_DESC, context, _currentBackend);
            })
                    .setNeutralButton(R.string.Seed_, (dialog, which) -> {
                        engineCallbacks.configCancel();
                        engineCallbacks.configEvent(activityCallbacks, GamePlay.CFG_SEED, context, _currentBackend);
                    });
        }
        final ScrollView sv = new ScrollView(_dialogBuilder.getContext());
        sv.addView(_dialogLayout = new TableLayout(_dialogBuilder.getContext()));
        _dialogBuilder.setView(sv);
        final int xPadding = context.getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal);
        final int yPadding = context.getResources().getDimensionPixelSize(R.dimen.dialog_padding_vertical);
        _dialogLayout.setPadding(xPadding, yPadding, xPadding, yPadding);
        _dialogIds.clear();
    }

    private void attachLabel(final View labeled, final AppCompatTextView label) {
        ViewCompat.setAccessibilityDelegate(labeled, new AccessibilityDelegateCompat() {
            @Override
            public void onInitializeAccessibilityNodeInfo(View host, AccessibilityNodeInfoCompat info) {
                super.onInitializeAccessibilityNodeInfo(host, info);
                info.setLabeledBy(label);
            }
        });
    }

    @UsedByJNI
    void dialogAddString(int whichEvent, String name, String value)
    {
        final Context context = _dialogBuilder.getContext();
        _dialogIds.add(name);
        AppCompatEditText et = new AppCompatEditText(context);
        // Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
        // Uglier temporary-er hack: Black Box must accept a range for ball count.
        if (whichEvent == GamePlay.CFG_SETTINGS && _currentBackend != BackendName.BLACKBOX) {
            et.setInputType(InputType.TYPE_CLASS_NUMBER
                    | InputType.TYPE_NUMBER_FLAG_DECIMAL
                    | InputType.TYPE_NUMBER_FLAG_SIGNED);
        }
        et.setTag(name);
        et.setText(value);
        et.setWidth(context.getResources().getDimensionPixelSize((whichEvent == GamePlay.CFG_SETTINGS)
                ? R.dimen.dialog_edit_text_width : R.dimen.dialog_long_edit_text_width));
        et.setMinHeight((int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 48, context.getResources().getDisplayMetrics()));
        et.setSelectAllOnFocus(true);
        AppCompatTextView tv = new AppCompatTextView(context);
        tv.setText(name);
        tv.setPadding(0, 0, context.getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal), 0);
        tv.setGravity(Gravity.END);
        attachLabel(et, tv);
        TableRow tr = new TableRow(context);
        tr.addView(tv);
        tr.addView(et);
        tr.setGravity(Gravity.CENTER_VERTICAL);
        _dialogLayout.addView(tr);
        if (whichEvent == GamePlay.CFG_SEED && value.indexOf('#') == value.length() - 1) {
            final AppCompatTextView seedWarning = new AppCompatTextView(context);
            seedWarning.setText(R.string.seedWarning);
            _dialogLayout.addView(seedWarning);
        }
        if (COLUMNS_OF_SUB_BLOCKS.equals(name)) {
            tv.setTag(COLS_LABEL_TAG);
        } else if (ROWS_OF_SUB_BLOCKS.equals(name)) {
            tr.setTag(ROWS_ROW_TAG);
        }
    }

    @UsedByJNI
    void dialogAddBoolean(int whichEvent, String name, boolean selected)
    {
        final Context context = _dialogBuilder.getContext();
        _dialogIds.add(name);
        final AppCompatCheckBox c = new AppCompatCheckBox(context);
        c.setTag(name);
        c.setText(name);
        c.setChecked(selected);
        c.setMinimumHeight((int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 48, context.getResources().getDisplayMetrics()));
        if (_currentBackend == BackendName.SOLO && name.startsWith("Jigsaw")) {
            jigsawHack(c);
            c.setOnClickListener(v -> jigsawHack(c));
        }
        _dialogLayout.addView(c);
    }

    private void jigsawHack(final AppCompatCheckBox jigsawCheckbox) {
        final View colTV = _dialogLayout.findViewWithTag(COLUMNS_OF_SUB_BLOCKS);
        final View rowTV = _dialogLayout.findViewWithTag(ROWS_OF_SUB_BLOCKS);
        if (colTV instanceof TextView && rowTV instanceof TextView) {
            final int cols = parseTextViewInt((TextView) colTV, 3);
            final int rows = parseTextViewInt((TextView) rowTV, 3);
            if (jigsawCheckbox.isChecked()) {
                setTextViewInt((TextView) colTV, cols * rows);
                setTextViewInt((TextView) rowTV, 1);
                rowTV.setEnabled(false);
                ((TextView) _dialogLayout.findViewWithTag(COLS_LABEL_TAG)).setText(R.string.Size_of_sub_blocks);
                _dialogLayout.findViewWithTag(ROWS_ROW_TAG).setVisibility(View.INVISIBLE);
            } else {
                if (rows == 1) {
                    int size = Math.max(2, (int) Math.sqrt(cols));
                    setTextViewInt((TextView) colTV, size);
                    setTextViewInt((TextView) rowTV, size);
                }
                rowTV.setEnabled(true);
                ((TextView) _dialogLayout.findViewWithTag(COLS_LABEL_TAG)).setText(R.string.Columns_of_sub_blocks);
                _dialogLayout.findViewWithTag(ROWS_ROW_TAG).setVisibility(View.VISIBLE);
            }
        }
    }

    @SuppressWarnings("SameParameterValue")
    private int parseTextViewInt(final TextView tv, final int defaultVal) {
        try {
            return (int) Math.round(Double.parseDouble(tv.getText().toString()));
        } catch (NumberFormatException e) {
            return defaultVal;
        }
    }

    private void setTextViewInt(final TextView tv, int val) {
        tv.setText(String.format(Locale.ROOT, "%d", val));
    }

    @UsedByJNI
    void dialogAddChoices(int whichEvent, String name, String value, int selection) {
        final Context context = _dialogBuilder.getContext();
        StringTokenizer st = new StringTokenizer(value.substring(1), value.substring(0, 1));
        ArrayList<String> choices = new ArrayList<>();
        while (st.hasMoreTokens()) choices.add(st.nextToken());
        _dialogIds.add(name);
        AppCompatSpinner s = new AppCompatSpinner(context);
        s.setTag(name);
        ArrayAdapter<String> a = new ArrayAdapter<>(context,
                android.R.layout.simple_spinner_item, choices.toArray(new String[0]));
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        s.setAdapter(a);
        s.setSelection(selection);
        s.setLayoutParams(new TableRow.LayoutParams(
                context.getResources().getDimensionPixelSize(R.dimen.dialog_spinner_width),
                (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 48, context.getResources().getDisplayMetrics())));
        AppCompatTextView tv = new AppCompatTextView(context);
        tv.setText(name);
        tv.setPadding(0, 0, context.getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal), 0);
        tv.setGravity(Gravity.END);
        attachLabel(s, tv);
        TableRow tr = new TableRow(context);
        tr.addView(tv);
        tr.addView(s);
        tr.setGravity(Gravity.CENTER_VERTICAL);
        _dialogLayout.addView(tr);
    }

    @UsedByJNI
    void dialogShow()
    {
        _dialogLayout.setColumnShrinkable(0, true);
        _dialogLayout.setColumnShrinkable(1, true);
        _dialogLayout.setColumnStretchable(0, true);
        _dialogLayout.setColumnStretchable(1, true);
        final AlertDialog dialog = _dialogBuilder.create();
        dialog.show();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
            for (String i : _dialogIds) {
                View v = _dialogLayout.findViewWithTag(i);
                if (v instanceof EditText) {
                    _engineCallbacks.configSetString(i, ((EditText) v).getText().toString());
                } else if (v instanceof CheckBox) {
                    _engineCallbacks.configSetBool(i, ((CheckBox) v).isChecked() ? 1 : 0);
                } else if (v instanceof Spinner) {
                    _engineCallbacks.configSetChoice(i, ((Spinner) v).getSelectedItemPosition());
                }
            }
            try {
                final GameLaunch launch;
                if (_dialogEvent == GamePlay.CFG_DESC) {
                    launch = GameLaunch.ofGameID(_currentBackend, _engineCallbacks.getFullGameIDFromDialog(), GameLaunch.Origin.CUSTOM_DIALOG);
                } else if (_dialogEvent == GamePlay.CFG_SEED) {
                    launch = GameLaunch.fromSeed(_currentBackend, _engineCallbacks.getFullSeedFromDialog());
                } else {
                    launch = GameLaunch.toGenerate(_currentBackend, _engineCallbacks.configOK(), GameLaunch.Origin.CUSTOM_DIALOG);
                }
                _activityCallbacks.startGame(launch);
                dialog.dismiss();
            } catch (IllegalArgumentException e) {
                _activityCallbacks.customDialogError(e.getMessage());
            }
        });
    }
}
