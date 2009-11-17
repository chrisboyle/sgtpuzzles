package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.Toast;
import java.io.BufferedReader;
import java.io.InputStreamReader;
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
		b.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			ArrayList<String> commandLine = new ArrayList<String>();
			commandLine.add("logcat");
			commandLine.add("-d");
			try {
				Process process = Runtime.getRuntime().exec(commandLine.toArray(new String[0]));
				BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(process.getInputStream()));
				String line;
				final StringBuilder log = new StringBuilder();
				while ((line = bufferedReader.readLine()) != null) {
					log.append(line);
					log.append("\n");
				}
				Toast.makeText(CrashHandler.this, "Got "+log.length()+" bytes of log, line: "+log.substring(0,200), Toast.LENGTH_SHORT).show();
			} catch (Exception e) {
				Toast.makeText(CrashHandler.this, "Got exception: "+e.toString(), Toast.LENGTH_SHORT).show();
			}
		}});
		c.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			finish();
		}});
	}
}
