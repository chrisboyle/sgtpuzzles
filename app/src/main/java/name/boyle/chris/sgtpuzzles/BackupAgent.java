package name.boyle.chris.sgtpuzzles;

import android.annotation.TargetApi;
import android.app.backup.BackupAgentHelper;
import android.app.backup.SharedPreferencesBackupHelper;
import android.os.Build;

@TargetApi(Build.VERSION_CODES.FROYO)
public class BackupAgent extends BackupAgentHelper {

    @Override
	public void onCreate()
	{
        /* For some reason PreferenceManager.getDefaultSharedPreferencesName()
	       is private - but the knowledge of its contents is now used widely
	       enough that I'm reasonably sure Google wouldn't break it. :-) */
        String DEFAULT_PREFS = "name.boyle.chris.sgtpuzzles_preferences";
        SharedPreferencesBackupHelper helper =
				new SharedPreferencesBackupHelper(this, DEFAULT_PREFS);
        String KEY = "preferences";
        addHelper(KEY, helper);
	}
}
