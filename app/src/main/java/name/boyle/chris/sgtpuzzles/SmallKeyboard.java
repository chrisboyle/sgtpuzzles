package name.boyle.chris.sgtpuzzles;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.inputmethodservice.Keyboard;
import android.inputmethodservice.KeyboardView;
import android.support.annotation.NonNull;
import android.support.v4.content.ContextCompat;
import android.util.AttributeSet;
import android.util.Log;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

public class SmallKeyboard extends KeyboardView implements KeyboardView.OnKeyboardActionListener
{
	private static final String TAG = "SmallKeyboard";
	private static final String SEEN_SWAP_L_R_TOAST = "seenSwapLRToast";
	private final GamePlay parent;
	private boolean undoEnabled = false, redoEnabled = false, followEnabled = true;
	private String backendForIcons;
	public static final char SWAP_L_R_KEY = '*';
	private boolean swapLR = false;
	private final SharedPreferences state;
	private final PrefsSaver prefsSaver;

	enum ArrowMode {
		NO_ARROWS,  // untangle
		ARROWS_ONLY,  // cube
		ARROWS_LEFT_CLICK,  // flip, filling, guess, keen, solo, towers, unequal
		ARROWS_LEFT_RIGHT_CLICK,  // unless phone has a d-pad (most games)
		ARROWS_DIAGONALS;  // Inertia

		boolean hasArrows() { return this != NO_ARROWS; }
	}
	private ArrowMode arrowMode = ArrowMode.NO_ARROWS;

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
		int undoKey = -1, redoKey = -1, primaryKey = -1, secondaryKey = -1, swapLRKey = -1;
		boolean followEnabled = true;
		boolean initDone = false;
		boolean swapLR = false;
		final String backendForIcons;
		private static final Map<String, String> SHARED_ICONS = new LinkedHashMap<>();
		static {
			SHARED_ICONS.put("blackbox_sym_key_mouse_right", "square_empty");
			SHARED_ICONS.put("bridges_sym_key_mouse_left", "line");
			SHARED_ICONS.put("bridges_sym_key_l", "lock");
			SHARED_ICONS.put("filling_sym_key_mouse_left", "square_filled");
			SHARED_ICONS.put("galaxies_sym_key_mouse_left", "line");
			SHARED_ICONS.put("guess_sym_key_mouse_right", "lock");
			SHARED_ICONS.put("inertia_sym_key_mouse_left", "ic_action_solve");
			SHARED_ICONS.put("keen_sym_key_mouse_left", "square_corner");
			SHARED_ICONS.put("keen_sym_key_m", "square_corner_123");
			SHARED_ICONS.put("lightup_sym_key_mouse_left", "square_circle");
			SHARED_ICONS.put("lightup_sym_key_mouse_right", "square_dot");
			SHARED_ICONS.put("loopy_sym_key_mouse_left", "line");
			SHARED_ICONS.put("loopy_sym_key_mouse_right", "no_line");
			SHARED_ICONS.put("mines_sym_key_mouse_left", "square_empty");
			SHARED_ICONS.put("net_sym_key_a", "rotate_left_90");
			SHARED_ICONS.put("net_sym_key_s", "lock");
			SHARED_ICONS.put("net_sym_key_d", "rotate_right_90");
			SHARED_ICONS.put("net_sym_key_f", "rotate_left_180");
			SHARED_ICONS.put("pattern_sym_key_mouse_left", "square_empty");  // black & white, really
			SHARED_ICONS.put("pattern_sym_key_mouse_right", "square_filled");
			SHARED_ICONS.put("pearl_sym_key_mouse_left", "line");
			SHARED_ICONS.put("pearl_sym_key_mouse_right", "no_line");
			SHARED_ICONS.put("range_sym_key_mouse_left", "square_filled");
			SHARED_ICONS.put("range_sym_key_mouse_right", "square_dot");
			SHARED_ICONS.put("rect_sym_key_mouse_left", "square_empty");
			SHARED_ICONS.put("rect_sym_key_mouse_right", "no_line");
			SHARED_ICONS.put("samegame_sym_key_mouse_left", "square_dot");
			SHARED_ICONS.put("samegame_sym_key_mouse_right", "square_empty");
			SHARED_ICONS.put("singles_sym_key_mouse_left", "square_filled");
			SHARED_ICONS.put("singles_sym_key_mouse_right", "square_circle");
			SHARED_ICONS.put("solo_sym_key_mouse_left", "square_corner");
			SHARED_ICONS.put("solo_sym_key_m", "square_corner_123");
			SHARED_ICONS.put("tents_sym_key_mouse_right", "square_filled");
			SHARED_ICONS.put("towers_sym_key_mouse_left", "square_corner");
			SHARED_ICONS.put("towers_sym_key_m", "square_corner_123");
			SHARED_ICONS.put("twiddle_sym_key_mouse_left", "rotate_left_90");
			SHARED_ICONS.put("twiddle_sym_key_mouse_right", "rotate_right_90");
			SHARED_ICONS.put("undead_sym_key_mouse_left", "square_corner");
			SHARED_ICONS.put("unequal_sym_key_mouse_left", "square_corner");
			SHARED_ICONS.put("unequal_sym_key_m", "square_corner_123");
			SHARED_ICONS.put("unruly_sym_key_mouse_left", "square_empty");
			SHARED_ICONS.put("unruly_sym_key_mouse_right", "square_filled");
		}

