package name.boyle.chris.sgtpuzzles;

import android.animation.LayoutTransition;
import android.annotation.SuppressLint;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Bundle;

import androidx.annotation.NonNull;
import androidx.gridlayout.widget.GridLayout;
import androidx.preference.PreferenceManager;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.TextAppearanceSpan;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

import name.boyle.chris.sgtpuzzles.databinding.ChooserBinding;
import name.boyle.chris.sgtpuzzles.databinding.ListItemBinding;

public class GameChooser extends ActivityWithLoadButton implements SharedPreferences.OnSharedPreferenceChangeListener
{
	private static final Set<BackendName> DEFAULT_STARRED = new LinkedHashSet<>();

	static {
		DEFAULT_STARRED.add(GUESS.INSTANCE);
		DEFAULT_STARRED.add(KEEN.INSTANCE);
		DEFAULT_STARRED.add(LIGHTUP.INSTANCE);
		DEFAULT_STARRED.add(NET.INSTANCE);
		DEFAULT_STARRED.add(SIGNPOST.INSTANCE);
		DEFAULT_STARRED.add(SOLO.INSTANCE);
		DEFAULT_STARRED.add(TOWERS.INSTANCE);
	}

	private ChooserBinding _binding;
	private SharedPreferences _prefs;
    private boolean _useGrid;
	private final Map<BackendName, ListItemBinding> _itemBindings = new LinkedHashMap<>();
	private Menu _menu;
	private BackendName _scrollToOnNextLayout = null;
	private long _resumeTime = 0;
	private boolean _wasNight;

	@Override
    protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		if (GameGenerator.executableIsMissing(this)) {
			finish();
			return;
		}
		_prefs = PreferenceManager.getDefaultSharedPreferences(this);
		_prefs.registerOnSharedPreferenceChangeListener(this);
		_wasNight = NightModeHelper.isNight(getResources().getConfiguration());

