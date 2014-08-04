package name.boyle.chris.sgtpuzzles;

import java.util.ArrayList;
import java.util.List;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.inputmethodservice.Keyboard;
import android.inputmethodservice.KeyboardView;
import android.preference.PreferenceManager;
import android.util.AttributeSet;
import android.util.Log;

public class SmallKeyboard extends KeyboardView implements KeyboardView.OnKeyboardActionListener
{
	private static final String TAG = "SmallKeyboard";
	private static final int KEYSP = 44;  // dip
	private final SGTPuzzles parent;
	private boolean undoEnabled = false, redoEnabled = false;
	private static final int NO_ARROWS = 0;  // untangle
			static final int ARROWS_LEFT_RIGHT_CLICK = 1;  // unless phone has a d-pad (most games)
			private static final int ARROWS_DIAGONALS = 2;  // Inertia
	int arrowMode = ARROWS_LEFT_RIGHT_CLICK;

	/** Key which can be disabled */
	static class DKey extends Keyboard.Key
	{
		boolean enabled;
		DKey(Keyboard.Row r) { super(r); }
	}

	static class KeyboardModel extends Keyboard
	{
		int mDefaultWidth = KEYSP, mDefaultHeight = KEYSP,
                mDefaultHorizontalGap = 0, mDefaultVerticalGap = 0,
				mTotalWidth = 0, mTotalHeight = 0;
		Context context;
        private final KeyboardView keyboardView;  // for invalidateKey()
        List<Key> mKeys;
		int undoKey = -1, redoKey = -1;
		boolean initDone = false;
		public KeyboardModel(Context context, KeyboardView keyboardView, boolean isInEditMode,
                CharSequence characters, int arrowMode, boolean columnMajor, int maxPx,
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
			if (arrowMode == ARROWS_DIAGONALS && inertiaForceArrows) {
				// Do nothing: allow arrows.
			} else if (arrowPref.equals("never")) {
				arrowMode = NO_ARROWS;
			} else if (arrowPref.equals("auto")) {
				Configuration c = context.getResources().getConfiguration();
				if ((c.navigation == Configuration.NAVIGATION_DPAD
						|| c.navigation == Configuration.NAVIGATION_TRACKBALL)
                        && (c.navigationHidden != Configuration.NAVIGATIONHIDDEN_YES)) {
					arrowMode = NO_ARROWS;
				}
			} // else we have "always": allow arrows.

			int maxPxMinusArrows = maxPx;
			if (arrowMode > NO_ARROWS) {
				maxPxMinusArrows -= 3 * keyPlusPad;
			}
			// How many rows do we need?
			final int majors = (int)Math.ceil((double)
					(characters.length() * keyPlusPad)/maxPxMinusArrows);
			// Spread the keys as evenly as possible
			final int minorsPerMajor = (int)Math.ceil((double)
					characters.length() / majors);
			int minorStartPx = (int)Math.round(((double)maxPxMinusArrows
					- (minorsPerMajor * keyPlusPad)) / 2);
			int minorPx = minorStartPx;
			int majorPx = 0;
			int arrowRows = (arrowMode == ARROWS_DIAGONALS) ? 3 : 2;
			int arrowMajors = columnMajor ? 3 : arrowRows;
			if (majors < 3 && arrowMode > NO_ARROWS) majorPx = (arrowMajors - majors) * keyPlusPad;
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
				if (minor == minorsPerMajor - 1 && arrowMode == NO_ARROWS)
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
								R.drawable.sym_keyboard_delete);
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
				int minorPxSoFar = minorPx - minorStartPx;
				if (columnMajor) {
					if (minorPxSoFar > mTotalHeight) mTotalHeight = minorPxSoFar;
				} else {
					if (minorPxSoFar > mTotalWidth) mTotalWidth = minorPxSoFar;
				}
			}
			if (columnMajor) {
				mTotalWidth = majorPx + mDefaultWidth;
			} else {
				mTotalHeight = majorPx + mDefaultHeight;
			}
			if (arrowMode > NO_ARROWS) {
				int[] arrows;
				if (arrowMode == ARROWS_DIAGONALS) {
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
				int leftRightRow = (arrowMode == ARROWS_DIAGONALS) ? 2 : 1;
				int bottomIf2Row = (arrowMode == ARROWS_DIAGONALS) ? 0 : EDGE_BOTTOM;
				int maybeTopIf2Row = (arrowMode == ARROWS_DIAGONALS) ? 0 : maybeTop;
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
								R.drawable.arrow_n);
						key.edgeFlags = maybeTop;
						break;
					case GameView.CURSOR_DOWN:
						key.x = arrowsRightEdge  - 2*keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_s);
						key.edgeFlags = EDGE_BOTTOM;
						break;
					case GameView.CURSOR_LEFT:
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge - leftRightRow*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_w);
						key.edgeFlags = bottomIf2Row | maybeLeft;
						break;
					case GameView.CURSOR_RIGHT:
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - leftRightRow*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_e);
						key.edgeFlags = bottomIf2Row | EDGE_RIGHT;
						break;
					case '\n':
						key.x = arrowsRightEdge  - ((arrowMode==ARROWS_DIAGONALS)?2:3)*keyPlusPad;
						key.y = arrowsBottomEdge - 2*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.mouse_left);
						key.edgeFlags = maybeTopIf2Row;
						break;
					case ' ': // right click
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - arrowRows*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.mouse_right);
						key.edgeFlags = maybeTop | EDGE_RIGHT;
						break;
					case GameView.MOD_NUM_KEYPAD | '7':
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_nw);
						key.edgeFlags = maybeTop | maybeLeft;
						break;
					case GameView.MOD_NUM_KEYPAD | '1':
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_sw);
						key.edgeFlags = EDGE_BOTTOM | maybeLeft;
						break;
					case GameView.MOD_NUM_KEYPAD | '9':
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_ne);
						key.edgeFlags = maybeTop | EDGE_RIGHT;
						break;
					case GameView.MOD_NUM_KEYPAD | '3':
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.icon = context.getResources().getDrawable(
								R.drawable.arrow_se);
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
					enabled ? R.drawable.sym_keyboard_redo
							: R.drawable.sym_keyboard_redo_disabled :
					enabled ? R.drawable.sym_keyboard_undo
							: R.drawable.sym_keyboard_undo_disabled);
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

    @SuppressWarnings("SameParameterValue")
	public SmallKeyboard(Context c, AttributeSet a)
	{
		super(c, a);
		parent = isInEditMode() ? null : (SGTPuzzles)c;
		setBackgroundColor( Color.BLACK );
		setOnKeyboardActionListener(this);
		if (isInEditMode()) setKeys("123456\bur", 1);
	}

	private CharSequence lastKeys = "";
	public void setKeys(CharSequence keys, int arrowMode)
	{
		lastKeys = keys;
		this.arrowMode = arrowMode;
		requestLayout();
	}

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
