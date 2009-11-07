package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.inputmethodservice.Keyboard;
import android.inputmethodservice.KeyboardView;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

public class SmallKeyboard extends KeyboardView implements KeyboardView.OnKeyboardActionListener
{
	static final String TAG="SmallKeyboard";
	SGTPuzzles parent;

	class KeyboardModel extends Keyboard
	{
		int mDefaultWidth = 40, mDefaultHeight = 40, mDefaultHorizontalGap = 0, mDefaultVerticalGap = 0, mTotalWidth, mTotalHeight;
		Context context;
		List<Key> mKeys;
		public KeyboardModel(Context context, CharSequence characters, boolean columnMajor)
		{
			super(context, R.layout.keyboard_template);
			DisplayMetrics dm = context.getResources().getDisplayMetrics();
			final int maxMajorPx = columnMajor ? dm.heightPixels : dm.widthPixels;
			int minorPx = 0;
			int majorPx = 0;
			int minor = 0;
			mTotalWidth = 0;
			mKeys = new ArrayList<Key>();
			
			Row row = new Row(this);
			row.defaultHeight = mDefaultHeight;
			row.defaultWidth = mDefaultWidth;
			row.defaultHorizontalGap = mDefaultHorizontalGap;
			row.verticalGap = mDefaultVerticalGap;
			final int keyPlusPad = columnMajor
					? mDefaultHeight + mDefaultVerticalGap
					: mDefaultWidth + mDefaultHorizontalGap;
			// How many rows do we need?
			final int majors = (int)Math.ceil((double)(characters.length() * keyPlusPad)/maxMajorPx);
			// Spread the keys as evenly as possible
			final int minorsPerMajor = (int)Math.ceil((double)characters.length() / majors);
			for (int i = 0; i < characters.length(); i++) {
				char c = characters.charAt(i);
				if (minor >= minorsPerMajor) {
					minorPx = (characters.length() - i < minorsPerMajor)  // last row
						? (int)Math.round((((double)minorsPerMajor - (characters.length() - i))/2) * keyPlusPad)
						: 0;
					majorPx += columnMajor
						? mDefaultHorizontalGap + mDefaultWidth
						: mDefaultVerticalGap + mDefaultHeight;
					minor = 0;
				}
				final Key key = new Key(row);
				key.edgeFlags = 0;
				// No two of these flags are mutually exclusive
				if (i < minorsPerMajor)               key.edgeFlags |= columnMajor ? EDGE_LEFT   : EDGE_TOP;
				if (i / minorsPerMajor + 1 == majors) key.edgeFlags |= columnMajor ? EDGE_RIGHT  : EDGE_BOTTOM;
				if (minor == 0)                       key.edgeFlags |= columnMajor ? EDGE_TOP    : EDGE_LEFT;
				if (minor == minorsPerMajor - 1)      key.edgeFlags |= columnMajor ? EDGE_BOTTOM : EDGE_RIGHT;
				key.x = columnMajor ? majorPx : minorPx;
				key.y = columnMajor ? minorPx : majorPx;
				key.width = mDefaultWidth;
				key.height = mDefaultHeight;
				key.gap = mDefaultHorizontalGap;
				if (c=='\b') {
					key.icon = context.getResources().getDrawable(R.drawable.sym_keyboard_delete);
					key.repeatable = true;
				} else {
					key.label = String.valueOf(c);
				}
				key.codes = new int[] { c };
				minor++;
				minorPx += keyPlusPad;
				mKeys.add(key);
				if (columnMajor) {
					if (minorPx > mTotalHeight) mTotalHeight = minorPx;
				} else {
					if (minorPx > mTotalWidth) mTotalWidth = minorPx;
				}
			}
			if (columnMajor) {
				mTotalWidth = majorPx + mDefaultWidth;
			} else {
				mTotalHeight = majorPx + mDefaultHeight;
			}
		}
		@Override
		public List<Key> getKeys() { return mKeys; }
		@Override
		public int[] getNearestKeys(int x, int y)
		{
			for (int i=0; i<mKeys.size(); i++) {
				if (mKeys.get(i).isInside(x,y)) return new int[]{i};
			}
			return new int[0];
		}
		@Override
		public int getHeight() { return mTotalHeight; }
		@Override
		public int getMinWidth() { return mTotalWidth; }
	}

	public SmallKeyboard(Context c, AttributeSet a)
	{
		super(c,a);
		parent = (SGTPuzzles)c;
		setOnKeyboardActionListener(this);
	}

	public void setKeys(CharSequence keys, boolean landscape)
	{
		setKeyboard(new KeyboardModel(parent, keys, landscape));
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
