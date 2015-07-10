package name.boyle.chris.sgtpuzzles.compat;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.os.Build;
import android.view.View;

@TargetApi(Build.VERSION_CODES.HONEYCOMB)
@SuppressLint("InlinedApi")
public class SysUIVisSetter {
	public static void set(View v, boolean fullScreen) {
		if (v == null) return;
		v.setSystemUiVisibility(fullScreen
				? View.SYSTEM_UI_FLAG_LOW_PROFILE
				: View.SYSTEM_UI_FLAG_VISIBLE);
	}
}