		final SharedPreferences state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE);
		final String oldCS = state.getString(PrefsConstants.CHOOSER_STYLE_KEY, null);
		if (oldCS != null) {  // migrate to somewhere more sensible
			SharedPreferences.Editor ed = _prefs.edit();
			ed.putString(PrefsConstants.CHOOSER_STYLE_KEY, oldCS);
			ed.apply();
			ed = state.edit();
			ed.remove(PrefsConstants.CHOOSER_STYLE_KEY);
			ed.apply();
		}

		_useGrid = _prefs.getString(PrefsConstants.CHOOSER_STYLE_KEY, "list").equals("grid");
		_binding = ChooserBinding.inflate(getLayoutInflater());
		setContentView(_binding.getRoot());
		buildViews();
		rethinkActionBarCapacity();
		if (getSupportActionBar() != null) {
			getSupportActionBar().addOnMenuVisibilityListener(visible -> {
				// https://code.google.com/p/android/issues/detail?id=69205
				if (!visible) supportInvalidateOptionsMenu();
			});
		}

		_binding.scrollView.getViewTreeObserver().addOnGlobalLayoutListener(() -> {
			if (_scrollToOnNextLayout != null) {
				final View v = Objects.requireNonNull(_itemBindings.get(_scrollToOnNextLayout)).getRoot();
				_binding.scrollView.requestChildRectangleOnScreen(v, new Rect(0, 0, v.getWidth(), v.getHeight()), true);
				_scrollToOnNextLayout = null;
			}
		});

		enableTableAnimations();
	}

	@Override
	protected void onResume() {
		super.onResume();
		_resumeTime = System.nanoTime();
		BackendName currentBackend = null;
		SharedPreferences state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE);
		if (state.contains(PrefsConstants.SAVED_BACKEND)) {
			currentBackend = BackendName.byLowerCase(state.getString(PrefsConstants.SAVED_BACKEND, null));
		}

		for (final BackendName backend : BackendName.getAll()) {
			final boolean isCurrent = backend == currentBackend;
			Objects.requireNonNull(_itemBindings.get(backend)).getRoot().setActivated(isCurrent);
			if (isCurrent) {
				// wait until we know the size
				_scrollToOnNextLayout = backend;
			}
		}
	}

	private void enableTableAnimations() {
		final LayoutTransition transition = new LayoutTransition();
		transition.enableTransitionType(LayoutTransition.CHANGING);
		_binding.table.setLayoutTransition(transition);
	}

	private void buildViews()
	{
		_itemBindings.clear();
		for (final BackendName backend : BackendName.getAll()) {
			final ListItemBinding itemBinding = ListItemBinding.inflate(getLayoutInflater());
			_itemBindings.put(backend, itemBinding);
			itemBinding.icon.setImageDrawable(backend.getIcon(this));
			SpannableStringBuilder desc = new SpannableStringBuilder(backend.getDisplayName());
			desc.setSpan(new TextAppearanceSpan(this, R.style.ChooserItemName),
					0, desc.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
			desc.append(": ").append(getString(backend.getDescription()));
			itemBinding.text.setText(desc);
			itemBinding.text.setVisibility(_useGrid ? View.GONE : View.VISIBLE);
			ignoreTouchAfterResume(itemBinding.getRoot());
			itemBinding.getRoot().setOnClickListener(v -> {
				Intent i1 = new Intent(GameChooser.this, GamePlay.class);
				i1.setData(Uri.fromParts(GamePlay.OUR_SCHEME, backend.toString(), null));
				startActivity(i1);
				overridePendingTransition(0, 0);
			});
			itemBinding.getRoot().setOnLongClickListener(v -> {
				toggleStarred(backend);
				return true;
			});
			itemBinding.getRoot().setFocusable(true);
			itemBinding.getRoot().setLayoutParams(mkLayoutParams());
			_binding.table.addView(itemBinding.getRoot());
		}
		rethinkColumns(true);
	}

	@SuppressLint("ClickableViewAccessibility")  // Does not define a new click mechanism
	private void ignoreTouchAfterResume(View view) {
		view.setOnTouchListener((v, event) -> {
			// Ignore touch within 300ms of resume
			return System.nanoTime() - _resumeTime < 300000000;
		});
	}

	private GridLayout.LayoutParams mkLayoutParams() {
		final GridLayout.LayoutParams params = new GridLayout.LayoutParams();
		params.setGravity(Gravity.CENTER_HORIZONTAL);
		return params;
	}


	@Override
	public void onConfigurationChanged(@NonNull Configuration newConfig)
	{
		super.onConfigurationChanged(newConfig);
		final boolean isNight = NightModeHelper.isNight(getResources().getConfiguration());
		if (_wasNight != isNight) {
			for (final BackendName backend : BackendName.getAll()) {
				Objects.requireNonNull(_itemBindings.get(backend)).icon.setImageDrawable(backend.getIcon(this));
			}
		}
		rethinkColumns(_wasNight != isNight);
		if (_menu != null) {
			// https://github.com/chrisboyle/sgtpuzzles/issues/227
			_menu.clear();
			onCreateOptionsMenu(_menu);
		}
		rethinkActionBarCapacity();
		_wasNight = isNight;
	}

	private int mColumns = 0;
	private int mColWidthPx = 0;

	private void rethinkColumns(boolean force) {
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int colWidthDipNeeded = _useGrid ? 72 : 298;
		final double screenWidthDip = (double) dm.widthPixels / dm.density;
		final int columns = Math.max(1, (int) Math.floor(
				screenWidthDip / colWidthDipNeeded));
		final int colWidthActualPx = (int) Math.floor((double)dm.widthPixels / columns);
		if (force || mColumns != columns || mColWidthPx != colWidthActualPx) {
			mColumns = columns;
			mColWidthPx = colWidthActualPx;
			List<ListItemBinding> starred = new ArrayList<>();
			List<ListItemBinding> others = new ArrayList<>();
			for (final BackendName backend : BackendName.getAll()) {
				(isStarred(backend) ? starred : others).add(_itemBindings.get(backend));
			}
			final boolean anyStarred = !starred.isEmpty();
			_binding.gamesStarred.setVisibility(anyStarred ? View.VISIBLE : View.GONE);
			int row = 0;
			if (anyStarred) {
				setGridCells(_binding.gamesStarred, 0, row++, mColumns);
				row = setViewsGridCells(row, starred, true);
			}
			_binding.gamesOthers.setText(anyStarred ? R.string.games_others : R.string.games_others_none_starred);
			setGridCells(_binding.gamesOthers, 0, row++, mColumns);
			setViewsGridCells(row, others, false);
		}
	}

	private void setGridCells(View v, int x, int y, int w) {
		final GridLayout.LayoutParams layoutParams = (GridLayout.LayoutParams) v.getLayoutParams();
		layoutParams.width = mColWidthPx * w;
		layoutParams.columnSpec = GridLayout.spec(x, w, GridLayout.START);
		layoutParams.rowSpec = GridLayout.spec(y, 1, GridLayout.START);
		layoutParams.setGravity((_useGrid && w==1) ? Gravity.CENTER_HORIZONTAL : Gravity.START);
		v.setLayoutParams(layoutParams);
	}

	private int setViewsGridCells(final int startRow, final List<ListItemBinding> itemBindings, final boolean starred) {
		int col = 0;
		int row = startRow;
		for (ListItemBinding itemBinding : itemBindings) {
			itemBinding.star.setVisibility(starred ? View.VISIBLE : View.GONE);
			if (col >= mColumns) {
				col = 0;
				row++;
			}
			setGridCells(itemBinding.getRoot(), col++, row, 1);
		}
		row++;
		return row;
	}

	private boolean isStarred(final BackendName game) {
		return _prefs.getBoolean("starred_" + game, DEFAULT_STARRED.contains(game));
	}

	private void toggleStarred(final BackendName game) {
		SharedPreferences.Editor ed = _prefs.edit();
		ed.putBoolean("starred_" + game, !isStarred(game));
		ed.apply();
		rethinkColumns(true);
	}

	private void rethinkActionBarCapacity() {
		if (_menu == null) return;
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int screenWidthDIP = (int) Math.round(((double) dm.widthPixels) / dm.density);
		int state = MenuItem.SHOW_AS_ACTION_ALWAYS;
		if (screenWidthDIP >= 480) {
			state |= MenuItem.SHOW_AS_ACTION_WITH_TEXT;
		}
		_menu.findItem(R.id.settings).setShowAsAction(state);
		_menu.findItem(R.id.load).setShowAsAction(state);
		_menu.findItem(R.id.help_menu).setShowAsAction(state);
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		this._menu = menu;
		getMenuInflater().inflate(R.menu.chooser, menu);
		rethinkActionBarCapacity();
		return true;
	}

	@Override
	public boolean onPrepareOptionsMenu(Menu menu)
	{
		super.onPrepareOptionsMenu(menu);
		return true;
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item)
	{
		boolean ret = true;
		int itemId = item.getItemId();
		if (itemId == R.id.settings) {
			startActivity(new Intent(this, PrefsActivity.class));
		} else if (itemId == R.id.load) {
			loadGame();
		} else if (itemId == R.id.contents) {
			Intent intent = new Intent(this, HelpActivity.class);
			intent.putExtra(HelpActivity.TOPIC, "index");
			startActivity(intent);
		} else if (itemId == R.id.feedback) {
			Utils.sendFeedbackDialog(this);
		} else {
			ret = super.onOptionsItemSelected(item);
		}
		// https://code.google.com/p/android/issues/detail?id=69205
		supportInvalidateOptionsMenu();
		return ret;
	}

	@Override
	public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
		if (key == null || !key.equals(PrefsConstants.CHOOSER_STYLE_KEY)) return;
		final boolean newGrid = "grid".equals(_prefs.getString(PrefsConstants.CHOOSER_STYLE_KEY, "list"));
		if(_useGrid == newGrid) return;
		_useGrid = newGrid;
		rethinkActionBarCapacity();
		for (ListItemBinding itemBinding : _itemBindings.values()) {
			itemBinding.text.setVisibility(_useGrid ? View.GONE : View.VISIBLE);
		}
		rethinkColumns(true);
	}

}
