package name.boyle.chris.sgtpuzzles;

import java.util.ArrayList;
import java.util.List;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.inputmethodservice.Keyboard;
import android.inputmethodservice.KeyboardView;
import android.preference.PreferenceManager;
import android.util.AttributeSet;
import android.util.Log;

class SmallKeyboard extends KeyboardView implements KeyboardView.OnKeyboardActionListener
{
	private static final String TAG = "SmallKeyboard";
	private static final int KEYSP = 44;  // dip
	private final SGTPuzzles parent;
	private boolean undoEnabled = false, redoEnabled = false;
	static enum ArrowMode {
		NO_ARROWS,  // untangle
		ARROWS_LEFT_RIGHT_CLICK,  // unless phone has a d-pad (most games)
		ARROWS_DIAGONALS;  // Inertia

		boolean hasArrows() { return this != NO_ARROWS; }
	}
	private ArrowMode arrowMode = ArrowMode.ARROWS_LEFT_RIGHT_CLICK;

	/** Key which can be disabled */
	static class DKey extends Keyboard.Key
	{
		boolean enabled;
		DKey(Keyboard.Row r) { super(r); }
	}

	static class KeyboardModel extends Keyboard
	{
		int mDefaultWidth = KEYSP;
        int mDefaultHeight = KEYSP;
        final int mDefaultHorizontalGap = 0;
        final int mDefaultVerticalGap = 0;
        int mTotalWidth = 0;
        int mTotalHeight = 0;
		final Context context;
        private final KeyboardView keyboardView;  // for invalidateKey()
        final List<Key> mKeys;
		int undoKey = -1, redoKey = -1;
		boolean initDone = false;
		public KeyboardModel(Context context, KeyboardView keyboardView, boolean isInEditMode,
                CharSequence characters, ArrowMode arrowMode, boolean columnMajor, int maxPx,
				boolean undoEnabled, boolean redoEnabled)
		{
			super(context, R.layout.keyboard_template);
			this.context = context;
            this.keyboardView = keyboardView;
            mDefaultWidth = mDefaultHeight =
					context.getResources().getDimensionPixelSize(R.dimen.keySize);
			mKeys = new ArrayList<Key>();

			Row row = new Row(this);
			row.defaultHeight = mDefaultHeight;
			row.defaultWidth = mDefaultWidth;
			row.defaultHorizontalGap = mDefaultHorizontalGap;
			row.verticalGap = mDefaultVerticalGap;
			final int keyPlusPad = columnMajor
					? mDefaultHeight + mDefaultVerticalGap
					: mDefaultWidth + mDefaultHorizontalGap;

			String arrowPref;
			boolean inertiaForceArrows;
			if (isInEditMode) {
				arrowPref = "always";
				inertiaForceArrows = true;
			} else {
				SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
				arrowPref = prefs.getString(SGTPuzzles.ARROW_KEYS_KEY, "auto");
				inertiaForceArrows = prefs.getBoolean(SGTPuzzles.INERTIA_FORCE_ARROWS_KEY, true);
			}
			if (arrowMode != ArrowMode.ARROWS_DIAGONALS || !inertiaForceArrows) {
                if (arrowPref.equals("never")) {
                    arrowMode = ArrowMode.NO_ARROWS;
                } else if (arrowPref.equals("auto")) {
                    Configuration c = context.getResources().getConfiguration();
                    if ((c.navigation == Configuration.NAVIGATION_DPAD
                            || c.navigation == Configuration.NAVIGATION_TRACKBALL)
                            && (c.navigationHidden != Configuration.NAVIGATIONHIDDEN_YES)) {
                        arrowMode = ArrowMode.NO_ARROWS;
                    }
                }
            }
            // else allow arrows.

			int maxPxMinusArrows = maxPx;
			if (arrowMode.hasArrows()) {
				maxPxMinusArrows -= 3 * keyPlusPad;
			}
			// How many rows do we need?
			final int majors = Math.max(1, (int)Math.ceil((double)
					(characters.length() * keyPlusPad)/maxPxMinusArrows));
			// Spread the keys as evenly as possible
			final int minorsPerMajor = (int)Math.ceil((double)
					characters.length() / majors);
			int minorStartPx = (int)Math.round(((double)maxPxMinusArrows
					- (minorsPerMajor * keyPlusPad)) / 2);
			int minorPx = minorStartPx;
			int majorPx = 0;
			int arrowRows = (arrowMode == ArrowMode.ARROWS_DIAGONALS) ? 3 : 2;
			int arrowMajors = columnMajor ? 3 : arrowRows;
			if (majors < 3 && arrowMode.hasArrows()) majorPx = (arrowMajors - majors) * keyPlusPad;
			int minor = 0;
			for (int i = 0; i < characters.length(); i++) {
				char c = characters.charAt(i);
				if (minor >= minorsPerMajor) {
					minorPx = (characters.length() - i < minorsPerMajor)  // last row
						? minorStartPx + (int)Math.round((((double)minorsPerMajor -
								(characters.length() - i))/2) * keyPlusPad)
						: minorStartPx;
					majorPx += columnMajor
						? mDefaultHorizontalGap + mDefaultWidth
						: mDefaultVerticalGap + mDefaultHeight;
					minor = 0;
				}
				final DKey key = new DKey(row);
				mKeys.add(key);
				key.edgeFlags = 0;
				// No two of these flags are mutually exclusive
				if (i < minorsPerMajor)
					key.edgeFlags |= columnMajor ? EDGE_LEFT   : EDGE_TOP;
				if (i / minorsPerMajor + 1 == majors)
					key.edgeFlags |= columnMajor ? EDGE_RIGHT  : EDGE_BOTTOM;
				if (minor == 0)
					key.edgeFlags |= columnMajor ? EDGE_TOP    : EDGE_LEFT;
				if (minor == minorsPerMajor - 1 && arrowMode == ArrowMode.NO_ARROWS)
					key.edgeFlags |= columnMajor ? EDGE_BOTTOM : EDGE_RIGHT;
				key.x = columnMajor ? majorPx : minorPx;
				key.y = columnMajor ? minorPx : majorPx;
				key.width = mDefaultWidth;
				key.height = mDefaultHeight;
				key.gap = mDefaultHorizontalGap;
				switch(c) {
					case 'u':
						undoKey = mKeys.size() - 1;
						key.repeatable = true;
						setUndoRedoEnabled(false, undoEnabled);
						break;
					case 'r':
						redoKey = mKeys.size() - 1;
						key.repeatable = true;
						setUndoRedoEnabled(true, redoEnabled);
						break;
					case '\b':
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_backspace);
						key.repeatable = true;
						key.enabled = true;
						break;
					default:
						key.label = String.valueOf(c);
						key.enabled = true;
						break;
				}
				key.codes = new int[] { c };
				minor++;
				minorPx += keyPlusPad;
			}
			if (characters.length() == 0 && ! arrowMode.hasArrows()) {
				mTotalWidth = mTotalHeight = 0;
			} else if (columnMajor) {
				mTotalWidth = majorPx + mDefaultWidth;
			} else {
				mTotalHeight = majorPx + mDefaultHeight;
			}
			if (arrowMode.hasArrows()) {
				int[] arrows;
				if (arrowMode == ArrowMode.ARROWS_DIAGONALS) {
					arrows = new int[] {
							GameView.CURSOR_UP,
							GameView.CURSOR_DOWN,
							GameView.CURSOR_LEFT,
							GameView.CURSOR_RIGHT,
							'\n',
							GameView.MOD_NUM_KEYPAD | '7',
							GameView.MOD_NUM_KEYPAD | '1',
							GameView.MOD_NUM_KEYPAD | '9',
							GameView.MOD_NUM_KEYPAD | '3' };
				} else {
					arrows = new int[] {
							GameView.CURSOR_UP,
							GameView.CURSOR_DOWN,
							GameView.CURSOR_LEFT,
							GameView.CURSOR_RIGHT,
							'\n',
							' ' };
				}
				final int arrowsRightEdge = columnMajor ? mTotalWidth : maxPx,
						arrowsBottomEdge = columnMajor ? maxPx : mTotalHeight;
				int maybeTop  = (!columnMajor && majors <= arrowRows) ? EDGE_TOP : 0;
				int maybeLeft = ( columnMajor && majors <= arrowRows) ? EDGE_LEFT : 0;
				int leftRightRow = (arrowMode == ArrowMode.ARROWS_DIAGONALS) ? 2 : 1;
				int bottomIf2Row = (arrowMode == ArrowMode.ARROWS_DIAGONALS) ? 0 : EDGE_BOTTOM;
				int maybeTopIf2Row = (arrowMode == ArrowMode.ARROWS_DIAGONALS) ? 0 : maybeTop;
				for (int arrow : arrows) {
					final DKey key = new DKey(row);
					mKeys.add(key);
					key.width = mDefaultWidth;
					key.height = mDefaultHeight;
					key.gap = mDefaultHorizontalGap;
					key.repeatable = true;
					key.enabled = true;
					key.codes = new int[] { arrow };
					switch (arrow) {
					case GameView.CURSOR_UP:
						key.x = arrowsRightEdge  - 2*keyPlusPad;
						key.y = arrowsBottomEdge - arrowRows*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_north);
						key.edgeFlags = maybeTop;
						break;
					case GameView.CURSOR_DOWN:
						key.x = arrowsRightEdge  - 2*keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_south);
						key.edgeFlags = EDGE_BOTTOM;
						break;
					case GameView.CURSOR_LEFT:
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge - leftRightRow*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_west);
						key.edgeFlags = bottomIf2Row | maybeLeft;
						break;
					case GameView.CURSOR_RIGHT:
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - leftRightRow*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_east);
						key.edgeFlags = bottomIf2Row | EDGE_RIGHT;
						break;
					case '\n':
						key.x = arrowsRightEdge  - ((arrowMode==ArrowMode.ARROWS_DIAGONALS)?2:3)*keyPlusPad;
						key.y = arrowsBottomEdge - 2*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_mouse_left);
						key.edgeFlags = maybeTopIf2Row;
						break;
					case ' ': // right click
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - arrowRows*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_mouse_right);
						key.edgeFlags = maybeTop | EDGE_RIGHT;
						break;
					case GameView.MOD_NUM_KEYPAD | '7':
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_north_west);
						key.edgeFlags = maybeTop | maybeLeft;
						break;
					case GameView.MOD_NUM_KEYPAD | '1':
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_south_west);
						key.edgeFlags = EDGE_BOTTOM | maybeLeft;
						break;
					case GameView.MOD_NUM_KEYPAD | '9':
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_north_east);
						key.edgeFlags = maybeTop | EDGE_RIGHT;
						break;
					case GameView.MOD_NUM_KEYPAD | '3':
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.sym_key_south_east);
						key.edgeFlags = EDGE_BOTTOM | EDGE_RIGHT;
						break;
					default:
						Log.e(TAG, "unknown key in keyboard: "+arrow);
						break;
					}
				}
			}
			initDone = true;
		}
		void setUndoRedoEnabled( boolean redo, boolean enabled )
		{
			int i = redo ? redoKey : undoKey;
			if (i < 0) return;
			DKey k = (DKey)mKeys.get(i);
			k.icon = context.getResources().getDrawable(redo ?
					enabled ? R.drawable.ic_action_redo
							: R.drawable.ic_action_redo_disabled :
					enabled ? R.drawable.ic_action_undo
							: R.drawable.ic_action_undo_disabled);
			k.enabled = enabled;
            if (initDone) keyboardView.invalidateKey(i);
		}
		@Override
		public List<Key> getKeys() { return mKeys; }
		@Override
		public int[] getNearestKeys(int x, int y)
		{
			for (int i=0; i<mKeys.size(); i++) {
				DKey k = (DKey)mKeys.get(i);
				if (k.isInside(x,y) && k.enabled) return new int[]{i};
			}
			return new int[0];
		}
		@Override
		public int getHeight() { return mTotalHeight; }
		@Override
		public int getMinWidth() { return mTotalWidth; }
	}

	public SmallKeyboard(Context c, boolean undoEnabled, boolean redoEnabled)
	{
		this(c, null);
		this.undoEnabled = undoEnabled;
		this.redoEnabled = redoEnabled;
	}

    @SuppressWarnings({"SameParameterValue", "WeakerAccess"})  // used by layout
    public SmallKeyboard(Context c, AttributeSet a)
	{
		super(c, a);
		parent = isInEditMode() ? null : (SGTPuzzles)c;
		setBackgroundColor(getResources().getColor(R.color.keyboard_background));
		setOnKeyboardActionListener(this);
		if (isInEditMode()) setKeys("123456\bur", ArrowMode.ARROWS_LEFT_RIGHT_CLICK);
	}

	private CharSequence lastKeys = "";
	public void setKeys(CharSequence keys, ArrowMode arrowMode)
	{
		lastKeys = keys;
		this.arrowMode = arrowMode;
		requestLayout();
	}

	@SuppressLint("DrawAllocation")  // layout not often repeated
	@Override
	public void onMeasure(int wSpec, int hSpec)
	{
		boolean landscape =
			(getContext().getResources().getConfiguration().orientation
			 == Configuration.ORIENTATION_LANDSCAPE);
		int maxPx = MeasureSpec.getSize(landscape ? hSpec : wSpec);
		// Doing this here seems the only way to be sure of dimensions.
		setKeyboard(new KeyboardModel(getContext(), this, isInEditMode(), lastKeys, arrowMode,
				landscape, maxPx, undoEnabled, redoEnabled));
		super.onMeasure(wSpec, hSpec);
	}

	void setUndoRedoEnabled(boolean redo, boolean enabled)
	{
		if (redo) redoEnabled = enabled; else undoEnabled = enabled;
		KeyboardModel m = (KeyboardModel)getKeyboard();
		if (m == null) return;
		m.setUndoRedoEnabled(redo,enabled);
	}

	public void swipeUp() {}
	public void swipeDown() {}
	public void swipeLeft() {}
	public void swipeRight() {}
	public void onPress(int k) {}
	public void onRelease(int k) {}
	public void onText(CharSequence s) {
		for (int i=0; i<s.length();i++) parent.sendKey(0,0,s.charAt(i));
	}
	public void onKey(int k,int[] ignore) { parent.sendKey(0,0,k); }
}
