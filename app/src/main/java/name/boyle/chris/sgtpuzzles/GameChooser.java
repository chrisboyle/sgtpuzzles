package name.boyle.chris.sgtpuzzles;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

import android.annotation.SuppressLint;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.DialogInterface.OnCancelListener;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.support.v7.app.ActionBarActivity;
import android.util.DisplayMetrics;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageView;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

@SuppressWarnings("WeakerAccess")  // used by manifest
public class GameChooser extends ActionBarActivity
{
	static final String CHOOSER_STYLE_KEY = "chooserStyle";

	/* This really ought to have been a GridView, but...
	 * http://stackoverflow.com/q/7545915/6540  */
    private TableLayout table;

	private SharedPreferences prefs;
    private boolean useGrid;
	private String[] games;
	private View[] views;
	private Menu menu;
	private boolean isTablet;
	private PrefsSaver prefsSaver;

	@Override
    @SuppressLint("CommitPrefEdits")
    protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		setTitle(R.string.chooser_title);
		prefs = PreferenceManager.getDefaultSharedPreferences(this);
        SharedPreferences state = getSharedPreferences(SGTPuzzles.STATE_PREFS_NAME, MODE_PRIVATE);
		prefsSaver = PrefsSaver.get(this);

		String oldCS = state.getString(CHOOSER_STYLE_KEY, null);
		if (oldCS != null) {  // migrate to somewhere more sensible
			SharedPreferences.Editor ed = prefs.edit();
			ed.putString(CHOOSER_STYLE_KEY, oldCS);
			prefsSaver.save(ed);
			ed = state.edit();
			ed.remove(CHOOSER_STYLE_KEY);
			prefsSaver.save(ed);
		}

		DisplayMetrics dm = getResources().getDisplayMetrics();
		int screenWidthDIP = (int)Math.round(((double)dm.widthPixels) / dm.density);
		int screenHeightDIP = (int)Math.round(((double)dm.heightPixels) / dm.density);
		isTablet = ( screenWidthDIP >= 600 && screenHeightDIP >= 600 );
		if (isTablet) {
			// Grid is just going to look silly here
			useGrid = false;
		} else {
			String s = prefs.getString(CHOOSER_STYLE_KEY,"list");
			useGrid = s.equals("grid");
		}
		games = getResources().getStringArray(R.array.games);
		views = new View[games.length];
		setContentView(R.layout.chooser);
		table = (TableLayout) findViewById(R.id.table);
		rebuildViews();
		if( ! state.contains("savedGame") || state.getString("savedGame", "").length() <= 0 ) {
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

	private int oldColumns = 0;
	@Override
	public void onConfigurationChanged(Configuration newConfig)
	{
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int colWidthDip = useGrid ? 68 : 230;
		final int columns = Math.max(1, (int) Math.floor(
				((double)dm.widthPixels / dm.density) / colWidthDip));
		final int margin = (int) Math.round((double)6 * dm.density);
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
				}
				if (views[i].getParent() != null) ((TableRow) views[i].getParent()).removeView(views[i]);
				TableRow.LayoutParams lp = new TableRow.LayoutParams(
						0, TableRow.LayoutParams.WRAP_CONTENT, 1);
				lp.setMargins(0, margin, 0, margin);
                assert tr != null;
                tr.addView(views[i], lp);
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
		this.menu = menu;
		if (isTablet) {
			menu.removeItem(R.id.gridchooser);
			menu.removeItem(R.id.listchooser);
		}
		return true;
	}

	@Override
	public boolean onPrepareOptionsMenu(Menu menu)
	{
		super.onPrepareOptionsMenu(menu);
		if (! isTablet) {
			menu.findItem(useGrid ? R.id.gridchooser : R.id.listchooser).setChecked(true);
		}
		updateStyleToggleVisibility();
		return true;
	}

	void updateStyleToggleVisibility()
	{
		if(! isTablet) {
			menu.findItem(useGrid ? R.id.gridchooser : R.id.listchooser).setVisible(false);
			menu.findItem(useGrid ? R.id.listchooser : R.id.gridchooser).setVisible(true);
		}
	}

	/** Possible android bug: onOptionsItemSelected(MenuItem item)
	 *  wasn't being called? */
	@Override
    @SuppressLint("CommitPrefEdits")
	public boolean onOptionsItemSelected(MenuItem item)
	{
		boolean newGrid;
		switch(item.getItemId()) {
			case R.id.listchooser: newGrid = false; break;
			case R.id.gridchooser: newGrid = true; break;
			case R.id.load:
				new FilePicker(this, Environment.getExternalStorageDirectory(),false).show();
				return true;
			case R.id.contents: SGTPuzzles.showHelp(this, "index"); return true;
			case R.id.email: SGTPuzzles.tryEmailAuthor(this); return true;
			default: return super.onOptionsItemSelected(item);
		}
		if( useGrid == newGrid ) return true;
		useGrid = newGrid;
		updateStyleToggleVisibility();
		rebuildViews();
        SharedPreferences.Editor ed = prefs.edit();
		ed.putString(CHOOSER_STYLE_KEY, useGrid ? "grid" : "list");
		prefsSaver.save(ed);
		return true;
	}
}
