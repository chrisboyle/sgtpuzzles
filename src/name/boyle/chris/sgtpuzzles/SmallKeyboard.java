package name.boyle.chris.sgtpuzzles;

import java.util.ArrayList;
import java.util.List;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Color;
import android.inputmethodservice.Keyboard;
import android.inputmethodservice.KeyboardView;
import android.util.AttributeSet;
import android.util.Log;

public class SmallKeyboard extends KeyboardView implements KeyboardView.OnKeyboardActionListener
{
	static final String TAG="SmallKeyboard";
	static final int KEYSP = 44;  // dip
	SGTPuzzles parent;
	boolean undoEnabled = false, redoEnabled = false;
	int arrowMode = 1;

	/** Key which can be disabled */
	class DKey extends Keyboard.Key
	{
		boolean enabled;
		DKey(Keyboard.Row r) { super(r); }
	}

	class KeyboardModel extends Keyboard
	{
		int mDefaultWidth = KEYSP, mDefaultHeight = KEYSP,
				mDefaultHorizontalGap = 0, mDefaultVerticalGap = 0,
				mTotalWidth = 0, mTotalHeight = 0;
		Context context;
		List<Key> mKeys;
		int undoKey = -1, redoKey = -1;
		boolean initDone = false;
		/** arrowMode:
		 *  0 = no arrows (untangle)
		 *  1 = arrows + left/right click, unless phone has a d-pad (most games)
		 *  2 = arrows + left click + diagonals, always (inertia) */
		public KeyboardModel(Context context, CharSequence characters,
				int arrowMode, boolean columnMajor, int maxPx,
				boolean undoEnabled, boolean redoEnabled)
		{
			super(context, R.layout.keyboard_template);
			this.context = context;
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
			int maxPxMinusArrows = maxPx;
			if (arrowMode == 1) {
				Configuration c = getResources().getConfiguration();
				boolean visibleDPad = (c.navigation == Configuration.NAVIGATION_DPAD
						|| c.navigation == Configuration.NAVIGATION_TRACKBALL);
				try {
					int hidden = Configuration.class.getField("navigationHidden").getInt(c);
					if (hidden == Configuration.class.getField("NAVIGATIONHIDDEN_YES").getInt(null)) {
						visibleDPad = false;
					}
				} catch (Exception e) {}
				if (visibleDPad) {
					arrowMode = 0;
				}
			}
			if (arrowMode > 0) {
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
			if (majors < 3 && arrowMode > 0) majorPx = (3 - majors) * keyPlusPad;
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
				if (minor == minorsPerMajor - 1 && arrowMode == 0)
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
			if (arrowMode > 0) {
				int[] arrows;
				if (arrowMode == 2) {
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
				for (int arrow : arrows) {
					final DKey key = new DKey(row);
					mKeys.add(key);
					key.width = mDefaultWidth;
					key.height = mDefaultHeight;
					key.gap = mDefaultHorizontalGap;
					key.repeatable = true;
					key.enabled = true;
					key.codes = new int[] { arrow };
					//key.icon = context.getResources().getDrawable(
					//		R.drawable.sym_keyboard_delete);
					key.edgeFlags = 0;  // TODO EDGE_TOP etc
					switch (arrow) {
					case GameView.CURSOR_UP:
						key.x = arrowsRightEdge  - 2*keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.label = "up";
						break;
					case GameView.CURSOR_DOWN:
						key.x = arrowsRightEdge  - 2*keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.label = "dn";
						break;
					case GameView.CURSOR_LEFT:
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge - 2*keyPlusPad;
						key.label = "le";
						break;
					case GameView.CURSOR_RIGHT:
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - 2*keyPlusPad;
						key.label = "rt";
						break;
					case '\n':
						key.x = arrowsRightEdge  - 2*keyPlusPad;
						key.y = arrowsBottomEdge - 2*keyPlusPad;
						key.label = "ok";
						break;
					case ' ': // right click
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.label = "al";
						break;
					case GameView.MOD_NUM_KEYPAD | '7':
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.label = "\\";
						break;
					case GameView.MOD_NUM_KEYPAD | '1':
						key.x = arrowsRightEdge  - 3*keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.label = "/";
						break;
					case GameView.MOD_NUM_KEYPAD | '9':
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge - 3*keyPlusPad;
						key.label = "/";
						break;
					case GameView.MOD_NUM_KEYPAD | '3':
						key.x = arrowsRightEdge  -   keyPlusPad;
						key.y = arrowsBottomEdge -   keyPlusPad;
						key.label = "\\";
						break;
					default:
						Log.wtf(TAG, "unknown key in keyboard: "+arrow);
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
			// Ugly hack for 1.5 compatibility: invalidateKey() is 1.6 and
			// invalidate() doesn't work on KeyboardView, so try to change
			// shift state (and claim, when asked below, that everything
			// needs a redraw).
			if (initDone) {
				SmallKeyboard.this.parent.runOnUiThread(new Runnable(){public void run(){
					SmallKeyboard.this.setShifted(false);
				}});
			}
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
		/** Ugly hack for 1.5 compatibility: invalidate() doesn't work on
		 *  KeyboardView, so pretend we've changed shift state so everything
		 *  needs redrawing on that basis. */
		@Override
		public boolean setShifted(boolean shifted) { return true; }
	}

	public SmallKeyboard(Context c, boolean undoEnabled, boolean redoEnabled)
	{
		this(c,null);
		this.undoEnabled = undoEnabled;
		this.redoEnabled = redoEnabled;
	}

	public SmallKeyboard(Context c, AttributeSet a)
	{
		super(c, a);
		parent = isInEditMode() ? null : (SGTPuzzles)c;
		setBackgroundColor( Color.BLACK );
		setOnKeyboardActionListener(this);
		if (isInEditMode()) setKeys("123456\bur", 1);
	}

	/** Horrible hack for edit mode... */
	@Override
	public Resources getResources()
	{
		Resources r = super.getResources();
		if (!isInEditMode()) return r;

		return new Resources(r.getAssets(), r.getDisplayMetrics(), r.getConfiguration()) {
			public boolean getBoolean(int id) {
				// provide com.android.internal.R.bool.config_swipeDisambiguation
				if (id == 0x1110015) return false; // 3.2
				if (id == 0x1110018) return false; // 3.1
				if (id == 0x1110019) return false; // 3.0
				if (id == 0x10d000e) return false; // 2.3.3
				if (id == 0x10d000b) return false; // 2.2
				if (id == 0x10d0009) return false; // 2.1-update1
				// Other versions explode with NPE in PopupWindow(Context) :-(
				return super.getBoolean(id);
			}
		};
	}

	CharSequence lastKeys = "";
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
		setKeyboard(new KeyboardModel(getContext(), lastKeys, arrowMode,
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
