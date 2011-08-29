package name.boyle.chris.sgtpuzzles;

import java.io.IOException;
import java.text.MessageFormat;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.SharedPreferences;
import android.os.AsyncTask;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.Toast;

public class CrashHandler extends Activity
{
	public static final String TAG = "CrashHandler";
	protected void onCreate(Bundle state)
	{
		super.onCreate(state);
		setTitle(R.string.crashtitle);
		setContentView(R.layout.crashhandler);
		final Button b = (Button)findViewById(R.id.report),
			   c = (Button)findViewById(R.id.close);
		final CheckBox cl = (CheckBox)findViewById(R.id.forget);
		b.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			final ProgressDialog progress = new ProgressDialog(CrashHandler.this);
			progress.setMessage(getString(R.string.getting_log));
			progress.setIndeterminate(true);
			progress.setCancelable(false);
			progress.show();
			final AsyncTask<Void, Void, Void> task = new AsyncTask<Void,Void,Void>(){
				String log;
				Process process;
				@Override
				protected Void doInBackground(Void... v) {
					try {
						process = Runtime.getRuntime().exec(new String[]{"logcat","-d","-v","threadtime"});
						log = SGTPuzzles.readAllOf(process.getInputStream());
					} catch (IOException e) {
						e.printStackTrace();
						Toast.makeText(CrashHandler.this, e.toString(), Toast.LENGTH_LONG).show();
					}
					return null;
				}
				@Override
				protected void onCancelled() {
					process.destroy();
				}
				@Override
				protected void onPostExecute(Void v) {
					progress.setMessage(getString(R.string.starting_email));
					boolean ok = SGTPuzzles.tryEmailAuthor(CrashHandler.this, true,
							getString(R.string.crash_preamble)+"\n\n\n\nLog:\n"+log);
					progress.dismiss();
					if (ok) {
						if (cl.isChecked()) clearState();
						finish();
					}
				}
			}.execute();
			b.postDelayed(new Runnable(){public void run(){
				if (task.getStatus() == AsyncTask.Status.FINISHED) return;
				// It's probably one of these devices where some fool broke logcat.
				progress.dismiss();
				task.cancel(true);
				new AlertDialog.Builder(CrashHandler.this)
					.setMessage(MessageFormat.format(getString(R.string.get_log_failed), getString(R.string.author_email)))
					.setCancelable(true)
					.setIcon(android.R.drawable.ic_dialog_alert)
					.show();
			}}, 3000);
		}});
		c.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			if (cl.isChecked()) clearState();
			finish();
		}});
	}

	void clearState()
	{
		SharedPreferences.Editor ed = getSharedPreferences("state", MODE_PRIVATE).edit();
		ed.clear();
		ed.commit();
	}
}
