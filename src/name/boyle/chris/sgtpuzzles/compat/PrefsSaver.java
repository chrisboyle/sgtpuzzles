package name.boyle.chris.sgtpuzzles.compat;

import android.content.Context;
import android.content.SharedPreferences.Editor;

public abstract class PrefsSaver
{
	public abstract void save(Editor ed);

	public abstract void backup();

	public static PrefsSaver get(Context c)
	{
		try {
			return new PrefsSaverGingerbread(c);
		} catch(Throwable t) {
			try {
				return new PrefsSaverFroyo(c);
			} catch (Throwable t2) {
				return new PrefsSaverLegacy();
			}
		}
	}
}
