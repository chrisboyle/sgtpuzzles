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
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.support.v4.view.MenuItemCompat;
import android.support.v7.app.ActionBar;
import android.support.v7.app.ActionBarActivity;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.TextAppearanceSpan;
import android.util.DisplayMetrics;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageView;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

@SuppressWarnings("WeakerAccess")  // used by manifest
public class GameChooser extends ActionBarActivity
{
	static final String CHOOSER_STYLE_KEY = "chooserStyle";
	private static final Set<String> DEFAULT_STARRED = new LinkedHashSet<String>();
	static {
		DEFAULT_STARRED.add("guess");
		DEFAULT_STARRED.add("keen");
		DEFAULT_STARRED.add("lightup");
		DEFAULT_STARRED.add("net");
		DEFAULT_STARRED.add("signpost");
		DEFAULT_STARRED.add("solo");
		DEFAULT_STARRED.add("towers");
	}

	/* This really ought to have been a GridView, but...
	 * http://stackoverflow.com/q/7545915/6540  */
    private TableLayout table;

	private TextView games_starred;
	private TextView games_others;

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
		final int screenWidthDIP = (int)Math.round(((double)dm.widthPixels) / dm.density);
		final int screenHeightDIP = (int)Math.round(((double)dm.heightPixels) / dm.density);
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
		games_starred = (TextView) findViewById(R.id.games_starred);
		games_others = (TextView) findViewById(R.id.games_others);
		rebuildViews();
		rethinkActionBarCapacity();
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			getSupportActionBar().addOnMenuVisibilityListener(new ActionBar.OnMenuVisibilityListener() {
				@Override
				public void onMenuVisibilityChanged(boolean visible) {
					// https://code.google.com/p/android/issues/detail?id=69205
					if (!visible) supportInvalidateOptionsMenu();
				}
			});
		}

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
			views[i] = getLayoutInflater().inflate(
					useGrid ? R.layout.grid_item : R.layout.list_item, dummy, false);
			((ImageView)views[i].findViewById(R.id.icon)).setImageResource(
					getResources().getIdentifier(gameId, "drawable", getPackageName()));
			if (! useGrid) {
				final int nameId = getResources().getIdentifier("name_"+gameId, "string", getPackageName());
				final int descId = getResources().getIdentifier("desc_"+gameId, "string", getPackageName());
				SpannableStringBuilder desc = new SpannableStringBuilder(nameId > 0 ?
						getString(nameId) : gameId.substring(0,1).toUpperCase() + gameId.substring(1));
				desc.setSpan(new TextAppearanceSpan(this, R.style.ChooserItemName),
						0, desc.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
				desc.append(": ").append(getString(descId > 0 ? descId : R.string.no_desc));
				((TextView)views[i].findViewById(R.id.text)).setText(desc);
			}
			views[i].setOnClickListener(new View.OnClickListener() {
				public void onClick(View arg1) {
					Intent i = new Intent(GameChooser.this, SGTPuzzles.class);
					i.setData(Uri.fromParts("sgtpuzzles", gameId, null));
					startActivity(i);
					finish();
					overridePendingTransition(0, 0);
				}
			});
			views[i].setOnLongClickListener(new View.OnLongClickListener() {
				@Override
				public boolean onLongClick(View v) {
					toggleStarred(gameId);
					return true;
				}
			});
			views[i].setFocusable(true);
		}
		rethinkColumns(true);
	}

	private int oldColumns = 0;
	@Override
	public void onConfigurationChanged(Configuration newConfig)
	{
		super.onConfigurationChanged(newConfig);
		rethinkColumns(false);
		rethinkActionBarCapacity();
	}

	private void rethinkColumns(boolean force) {
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int colWidthDip = useGrid ? 68 : 230;
		final int columns = Math.max(1, (int) Math.floor(
				((double)dm.widthPixels / dm.density) / colWidthDip));
		final int margin = (int) Math.round((double)6 * dm.density);
		if (force || oldColumns != columns) {
			List<View> starred = new ArrayList<View>();
			List<View> others = new ArrayList<View>();
			for (int i=0; i < games.length; i++) {
				(isStarred(games[i]) ? starred : others).add(views[i]);
			}
			table.removeAllViews();
			final boolean anyStarred = !starred.isEmpty();
			if (anyStarred) {
				table.addView(games_starred);
			}
			addViews(columns, margin, starred, true);
			table.addView(games_others);
			games_others.setText(anyStarred ? R.string.games_others : R.string.games_others_none_starred);
			addViews(columns, margin, others, false);
			oldColumns = columns;
		}
	}

	private TableRow mkTableRow() {
		TableRow tr;
		tr = new TableRow(this);
		table.addView(tr);
		tr.setLayoutParams(new TableRow.LayoutParams(
				TableRow.LayoutParams.WRAP_CONTENT,
				TableRow.LayoutParams.WRAP_CONTENT));
		return tr;
	}

	private void addViews(final int columns, final int margin, final List<View> views, final boolean starred) {
		TableRow tr = null;
		int i = 0;
		for (View v : views) {
			// TODO add/remove star (N.B. v might be icon+text or just icon)
			if (i % columns == 0) {
				tr = mkTableRow();
			}
			if (v.getParent() != null) ((TableRow) v.getParent()).removeView(v);
			TableRow.LayoutParams lp = new TableRow.LayoutParams(
					0, TableRow.LayoutParams.WRAP_CONTENT, 1);
			lp.setMargins(0, margin, 0, margin);
			assert tr != null;
			tr.addView(v, lp);
			i++;
		}
	}

	private boolean isStarred(String game) {
		return prefs.getBoolean("starred_" + game, DEFAULT_STARRED.contains(game));
	}

	@SuppressLint("CommitPrefEdits")
	private void toggleStarred(String game) {
		SharedPreferences.Editor ed = prefs.edit();
		ed.putBoolean("starred_" + game, !isStarred(game));
		prefsSaver.save(ed);
		// TODO just move the one affected view & enable animateLayoutChanges on table in layout xml
		rethinkColumns(true);
	}

	private void rethinkActionBarCapacity() {
		if (menu == null) return;
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int screenWidthDIP = (int) Math.round(((double) dm.widthPixels) / dm.density);
		int state = MenuItemCompat.SHOW_AS_ACTION_ALWAYS;
		if (screenWidthDIP > 500) {
			state |= MenuItemCompat.SHOW_AS_ACTION_WITH_TEXT;
		}
		MenuItemCompat.setShowAsAction(menu.findItem(useGrid ? R.id.listchooser : R.id.gridchooser), state);
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.load), state);
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.help), state);
		supportInvalidateOptionsMenu();
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		getMenuInflater().inflate(R.menu.chooser, menu);
		if (isTablet) {
			menu.removeItem(R.id.gridchooser);
			menu.removeItem(R.id.listchooser);
		}
		if (this.menu == null) {  // first time
			this.menu = menu;
			updateStyleToggleVisibility();
			rethinkActionBarCapacity();
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

	@Override
	public boolean onOptionsItemSelected(MenuItem item)
	{
		boolean ret = true;
		switch(item.getItemId()) {
			case R.id.listchooser: updateUseGrid(false); break;
			case R.id.gridchooser: updateUseGrid(true); break;
			case R.id.load:
				new FilePicker(this, Environment.getExternalStorageDirectory(),false).show();
				break;
			case R.id.contents: SGTPuzzles.showHelp(this, "index"); break;
			case R.id.email:
				startActivity(new Intent(this, SendFeedbackActivity.class));
				break;
			case R.id.settings:
				startActivity(new Intent(this, PrefsActivity.class));
				break;
			default: ret = super.onOptionsItemSelected(item);
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			// https://code.google.com/p/android/issues/detail?id=69205
			supportInvalidateOptionsMenu();
		}
		return ret;
	}

	@SuppressLint("CommitPrefEdits")
	private void updateUseGrid(boolean newGrid) {
		if( useGrid == newGrid ) return;
		useGrid = newGrid;
		updateStyleToggleVisibility();
		rebuildViews();
		SharedPreferences.Editor ed = prefs.edit();
		ed.putString(CHOOSER_STYLE_KEY, useGrid ? "grid" : "list");
		prefsSaver.save(ed);
	}
}