		public KeyboardModel(final Context context, final KeyboardView keyboardView,
				final boolean isInEditMode, final CharSequence characters,
				final ArrowMode requestedArrowMode, final boolean columnMajor, final int maxPx,
				final boolean undoEnabled, final boolean redoEnabled, final boolean followEnabled,
				final String backendForIcons)
		{
			super(context, R.layout.keyboard_template);
			this.context = context;
			this.keyboardView = keyboardView;
			this.followEnabled = followEnabled;
			mDefaultWidth = mDefaultHeight =
					context.getResources().getDimensionPixelSize(R.dimen.keySize);
			mKeys = new ArrayList<>();
			this.backendForIcons = backendForIcons;

			Row row = new Row(this);
			row.defaultHeight = mDefaultHeight;
			row.defaultWidth = mDefaultWidth;
			row.defaultHorizontalGap = mDefaultHorizontalGap;
			row.verticalGap = mDefaultVerticalGap;
			final int keyPlusPad = columnMajor
					? mDefaultHeight + mDefaultVerticalGap
					: mDefaultWidth + mDefaultHorizontalGap;

			final ArrowMode arrowMode = isInEditMode ? ArrowMode.ARROWS_LEFT_RIGHT_CLICK : requestedArrowMode;

			final boolean isDiagonals = arrowMode == ArrowMode.ARROWS_DIAGONALS;
			final int arrowRows = arrowMode.hasArrows() ? isDiagonals ? 3 : 2 : 0;
			final int arrowCols = arrowMode.hasArrows() ? 3 : 0;
			final int arrowMajors = columnMajor ? arrowCols : arrowRows;
			final int arrowMinors = columnMajor ? arrowRows : arrowCols;

			// How many keys can we fit on a row?
			final int maxPxMinusArrows = arrowMode.hasArrows() ? maxPx - arrowMinors * keyPlusPad : maxPx;
			final int minorsPerMajor = (int)Math.floor(((double)maxPxMinusArrows) / keyPlusPad);
			final int minorsPerMajorWithoutArrows = (int)Math.floor(((double)maxPx) / keyPlusPad);
			// How many rows do we need?
			int majors = (int)Math.ceil(((double)characters.length())/minorsPerMajor);
			int overlappingMajors = majors;
			if (majors > arrowMajors) {
				overlappingMajors = arrowMajors + (int)Math.ceil(((double)characters.length() - (arrowMajors * minorsPerMajor))/minorsPerMajorWithoutArrows);
			}
			final int majorWhereArrowsStart;
			if (overlappingMajors < majors) {  // i.e. extending over arrows saves us anything
				majors = overlappingMajors;
				majorWhereArrowsStart = Math.max(0, majors - arrowMajors);
			} else {
				majorWhereArrowsStart = -1;
			}
			mEmpty = (majors == 0) && ! arrowMode.hasArrows();

			if (majors > 0) {
				final int minorStartPx = (int) Math.round(((double) maxPxMinusArrows
						- (minorsPerMajor * keyPlusPad)) / 2);
				final int majorStartPx = (majors < 3 && arrowMode.hasArrows()) ? (arrowMajors - majors) * keyPlusPad : 0;
				addCharacters(context, characters, columnMajor, undoEnabled, redoEnabled,
						row, keyPlusPad, arrowMode, minorsPerMajor, minorsPerMajorWithoutArrows,
						majorWhereArrowsStart, majors, minorStartPx, majorStartPx);
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
			final int spaceAfterKeys = Math.round((columnMajor ? mDefaultWidth : mDefaultHeight)/12.f);
			final int maxWidth = Math.max(charsWidth, arrowCols * keyPlusPad);
			mTotalWidth  = columnMajor ? maxWidth + spaceAfterKeys : charsWidth + (arrowCols * keyPlusPad);
			final int maxHeight = Math.max(charsHeight, arrowRows * keyPlusPad);
			mTotalHeight = columnMajor ? charsHeight + arrowRows * keyPlusPad : maxHeight + spaceAfterKeys;

			if (arrowMode.hasArrows()) {
				final int arrowsRightEdge = columnMajor ? maxWidth : maxPx,
						arrowsBottomEdge = columnMajor ? maxPx : maxHeight;
				int maybeTop  = (!columnMajor && majors <= arrowRows) ? EDGE_TOP : 0;
				int maybeLeft = ( columnMajor && majors <= arrowRows) ? EDGE_LEFT : 0;
				addArrows(context, row, keyPlusPad, arrowMode, arrowRows,
						arrowsRightEdge, arrowsBottomEdge, maybeTop, maybeLeft);
			}
			initDone = true;
		}

		private void addCharacters(final Context context, final CharSequence characters,
					final boolean columnMajor, final boolean undoEnabled, final boolean redoEnabled,
					final Row row, final int keyPlusPad, final ArrowMode arrowMode,
					final int minorsPerMajor, final int minorsPerMajorWithoutArrows, final int majorWhereArrowsStart,
					final int majors, final int minorStartPx, final int majorStartPx) {
			final int length = characters.length();
			int minorPx = minorStartPx;
			int majorPx = majorStartPx;
			int minor = 0;
			int major = -1;
			// avoid having a last major with a single character
			for (int i = 0; i < length; i++) {
				final int charsThisMajor = (major < majorWhereArrowsStart) ? minorsPerMajorWithoutArrows : minorsPerMajor;
				final char c = characters.charAt(i);
				final boolean preventingSingleton = (major == majors - 2) && i == length - 2;
				if (i == 0 || minor >= charsThisMajor || preventingSingleton) {
					major++;
					minor = 0;
					final boolean willPreventSingleton = ((length - i) % minorsPerMajor == 1);
					final int charsNextMajor = (major == majors - 1) ? (length - i)
							: (major == majors - 2 && willPreventSingleton) ? (minorsPerMajor - 1)
							: minorsPerMajor;
					minorPx = minorStartPx + (int)Math.round((((double)minorsPerMajor - charsNextMajor)/2) * keyPlusPad);
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
				case 'U':
					undoKey = mKeys.size() - 1;
					key.repeatable = true;
					setUndoRedoEnabled(ExtraKey.UNDO, undoEnabled);
					break;
				case 'R':
					redoKey = mKeys.size() - 1;
					key.repeatable = true;
					setUndoRedoEnabled(ExtraKey.REDO, redoEnabled);
					break;
				case '\b':
					key.icon = ContextCompat.getDrawable(context, R.drawable.sym_key_backspace);
					key.repeatable = true;
					key.enabled = true;
					break;
				case SWAP_L_R_KEY:
					swapLRKey = mKeys.size() - 1;
					key.icon = ContextCompat.getDrawable(context, R.drawable.ic_action_swap_l_r);
					key.sticky = true;
					key.enabled = true;
					break;
				default:
					trySpecificCharacterIcon(context.getResources(), key, c);
					key.enabled = true;
					break;
			}
			key.codes = new int[] { c };
		}

		private void addArrows(Context context, Row row, int keyPlusPad, final ArrowMode arrowMode,
				int arrowRows, int arrowsRightEdge, int arrowsBottomEdge, int maybeTop, int maybeLeft) {
			int[] arrows;
			final boolean isDiagonals = (arrowMode == ArrowMode.ARROWS_DIAGONALS);
			switch (arrowMode) {
				case ARROWS_DIAGONALS:
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
					break;
				case ARROWS_ONLY:
					arrows = new int[] {
							GameView.CURSOR_UP,
							GameView.CURSOR_DOWN,
							GameView.CURSOR_LEFT,
							GameView.CURSOR_RIGHT};
					break;
				case ARROWS_LEFT_CLICK:
					arrows = new int[] {
							GameView.CURSOR_UP,
							GameView.CURSOR_DOWN,
							GameView.CURSOR_LEFT,
							GameView.CURSOR_RIGHT,
							'\n'};
					break;
				case ARROWS_LEFT_RIGHT_CLICK:
					arrows = new int[] {
							GameView.CURSOR_UP,
							GameView.CURSOR_DOWN,
							GameView.CURSOR_LEFT,
							GameView.CURSOR_RIGHT,
							'\n',
							' ' };
					break;
				default:
					throw new RuntimeException("bogus arrow mode!");
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
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_north);
					key.edgeFlags = maybeTop;
					break;
				case GameView.CURSOR_DOWN:
					key.x = arrowsRightEdge  - 2*keyPlusPad;
					key.y = arrowsBottomEdge -   keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_south);
					key.edgeFlags = EDGE_BOTTOM;
					break;
				case GameView.CURSOR_LEFT:
					key.x = arrowsRightEdge  - 3*keyPlusPad;
					key.y = arrowsBottomEdge - leftRightRow*keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_west);
					key.edgeFlags = bottomIf2Row | maybeLeft;
					break;
				case GameView.CURSOR_RIGHT:
					key.x = arrowsRightEdge  -   keyPlusPad;
					key.y = arrowsBottomEdge - leftRightRow*keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_east);
					key.edgeFlags = bottomIf2Row | EDGE_RIGHT;
					break;
				case '\n':
					primaryKey = mKeys.size() - 1;
					key.x = arrowsRightEdge  - (isDiagonals ? 2 : 3) * keyPlusPad;
					key.y = arrowsBottomEdge - 2*keyPlusPad;
					key.icon = followEnabled ? trySpecificIcon(context.getResources(), R.drawable.sym_key_mouse_left) : null;
					key.enabled = followEnabled;
					key.edgeFlags = maybeTopIf2Row;
					break;
				case ' ': // right click
					secondaryKey = mKeys.size() - 1;
					key.x = arrowsRightEdge  -   keyPlusPad;
					key.y = arrowsBottomEdge - arrowRows*keyPlusPad;
					key.icon = trySpecificIcon(context.getResources(), R.drawable.sym_key_mouse_right);
					key.edgeFlags = maybeTop | EDGE_RIGHT;
					break;
				case GameView.MOD_NUM_KEYPAD | '7':
					key.x = arrowsRightEdge  - 3*keyPlusPad;
					key.y = arrowsBottomEdge - 3*keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_north_west);
					key.edgeFlags = maybeTop | maybeLeft;
					break;
				case GameView.MOD_NUM_KEYPAD | '1':
					key.x = arrowsRightEdge  - 3*keyPlusPad;
					key.y = arrowsBottomEdge -   keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_south_west);
					key.edgeFlags = EDGE_BOTTOM | maybeLeft;
					break;
				case GameView.MOD_NUM_KEYPAD | '9':
					key.x = arrowsRightEdge  -   keyPlusPad;
					key.y = arrowsBottomEdge - 3*keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_north_east);
					key.edgeFlags = maybeTop | EDGE_RIGHT;
					break;
				case GameView.MOD_NUM_KEYPAD | '3':
					key.x = arrowsRightEdge  -   keyPlusPad;
					key.y = arrowsBottomEdge -   keyPlusPad;
					key.icon = ContextCompat.getDrawable(context,
							R.drawable.sym_key_south_east);
					key.edgeFlags = EDGE_BOTTOM | EDGE_RIGHT;
					break;
				default:
					Log.e(TAG, "unknown key in keyboard: " + arrow);
					break;
				}
			}
		}

		protected void setSwapLR(final boolean swap, final boolean fromKeyPress) {
			if (swap != swapLR) {
				swapLR = swap;
				if (primaryKey != -1 && secondaryKey != -1) {
					final Key left = getKeys().get(primaryKey);
					final Key right = getKeys().get(secondaryKey);
					Drawable tmp = left.icon;
					left.icon = right.icon;
					right.icon = tmp;
					int[] tmpCodes = left.codes;
					left.codes = right.codes;
					right.codes = tmpCodes;
					keyboardView.invalidateKey(primaryKey);
					keyboardView.invalidateKey(secondaryKey);
				}
				if (!fromKeyPress && swapLRKey != -1) {
					mKeys.get(swapLRKey).on = true;
					keyboardView.invalidateKey(swapLRKey);
				}
			}
		}

		private Drawable trySpecificIcon(final Resources resources, final int orig) {
			final String name = resources.getResourceEntryName(orig);
			final String specificName = backendForIcons + "_" + name;
			final String sharedIcon = SHARED_ICONS.get(specificName);
			final int specific = resources.getIdentifier(
					(sharedIcon != null) ? sharedIcon : specificName,
					"drawable", context.getPackageName());
			return ContextCompat.getDrawable(context, (specific == 0) ? orig : specific);
		}

		private void trySpecificCharacterIcon(final Resources resources, final Key key, final char c) {
			final int icon;
			if (Character.isUpperCase(c)) {
				final String specificName = backendForIcons + "_sym_key_" + Character.toLowerCase(c);
				final String sharedIcon = SHARED_ICONS.get(specificName);
				icon = resources.getIdentifier(
						(sharedIcon != null) ? sharedIcon : specificName,
						"drawable", context.getPackageName());
			} else {
				icon = 0;  // data entry letter never gets an icon
			}
			if (icon == 0) {
				// Not proud of this, but: I'm using uppercase letters to mean it's a command as
				// opposed to data entry (Mark all squares versus enter 'm'). But I still want the
				// keys for data entry to be uppercase in unequal because that matches the board.
				final boolean drawUppercaseForLowercase = (backendForIcons != null && backendForIcons.equals("unequal"));
				key.label = String.valueOf(drawUppercaseForLowercase ? Character.toUpperCase(c) : c);
			} else {
				key.icon = ContextCompat.getDrawable(context, icon);
			}
		}

		enum ExtraKey { UNDO, REDO }

		void setUndoRedoEnabled(ExtraKey which, boolean enabled)
		{
			final boolean redo = (which == ExtraKey.REDO);
			int i = redo ? redoKey : undoKey;
			if (i < 0) return;
			DKey k = (DKey)mKeys.get(i);
			k.icon = ContextCompat.getDrawable(context, redo ?
					enabled ? R.drawable.ic_action_redo
							: R.drawable.ic_action_redo_disabled :
					enabled ? R.drawable.ic_action_undo
							: R.drawable.ic_action_undo_disabled);
			k.enabled = enabled;
			if (initDone) keyboardView.invalidateKey(i);
		}

		void setInertiaFollowEnabled(final boolean enabled) {
			followEnabled = enabled;
			if (primaryKey == -1 || swapLR) return;  // can't swapLR in inertia
			final DKey k = (DKey)mKeys.get(primaryKey);
			k.enabled = enabled;
			k.icon = enabled ? trySpecificIcon(context.getResources(), R.drawable.sym_key_mouse_left) : null;
			if (initDone) keyboardView.invalidateKey(primaryKey);
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

    @SuppressWarnings({"SameParameterValue", "WeakerAccess"})  // used by layout
    public SmallKeyboard(Context c, AttributeSet a)
	{
		super(c, a);
		parent = isInEditMode() ? null : (GamePlay)c;
		setOnKeyboardActionListener(this);
		if (isInEditMode()) setKeys("123456\bur", ArrowMode.ARROWS_LEFT_RIGHT_CLICK, "");
		setPreviewEnabled(false);  // can't get icon buttons to darken properly and there are positioning bugs anyway
		state = c.getSharedPreferences(GamePlay.STATE_PREFS_NAME, Context.MODE_PRIVATE);
		prefsSaver = PrefsSaver.get(c);
	}

	private CharSequence lastKeys = "";
	public void setKeys(final CharSequence keys, final ArrowMode arrowMode, final String backendForIcons)
	{
		lastKeys = keys;
		this.arrowMode = arrowMode;
		this.backendForIcons = backendForIcons;
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
				arrowMode, landscape, maxPx, undoEnabled, redoEnabled, followEnabled, backendForIcons);
		setKeyboard(model);
		model.setSwapLR(swapLR, false);
		if (model.isEmpty()) {
			setMeasuredDimension(0, 0);
		} else {
			super.onMeasure(wSpec, hSpec);
		}
	}

	@Override
	public void onDraw(@NonNull Canvas canvas) {
		final Keyboard keyboard = getKeyboard();
		if (keyboard != null && !((KeyboardModel) keyboard).isEmpty()) {
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

	void setInertiaFollowEnabled(final boolean enabled) {
		followEnabled = enabled;
		KeyboardModel m = (KeyboardModel)getKeyboard();
		if (m == null) return;
		m.setInertiaFollowEnabled(enabled);
	}

	void setSwapLR(final boolean swap) {
		swapLR = swap;
		KeyboardModel m = (KeyboardModel)getKeyboard();
		if (m == null) return;
		m.setSwapLR(swap, false);
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
	public void onKey(int k, int[] ignore) {
		if (k == '*') {
			final SmallKeyboard.KeyboardModel model = (KeyboardModel) getKeyboard();
			final Keyboard.Key key = model.getKeys().get(model.swapLRKey);
			model.setSwapLR(key.on, true);
			parent.setSwapLR(key.on);
			Utils.toastFirstFewTimes(getContext(), state, prefsSaver, SEEN_SWAP_L_R_TOAST, 4,
					key.on ? R.string.toast_swap_l_r_on : R.string.toast_swap_l_r_off);
		} else {
			parent.sendKey(0,0,k);
		}
	}
}
