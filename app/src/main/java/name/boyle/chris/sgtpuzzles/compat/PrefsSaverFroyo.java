package name.boyle.chris.sgtpuzzles.compat;

import android.annotation.TargetApi;
import android.app.backup.BackupManager;
import android.content.Context;
import android.content.SharedPreferences.Editor;
import android.os.Build;

@TargetApi(Build.VERSION_CODES.FROYO)
public class PrefsSaverFroyo extends PrefsSaver
{
	private final BackupManager backupMgr;

	static {
		try {
			Class.forName("android.app.backup.BackupManager");
		} catch (Exception e) {
			throw new RuntimeException(e);
		}
	}

    public PrefsSaverFroyo(Context c)
	{
		backupMgr = new BackupManager(c);
	}

	@Override
	public void save(Editor ed) {
		ed.commit();
		backup();
	}

	@Override
	public void backup() {
		backupMgr.dataChanged();
	}
}
