package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.support.v4.app.TaskStackBuilder;

public class SGTPuzzles extends Activity
{
	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		final Intent intent = getIntent();
		intent.setClass(this, GamePlay.class);
		TaskStackBuilder.create(this)
				.addNextIntentWithParentStack(intent)
				.startActivities();
		finish();
	}
}
