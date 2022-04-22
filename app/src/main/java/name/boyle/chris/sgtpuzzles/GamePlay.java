package name.boyle.chris.sgtpuzzles;

import android.annotation.SuppressLint;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.CountDownTimer;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.core.app.NavUtils;
import androidx.core.app.ShareCompat;
import androidx.core.content.FileProvider;
import androidx.core.content.res.ResourcesCompat;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.widget.AppCompatCheckBox;
import androidx.appcompat.widget.AppCompatEditText;
import androidx.appcompat.widget.AppCompatSpinner;
import androidx.appcompat.widget.AppCompatTextView;
import androidx.appcompat.widget.PopupMenu;
import androidx.preference.PreferenceManager;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.text.MessageFormat;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.StringTokenizer;
import java.util.concurrent.Future;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static name.boyle.chris.sgtpuzzles.GameView.CURSOR_KEYS;
import static name.boyle.chris.sgtpuzzles.GameView.UI_REDO;
import static name.boyle.chris.sgtpuzzles.GameView.UI_UNDO;

public class GamePlay extends ActivityWithLoadButton implements OnSharedPreferenceChangeListener, NightModeHelper.Parent, GameGenerator.Callback
{
	private static final String TAG = "GamePlay";
	private static final String OUR_SCHEME = "sgtpuzzles";
	static final String MIME_TYPE = "text/prs.sgtatham.puzzles";
	private static final String LIGHTUP_383_PARAMS_ROT4 = "^(\\d+(?:x\\d+)?(?:b\\d+)?)s4(.*)$";
	private static final String LIGHTUP_383_REPLACE_ROT4 = "$1s3$2";
	private static final String[] OBSOLETE_EXECUTABLES_IN_DATA_DIR = {"puzzlesgen", "puzzlesgen-with-pie", "puzzlesgen-no-pie"};
	private static final String COLUMNS_OF_SUB_BLOCKS = "Columns of sub-blocks";
	private static final String ROWS_OF_SUB_BLOCKS = "Rows of sub-blocks";
	private static final String COLS_LABEL_TAG = "colLabel";
	private static final String ROWS_ROW_TAG = "rowsRow";
	private static final Pattern DIMENSIONS = Pattern.compile("(\\d+)( ?)x\\2(\\d+)(.*)");

	private ProgressDialog progress;
	private CountDownTimer progressResetRevealer;
	private TextView statusBar;
	private SmallKeyboard keyboard;
	private RelativeLayout mainLayout;
	private GameView gameView;
	private Map<Integer, String> gameTypesById;
	private MenuEntry[] gameTypesMenu = new MenuEntry[]{};
	private int currentType = 0;
	private final GameGenerator gameGenerator = new GameGenerator();
	private Future<?> generationInProgress = null;
	private boolean solveEnabled = false, customVisible = false,
			undoEnabled = false, redoEnabled = false,
			undoIsLoadGame = false, redoIsLoadGame = false;
	private String undoToGame = null, redoToGame = null;
	private SharedPreferences prefs, state;
	private static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2;
	static final long MAX_SAVE_SIZE = 1000000; // 1MB; we only have 16MB of heap
	private boolean gameWantsTimer = false;
	private static final int TIMER_INTERVAL = 20;
	private AlertDialog dialog;
	private AlertDialog.Builder dialogBuilder;
	private int dialogEvent;
	private ArrayList<String> dialogIds;
	private TableLayout dialogLayout;
	BackendName currentBackend = null;
	private BackendName startingBackend = null;
	private String lastKeys = "", lastKeysIfArrows = "";
	private Menu menu;
	private String maybeUndoRedo = "" + ((char)UI_UNDO) + ((char)UI_REDO);
	private boolean startedFullscreen = false, cachedFullscreen = false;
	private boolean keysAlreadySet = false;
	private boolean everCompleted = false;
	private long lastKeySent = 0;
	private NightModeHelper nightModeHelper;
	private Intent appStartIntentOnResume = null;
	private boolean swapLR = false;
	private boolean migrateLightUp383InProgress = false;

	private enum UIVisibility {
		UNDO(1), REDO(2), CUSTOM(4), SOLVE(8), STATUS(16);
		private final int _flag;
		UIVisibility(final int flag) { _flag = flag; }
		public int getValue() { return _flag; }
	}

	private enum MsgType { TIMER, COMPLETED }
	private static class PuzzlesHandler extends Handler
	{
		final WeakReference<GamePlay> ref;
		PuzzlesHandler(GamePlay outer) {
			super(Looper.getMainLooper());
			ref = new WeakReference<>(outer);
		}
		public void handleMessage( Message msg ) {
			GamePlay outer = ref.get();
			if (outer != null) outer.handleMessage(msg);
		}
	}
	final Handler handler = new PuzzlesHandler(this);

	private void handleMessage(Message msg) {
		switch( MsgType.values()[msg.what] ) {
		case TIMER:
			if( progress == null ) {
				timerTick();
				if (currentBackend == BackendName.INERTIA) {
					gameView.ensureCursorVisible();
				}
			}
			if( gameWantsTimer ) {
				handler.sendMessageDelayed(
						handler.obtainMessage(MsgType.TIMER.ordinal()),
						TIMER_INTERVAL);
			}
			break;
		case COMPLETED:
			try {
				completedInternal();
			} catch (WindowManager.BadTokenException activityWentAway) {
				// fine, nothing we can do here
				Log.d(TAG, "completed failed!", activityWentAway);
			}
			break;
		}
	}

	private void showProgress(final GameLaunch launch)
	{
		int msgId = launch.needsGenerating() ? R.string.starting : R.string.resuming;
		final boolean returnToChooser = launch.isFromChooser();
		progress = new ProgressDialog(this);
		progress.setMessage(getString(msgId));
		progress.setIndeterminate(true);
		progress.setCancelable(true);
		progress.setCanceledOnTouchOutside(false);
		progress.setOnCancelListener(dialog1 -> abort(null, returnToChooser));
		progress.setButton(DialogInterface.BUTTON_NEGATIVE, getString(android.R.string.cancel), (dialog, which) -> abort(null, returnToChooser));
		if (launch.needsGenerating()) {
			final BackendName backend = launch.getWhichBackend();
			final String label = getString(R.string.reset_this_backend, backend.getDisplayName());
			progress.setButton(DialogInterface.BUTTON_NEUTRAL, label, (dialog, which) -> {
				final SharedPreferences.Editor editor = state.edit();
				editor.remove(PrefsConstants.SAVED_GAME_PREFIX + backend);
				editor.remove(PrefsConstants.SAVED_COMPLETED_PREFIX + backend);
				editor.remove(PrefsConstants.LAST_PARAMS_PREFIX + backend);
				editor.apply();
				currentBackend = null;  // prevent save undoing our reset
				abort(null, true);
			});
		}
		progress.show();
		if (launch.needsGenerating()) {
			progress.getButton(DialogInterface.BUTTON_NEUTRAL).setVisibility(View.GONE);
			progressResetRevealer = new CountDownTimer(3000, 3000) {
				public void onTick(long millisUntilFinished) {
				}

				public void onFinish() {
					progress.getButton(DialogInterface.BUTTON_NEUTRAL).setVisibility(View.VISIBLE);
				}
			}.start();
		}
	}

	private void dismissProgress()
	{
		if (progress == null) return;
		if (progressResetRevealer != null) {
			progressResetRevealer.cancel();
			progressResetRevealer = null;
		}
		try {
			progress.dismiss();
		} catch (IllegalArgumentException ignored) {}  // race condition?
		progress = null;
	}

	@NonNull
	private String saveToString()
	{
		if (currentBackend == null || progress != null) throw new IllegalStateException("saveToString in invalid state");
		final ByteArrayOutputStream baos = new ByteArrayOutputStream();
		serialise(baos);
		final String saved = baos.toString();
		if (saved.isEmpty()) throw new IllegalStateException("serialise returned empty string");
		return saved;
	}

	private void save()
	{
		if (currentBackend == null) return;
		final String saved = saveToString();
		final SharedPreferences.Editor ed = state.edit();
		ed.remove("engineName");
		ed.putString(PrefsConstants.SAVED_BACKEND, currentBackend.toString());
		ed.putString(PrefsConstants.SAVED_GAME_PREFIX + currentBackend, saved);
		ed.putBoolean(PrefsConstants.SAVED_COMPLETED_PREFIX + currentBackend, everCompleted);
		ed.putString(PrefsConstants.LAST_PARAMS_PREFIX + currentBackend, getCurrentParams());
		ed.apply();
	}

