package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.DialogInterface;
import android.content.Intent;
import android.os.Bundle;

import java.text.MessageFormat;

@SuppressWarnings("WeakerAccess")  // used by manifest
public class SendFeedbackActivity extends Activity
{
	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		Intent i = new Intent(Intent.ACTION_SEND);
		i.putExtra(Intent.EXTRA_EMAIL, new String[]{getString(R.string.author_email)});
		final String emailSubject = SGTPuzzles.getEmailSubject(this);
		i.putExtra(Intent.EXTRA_SUBJECT, emailSubject);
		i.setType("message/rfc822");
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
}
