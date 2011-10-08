package name.boyle.chris.sgtpuzzles;

import java.util.List;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.Bundle;
import android.os.PatternMatcher;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;

public class CrashHandler extends Activity
{
	public static final String LOG_COLLECTOR_PACKAGE_NAME = "com.xtralogic.android.logcollector";//$NON-NLS-1$
	public static final String ACTION_SEND_LOG = "com.xtralogic.logcollector.intent.action.SEND_LOG";//$NON-NLS-1$
	public static final String EXTRA_SEND_INTENT_ACTION = "com.xtralogic.logcollector.intent.extra.SEND_INTENT_ACTION";//$NON-NLS-1$
	public static final String EXTRA_DATA = "com.xtralogic.logcollector.intent.extra.DATA";//$NON-NLS-1$
	public static final String EXTRA_ADDITIONAL_INFO = "com.xtralogic.logcollector.intent.extra.ADDITIONAL_INFO";//$NON-NLS-1$
	public static final String EXTRA_FILTER_SPECS = "com.xtralogic.logcollector.intent.extra.FILTER_SPECS";//$NON-NLS-1$
	public static final String EXTRA_FORMAT = "com.xtralogic.logcollector.intent.extra.FORMAT";//$NON-NLS-1$
	protected void onCreate(Bundle state)
	{
		super.onCreate(state);
		setTitle(R.string.crashtitle);
		setContentView(R.layout.crashhandler);
		final Button b = (Button)findViewById(R.id.report),
			   c = (Button)findViewById(R.id.close);
		final CheckBox cl = (CheckBox)findViewById(R.id.forget);
		b.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			if (cl.isChecked()) clearState();
			final PackageManager packageManager = getPackageManager();
			final Intent intent = new Intent(ACTION_SEND_LOG);
			List<ResolveInfo> list = packageManager.queryIntentActivities(intent,
					PackageManager.MATCH_DEFAULT_ONLY);
			final boolean isInstalled = list.size() > 0;
			if (! isInstalled) {
				IntentFilter i = new IntentFilter(Intent.ACTION_PACKAGE_ADDED);
				i.addDataScheme("package");
				i.addDataPath(LOG_COLLECTOR_PACKAGE_NAME, PatternMatcher.PATTERN_LITERAL);
				registerReceiver(new BroadcastReceiver() {
					@Override public void onReceive(Context context, Intent intent) {
						// User has just installed Log Collector - let's start
						// it, with specific options, before they get tempted to
						// click "Open" themselves with no options and no
						// destination
						sendLog();
						unregisterReceiver(this);
					}}, i);
				new AlertDialog.Builder(CrashHandler.this)
						.setTitle(getString(R.string.app_name))
						.setIcon(android.R.drawable.ic_dialog_info)
						.setMessage(R.string.prompt_install_log_collector)
						.setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener(){
							public void onClick(DialogInterface dialog, int whichButton){
								Intent marketIntent = new Intent(Intent.ACTION_VIEW, Uri.parse("market://details?id=" + LOG_COLLECTOR_PACKAGE_NAME));
								marketIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
								startActivity(marketIntent);
							}
						})
						.setNegativeButton(android.R.string.cancel, null)
						.show();
			} else {
				sendLog();
			}
		}});
		c.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			if (cl.isChecked()) clearState();
			finish();
		}});
	}

	protected void sendLog()
	{
		Intent intent = new Intent(ACTION_SEND_LOG);
		intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
		intent.putExtra(EXTRA_SEND_INTENT_ACTION, Intent.ACTION_SENDTO);
		intent.putExtra(EXTRA_DATA, Uri.parse("mailto:" + getString(R.string.author_email)));
		intent.putExtra(EXTRA_ADDITIONAL_INFO, getString(R.string.crash_preamble)+"\n\n\n");
		intent.putExtra(Intent.EXTRA_SUBJECT, SGTPuzzles.getEmailSubject(this, true));

		intent.putExtra(EXTRA_FORMAT, "threadtime");

		String[] filterSpecs = new String[] {
			"AndroidRuntime:E",
			SGTPuzzles.TAG + ":V",
			"DEBUG:I",
			"System.err:V",
			"dalvikvm:W",
			"ActivityManager:W",
			"WindowManager:W",
			"*:S",
		};
		intent.putExtra(EXTRA_FILTER_SPECS, filterSpecs);

		startActivity(intent);
	}

	void clearState()
	{
		SharedPreferences.Editor ed = getSharedPreferences("state", MODE_PRIVATE).edit();
		ed.clear();
		ed.commit();
	}
}
