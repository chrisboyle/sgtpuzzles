package name.boyle.chris.sgtpuzzles;

import java.util.ArrayList;
import java.util.List;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.inputmethodservice.Keyboard;
import android.inputmethodservice.KeyboardView;
import android.preference.PreferenceManager;
import android.support.annotation.NonNull;
import android.util.AttributeSet;
import android.util.Log;

public class SmallKeyboard extends KeyboardView implements KeyboardView.OnKeyboardActionListener
{
	private static final String TAG = "SmallKeyboard";
	private final GamePlay parent;
	private boolean undoEnabled = false, redoEnabled = false;
	static enum ArrowMode {
		NO_ARROWS,  // untangle
		ARROWS_LEFT_RIGHT_CLICK,  // unless phone has a d-pad (most games)
		ARROWS_DIAGONALS;  // Inertia

		boolean hasArrows() { return this != NO_ARROWS; }
	}
	private ArrowMode arrowModeFromGame = ArrowMode.ARROWS_LEFT_RIGHT_CLICK;

	/** Key which can be disabled */
	static class DKey extends Keyboard.Key
	{
		boolean enabled;
		DKey(Keyboard.Row r) { super(r); }
	}

	static class KeyboardModel extends Keyboard
	{
		final int mDefaultWidth;
		final int mDefaultHeight;
        final int mDefaultHorizontalGap = 0;
        final int mDefaultVerticalGap = 0;
        int mTotalWidth = 0;
        int mTotalHeight = 0;
		boolean mEmpty = false;
		final Context context;
        private final KeyboardView keyboardView;  // for invalidateKey()
        final List<Key> mKeys;
		int undoKey = -1, redoKey = -1;
		boolean initDone = false;
		public KeyboardModel(final Context context, final KeyboardView keyboardView,
				final boolean isInEditMode, final CharSequence characters,
				final ArrowMode arrowModeFromGame, final boolean columnMajor, final int maxPx,
				final boolean undoEnabled, final boolean redoEnabled)
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
				arrowPref = prefs.getString(GamePlay.ARROW_KEYS_KEY, "auto");
				inertiaForceArrows = prefs.getBoolean(GamePlay.INERTIA_FORCE_ARROWS_KEY, true);
			}
			ArrowMode arrowMode = arrowModeFromGame;
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
			final boolean isDiagonals = arrowMode == ArrowMode.ARROWS_DIAGONALS;
			final int arrowRows = isDiagonals ? 3 : 2;
			final int arrowCols = 3;
			final int arrowMajors = columnMajor ? arrowCols : arrowRows;
			final int arrowMinors = columnMajor ? arrowRows : arrowCols;

			// How many keys can we fit on a row?
			final int maxPxMinusArrows = arrowMode.hasArrows() ? maxPx - arrowMinors * keyPlusPad : maxPx;
			final int minorsPerMajor = (int)Math.floor(((double)maxPxMinusArrows) / keyPlusPad);
			// How many rows do we need?
			final int majors = (int)Math.ceil(((double)characters.length())/minorsPerMajor);
			mEmpty = (majors == 0) && ! arrowMode.hasArrows();

			if (majors > 0) {
				final int minorStartPx = (int) Math.round(((double) maxPxMinusArrows
						- (minorsPerMajor * keyPlusPad)) / 2);
				final int majorStartPx = (majors < 3 && arrowMode.hasArrows()) ? (arrowMajors - majors) * keyPlusPad : 0;
				addCharacters(context, characters, columnMajor, undoEnabled, redoEnabled,
						row, keyPlusPad, arrowMode, minorsPerMajor, majors, minorStartPx, majorStartPx);
			}

