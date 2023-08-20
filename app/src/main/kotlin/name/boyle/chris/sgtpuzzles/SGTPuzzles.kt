package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import androidx.core.app.TaskStackBuilder;

public class SGTPuzzles extends Activity
{
	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		if (GameGenerator.executableIsMissing(this)) {
			finish();
			return;
		}
		final Intent intent = getIntent();
		intent.setClass(this, GamePlay.class);
		TaskStackBuilder.create(this)
				.addNextIntentWithParentStack(intent)
				.startActivities();
		finish();
	}
}
