package name.boyle.chris.sgtpuzzles.compat;

import android.annotation.TargetApi;
import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH)
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

	@Override
	public void lightsOut(Window w, View v, boolean dim) {
		super.lightsOut(w, v, dim);
		if (dim) {
			w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		} else {
			w.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		}
	}
}
