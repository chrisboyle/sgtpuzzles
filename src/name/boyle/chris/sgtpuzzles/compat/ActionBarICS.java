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
	public void setIcon(int resId) {
		actionBar.setIcon(resId);
	}

	@Override
	public boolean hasMenuButton() { return false; }
}
