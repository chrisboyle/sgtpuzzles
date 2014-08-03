package name.boyle.chris.sgtpuzzles.compat;

import android.annotation.TargetApi;
import android.content.Context;
import android.content.SharedPreferences.Editor;
import android.os.Build;

@TargetApi(Build.VERSION_CODES.GINGERBREAD)
public class PrefsSaverGingerbread extends PrefsSaverFroyo
{
	static {
		try {
			Editor.class.getMethod("apply");
		} catch (Exception e) {
			throw new RuntimeException(e);
		}
	}

	public PrefsSaverGingerbread(Context c)
	{
		super(c);
	}

	@Override
	public void save(Editor ed) {
		ed.apply();
		backup();
	}
}
