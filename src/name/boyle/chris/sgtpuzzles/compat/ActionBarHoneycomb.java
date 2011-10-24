package name.boyle.chris.sgtpuzzles.compat;

import android.app.ActionBar;
import android.app.Activity;
import android.view.MenuItem;
import android.view.View;

public class ActionBarHoneycomb extends ActionBarCompat
{
	protected ActionBar actionBar;

	static {
		try {
			Class.forName("android.app.ActionBar");
		} catch (Exception e) {
			throw new RuntimeException(e);
		}
	}

	public ActionBarHoneycomb(Activity a)
	{
		actionBar = a.getActionBar();
		if (actionBar == null) throw new RuntimeException("no Actionbar after all");
	}

	@Override
	public void setIcon(int resId)
	{
		// We want a custom icon, but can't have one on Honeycomb, so have no icon
		actionBar.setDisplayShowHomeEnabled(false);
	}

	@Override
	public void menuItemSetShowAsAction(MenuItem mi, int flags) {
		mi.setShowAsAction(flags);
	}

	@Override
	public void lightsOut(View v, boolean dim) {
		v.setSystemUiVisibility(dim ? View.SYSTEM_UI_FLAG_LOW_PROFILE
				: View.SYSTEM_UI_FLAG_VISIBLE);
	}
}
