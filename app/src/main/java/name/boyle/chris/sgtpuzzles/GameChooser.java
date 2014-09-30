package name.boyle.chris.sgtpuzzles;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

import android.animation.LayoutTransition;
import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.support.v4.view.MenuItemCompat;
import android.support.v7.app.ActionBar;
import android.support.v7.app.ActionBarActivity;
import android.support.v7.widget.GridLayout;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.TextAppearanceSpan;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageView;
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

    private GridLayout table;

	private TextView starredHeader;
	private TextView otherHeader;

	private SharedPreferences prefs;
    private boolean useGrid;
	private String[] games;
	private View[] views;
	private Menu menu;
	private boolean chooserHideGrid;
	private PrefsSaver prefsSaver;

	@Override
    @SuppressLint("CommitPrefEdits")
    protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		prefs = PreferenceManager.getDefaultSharedPreferences(this);
        SharedPreferences state = getSharedPreferences(GamePlay.STATE_PREFS_NAME, MODE_PRIVATE);
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

		chooserHideGrid = getResources().getBoolean(R.bool.chooserHideGrid);
		if (chooserHideGrid) {
			// Tablets - grid is just going to look silly here
			useGrid = false;
		} else {
			String s = prefs.getString(CHOOSER_STYLE_KEY,"list");
			useGrid = s.equals("grid");
		}
		games = getResources().getStringArray(R.array.games);
		views = new View[games.length];
		setContentView(R.layout.chooser);
		table = (GridLayout) findViewById(R.id.table);
		starredHeader = (TextView) findViewById(R.id.games_starred);
		otherHeader = (TextView) findViewById(R.id.games_others);
		buildViews();
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

