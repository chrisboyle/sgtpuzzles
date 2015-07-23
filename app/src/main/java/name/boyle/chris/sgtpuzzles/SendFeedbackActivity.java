package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;

import java.text.MessageFormat;

@SuppressWarnings("WeakerAccess")  // used by manifest
public class SendFeedbackActivity extends Activity
{
	public static final String REASON = SendFeedbackActivity.class.getName() + ".REASON";

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		String reason = getIntent().getStringExtra(REASON);
		Intent i = new Intent(Intent.ACTION_SENDTO);
		final SharedPreferences state = getSharedPreferences(GamePlay.STATE_PREFS_NAME, MODE_PRIVATE);
		final String currentBackend = state.getString(GamePlay.SAVED_BACKEND, null);
		String currentGame = "-";
		String body = "";
		try {
			if (currentBackend != null) {
				final String savedGame = state.getString(GamePlay.SAVED_GAME_PREFIX + currentBackend, null);
				body = MessageFormat.format(getString(R.string.feedback_body), savedGame);
				currentGame = currentBackend + " " + state.getString(GamePlay.LAST_PARAMS_PREFIX + currentGame, "-");
			}
		} catch (Exception ignored) {}
		final String emailSubject = getEmailSubject(this, currentGame);
		String uri = "mailto:" + getString(R.string.author_email) + "?subject=" + Uri.encode(emailSubject);
		if (reason != null) {
			body = "Reason: " + reason + "\n" + body;
		}
		i.putExtra(Intent.EXTRA_TEXT, body);
		i.setData(Uri.parse(uri));
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
			i.addFlags(Intent.FLAG_ACTIVITY_NEW_DOCUMENT);
		}
		try {
			startActivity(i);
			finish();
		} catch (ActivityNotFoundException e) {
			new AlertDialog.Builder(this)
					.setTitle(getString(R.string.no_email_app))
					.setMessage(MessageFormat.format(getString(R.string.email_manually),
							getString(R.string.author_email),
							emailSubject))
					.setIcon(android.R.drawable.ic_dialog_alert)
					.setOnCancelListener(new DialogInterface.OnCancelListener() {
						@Override
						public void onCancel(DialogInterface dialog) {
							finish();
						}
					})
					.show();
		}
	}

	static String getEmailSubject(Context c, String currentGame)
	{
		String modVer = "unknown";
		try {
			Process p = Runtime.getRuntime().exec(new String[]{"getprop","ro.modversion"});
			modVer = Utils.readAllOf(p.getInputStream()).trim();
			if (modVer.length() == 0) modVer = "original";
		} catch (Exception ignored) {}
		return MessageFormat.format(c.getString(R.string.email_subject),
				BuildConfig.VERSION_NAME, currentGame, Build.MODEL, modVer, Build.FINGERPRINT);
	}

	public static void promptToReport(final Context context, final int descId, final int shortId) {
		new AlertDialog.Builder(context)
				.setTitle(R.string.Error)
				.setIcon(android.R.drawable.ic_dialog_alert)
				.setMessage(descId)
				.setPositiveButton(R.string.report_it, new DialogInterface.OnClickListener() {
					@Override
					public void onClick(DialogInterface dialog, int which) {
						final Intent intent = new Intent(context, SendFeedbackActivity.class);
						intent.putExtra(SendFeedbackActivity.REASON, context.getString(shortId));
						context.startActivity(intent);
					}
				}).show();
	}
}