	void gameViewResized()
	{
		if (progress == null && gameView.w > 10 && gameView.h > 10)
			resizeEvent(gameView.wDip, gameView.hDip);
	}

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		prefs = PreferenceManager.getDefaultSharedPreferences(this);
		prefs.registerOnSharedPreferenceChangeListener(this);
		state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE);
		gameTypesById = new LinkedHashMap<>();
		gameTypesMenu = new MenuEntry[]{};

		applyFullscreen(false);  // must precede super.onCreate and setContentView
		cachedFullscreen = startedFullscreen = prefs.getBoolean(PrefsConstants.FULLSCREEN_KEY, false);
		applyStayAwake();
		applyOrientation();
		super.onCreate(savedInstanceState);
		if (GameGenerator.executableIsMissing(this)) {
			finish();
			return;
		}
		setContentView(R.layout.main);
		if (getSupportActionBar() != null) {
			getSupportActionBar().setDisplayHomeAsUpEnabled(true);
			getSupportActionBar().setDisplayUseLogoEnabled(false);
			getSupportActionBar().addOnMenuVisibilityListener(visible -> {
				// https://code.google.com/p/android/issues/detail?id=69205
				if (!visible) {
					supportInvalidateOptionsMenu();
					rethinkActionBarCapacity();
				}
			});
		}
		mainLayout = findViewById(R.id.mainLayout);
		statusBar = findViewById(R.id.statusBar);
		gameView = findViewById(R.id.game);
		keyboard = findViewById(R.id.keyboard);
		dialogIds = new ArrayList<>();
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);
		gameView.requestFocus();
		nightModeHelper = new NightModeHelper(this, this);
		applyLimitDPI(false);
		if (prefs.getBoolean(PrefsConstants.KEYBOARD_BORDERS_KEY, false)) {
			applyKeyboardBorders();
		}
		applyMouseLongPress();
		applyMouseBackKey();
		refreshStatusBarColours();
		getWindow().setBackgroundDrawable(null);
		appStartIntentOnResume = getIntent();
		cleanUpOldExecutables();
	}

	private void refreshStatusBarColours() {
		final boolean night = nightModeHelper.isNight();
		final int foreground = ResourcesCompat.getColor(getResources(), night ? R.color.night_status_bar_text : R.color.status_bar_text, getTheme());
		final int background = ResourcesCompat.getColor(getResources(), night ? R.color.night_game_background : R.color.game_background, getTheme());
		statusBar.setTextColor(foreground);
		statusBar.setBackgroundColor(background);
	}

	/** work around http://code.google.com/p/android/issues/detail?id=21181 */
	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		return progress == null && (gameView.onKeyDown(keyCode, event) || super.onKeyDown(keyCode, event));
	}

	@Override
	public boolean onKeyUp(int keyCode, @NonNull KeyEvent event) {
		if (keyCode == KeyEvent.KEYCODE_MENU) {
			if (hackForSubmenus == null) openOptionsMenu();
			hackForSubmenus.performIdentifierAction(R.id.game_menu, 0);
			return true;
		}
		// work around http://code.google.com/p/android/issues/detail?id=21181
		return progress == null && (gameView.onKeyUp(keyCode, event) || super.onKeyUp(keyCode, event));
	}

	@Override
	public void onBackPressed() {
		// ignore if game key or touch processed in last 600ms - likely accidental
		if (System.nanoTime() - lastKeySent < 600000000) return;
		super.onBackPressed();
		overridePendingTransition(0, 0);
	}

	@Override
	public boolean dispatchKeyEvent(@NonNull KeyEvent event) {
		int keyCode = event.getKeyCode();
		// Only delegate for MENU key, delegating for other keys might break something(?)
		if (progress == null && keyCode == KeyEvent.KEYCODE_MENU && gameView.dispatchKeyEvent(event)) {
			return true;
		}
		return super.dispatchKeyEvent(event);
	}

	@Override
	protected void onNewIntent(Intent intent)
	{
		super.onNewIntent(intent);
		if (progress != null) {
			stopGameGeneration();
			dismissProgress();
		}
		migrateToPerPuzzleSave();
		migrateLightUp383Start();
		BackendName backendFromChooser = null;
		// Don't regenerate on resurrecting a URL-bound activity from the recent list
		if ((intent.getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) == 0) {
			String s = intent.getStringExtra("game");
			Uri u = intent.getData();
			if (s != null && s.length() > 0) {
				Log.d(TAG, "starting game from Intent, " + s.length() + " bytes");
				startGame(GameLaunch.ofSavedGame(s));
				return;
			} else if (u != null) {
				Log.d(TAG, "URI is: \"" + u + "\"");
				if (OUR_SCHEME.equals(u.getScheme())) {
					final String[] split = u.getSchemeSpecificPart().split(":", 2);
					final BackendName incoming = BackendName.byLowerCase(split[0]);
					if (incoming == null) {
						abort("Unrecognised game in URI: " + u, true);
						return;
					}
					if (split.length > 1) {
						if (split[1].contains(":")) {
							startGame(GameLaunch.ofGameID(incoming, split[1]));
						} else {  // params only
							startGame(GameLaunch.toGenerate(incoming, split[1]));
						}
						return;
					}
					if (incoming.equals(currentBackend) && !everCompleted) {
						// already alive & playing incomplete game of that kind; keep it.
						return;
					}
					backendFromChooser = incoming;
				}
				if (backendFromChooser == null) {
					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
							&& ("file".equalsIgnoreCase(u.getScheme()) || u.getScheme() == null)) {
						Utils.unlikelyBug(this, R.string.old_file_manager);
						return;
					}
					try {
						checkSize(u);
						startGame(GameLaunch.ofSavedGame(Utils.readAllOf(getContentResolver().openInputStream(u))));
					} catch (IllegalArgumentException | IOException e) {
						e.printStackTrace();
						abort(e.getMessage(), true);
					}
					return;
				}
			}
		}

		if (backendFromChooser != null) {
			final String savedGame = state.getString(PrefsConstants.SAVED_GAME_PREFIX + backendFromChooser, null);
			// We have a saved game, and if it's completed the user probably wants a fresh one.
			// Theoretically we could silently load it and ask midend_status() but remembering is
			// still faster and some people play large games.
			final boolean wasCompleted = state.getBoolean(PrefsConstants.SAVED_COMPLETED_PREFIX + backendFromChooser, false);
			if (savedGame == null || wasCompleted) {
				Log.d(TAG, "generating as requested");
				startGame(GameLaunch.toGenerateFromChooser(backendFromChooser));
				return;
			}
			Log.d(TAG, "restoring last state of " + backendFromChooser);
			startGame(GameLaunch.ofLocalState(backendFromChooser, savedGame, true));
		} else {
			final BackendName savedBackend = BackendName.byLowerCase(state.getString(PrefsConstants.SAVED_BACKEND, null));
			if (savedBackend != null) {
				final String savedGame = state.getString(PrefsConstants.SAVED_GAME_PREFIX + savedBackend, null);
				if (savedGame == null) {
					Log.e(TAG, "missing state for " + savedBackend);
					startGame(GameLaunch.toGenerateFromChooser(savedBackend));
				} else {
					Log.d(TAG, "normal launch; resuming game of " + savedBackend);
					startGame(GameLaunch.ofLocalState(savedBackend, savedGame, false));
				}
			} else {
				Log.d(TAG, "no state, starting chooser");
				startChooserAndFinish();
			}
		}
	}

	private void warnOfStateLoss(String newGame, final Runnable continueLoading, final boolean returnToChooser) {
		final BackendName backend;
		try {
			backend = identifyBackend(newGame);
		} catch (IllegalArgumentException ignored) {
			// It won't replace an existing game if it's invalid (we'll handle this later during load).
			continueLoading.run();
			return;
		}
		boolean careAboutOldGame = !state.getBoolean(PrefsConstants.SAVED_COMPLETED_PREFIX + backend, true);
		if (careAboutOldGame) {
			final String savedGame = state.getString(PrefsConstants.SAVED_GAME_PREFIX + backend, null);
			if (savedGame == null || savedGame.contains("NSTATES :1:1")) {
				careAboutOldGame = false;
			}
		}
		if (careAboutOldGame) {
			new AlertDialog.Builder(GamePlay.this)
					.setMessage(MessageFormat.format(getString(R.string.replaceGame), backend.getDisplayName()))
					.setPositiveButton(android.R.string.ok, (dialog1, which) -> continueLoading.run())
					.setNegativeButton(android.R.string.cancel, (dialog1, which) -> abort(null, returnToChooser))
					.show();
		} else {
			continueLoading.run();
		}
	}

	private void migrateToPerPuzzleSave() {
		final String oldSave = state.getString(PrefsConstants.OLD_SAVED_GAME, null);
		if (oldSave != null) {
			final boolean oldCompleted = state.getBoolean(PrefsConstants.OLD_SAVED_COMPLETED, false);
			SharedPreferences.Editor ed = state.edit();
			ed.remove(PrefsConstants.OLD_SAVED_GAME);
			ed.remove(PrefsConstants.OLD_SAVED_COMPLETED);
			try {
				final BackendName oldBackend = identifyBackend(oldSave);
				ed.putString(PrefsConstants.SAVED_BACKEND, oldBackend.toString());
				ed.putString(PrefsConstants.SAVED_GAME_PREFIX + oldBackend, oldSave);
				ed.putBoolean(PrefsConstants.SAVED_COMPLETED_PREFIX + oldBackend, oldCompleted);
			} catch (IllegalArgumentException ignored) {}
			ed.apply();
		}
	}

	private void migrateLightUp383Start() {
		if (state.contains(PrefsConstants.LIGHTUP_383_NEED_MIGRATE)) return;
		final String lastLightUpParams = state.getString(PrefsConstants.LAST_PARAMS_PREFIX + "lightup", "");
		final String savedLightUp = state.getString(PrefsConstants.SAVED_GAME_PREFIX + "lightup", "");
		final String[] parts = savedLightUp.split("PARAMS {2}:\\d+:");
		final boolean needMigrate = lastLightUpParams.matches(LIGHTUP_383_PARAMS_ROT4)
				|| (parts.length > 1 && parts[1].matches(LIGHTUP_383_PARAMS_ROT4));
		state.edit().putBoolean(PrefsConstants.LIGHTUP_383_NEED_MIGRATE, needMigrate).apply();
	}

	private String migrateLightUp383(final BackendName currentBackend, final String in) {
		migrateLightUp383InProgress = (currentBackend == BackendName.LIGHTUP) && state.getBoolean(PrefsConstants.LIGHTUP_383_NEED_MIGRATE, false);
		return migrateLightUp383InProgress
				? in.replaceAll(LIGHTUP_383_PARAMS_ROT4, LIGHTUP_383_REPLACE_ROT4)
				: in;
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		this.menu = menu;
		getMenuInflater().inflate(R.menu.main, menu);
		applyUndoRedoKbd();
		return true;
	}

	private Menu hackForSubmenus;
	@Override
	public boolean onPrepareOptionsMenu(Menu menu)
	{
		super.onPrepareOptionsMenu(menu);
		hackForSubmenus = menu;
		updateUndoRedoEnabled();
		final MenuItem typeItem = menu.findItem(R.id.type_menu);
		final boolean enableType = generationInProgress != null || !gameTypesById.isEmpty() || customVisible;
		typeItem.setEnabled(enableType);
		typeItem.setVisible(enableType);
		return true;
	}

	private String orientGameType(final BackendName backendName, final String type) {
		if (!prefs.getBoolean(PrefsConstants.AUTO_ORIENT, PrefsConstants.AUTO_ORIENT_DEFAULT)
			|| backendName == BackendName.SOLO  // Solo is square whatever happens so no point
			|| type == null) {
			return type;
		}
		final boolean viewLandscape = (gameView.w > gameView.h);
		final Matcher matcher = DIMENSIONS.matcher(type);
		if (matcher.matches()) {
			int w = Integer.parseInt(Objects.requireNonNull(matcher.group(1)));
			int h = Integer.parseInt(Objects.requireNonNull(matcher.group(3)));
			boolean typeLandscape = (w > h);
			if (typeLandscape != viewLandscape) {
				return matcher.group(3) + matcher.group(2) + "x" + matcher.group(2) + matcher.group(1) + matcher.group(4);
			}
		}
		return type;
	}

	private void updateUndoRedoEnabled() {
		final MenuItem undoItem = menu.findItem(R.id.undo);
		final MenuItem redoItem = menu.findItem(R.id.redo);
		undoItem.setEnabled(undoEnabled);
		redoItem.setEnabled(redoEnabled);
		undoItem.setIcon(undoEnabled ? R.drawable.ic_action_undo : R.drawable.ic_action_undo_disabled);
		redoItem.setIcon(redoEnabled ? R.drawable.ic_action_redo : R.drawable.ic_action_redo_disabled);
	}

	private void startChooserAndFinish()
	{
		NavUtils.navigateUpFromSameTask(this);
		overridePendingTransition(0, 0);
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item)
	{
		int itemId = item.getItemId();
		boolean ret = true;
		// these all build menus on demand because we had to wait for anchor buttons to exist
		if (itemId == R.id.game_menu) {
			doGameMenu();
		} else if (itemId == R.id.type_menu) {
			doTypeMenu();
		} else if (itemId == R.id.help_menu) {
			doHelpMenu();
		} else if (itemId == android.R.id.home) {
			startChooserAndFinish();
		} else if (itemId == R.id.undo) {
			sendKey(0, 0, UI_UNDO);
		} else if (itemId == R.id.redo) {
			sendKey(0, 0, UI_REDO);
		} else {
			ret = super.onOptionsItemSelected(item);
		}
		// https://code.google.com/p/android/issues/detail?id=69205
		supportInvalidateOptionsMenu();
		rethinkActionBarCapacity();
		return ret;
	}

	private void doGameMenu() {
		final PopupMenu gameMenu = popupMenuWithIcons();
		gameMenu.getMenuInflater().inflate(R.menu.game_menu, gameMenu.getMenu());
		final MenuItem solveItem = gameMenu.getMenu().findItem(R.id.solve);
		solveItem.setEnabled(solveEnabled);
		solveItem.setVisible(solveEnabled);
		gameMenu.setOnMenuItemClickListener(item -> {
			int itemId = item.getItemId();
			if (itemId == R.id.newgame) {
				startNewGame();
			} else if (itemId == R.id.restart) {
				restartEvent();
			} else if (itemId == R.id.solve) {
				solveMenuItemClicked();
			} else if (itemId == R.id.load) {
				loadGame();
			} else if (itemId == R.id.save) {
				try {
					saveLauncher.launch(suggestFilenameForShare());
				} catch (ActivityNotFoundException e) {
					Utils.unlikelyBug(this, R.string.saf_missing_short);
				}
			} else if (itemId == R.id.share) {
				share();
			} else if (itemId == R.id.settings) {
				final Intent prefsIntent = new Intent(GamePlay.this, PrefsActivity.class);
				prefsIntent.putExtra(PrefsActivity.PrefsMainFragment.BACKEND_EXTRA, currentBackend.toString());
				startActivity(prefsIntent);
			} else {
				return false;
			}
			return true;
		});
		gameMenu.show();
	}

	private void solveMenuItemClicked() {
		try {
			solveEvent();
		} catch (IllegalArgumentException e) {
			messageBox(getString(R.string.Error), e.getMessage(), false);
		}
	}

	private final MenuItem.OnMenuItemClickListener TYPE_CLICK_LISTENER = item -> {
		final int itemId = item.getItemId();
		if (itemId == R.id.custom) {
			configEvent(CFG_SETTINGS);
		} else {
			final String presetParams = orientGameType(currentBackend, Objects.requireNonNull(gameTypesById.get(itemId)));
			Log.d(TAG, "preset: " + itemId + ": " + presetParams);
			startGame(GameLaunch.toGenerate(currentBackend, presetParams));
		}
		return true;
	};

	private void doTypeMenu() {
		doTypeMenu(gameTypesMenu, true);
	}

	private void doTypeMenu(final MenuEntry[] menuEntries, final boolean includeCustom) {
		final PopupMenu typeMenu = new PopupMenu(GamePlay.this, findViewById(R.id.type_menu));
		typeMenu.getMenuInflater().inflate(R.menu.type_menu, typeMenu.getMenu());
		for (final MenuEntry entry : menuEntries) {
			final MenuItem added = typeMenu.getMenu().add(R.id.typeGroup, entry.getId(), Menu.NONE, orientGameType(currentBackend, entry.getTitle()));
			if (entry.getParams() != null) {
				added.setOnMenuItemClickListener(TYPE_CLICK_LISTENER);
				if (currentType == entry.getId()) {
					added.setChecked(true);
				}
			} else {
				if (menuContainsCurrent(entry.getSubmenu())) {
					added.setChecked(true);
				}
				added.setOnMenuItemClickListener(item -> {
					doTypeMenu(entry.getSubmenu(), false);
					return true;
				});
			}
		}
		typeMenu.getMenu().setGroupCheckable(R.id.typeGroup, true, true);
		if (includeCustom) {
			final MenuItem customItem = typeMenu.getMenu().findItem(R.id.custom);
			customItem.setVisible(customVisible);
			customItem.setOnMenuItemClickListener(TYPE_CLICK_LISTENER);
			if( currentType < 0 ) customItem.setChecked(true);
		}
		typeMenu.show();
	}

	private boolean menuContainsCurrent(MenuEntry[] submenu) {
		for (final MenuEntry entry : submenu) {
			if (entry.getId() == currentType) {
				return true;
			}
			if (entry.getSubmenu() != null && menuContainsCurrent(entry.getSubmenu())) {
				return true;
			}
		}
		return false;
	}

	private void doHelpMenu() {
		final PopupMenu helpMenu = new PopupMenu(GamePlay.this, findViewById(R.id.help_menu));
		helpMenu.setForceShowIcon(true);
		helpMenu.getMenuInflater().inflate(R.menu.help_menu, helpMenu.getMenu());
		final MenuItem solveItem = helpMenu.getMenu().findItem(R.id.solve);
		solveItem.setEnabled(solveEnabled);
		solveItem.setVisible(solveEnabled);
		helpMenu.getMenu().findItem(R.id.this_game).setTitle(MessageFormat.format(
				getString(R.string.how_to_play_game), GamePlay.this.getTitle()));
		helpMenu.setOnMenuItemClickListener(item -> {
			int itemId = item.getItemId();
			if (itemId == R.id.this_game) {
				Intent intent = new Intent(GamePlay.this, HelpActivity.class);
				intent.putExtra(HelpActivity.TOPIC, htmlHelpTopic());
				startActivity(intent);
				return true;
			} else if (itemId == R.id.solve) {
				solveMenuItemClicked();
				return true;
			} else if (itemId == R.id.feedback) {
				Utils.sendFeedbackDialog(this);
				return true;
			}
			return false;
		});
		helpMenu.show();
	}

	private PopupMenu popupMenuWithIcons() {
		final PopupMenu popupMenu = new PopupMenu(GamePlay.this, findViewById(R.id.game_menu));
		popupMenu.setForceShowIcon(true);
		return popupMenu;
	}

	private void share() {
		final Uri uriWithMimeType;
		final String saved = saveToString();
		try {
			uriWithMimeType = writeCacheFile(saved);
		} catch (IOException e) {
			Utils.unlikelyBug(this, R.string.cache_fail_short);
			return;
		}
		final ShareCompat.IntentBuilder intentBuilder = new ShareCompat.IntentBuilder(this)
				.setStream(uriWithMimeType)
				.setType(GamePlay.MIME_TYPE);
		startActivity(intentBuilder.createChooserIntent());
	}

	private Uri writeCacheFile(final String content) throws IOException {
		Uri uri;
		final File shareDir = new File(getCacheDir(), "share");
		//noinspection ResultOfMethodCallIgnored
		shareDir.mkdir();
		final File file = new File(shareDir, suggestFilenameForShare());
		FileOutputStream out = new FileOutputStream(file);
		out.write(content.getBytes());
		out.close();
		uri = FileProvider.getUriForFile(this, getPackageName() + ".fileprovider", file);
		return uri;
	}

	private String suggestFilenameForShare() {
		return currentBackend.getDisplayName() + ".sgtp";
	}

	private final ActivityResultLauncher<String> saveLauncher = registerForActivityResult(new ActivityResultContracts.CreateDocument() {
		@NonNull
		@Override
		public Intent createIntent(@NonNull Context context, @NonNull String input) {
			return super.createIntent(context, input)
					.setType(MIME_TYPE);
		}
	}, uri -> {
		if (uri == null) return;
		FileOutputStream fileOutputStream = null;
		ParcelFileDescriptor pfd = null;
		try {
			final String saved = saveToString();
			pfd = getContentResolver().openFileDescriptor(uri, "w");
			if (pfd == null) {
				throw new IOException("Could not open " + uri);
			}
			fileOutputStream = new FileOutputStream(pfd.getFileDescriptor());
			fileOutputStream.write(saved.getBytes());
		} catch (IOException e) {
			messageBox(getString(R.string.Error), getString(R.string.save_failed_prefix) + e.getMessage(), false);
		} finally {
			Utils.closeQuietly(fileOutputStream);
			Utils.closeQuietly(pfd);
		}
	});

	private void abort(final String why, final boolean returnToChooser)
	{
		stopGameGeneration();
		dismissProgress();
		if (why != null && !why.equals("")) {
			messageBox(getString(R.string.Error), why, returnToChooser);
		} else if (returnToChooser) {
			startChooserAndFinish();
			return;
		}
		startingBackend = currentBackend;
		if (currentBackend != null) {
			requestKeys(currentBackend, getCurrentParams());
		}
	}

	private void cleanUpOldExecutables() {
		if (state.getBoolean(PrefsConstants.PUZZLESGEN_CLEANUP_DONE, false)) {
			return;
		}
		// We used to copy the executable to our dataDir and execute it. I don't remember why
		// executing directly from nativeLibraryDir didn't work, but it definitely does now.
		// Clean up any previously stashed executable in dataDir to save the user some space.
		for (final String toDelete : OBSOLETE_EXECUTABLES_IN_DATA_DIR) {
			try {
				Log.d(TAG, "deleting obsolete file: " + toDelete);
				//noinspection ResultOfMethodCallIgnored
				new File(getApplicationInfo().dataDir, toDelete).delete();  // ok to fail
			}
			catch (SecurityException ignored) {}
		}
		prefs.edit().remove(PrefsConstants.OLD_PUZZLESGEN_LAST_UPDATE).apply();
		state.edit().putBoolean(PrefsConstants.PUZZLESGEN_CLEANUP_DONE, true).apply();
	}

	private void startNewGame()
	{
		startGame(GameLaunch.toGenerate(currentBackend, orientGameType(currentBackend, migrateLightUp383(currentBackend, getCurrentParams()))));
	}

	private void startGame(final GameLaunch launch)
	{
		startGame(launch, false);
	}

	private void startGame(final GameLaunch launch, final boolean isRedo)
	{
		Log.d(TAG, "startGame: " + launch);
		if (progress != null) {
			throw new RuntimeException("startGame while already starting!");
		}
		final String previousGame;
		if (isRedo || launch.needsGenerating()) {
			purgeStates();
			redoToGame = null;
			previousGame = (currentBackend == null) ? null : saveToString();
		} else {
			previousGame = null;
		}
		showProgress(launch);
		stopGameGeneration();
		BackendName backend = launch.getWhichBackend();
		if (backend == null) {
			try {
				backend = identifyBackend(launch.getSaved());
			} catch (IllegalArgumentException e) {
				abort(e.getMessage(), launch.isFromChooser());  // invalid file
				return;
			}
		}
		startingBackend = backend;
		if (launch.needsGenerating()) {
			startGameGeneration(launch, previousGame);
		} else if (!launch.isOfLocalState() && launch.getSaved() != null) {
			warnOfStateLoss(launch.getSaved(), () -> startGameConfirmed(false, launch, previousGame), launch.isFromChooser());
		} else {
			startGameConfirmed(false, launch, previousGame);
		}
	}

	private void startGameGeneration(GameLaunch launch, String previousGame) {
		final BackendName whichBackend = launch.getWhichBackend();
		String params = launch.getParams();
		final List<String> args = new ArrayList<>();
		args.add(whichBackend.toString());
		if (launch.getSeed() != null) {
			args.add("--seed");
			args.add(launch.getSeed());
		} else {
			if (params == null) {
				params = migrateLightUp383(whichBackend, getLastParams(whichBackend));
				if (params == null) {
					params = (getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE)
							? "--landscape" : "--portrait";
					Log.d(TAG, "Using default params with orientation: " + params);
				} else {
					Log.d(TAG, "Using last params: " + params);
				}
			} else {
				Log.d(TAG, "Using specified params: " + params);
			}
			args.add(params);
		}
		requestKeys(startingBackend, params);
		generationInProgress = gameGenerator.generate(getApplicationInfo(), launch, args, previousGame, this);
	}

	@Override
	public void gameGeneratorSuccess(final GameLaunch launch, final String previousGame) {
		runOnUiThread(() -> startGameConfirmed(true, launch, previousGame));
	}

	@Override
	public void gameGeneratorFailure(final Exception e, final boolean isFromChooser) {
		runOnUiThread(() -> abort(e.getMessage(), isFromChooser));  // probably bogus params
	}

	private void startGameConfirmed(final boolean generating, final GameLaunch launch, final String previousGame) {
		final String toPlay = launch.getSaved();
		final String gameID = launch.getGameID();
		if (toPlay == null && gameID == null) {
			Log.d(TAG, "startGameThread: null game, presumably cancelled");
			return;
		}
		final boolean changingGame;
		if (currentBackend == null) {
			if (launch.isFromChooser()) {
				final BackendName savedBackend = BackendName.byLowerCase(state.getString(PrefsConstants.SAVED_BACKEND, null));
				changingGame = savedBackend == null || savedBackend != startingBackend;
			} else {
				changingGame = true;  // launching app
			}
		} else {
			changingGame = (currentBackend != startingBackend);
		}
		if (previousGame != null && !changingGame && !previousGame.equals(toPlay)) {
			undoToGame = previousGame;
		} else {
			undoToGame = null;
		}

		try {
			if (toPlay != null) {
				startPlaying(gameView, toPlay);
			} else {
				startPlayingGameID(gameView, startingBackend, gameID);
			}
		} catch (IllegalArgumentException e) {
			abort(e.getMessage(), launch.isFromChooser());  // probably bogus params
			return;
		}

		currentBackend = startingBackend;
		gameView.refreshColours(currentBackend);
		gameView.resetZoomForClear();
		gameView.clear();
		applyUndoRedoKbd();
		gameView.keysHandled = 0;
		everCompleted = false;

		final String currentParams = orientGameType(currentBackend, getCurrentParams());
		refreshPresets(currentParams);
		gameView.setDragModeFor(currentBackend);
		setTitle(currentBackend.getDisplayName());
		if (getSupportActionBar() != null) {
			final int titleOverride = getResources().getIdentifier("title_" + currentBackend, "string", getPackageName());
			getSupportActionBar().setTitle(titleOverride > 0 ? getString(titleOverride) : currentBackend.getDisplayName());
		}
		final int flags = getUIVisibility();
		changedState((flags & UIVisibility.UNDO.getValue()) > 0, (flags & UIVisibility.REDO.getValue()) > 0);
		customVisible = (flags & UIVisibility.CUSTOM.getValue()) > 0;
		solveEnabled = (flags & UIVisibility.SOLVE.getValue()) > 0;
		setStatusBarVisibility((flags & UIVisibility.STATUS.getValue()) > 0);

		if (!generating) {  // we didn't know params until we loaded the game
			requestKeys(currentBackend, currentParams);
		}
		inertiaFollow(false);
		// We have a saved completion flag but completion could have been done; find out whether
		// it's really completed
		if (launch.isOfLocalState() && !launch.isUndoingOrRedoing() && isCompletedNow()) {
			completed();
		}
		final boolean hasArrows = computeArrowMode(currentBackend).hasArrows();
		setCursorVisibility(hasArrows);
		if (changingGame) {
			if (prefs.getBoolean(PrefsConstants.CONTROLS_REMINDERS_KEY, true)) {
				if (hasArrows || !showToastIfExists("toast_no_arrows_" + currentBackend)) {
					showToastIfExists("toast_" + currentBackend);
				}
			}
		}
		dismissProgress();
		gameView.rebuildBitmap();
		if (menu != null) onPrepareOptionsMenu(menu);
		save();
		if (migrateLightUp383InProgress) {
			state.edit().putBoolean(PrefsConstants.LIGHTUP_383_NEED_MIGRATE, false).apply();
		}
	}

	private boolean showToastIfExists(final String name) {
		final int reminderId = getResources().getIdentifier(name, "string", getPackageName());
		if (reminderId <= 0) {
			return false;
		}
		Toast.makeText(GamePlay.this, reminderId, Toast.LENGTH_LONG).show();
		return true;
	}

	private void refreshPresets(final String currentParams) {
		currentType = -1;
		gameTypesMenu = getPresets();
		populateGameTypesById(gameTypesMenu, currentParams);
	}

	private void populateGameTypesById(final MenuEntry[] menuEntries, final String currentParams) {
		for (final MenuEntry entry : menuEntries) {
			if (entry.getParams() != null) {
				gameTypesById.put(entry.getId(), entry.getParams());
				if (orientGameType(currentBackend, currentParams).equals(orientGameType(currentBackend, entry.getParams()))) {
					currentType = entry.getId();
				}
			} else {
				populateGameTypesById(entry.getSubmenu(), currentParams);
			}
		}
	}

	private void checkSize(Uri uri) {
		Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.SIZE}, null, null, null, null);
		try {
			if (cursor != null && cursor.moveToFirst()) {
				int sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE);
				if (cursor.isNull(sizeIndex)) return;
				if (cursor.getInt(sizeIndex) > MAX_SAVE_SIZE) {
					throw new IllegalArgumentException(getString(R.string.file_too_big));
				}
			}
		} finally {
			Utils.closeQuietly(cursor);
		}
	}

	private String getLastParams(@NonNull final BackendName whichBackend) {
		return orientGameType(whichBackend, state.getString(PrefsConstants.LAST_PARAMS_PREFIX + whichBackend, null));
	}

	private void stopGameGeneration()
	{
		if (generationInProgress == null) {
			return;
		}
		generationInProgress.cancel(true);
		generationInProgress = null;
	}

	@Override
	protected void onPause()
	{
		handler.removeMessages(MsgType.TIMER.ordinal());
		nightModeHelper.onPause();
		if (progress == null) save();
		super.onPause();
	}

	private boolean restartOnResume = false;

	@Override
	protected void onResume()
	{
		super.onResume();
		if (restartOnResume) {
			startActivity(new Intent(this, RestartActivity.class));
			finish();
			return;
		}
		nightModeHelper.onResume();
		if (appStartIntentOnResume != null) {
			onNewIntent(appStartIntentOnResume);
			appStartIntentOnResume = null;
		}
	}

	@Override
	protected void onDestroy()
	{
		stopGameGeneration();
		super.onDestroy();
	}

	@Override
	public void onWindowFocusChanged( boolean f )
	{
		if (f && gameWantsTimer && currentBackend != null
				&& ! handler.hasMessages(MsgType.TIMER.ordinal())) {
			resetTimerBaseline();
			handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()),
					TIMER_INTERVAL);
		}
	}

	@SuppressLint("CommitPrefEdits")
	public void setSwapLR(boolean swap) {
		swapLR = swap;
		prefs.edit().putBoolean(PrefsConstants.SWAP_L_R_PREFIX + currentBackend, swap).apply();
	}

	void sendKey(PointF p, int k)
	{
		sendKey(Math.round(p.x), Math.round(p.y), k);
	}

	void sendKey(int x, int y, int k)
	{
		sendKey(x, y, k, false);
	}

	void sendKey(int x, int y, int k, boolean isRepeat)
	{
		if (progress != null || currentBackend == null) return;
		if (k == '\f') {
			// menu button hack
			openOptionsMenu();
			return;
		}
		if (k == UI_UNDO && undoIsLoadGame) {
			if (!isRepeat) {
				Utils.toastFirstFewTimes(this, state, PrefsConstants.UNDO_NEW_GAME_SEEN, 3, R.string.undo_new_game_toast);
				final GameLaunch launchUndo = GameLaunch.undoingOrRedoingNewGame(undoToGame);
				redoToGame = saveToString();
				startGame(launchUndo);
			}
			return;
		}
		if (k == UI_REDO && redoIsLoadGame) {
			if (!isRepeat) {
				Utils.toastFirstFewTimes(this, state, PrefsConstants.REDO_NEW_GAME_SEEN, 3, R.string.redo_new_game_toast);
				final GameLaunch launchRedo = GameLaunch.undoingOrRedoingNewGame(redoToGame);
				redoToGame = null;
				startGame(launchRedo, true);
			}
			return;
		}
		if (swapLR && (k >= GameView.FIRST_MOUSE && k <= GameView.LAST_MOUSE)) {
			final int whichButton = (k - GameView.FIRST_MOUSE) % 3;
			if (whichButton == 0) {
				k += 2;  // left; send right
			} else if (whichButton == 2) {
				k -= 2;  // right; send left
			}
		}
		keyEvent(x, y, k);
		if (CURSOR_KEYS.contains(k) || (currentBackend == BackendName.INERTIA && k == '\n')) {
			gameView.ensureCursorVisible();
		}
		gameView.requestFocus();
		if (startedFullscreen) {
			lightsOut(true);
		}
		lastKeySent = System.nanoTime();
	}

	private boolean prevLandscape = false;
	private void setKeyboardVisibility(final BackendName whichBackend, final Configuration c)
	{
		boolean landscape = (c.orientation == Configuration.ORIENTATION_LANDSCAPE);
		if (landscape != prevLandscape || keyboard == null) {
			// Must recreate KeyboardView on orientation change because it
			// caches the x,y for its preview popups
			// http://code.google.com/p/android/issues/detail?id=4559
			if (keyboard != null) mainLayout.removeView(keyboard);
			final boolean showBorders = prefs.getBoolean(PrefsConstants.KEYBOARD_BORDERS_KEY, false);
			final int layout = showBorders ? R.layout.keyboard_bordered : R.layout.keyboard_borderless;
			keyboard = (SmallKeyboard) getLayoutInflater().inflate(layout, mainLayout, false);
			keyboard.setUndoRedoEnabled(undoEnabled, redoEnabled);
			RelativeLayout.LayoutParams klp = new RelativeLayout.LayoutParams(
					RelativeLayout.LayoutParams.WRAP_CONTENT,
					RelativeLayout.LayoutParams.WRAP_CONTENT);
			RelativeLayout.LayoutParams slp = new RelativeLayout.LayoutParams(
					RelativeLayout.LayoutParams.MATCH_PARENT,
					statusBar.getLayoutParams().height);
			RelativeLayout.LayoutParams glp = new RelativeLayout.LayoutParams(
					RelativeLayout.LayoutParams.WRAP_CONTENT,
					RelativeLayout.LayoutParams.WRAP_CONTENT);
			if (landscape) {
				klp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
				klp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
				klp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
				slp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
				slp.addRule(RelativeLayout.LEFT_OF, R.id.keyboard);
				glp.addRule(RelativeLayout.ABOVE, R.id.statusBar);
				glp.addRule(RelativeLayout.LEFT_OF, R.id.keyboard);
			} else {
				klp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
				klp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
				klp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
				slp.addRule(RelativeLayout.ABOVE, R.id.keyboard);
				glp.addRule(RelativeLayout.ABOVE, R.id.statusBar);
			}
			mainLayout.addView(keyboard, klp);
			mainLayout.updateViewLayout(statusBar, slp);
			mainLayout.updateViewLayout(gameView, glp);
		}
		final SmallKeyboard.ArrowMode arrowMode = computeArrowMode(whichBackend);
		final boolean shouldHaveSwap = (lastArrowMode == SmallKeyboard.ArrowMode.ARROWS_LEFT_RIGHT_CLICK)
				|| whichBackend == BackendName.PALISADE
				|| whichBackend == BackendName.NET;
		final String maybeSwapLRKey = shouldHaveSwap ? String.valueOf(SmallKeyboard.SWAP_L_R_KEY) : "";
		keyboard.setKeys(shouldShowFullSoftKeyboard(c)
				? filterKeys(arrowMode) + maybeSwapLRKey + maybeUndoRedo
				: maybeSwapLRKey + maybeUndoRedo,
		arrowMode, whichBackend);
		swapLR = prefs.getBoolean(PrefsConstants.SWAP_L_R_PREFIX + whichBackend, false);
		keyboard.setSwapLR(swapLR);
		prevLandscape = landscape;
		mainLayout.requestLayout();
	}

	/** Whether to show data-entry keys, as opposed to undo/redo/swap-L-R which are always shown.
	 *  We show data-entry if we either don't have a real hardware keyboard (we usually don't),
	 *  or we're on the Android SDK's emulator, which has the host's keyboard, but showing the full
	 *  keyboard is useful for UI development and screenshots. */
	private static boolean shouldShowFullSoftKeyboard(final Configuration c) {
		return c.hardKeyboardHidden != Configuration.HARDKEYBOARDHIDDEN_NO || isProbablyEmulator();
	}

	private static boolean isProbablyEmulator() {
		return Build.MODEL.startsWith("sdk_");
	}

	static String getArrowKeysPrefName(final BackendName whichBackend, final Configuration c) {
		return whichBackend + PrefsConstants.ARROW_KEYS_KEY_SUFFIX
				+ (hasDpadOrTrackball(c) ? "WithDpad" : "");
	}

	static boolean getArrowKeysDefault(final BackendName whichBackend, final Resources resources, final String packageName) {
		if (hasDpadOrTrackball(resources.getConfiguration()) && !isProbablyEmulator()) return false;
		final int defaultId = resources.getIdentifier(
				whichBackend + "_arrows_default", "bool", packageName);
		return defaultId > 0 && resources.getBoolean(defaultId);
	}

	private SmallKeyboard.ArrowMode computeArrowMode(final BackendName whichBackend) {
		final boolean arrowPref = prefs.getBoolean(
				getArrowKeysPrefName(whichBackend, getResources().getConfiguration()),
				getArrowKeysDefault(whichBackend, getResources(), getPackageName()));
		return arrowPref ? lastArrowMode : SmallKeyboard.ArrowMode.NO_ARROWS;
	}

	private static boolean hasDpadOrTrackball(Configuration c) {
		return (c.navigation == Configuration.NAVIGATION_DPAD
				|| c.navigation == Configuration.NAVIGATION_TRACKBALL)
				&& (c.navigationHidden != Configuration.NAVIGATIONHIDDEN_YES);
	}

	private String filterKeys(final SmallKeyboard.ArrowMode arrowMode) {
		String filtered = lastKeys;
		if (startingBackend != null &&
				((startingBackend == BackendName.BRIDGES && !prefs.getBoolean(PrefsConstants.BRIDGES_SHOW_H_KEY, false))
				|| (startingBackend == BackendName.UNEQUAL && !prefs.getBoolean(PrefsConstants.UNEQUAL_SHOW_H_KEY, false)))) {
			filtered = filtered.replace("H", "");
		}
		if (arrowMode.hasArrows()) {
			filtered = lastKeysIfArrows + filtered;
		} else if (filtered.length() == 1 && filtered.charAt(0) == '\b') {
			filtered = "";
		}
		return filtered;
	}

	@SuppressLint("InlinedApi")
	private void setStatusBarVisibility(boolean visible)
	{
		if (!visible) statusBar.setText("");
		RelativeLayout.LayoutParams lp = (RelativeLayout.LayoutParams)statusBar.getLayoutParams();
		lp.height = visible ? RelativeLayout.LayoutParams.WRAP_CONTENT : 0;
		mainLayout.updateViewLayout(statusBar, lp);
	}

	@Override
	public void onConfigurationChanged(@NonNull Configuration newConfig)
	{
		if (keysAlreadySet) setKeyboardVisibility(startingBackend, newConfig);
		super.onConfigurationChanged(newConfig);
		rethinkActionBarCapacity();
		supportInvalidateOptionsMenu();  // for orientation of presets in type menu
	}

	/** ActionBar's capacity (width) has probably changed, so work around
	 *  http://code.google.com/p/android/issues/detail?id=20493
	 * (invalidateOptionsMenu() does not help here) */
	private void rethinkActionBarCapacity() {
		if (menu == null) return;
		DisplayMetrics dm = getResources().getDisplayMetrics();
		final int screenWidthDIP = (int) Math.round(((double) dm.widthPixels) / dm.density);
		int state = MenuItem.SHOW_AS_ACTION_ALWAYS;
		if (screenWidthDIP >= 480) {
			state |= MenuItem.SHOW_AS_ACTION_WITH_TEXT;
		}
		menu.findItem(R.id.type_menu).setShowAsAction(state);
		menu.findItem(R.id.game_menu).setShowAsAction(state);
		menu.findItem(R.id.help_menu).setShowAsAction(state);
		final boolean undoRedoKbd = prefs.getBoolean(PrefsConstants.UNDO_REDO_KBD_KEY, PrefsConstants.UNDO_REDO_KBD_DEFAULT);
		final MenuItem undoItem = menu.findItem(R.id.undo);
		undoItem.setVisible(!undoRedoKbd);
		final MenuItem redoItem = menu.findItem(R.id.redo);
		redoItem.setVisible(!undoRedoKbd);
		if (!undoRedoKbd) {
			undoItem.setShowAsAction(state);
			redoItem.setShowAsAction(state);
			updateUndoRedoEnabled();
		}
		// emulator at 598 dip looks bad with title+undo; GT-N7100 at 640dip looks good
		if (getSupportActionBar() != null) {
			getSupportActionBar().setDisplayShowTitleEnabled(screenWidthDIP > 620 || undoRedoKbd);
		}
	}

	private void messageBox(final String title, final String msg, final boolean returnToChooser)
	{
		new AlertDialog.Builder(this)
				.setTitle(title)
				.setMessage(msg)
				.setIcon(android.R.drawable.ic_dialog_alert)
				.setOnCancelListener(returnToChooser ? dialog1 -> startChooserAndFinish() : null)
				.show();
	}

	@UsedByJNI
	void showToast(final String msg, final boolean fromPattern) {
		if (fromPattern && ! prefs.getBoolean(PrefsConstants.PATTERN_SHOW_LENGTHS_KEY, false)) return;
		Toast.makeText(GamePlay.this, msg, Toast.LENGTH_SHORT).show();
	}

	@UsedByJNI
	void completed() {
		handler.sendMessageDelayed(handler.obtainMessage(MsgType.COMPLETED.ordinal()), 0);
	}

	@UsedByJNI
	void inertiaFollow(final boolean isSolved) {
		keyboard.setInertiaFollowEnabled(isSolved || currentBackend != BackendName.INERTIA);
	}

	private void completedInternal() {
		everCompleted = true;
		final boolean copyStatusBar = currentBackend == BackendName.MINES || currentBackend == BackendName.FLOOD || currentBackend == BackendName.SAMEGAME;
		final CharSequence titleText = copyStatusBar ? statusBar.getText() : getString(R.string.COMPLETED);
		if (! prefs.getBoolean(PrefsConstants.COMPLETED_PROMPT_KEY, true)) {
			Toast.makeText(GamePlay.this, titleText, Toast.LENGTH_SHORT).show();
			return;
		}
		final Dialog d = new Dialog(this, R.style.Dialog_Completed);
		WindowManager.LayoutParams lp = d.getWindow().getAttributes();
		lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
		d.getWindow().setAttributes(lp);
		d.setContentView(R.layout.completed);
		final TextView title = d.findViewById(R.id.completedTitle);
		title.setText(titleText);
		d.setCanceledOnTouchOutside(true);
		final Button newButton = d.findViewById(R.id.newgame);
		darkenTopDrawable(newButton);
		newButton.setOnClickListener(v -> {
			d.dismiss();
			startNewGame();
		});
		final Button typeButton = d.findViewById(R.id.type_menu);
		darkenTopDrawable(typeButton);
		typeButton.setOnClickListener(v -> {
			d.dismiss();
			if (hackForSubmenus == null) openOptionsMenu();
			hackForSubmenus.performIdentifierAction(R.id.type_menu, 0);
		});
		final String style = prefs.getString(PrefsConstants.CHOOSER_STYLE_KEY, "list");
		final boolean useGrid = style.equals("grid");
		final Button chooserButton = d.findViewById(R.id.other);
		chooserButton.setCompoundDrawablesWithIntrinsicBounds(0, useGrid
				? R.drawable.ic_action_view_as_grid
				: R.drawable.ic_action_view_as_list, 0, 0);
		darkenTopDrawable(chooserButton);
		chooserButton.setOnClickListener(v -> {
			d.dismiss();
			startChooserAndFinish();
		});
		d.show();
	}

	private void darkenTopDrawable(Button b) {
		final Drawable drawable = b.getCompoundDrawables()[1].mutate();
		drawable.setColorFilter(Color.BLACK, PorterDuff.Mode.SRC_ATOP);
		b.setCompoundDrawables(null, drawable, null, null);
	}

	@UsedByJNI
	void setStatus(final String status)
	{
		statusBar.setText(status.length() == 0 ? " " : status);
	}

	@UsedByJNI
	void requestTimer(boolean on)
	{
		if( gameWantsTimer && on ) return;
		gameWantsTimer = on;
		if( on ) handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()), TIMER_INTERVAL);
		else handler.removeMessages(MsgType.TIMER.ordinal());
	}

	@UsedByJNI
	void dialogInit(final int whichEvent, String title)
	{
		dialogBuilder = new AlertDialog.Builder(GamePlay.this)
				.setTitle(title)
				.setOnCancelListener(dialog1 -> configCancel())
				.setPositiveButton(android.R.string.ok, (d, whichButton) -> {
					// do nothing - but must have listener to ensure button is shown
					// (listener is overridden in dialogShow to prevent dismiss)
				});
		ScrollView sv = new ScrollView(dialogBuilder.getContext());
		dialogBuilder.setView(sv);
		if (whichEvent == CFG_SETTINGS) {
			dialogBuilder.setNegativeButton(R.string.Game_ID_, (dialog, which) -> {
				configCancel();
				configEvent(CFG_DESC);
			})
			.setNeutralButton(R.string.Seed_, (dialog, which) -> {
				configCancel();
				configEvent(CFG_SEED);
			});
		}
		sv.addView(dialogLayout = new TableLayout(dialogBuilder.getContext()));
		dialog = dialogBuilder.create();
		dialogEvent = whichEvent;
		final int xPadding = getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal);
		final int yPadding = getResources().getDimensionPixelSize(R.dimen.dialog_padding_vertical);
		dialogLayout.setPadding(xPadding, yPadding, xPadding, yPadding);
		dialogIds.clear();
	}

	@SuppressLint("InlinedApi")
	@UsedByJNI
	void dialogAddString(int whichEvent, String name, String value)
	{
		final Context context = dialogBuilder.getContext();
		dialogIds.add(name);
		AppCompatEditText et = new AppCompatEditText(context);
		// Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
		// Uglier temporary-er hack: Black Box must accept a range for ball count.
		if (whichEvent == CFG_SETTINGS && currentBackend != BackendName.BLACKBOX) {
			et.setInputType(InputType.TYPE_CLASS_NUMBER
					| InputType.TYPE_NUMBER_FLAG_DECIMAL
					| InputType.TYPE_NUMBER_FLAG_SIGNED);
		}
		et.setTag(name);
		et.setText(value);
		et.setWidth(getResources().getDimensionPixelSize((whichEvent == CFG_SETTINGS)
				? R.dimen.dialog_edit_text_width : R.dimen.dialog_long_edit_text_width));
		et.setMinHeight((int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 48, getResources().getDisplayMetrics()));
		et.setSelectAllOnFocus(true);
		AppCompatTextView tv = new AppCompatTextView(context);
		tv.setText(name);
		tv.setPadding(0, 0, getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal), 0);
		tv.setGravity(Gravity.END);
		TableRow tr = new TableRow(context);
		tr.addView(tv);
		tr.addView(et);
		tr.setGravity(Gravity.CENTER_VERTICAL);
		dialogLayout.addView(tr);
		if (whichEvent == CFG_SEED && value.indexOf('#') == value.length() - 1) {
			final AppCompatTextView seedWarning = new AppCompatTextView(context);
			seedWarning.setText(R.string.seedWarning);
			dialogLayout.addView(seedWarning);
		}
		if (COLUMNS_OF_SUB_BLOCKS.equals(name)) {
			tv.setTag(COLS_LABEL_TAG);
		} else if (ROWS_OF_SUB_BLOCKS.equals(name)) {
			tr.setTag(ROWS_ROW_TAG);
		}
	}

	@UsedByJNI
	void dialogAddBoolean(int whichEvent, String name, boolean selected)
	{
		final Context context = dialogBuilder.getContext();
		dialogIds.add(name);
		final AppCompatCheckBox c = new AppCompatCheckBox(context);
		c.setTag(name);
		c.setText(name);
		c.setChecked(selected);
		c.setMinimumHeight((int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 48, getResources().getDisplayMetrics()));
		if (currentBackend == BackendName.SOLO && name.startsWith("Jigsaw")) {
			jigsawHack(c);
			c.setOnClickListener(v -> jigsawHack(c));
		}
		dialogLayout.addView(c);
	}

	private void jigsawHack(final AppCompatCheckBox jigsawCheckbox) {
		final View colTV = dialogLayout.findViewWithTag(COLUMNS_OF_SUB_BLOCKS);
		final View rowTV = dialogLayout.findViewWithTag(ROWS_OF_SUB_BLOCKS);
		if (colTV instanceof TextView && rowTV instanceof TextView) {
			final int cols = parseTextViewInt((TextView) colTV, 3);
			final int rows = parseTextViewInt((TextView) rowTV, 3);
			if (jigsawCheckbox.isChecked()) {
				setTextViewInt((TextView) colTV, cols * rows);
				setTextViewInt((TextView) rowTV, 1);
				rowTV.setEnabled(false);
				((TextView) dialogLayout.findViewWithTag(COLS_LABEL_TAG)).setText(R.string.Size_of_sub_blocks);
				dialogLayout.findViewWithTag(ROWS_ROW_TAG).setVisibility(View.INVISIBLE);
			} else {
				if (rows == 1) {
					int size = Math.max(2, (int) Math.sqrt(cols));
					setTextViewInt((TextView) colTV, size);
					setTextViewInt((TextView) rowTV, size);
				}
				rowTV.setEnabled(true);
				((TextView) dialogLayout.findViewWithTag(COLS_LABEL_TAG)).setText(R.string.Columns_of_sub_blocks);
				dialogLayout.findViewWithTag(ROWS_ROW_TAG).setVisibility(View.VISIBLE);
			}
		}
	}

	@SuppressWarnings("SameParameterValue")
	private int parseTextViewInt(final TextView tv, final int defaultVal) {
		try {
			return (int) Math.round(Double.parseDouble(tv.getText().toString()));
		} catch (NumberFormatException e) {
			return defaultVal;
		}
	}

	private void setTextViewInt(final TextView tv, int val) {
		tv.setText(String.format(Locale.ROOT, "%d", val));
	}

	@UsedByJNI
	void dialogAddChoices(int whichEvent, String name, String value, int selection) {
		final Context context = dialogBuilder.getContext();
		StringTokenizer st = new StringTokenizer(value.substring(1), value.substring(0, 1));
		ArrayList<String> choices = new ArrayList<>();
		while (st.hasMoreTokens()) choices.add(st.nextToken());
		dialogIds.add(name);
		AppCompatSpinner s = new AppCompatSpinner(context);
		s.setTag(name);
		ArrayAdapter<String> a = new ArrayAdapter<>(context,
				android.R.layout.simple_spinner_item, choices.toArray(new String[0]));
		a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		s.setAdapter(a);
		s.setSelection(selection);
		s.setLayoutParams(new TableRow.LayoutParams(
				getResources().getDimensionPixelSize(R.dimen.dialog_spinner_width),
				(int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 48, getResources().getDisplayMetrics())));
		TextView tv = new TextView(context);
		tv.setText(name);
		tv.setPadding(0, 0, getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal), 0);
		tv.setGravity(Gravity.END);
		TableRow tr = new TableRow(context);
		tr.addView(tv);
		tr.addView(s);
		tr.setGravity(Gravity.CENTER_VERTICAL);
		dialogLayout.addView(tr);
	}

	@UsedByJNI
	void dialogShow()
	{
		dialogLayout.setColumnShrinkable(0, true);
		dialogLayout.setColumnShrinkable(1, true);
		dialogLayout.setColumnStretchable(0, true);
		dialogLayout.setColumnStretchable(1, true);
		dialog = dialogBuilder.create();
		if (getResources().getConfiguration().hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_YES) {
			// Hack to prevent the first EditText from receiving focus when the dialog is shown
			dialogLayout.setFocusableInTouchMode(true);
			dialogLayout.requestFocus();
		}
		dialog.show();
		dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(button -> {
			for (String i : dialogIds) {
				View v = dialogLayout.findViewWithTag(i);
				if (v instanceof EditText) {
					configSetString(i, ((EditText) v).getText().toString());
				} else if (v instanceof CheckBox) {
					configSetBool(i, ((CheckBox) v).isChecked() ? 1 : 0);
				} else if (v instanceof Spinner) {
					configSetChoice(i, ((Spinner) v).getSelectedItemPosition());
				}
			}
			try {
				final GameLaunch launch;
				if (dialogEvent == CFG_DESC) {
					launch = GameLaunch.ofGameID(currentBackend, getFullGameIDFromDialog());
				} else if (dialogEvent == CFG_SEED) {
					launch = GameLaunch.fromSeed(currentBackend, getFullSeedFromDialog());
				} else {
					launch = GameLaunch.toGenerate(currentBackend, configOK());
				}
				startGame(launch);
				dialog.dismiss();
			} catch (IllegalArgumentException e) {
				dismissProgress();
				messageBox(getString(R.string.Error), e.getMessage(), false);
			}
		});
	}

	private SmallKeyboard.ArrowMode lastArrowMode = SmallKeyboard.ArrowMode.NO_ARROWS;

	@UsedByJNI
	void setKeys(final String keys, final String keysIfArrows, SmallKeyboard.ArrowMode arrowMode)
	{
		if (arrowMode == null) arrowMode = SmallKeyboard.ArrowMode.ARROWS_LEFT_RIGHT_CLICK;
		lastArrowMode = arrowMode;
		lastKeys = (keys == null) ? "" : keys;
		lastKeysIfArrows = (keysIfArrows == null) ? "" : keysIfArrows;
		gameView.setHardwareKeys(lastKeys + lastKeysIfArrows);
		setKeyboardVisibility(startingBackend, getResources().getConfiguration());
		keysAlreadySet = true;
	}

	@SuppressLint("CommitPrefEdits")
	@Override
	public void onSharedPreferenceChanged(SharedPreferences p, String key)
	{
		final Configuration configuration = getResources().getConfiguration();
		if (key.equals(getArrowKeysPrefName(currentBackend, configuration))) {
			setKeyboardVisibility(startingBackend, configuration);
			setCursorVisibility(computeArrowMode(startingBackend).hasArrows());
			gameViewResized();  // cheat - we just want a redraw in case size unchanged
		} else if (key.equals(PrefsConstants.FULLSCREEN_KEY)) {
			applyFullscreen(true);  // = already started
		} else if (key.equals(PrefsConstants.STAY_AWAKE_KEY)) {
			applyStayAwake();
		} else if (key.equals(PrefsConstants.LIMIT_DPI_KEY)) {
			applyLimitDPI(true);
		} else if (key.equals(PrefsConstants.ORIENTATION_KEY)) {
			applyOrientation();
		} else if (key.equals(PrefsConstants.UNDO_REDO_KBD_KEY)) {
			applyUndoRedoKbd();
		} else if (key.equals(PrefsConstants.KEYBOARD_BORDERS_KEY)) {
			applyKeyboardBorders();
		} else if (key.equals(PrefsConstants.BRIDGES_SHOW_H_KEY) || key.equals(PrefsConstants.UNEQUAL_SHOW_H_KEY)) {
			applyShowH();
		} else if (key.equals(PrefsConstants.MOUSE_LONG_PRESS_KEY)) {
			applyMouseLongPress();
		} else if (key.equals(PrefsConstants.MOUSE_BACK_KEY)) {
			applyMouseBackKey();
		}
	}

	private void applyMouseLongPress() {
		final String pref = prefs.getString(PrefsConstants.MOUSE_LONG_PRESS_KEY, "auto");
		gameView.alwaysLongPress = "always".equals(pref);
		gameView.hasRightMouse = "never".equals(pref);
	}

	private void applyMouseBackKey() {
		gameView.mouseBackSupport = prefs.getBoolean(PrefsConstants.MOUSE_BACK_KEY, true);
	}

	private void applyKeyboardBorders() {
		if (keyboard != null) {
			mainLayout.removeView(keyboard);
		}
		keyboard = null;
		setKeyboardVisibility(startingBackend, getResources().getConfiguration());
	}

	private void applyLimitDPI(final boolean alreadyStarted) {
		final String pref = prefs.getString(PrefsConstants.LIMIT_DPI_KEY, "auto");
		gameView.limitDpi = "auto".equals(pref) ? GameView.LimitDPIMode.LIMIT_AUTO :
				"off".equals(pref) ? GameView.LimitDPIMode.LIMIT_OFF :
						GameView.LimitDPIMode.LIMIT_ON;
		if (alreadyStarted) {
			gameView.rebuildBitmap();
		}
	}

	private void applyFullscreen(final boolean alreadyStarted) {
		cachedFullscreen = prefs.getBoolean(PrefsConstants.FULLSCREEN_KEY, false);
		if (cachedFullscreen) {
			lightsOut(true);
		} else {
			lightsOut(false);
			// This shouldn't be necessary but is on Galaxy Tab 10.1
			if (alreadyStarted && startedFullscreen) restartOnResume = true;
		}
	}

	private void lightsOut(final boolean fullScreen) {
		if (fullScreen) {
			getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		} else {
			getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		}
	}

	private void applyStayAwake()
	{
		if (prefs.getBoolean(PrefsConstants.STAY_AWAKE_KEY, false)) {
			getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		} else {
			getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		}
	}

	@SuppressLint({"InlinedApi", "SourceLockedOrientationActivity"})  // This is only done at the user's explicit request
	private void applyOrientation() {
		final String orientationPref = prefs.getString(PrefsConstants.ORIENTATION_KEY, "unspecified");
		if ("landscape".equals(orientationPref)) {
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
		} else if ("portrait".equals(orientationPref)) {
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
		} else {
			setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
		}
	}

	@Override
	public void refreshNightNow(final boolean isNight, final boolean alreadyStarted) {
		gameView.night = isNight;
		if (alreadyStarted) {
			if (currentBackend != null) {
				gameView.refreshColours(currentBackend);
				gameView.clear();
				gameViewResized();  // cheat - we just want a redraw
			}
			refreshStatusBarColours();
		}
	}

	@Override
	public int getUIMode() {
		return getResources().getConfiguration().uiMode;
	}

	private void applyUndoRedoKbd() {
		boolean undoRedoKbd = prefs.getBoolean(PrefsConstants.UNDO_REDO_KBD_KEY, PrefsConstants.UNDO_REDO_KBD_DEFAULT);
		final String wantKbd = undoRedoKbd ? "UR" : "";
		if (!wantKbd.equals(maybeUndoRedo)) {
			maybeUndoRedo = wantKbd;
			setKeyboardVisibility(startingBackend, getResources().getConfiguration());
		}
		rethinkActionBarCapacity();
	}

	private void applyShowH() {
		setKeyboardVisibility(startingBackend, getResources().getConfiguration());
	}

	@UsedByJNI
	String gettext(String s)
	{
		if (s.startsWith(":")) {
			String[] choices = s.substring(1).split(":");
			StringBuilder ret = new StringBuilder();
			for (String choice : choices) ret.append(":").append(gettext(choice));
			return ret.toString();
		}
		String id = s
				.replaceAll("^([0-9])","_$1")
				.replaceAll("%age","percentage")
				.replaceAll("','","comma")
				.replaceAll("%[.0-9]*u?[sd]","X")
				.replaceAll("[^A-Za-z0-9_]+", "_");
		if( id.endsWith("_") ) id = id.substring(0,id.length()-1);
		int resId = getResources().getIdentifier(id, "string", getPackageName());
		if (resId > 0) {
			return getString(resId);
		}
		Log.i(TAG, "gettext: NO TRANSLATION: " + s + " -> " + id + " -> ???");
		return s;
	}

	@UsedByJNI
	void changedState(final boolean canUndo, final boolean canRedo) {
		undoEnabled = canUndo || undoToGame != null;
		undoIsLoadGame = !canUndo && undoToGame != null;
		redoEnabled = canRedo || redoToGame != null;
		redoIsLoadGame = !canRedo && redoToGame != null;
		if (keyboard != null) {
			keyboard.setUndoRedoEnabled(undoEnabled, redoEnabled);
		}
		if (menu != null) {
			MenuItem mi;
			mi = menu.findItem(R.id.undo);
			if (mi != null) {
				mi.setEnabled(undoEnabled);
				mi.setIcon(undoEnabled ? R.drawable.ic_action_undo : R.drawable.ic_action_undo_disabled);
			}
			mi = menu.findItem(R.id.redo);
			if (mi != null) {
				mi.setEnabled(redoEnabled);
				mi.setIcon(redoEnabled ? R.drawable.ic_action_redo : R.drawable.ic_action_redo_disabled);
			}
		}
	}

	@UsedByJNI
	void purgingStates()
	{
		redoToGame = null;
	}

	@UsedByJNI
	boolean allowFlash()
	{
		return prefs.getBoolean(PrefsConstants.VICTORY_FLASH_KEY, true);
	}

	native void startPlaying(GameView _gameView, String savedGame);
	native void startPlayingGameID(GameView _gameView, BackendName whichBackend, String gameID);
	native void timerTick();
	native String htmlHelpTopic();
	native void keyEvent(int x, int y, int k);
	native void restartEvent();
	native void solveEvent();
	native void resizeEvent(int x, int y);
	native void configEvent(int whichEvent);
	native String configOK();
	native String getFullGameIDFromDialog();
	native String getFullSeedFromDialog();
	native void configCancel();
	native void configSetString(String item_ptr, String s);
	native void configSetBool(String item_ptr, int selected);
	native void configSetChoice(String item_ptr, int selected);
	native void serialise(ByteArrayOutputStream baos);
	@NonNull native static BackendName identifyBackend(String savedGame);
	native String getCurrentParams();
	native void requestKeys(BackendName backend, String params);
	native void setCursorVisibility(boolean visible);
	native MenuEntry[] getPresets();
	native int getUIVisibility();
	native void resetTimerBaseline();
	native void purgeStates();
	native boolean isCompletedNow();

	static {
		System.loadLibrary("puzzles");
	}
}