//		if( ! state.contains("savedGame") || state.getString("savedGame", "").length() <= 0 ) {
//			// first run
//			new AlertDialog.Builder(this)
//					.setMessage(R.string.welcome)
//					.setPositiveButton(android.R.string.ok, null)
//					.show();
//		}

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
			enableTableAnimations();
		}
	}

	@TargetApi(Build.VERSION_CODES.JELLY_BEAN)
	private void enableTableAnimations() {
		final LayoutTransition transition = new LayoutTransition();
		transition.enableTransitionType(LayoutTransition.CHANGING);
		table.setLayoutTransition(transition);
	}

	void buildViews()
	{
		for( int i = 0; i < games.length; i++ ) {
			final String gameId = games[i];
			views[i] = getLayoutInflater().inflate(
					R.layout.list_item, table, false);
			final StateListDrawable stateListDrawable = mkStarryIcon(gameId);
			((ImageView)views[i].findViewById(R.id.icon)).setImageDrawable(stateListDrawable);
			final int nameId = getResources().getIdentifier("name_"+gameId, "string", getPackageName());
			final int descId = getResources().getIdentifier("desc_"+gameId, "string", getPackageName());
			SpannableStringBuilder desc = new SpannableStringBuilder(nameId > 0 ?
					getString(nameId) : gameId.substring(0,1).toUpperCase() + gameId.substring(1));
			desc.setSpan(new TextAppearanceSpan(this, R.style.ChooserItemName),
					0, desc.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
			desc.append(": ").append(getString(descId > 0 ? descId : R.string.no_desc));
			final TextView textView = (TextView) views[i].findViewById(R.id.text);
			textView.setText(desc);
			textView.setVisibility(useGrid ? View.GONE : View.VISIBLE);
			views[i].setOnClickListener(new View.OnClickListener() {
				public void onClick(View arg1) {
					Intent i = new Intent(GameChooser.this, GamePlay.class);
					i.setData(Uri.fromParts("sgtpuzzles", gameId, null));
					startActivity(i);
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
			views[i].setLayoutParams(mkLayoutParams());
			table.addView(views[i]);
		}
		rethinkColumns(true);
	}

	private StateListDrawable mkStarryIcon(String gameId) {
		final StateListDrawable stateListDrawable = new StateListDrawable();
		final Drawable icon = getResources().getDrawable(
				getResources().getIdentifier(gameId, "drawable", getPackageName()));
		final LayerDrawable starredIcon = new LayerDrawable(new Drawable[]{
				icon, getResources().getDrawable(R.drawable.ic_star) });
		final float density = getResources().getDisplayMetrics().density;
		starredIcon.setLayerInset(1, (int)(42*density), (int)(42*density), 0, 0);
		stateListDrawable.addState(new int[]{android.R.attr.state_checked}, starredIcon);
		stateListDrawable.addState(new int[0], icon);
		return stateListDrawable;
	}

	private GridLayout.LayoutParams mkLayoutParams() {
		final GridLayout.LayoutParams params = new GridLayout.LayoutParams();
		params.setGravity(Gravity.CENTER_HORIZONTAL);
		return params;
	}


	@Override
	public void onConfigurationChanged(Configuration newConfig)
	{
		super.onConfigurationChanged(newConfig);
		rethinkColumns(false);
		rethinkActionBarCapacity();
	}

	private int mColumns = 0;
	private int mColWidthPx = 0;

	private void rethinkColumns(boolean force) {
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int colWidthDipNeeded = useGrid ? 72 : 318;
		final double screenWidthDip = (double) dm.widthPixels / dm.density;
		final int columns = Math.max(1, (int) Math.floor(
				screenWidthDip / colWidthDipNeeded));
		final int colWidthActualPx = (int) Math.floor(dm.widthPixels / columns);
		if (force || mColumns != columns || mColWidthPx != colWidthActualPx) {
			mColumns = columns;
			mColWidthPx = colWidthActualPx;
			List<View> starred = new ArrayList<View>();
			List<View> others = new ArrayList<View>();
			for (int i=0; i < games.length; i++) {
				(isStarred(games[i]) ? starred : others).add(views[i]);
			}
			final boolean anyStarred = !starred.isEmpty();
			starredHeader.setVisibility(anyStarred ? View.VISIBLE : View.GONE);
			int row = 0;
			if (anyStarred) {
				setGridCells(starredHeader, 0, row++, mColumns, 1);
				row = setViewsGridCells(row, starred, true);
			}
			otherHeader.setText(anyStarred ? R.string.games_others : R.string.games_others_none_starred);
			setGridCells(otherHeader, 0, row++, mColumns, 1);
			setViewsGridCells(row, others, false);
		}
	}

	@SuppressLint("InlinedApi")
	private void setGridCells(View v, int x, int y, int w, int h) {
		final GridLayout.LayoutParams layoutParams = (GridLayout.LayoutParams) v.getLayoutParams();
		layoutParams.width = mColWidthPx * w;
		layoutParams.columnSpec = GridLayout.spec(x, w, GridLayout.START);
		layoutParams.rowSpec = GridLayout.spec(y, h, GridLayout.START);
		layoutParams.setGravity((useGrid && w==1) ? Gravity.CENTER_HORIZONTAL : Gravity.START);
		v.setLayoutParams(layoutParams);
	}

	private int setViewsGridCells(final int startRow, final List<View> views, final boolean starred) {
		int col = 0;
		int row = startRow;
		for (View v : views) {
			((ImageView) v.findViewById(R.id.icon)).setImageState(
					starred ? new int[]{android.R.attr.state_checked} : new int[0], false);
			if (col >= mColumns) {
				col = 0;
				row++;
			}
			setGridCells(v, col++, row, 1, 1);
		}
		row++;
		return row;
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
		if (screenWidthDIP >= 480) {
			state |= MenuItemCompat.SHOW_AS_ACTION_WITH_TEXT;
		}
		if (!chooserHideGrid) {
			MenuItemCompat.setShowAsAction(menu.findItem(
					useGrid ? R.id.listchooser : R.id.gridchooser), state);
		}
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.load), state);
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.help), state);
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		getMenuInflater().inflate(R.menu.chooser, menu);
		if (chooserHideGrid) {
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
		if (!chooserHideGrid) {
			menu.findItem(useGrid ? R.id.gridchooser : R.id.listchooser).setChecked(true);
		}
		updateStyleToggleVisibility();
		return true;
	}

	void updateStyleToggleVisibility()
	{
		if(!chooserHideGrid) {
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
			case R.id.contents:
				Intent intent = new Intent(this, HelpActivity.class);
				intent.putExtra(HelpActivity.TOPIC, "index");
				startActivity(intent);
				break;
			case R.id.email:
				startActivity(new Intent(this, SendFeedbackActivity.class));
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
		rethinkActionBarCapacity();
		for (View v : views) {
			v.findViewById(R.id.text).setVisibility(useGrid ? View.GONE : View.VISIBLE);
			v.setLayoutParams(v.getLayoutParams());
		}
		rethinkColumns(true);
		SharedPreferences.Editor ed = prefs.edit();
		ed.putString(CHOOSER_STYLE_KEY, useGrid ? "grid" : "list");
		prefsSaver.save(ed);
	}
}
