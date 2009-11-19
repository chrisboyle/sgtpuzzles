package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.MailTo;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.Toast;
import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.IOException;
import java.util.ArrayList;

public class CrashHandler extends Activity
{
	protected void onCreate(Bundle state)
	{
		super.onCreate(state);
		setTitle(R.string.crashtitle);
		setContentView(R.layout.crashhandler);
		Button b = (Button)findViewById(R.id.report),
			   c = (Button)findViewById(R.id.close);
		final CheckBox cl = (CheckBox)findViewById(R.id.forget);
		b.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			if (cl.isChecked()) clearState();
			try {
				Process process = Runtime.getRuntime().exec(new String[]{"logcat","-d"});
				String log = readAllOf(process.getInputStream());
				SGTPuzzles.tryEmailAuthor(CrashHandler.this, true,
						getString(R.string.crash_preamble)+"\n\n\n\n"+log);
			} catch (IOException e) {
				Toast.makeText(CrashHandler.this, "Exception in crash handler! "+e.toString(), Toast.LENGTH_LONG).show();
			}
			finish();
		}});
		c.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			if (cl.isChecked()) clearState();
			finish();
		}});
	}

	String readAllOf(InputStream s) throws IOException
	{
		BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(s));
		String line;
		StringBuilder log = new StringBuilder();
		while ((line = bufferedReader.readLine()) != null) {
			log.append(line);
			log.append("\n");
		}
		return log.toString();
	}

	void clearState()
	{
		SharedPreferences.Editor ed = getSharedPreferences("state", MODE_PRIVATE).edit();
		ed.clear();
		ed.commit();
	}
}
