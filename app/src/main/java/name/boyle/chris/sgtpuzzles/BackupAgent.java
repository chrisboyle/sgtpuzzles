package name.boyle.chris.sgtpuzzles;

import android.app.backup.BackupAgentHelper;
import android.app.backup.SharedPreferencesBackupHelper;

public class BackupAgent extends BackupAgentHelper {
	private final String KEY = "preferences";

	/** For some reason PreferenceManager.getDefaultSharedPreferencesName()
	 * is private - but the knowledge of its contents is now used widely
	 * enough that I'm reasonably sure Google wouldn't break it. :-) */
	private final String DEFAULT_PREFS = "name.boyle.chris.sgtpuzzles_preferences";

	@Override
	public void onCreate()
	{
		SharedPreferencesBackupHelper spbh =
				new SharedPreferencesBackupHelper(this, DEFAULT_PREFS);
		addHelper(KEY, spbh);
	}
}
