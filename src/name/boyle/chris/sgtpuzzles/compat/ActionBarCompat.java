package name.boyle.chris.sgtpuzzles.compat;

import android.app.Activity;
import android.view.MenuItem;
import android.view.View;

public abstract class ActionBarCompat
{
	public abstract boolean setIconAsShortcut(int resId);

	public abstract void menuItemSetShowAsAction(MenuItem mi, int flags);

	public abstract void lightsOut(View v, boolean dim);

	// from MenuItem
	public static final int
			SHOW_AS_ACTION_IF_ROOM   = 1,
			SHOW_AS_ACTION_ALWAYS    = 2,
			SHOW_AS_ACTION_WITH_TEXT = 4;

	// from Window
	public static final int
			FEATURE_ACTION_BAR = 8;

	public static interface OnMenuVisibilityListener
	{
		void onMenuVisibilityChanged(boolean isVisible);
	}

	public abstract void addOnMenuVisibilityListener(OnMenuVisibilityListener l);

	public abstract boolean isShowing();

	public boolean hasMenuButton() { return true; }

	public static ActionBarCompat get(Activity a)
	{
		try {
			return new ActionBarICS(a);
		} catch (Throwable t) {
			try {
				return new ActionBarHoneycomb(a);
			} catch (Throwable t2) {
				return null;
			}
		}
	}

	public static boolean earlyHasActionBar() {
		// can't just getActionBar() because we're before setContentView()
		try {
			Class.forName("android.app.ActionBar");
			return true;
		} catch (Exception e) {
			return false;
		}
	}
}
