package name.boyle.chris.sgtpuzzles

import android.app.backup.BackupAgentHelper
import android.app.backup.SharedPreferencesBackupHelper

class BackupAgent : BackupAgentHelper() {
    override fun onCreate() {
        /* For some reason PreferenceManager.getDefaultSharedPreferencesName()
           is private - but the knowledge of its contents is now used widely
           enough that I'm reasonably sure Google wouldn't break it. :-) */
        val defaultPrefs = "name.boyle.chris.sgtpuzzles_preferences"
        val helper = SharedPreferencesBackupHelper(this, defaultPrefs)
        addHelper("preferences", helper)
    }
}