package name.boyle.chris.sgtpuzzles.compat;

import android.app.Activity;

public class ActionBarICS extends ActionBarHoneycomb
{
	static {
		try {
			Class.forName("android.app.ActionBar").getMethod("setIcon", Integer.TYPE);
		} catch (Exception e) {
			throw new RuntimeException(e);
		}
	}

	public ActionBarICS(Activity a)
	{
		super(a);
	}

	@Override
	public boolean setIconAsShortcut(int resId) {
		actionBar.setDisplayHomeAsUpEnabled(true);
		actionBar.setIcon(resId);
		return true;  // shortcut is likely to be noticed, don't need menu item
	}

	@Override
	public boolean hasMenuButton() { return false; }
}
