package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;

public class CrashHandler extends Activity
{
	protected void onCreate(Bundle b)
	{
		super.onCreate(b);
		setTitle(R.string.crashtitle);
		setContentView(R.layout.crashhandler);
/*		Button b = findViewById(R.id.report),
			   c = findViewById(R.id.close);
		b.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			Toast.makeText(this, "To do!", Toast.LENGTH_SHORT).show();
		}});
		c.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
			finish();
		}});*/
	}
}