			int charsWidth = 0;
			int charsHeight = 0;
			if (majors > 0) {
				if (columnMajor) {
					charsWidth  = majors * mDefaultWidth + (majors - 1) * mDefaultHorizontalGap;
					charsHeight = keyPlusPad * ((majors > 1) ? minorsPerMajor : characters.length());
				} else {
					charsHeight = majors * mDefaultHeight + (majors - 1) * mDefaultVerticalGap;
					charsWidth  = keyPlusPad * ((majors > 1) ? minorsPerMajor : characters.length());
				}
			}
			final int maxWidth = Math.max(charsWidth, arrowCols * keyPlusPad);
			mTotalWidth  = columnMajor ? maxWidth : charsWidth + (arrowCols * keyPlusPad);
			final int maxHeight = Math.max(charsHeight, arrowRows * keyPlusPad);
			mTotalHeight = columnMajor ? charsHeight + arrowRows * keyPlusPad : maxHeight;

			if (arrowMode.hasArrows()) {
				final int arrowsRightEdge = columnMajor ? maxWidth : maxPx,
						arrowsBottomEdge = columnMajor ? maxPx : maxHeight;
				int maybeTop  = (!columnMajor && majors <= arrowRows) ? EDGE_TOP : 0;
				int maybeLeft = ( columnMajor && majors <= arrowRows) ? EDGE_LEFT : 0;
				addArrows(context, row, keyPlusPad, isDiagonals, arrowRows,
						arrowsRightEdge, arrowsBottomEdge, maybeTop, maybeLeft);
			}
			initDone = true;
		}

		private void addCharacters(final Context context, final CharSequence characters,
					final boolean columnMajor, final boolean undoEnabled, final boolean redoEnabled,
					final Row row, final int keyPlusPad, final ArrowMode arrowMode,
					final int minorsPerMajor, final int majors, final int minorStartPx,
					final int majorStartPx) {
			final int length = characters.length();
			int minorPx = minorStartPx;
			int majorPx = majorStartPx;
			int minor = 0;
			int major = -1;
			// avoid having a last major with a single character
			final boolean willPreventSingleton = (length % minorsPerMajor == 1);
			for (int i = 0; i < length; i++) {
				final char c = characters.charAt(i);
				final boolean preventingSingleton = (major == majors - 2) && i == length - 2;
				if (i == 0 || minor >= minorsPerMajor || preventingSingleton) {
					major++;
					minor = 0;
					final int charsOnThisMajor = (major == majors - 1) ? (length - i)
							: (major == majors - 2 && willPreventSingleton) ? (minorsPerMajor - 1)
							: minorsPerMajor;
					minorPx = minorStartPx + (int)Math.round((((double)minorsPerMajor - charsOnThisMajor)/2) * keyPlusPad);
					if (i > 0) {
						majorPx += columnMajor
								? mDefaultHorizontalGap + mDefaultWidth
								: mDefaultVerticalGap + mDefaultHeight;
					}
				}
				final int edgeFlags = edgeFlags(columnMajor, arrowMode,
						major == 0, major == majors - 1, minor == 0, minor == minorsPerMajor - 1);
				final int x = columnMajor ? majorPx : minorPx;
				final int y = columnMajor ? minorPx : majorPx;
				addCharacterKey(context, undoEnabled, redoEnabled, row, c, edgeFlags, x, y);
				minor++;
				minorPx += keyPlusPad;
			}
		}

		private int edgeFlags(boolean columnMajor, ArrowMode arrowMode,
				boolean firstMajor, boolean lastMajor, boolean firstMinor, boolean lastMinor) {
			// No two of these flags are mutually exclusive
			int edgeFlags = 0;
			if (firstMajor)
				edgeFlags |= columnMajor ? EDGE_LEFT   : EDGE_TOP;
			if (lastMajor)
				edgeFlags |= columnMajor ? EDGE_RIGHT  : EDGE_BOTTOM;
			if (firstMinor)
				edgeFlags |= columnMajor ? EDGE_TOP    : EDGE_LEFT;
			if (lastMinor && arrowMode == ArrowMode.NO_ARROWS)
				edgeFlags |= columnMajor ? EDGE_BOTTOM : EDGE_RIGHT;
			return edgeFlags;
		}

		private void addCharacterKey(final Context context, final boolean undoEnabled,
					final boolean redoEnabled, final Row row, final char c, final int edgeFlags,
					final int x, final int y) {
			final DKey key = new DKey(row);
			mKeys.add(key);
			key.x = x;
			key.y = y;
			key.edgeFlags = edgeFlags;
			key.width = mDefaultWidth;
			key.height = mDefaultHeight;
			key.gap = mDefaultHorizontalGap;
			switch(c) {
				case 'u':
					undoKey = mKeys.size() - 1;
					key.repeatable = true;
					setUndoRedoEnabled(ExtraKey.UNDO, undoEnabled);
					break;
				case 'r':
					redoKey = mKeys.size() - 1;
					key.repeatable = true;
					setUndoRedoEnabled(ExtraKey.REDO, redoEnabled);
					break;
				case '\b':
					key.icon = context.getResources().getDrawable(R.drawable.sym_key_backspace);
					key.repeatable = true;
					key.enabled = true;
					break;
				default:
					key.label = String.valueOf(c);
					key.enabled = true;
					break;
			}
			key.codes = new int[] { c };
		}

		private void addArrows(Context context, Row row, int keyPlusPad, boolean isDiagonals, int arrowRows, int arrowsRightEdge, int arrowsBottomEdge, int maybeTop, int maybeLeft) {
			int[] arrows;
			if (isDiagonals) {
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
			int leftRightRow = isDiagonals ? 2 : 1;
			int bottomIf2Row = isDiagonals ? 0 : EDGE_BOTTOM;
			int maybeTopIf2Row = isDiagonals ? 0 : maybeTop;
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
					key.x = arrowsRightEdge  - (isDiagonals ? 2 : 3) * keyPlusPad;
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
					Log.e(TAG, "unknown key in keyboard: " + arrow);
					break;
				}
			}
		}

		enum ExtraKey { UNDO, REDO }

		void setUndoRedoEnabled(ExtraKey which, boolean enabled)
		{
			final boolean redo = (which == ExtraKey.REDO);
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

		public boolean isEmpty() {
			return mEmpty;
		}
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
		parent = isInEditMode() ? null : (GamePlay)c;
		setBackgroundColor(getResources().getColor(R.color.keyboard_background));
		setOnKeyboardActionListener(this);
		if (isInEditMode()) setKeys("123456\bur", ArrowMode.ARROWS_LEFT_RIGHT_CLICK);
		setPreviewEnabled(false);  // can't get icon buttons to darken properly and there are positioning bugs anyway
	}

	private CharSequence lastKeys = "";
	public void setKeys(CharSequence keys, ArrowMode arrowModeFromGame)
	{
		lastKeys = keys;
		this.arrowModeFromGame = arrowModeFromGame;
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
		final KeyboardModel model = new KeyboardModel(getContext(), this, isInEditMode(), lastKeys,
				arrowModeFromGame, landscape, maxPx, undoEnabled, redoEnabled);
		setKeyboard(model);
		if (model.isEmpty()) {
			setMeasuredDimension(0, 0);
		} else {
			super.onMeasure(wSpec, hSpec);
		}
	}

	@Override
	public void onDraw(@NonNull Canvas canvas) {
		if (!((KeyboardModel)getKeyboard()).isEmpty()) {
			super.onDraw(canvas);
		}
	}

	void setUndoRedoEnabled(boolean canUndo, boolean canRedo)
	{
		undoEnabled = canUndo;
		redoEnabled = canRedo;
		KeyboardModel m = (KeyboardModel)getKeyboard();
		if (m == null) return;
		m.setUndoRedoEnabled(KeyboardModel.ExtraKey.UNDO, canUndo);
		m.setUndoRedoEnabled(KeyboardModel.ExtraKey.REDO, canRedo);
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
