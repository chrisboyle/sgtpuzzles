package name.boyle.chris.sgtpuzzles;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.DialogInterface.OnCancelListener;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.util.DisplayMetrics;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageView;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

public class GameChooser extends Activity
{
	/* This really ought to have been a GridView, but...
	 * http://stackoverflow.com/q/7545915/6540  */
	TableLayout table;

	SharedPreferences prefs;
	boolean useGrid;
	String[] games;
	View[] views;

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		setTitle(R.string.chooser_title);
		prefs = getSharedPreferences("state", MODE_PRIVATE);
		useGrid = prefs.getString("chooserStyle","list").equals("grid");
		games = getResources().getStringArray(R.array.games);
		views = new View[games.length];
		setContentView(R.layout.chooser);
		table = (TableLayout) findViewById(R.id.table);
		rebuildViews();
		if( ! prefs.contains("savedGame") || prefs.getString("savedGame","").length() <= 0 ) {
			// first run
			new AlertDialog.Builder(this)
					.setMessage(R.string.welcome)
					.setOnCancelListener(new OnCancelListener() {
						public void onCancel(DialogInterface dialog) { finish(); }
					})
					.setPositiveButton(android.R.string.yes, null)
					.show();
		}
	}

	void rebuildViews()
	{
		TableRow dummy = new TableRow(this);
		for( int i = 0; i < games.length; i++ ) {
			final String gameId = games[i];
			int nameId = getResources().getIdentifier("name_"+gameId, "string", getPackageName());
			views[i] = getLayoutInflater().inflate(
					useGrid ? R.layout.grid_item : R.layout.list_item, dummy, false);
			((ImageView)views[i].findViewById(android.R.id.icon)).setImageResource(
					getResources().getIdentifier(gameId, "drawable", getPackageName()));
			if (! useGrid) {
				int descId = getResources().getIdentifier("desc_"+gameId, "string", getPackageName());
				String desc;
				if( nameId > 0 ) desc = getString(nameId);
				else desc = gameId.substring(0,1).toUpperCase() + gameId.substring(1);
				desc += ": " + getString( descId > 0 ? descId : R.string.no_desc );
				((TextView)views[i].findViewById(android.R.id.text1)).setText(desc);
			}
			views[i].setBackgroundResource(android.R.drawable.list_selector_background);
			views[i].setOnClickListener(new View.OnClickListener() {
				public void onClick(View arg1) {
					Intent i = new Intent(GameChooser.this, SGTPuzzles.class);
					i.setData(Uri.fromParts("sgtpuzzles", gameId, null));
					startActivity(i);
					finish();
				}
			});
			views[i].setFocusable(true);
		}
		onConfigurationChanged(getResources().getConfiguration());
	}

	int oldColumns = 0;
	@Override
	public void onConfigurationChanged(Configuration newConfig)
	{
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int colWidthDip = useGrid ? 68 : 230;
		final int columns = Math.max(1, (int) Math.floor(
				((double)dm.widthPixels / dm.density) / colWidthDip));
		if (oldColumns != columns) {
			table.removeAllViews();
			TableRow tr = null;
			for (int i=0; i < games.length; i++) {
				if (i % columns == 0) {
					tr = new TableRow(this);
					table.addView(tr);
					tr.setLayoutParams(new TableRow.LayoutParams(
							TableRow.LayoutParams.WRAP_CONTENT,
							TableRow.LayoutParams.WRAP_CONTENT));
					tr.setPadding(0, 4, 0, 4);
				}
				if (views[i].getParent() != null) ((TableRow) views[i].getParent()).removeView(views[i]);
				tr.addView(views[i], new TableRow.LayoutParams(
						0, TableRow.LayoutParams.WRAP_CONTENT, 1));
			}
			oldColumns = columns;
		}
		super.onConfigurationChanged(newConfig);
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		getMenuInflater().inflate(R.menu.chooser, menu);
		return true;
	}

	@Override
	public boolean onPrepareOptionsMenu(Menu menu)
	{
		super.onPrepareOptionsMenu(menu);
		menu.findItem(useGrid ? R.id.gridchooser : R.id.listchooser).setChecked(true);
		return true;
	}

	/** Possible android bug: onOptionsItemSelected(MenuItem item)
	 *  wasn't being called? */
	@Override
	public boolean onMenuItemSelected(int f, MenuItem item)
	{
		boolean newGrid;
		switch(item.getItemId()) {
			case R.id.listchooser: newGrid = false; break;
			case R.id.gridchooser: newGrid = true; break;
			case R.id.load:
				new FilePicker(this, Environment.getExternalStorageDirectory(),false).show();
				return true;
			default: return super.onMenuItemSelected(f,item);
		}
		if( useGrid == newGrid ) return true;
		useGrid = newGrid;
		rebuildViews();
		SharedPreferences.Editor ed = prefs.edit();
		ed.putString("chooserStyle", useGrid ? "grid" : "list");
		ed.commit();
		return true;
	}
}
