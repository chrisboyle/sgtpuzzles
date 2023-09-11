package name.boyle.chris.sgtpuzzles;

import static android.view.View.IMPORTANT_FOR_ACCESSIBILITY_NO;
import static android.view.View.IMPORTANT_FOR_ACCESSIBILITY_YES;

import android.annotation.SuppressLint;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
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
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.annotation.VisibleForTesting;
import androidx.core.app.NavUtils;
import androidx.core.app.ShareCompat;
import androidx.core.content.FileProvider;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.widget.PopupMenu;
import androidx.core.content.res.ResourcesCompat;
import androidx.core.graphics.drawable.DrawableCompat;
import androidx.lifecycle.Lifecycle;
import androidx.preference.PreferenceManager;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.text.MessageFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Future;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static name.boyle.chris.sgtpuzzles.SmallKeyboard.SEEN_SWAP_L_R_TOAST;
import static name.boyle.chris.sgtpuzzles.config.ConfigBuilder.Event.CFG_SETTINGS;
import static name.boyle.chris.sgtpuzzles.GameView.CURSOR_KEYS;
import static name.boyle.chris.sgtpuzzles.GameView.UI_REDO;
import static name.boyle.chris.sgtpuzzles.GameView.UI_UNDO;

import kotlin.Unit;
import name.boyle.chris.sgtpuzzles.backend.BRIDGES;
import name.boyle.chris.sgtpuzzles.backend.BackendName;
import name.boyle.chris.sgtpuzzles.backend.FLOOD;
import name.boyle.chris.sgtpuzzles.backend.GUESS;
import name.boyle.chris.sgtpuzzles.backend.GameEngine;
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl;
import name.boyle.chris.sgtpuzzles.backend.INERTIA;
import name.boyle.chris.sgtpuzzles.backend.MINES;
import name.boyle.chris.sgtpuzzles.backend.NET;
import name.boyle.chris.sgtpuzzles.backend.PALISADE;
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME;
import name.boyle.chris.sgtpuzzles.backend.SOLO;
import name.boyle.chris.sgtpuzzles.backend.UNDEAD;
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL;
import name.boyle.chris.sgtpuzzles.backend.UsedByJNI;
import name.boyle.chris.sgtpuzzles.config.CustomDialogBuilder;
import name.boyle.chris.sgtpuzzles.config.PrefsConstants;
import name.boyle.chris.sgtpuzzles.databinding.CompletedDialogBinding;
import name.boyle.chris.sgtpuzzles.databinding.MainBinding;
import name.boyle.chris.sgtpuzzles.launch.GameGenerator;
import name.boyle.chris.sgtpuzzles.launch.GameLaunch;
import name.boyle.chris.sgtpuzzles.launch.MenuEntry;

public class GamePlay extends ActivityWithLoadButton implements OnSharedPreferenceChangeListener, GameGenerator.Callback, CustomDialogBuilder.ActivityCallbacks, GameEngine.ActivityCallbacks
{
	private static final String TAG = "GamePlay";
	static final String OUR_SCHEME = "sgtpuzzles";
	static final String MIME_TYPE = "text/prs.sgtatham.puzzles";
	private static final Pattern DIMENSIONS = Pattern.compile("(\\d+)( ?)x\\2(\\d+)(.*)");

	private MainBinding _binding;
	private ProgressDialog progress;
	private TextView statusBar;
	private SmallKeyboard keyboard;
	private ButtonsView newKeyboard;
	private RelativeLayout mainLayout;
	private GameView gameView;
	private Map<Integer, String> gameTypesById;
	private MenuEntry[] gameTypesMenu = new MenuEntry[]{};
	private int currentType = 0;
	private GameGenerator gameGenerator;
	private Future<?> generationInProgress = null;
	private boolean solveEnabled = false, customVisible = false,
			undoEnabled = false, redoEnabled = false,
			undoIsLoadGame = false, redoIsLoadGame = false;
	private String undoToGame = null, redoToGame = null;
	private SharedPreferences prefs, state;
	static final long MAX_SAVE_SIZE = 1000000; // 1MB; we only have 16MB of heap
	private boolean gameWantsTimer = false;
	private static final int TIMER_INTERVAL = 20;
	@NonNull private GameEngine gameEngine = GameEngine.NOT_LOADED_YET;
	BackendName currentBackend = null;
	private BackendName startingBackend = null;
	private String lastKeys = "", lastKeysIfArrows = "";
	private Menu menu;
	private String maybeUndoRedo = "" + ((char)UI_UNDO) + ((char)UI_REDO);
	private boolean startedFullscreen = false, cachedFullscreen = false;
	private boolean keysAlreadySet = false;
	private boolean everCompleted = false;
	private long lastKeySent = 0;
	private boolean _wasNight;
	private Intent appStartIntentOnResume = null;
	private boolean swapLR = false;

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
		switch (MsgType.values()[msg.what]) {
			case TIMER -> {
				if (progress == null) {
					gameEngine.timerTick();
					if (currentBackend == INERTIA.INSTANCE) {
						gameView.ensureCursorVisible(gameEngine.getCursorLocation());
					}
				}
				if (gameWantsTimer) {
					handler.sendMessageDelayed(
							handler.obtainMessage(MsgType.TIMER.ordinal()),
							TIMER_INTERVAL);
				}
			}
			case COMPLETED -> {
				try {
					completedInternal();
				} catch (WindowManager.BadTokenException activityWentAway) {
					// fine, nothing we can do here
					Log.d(TAG, "completed failed!", activityWentAway);
				}
			}
		}
	}

	// Yes, it would be more modern-looking to put the spinner in the game area instead of a dialog,
	// but the user experience wouldn't be that different as we do need to block everything.
	/** @noinspection deprecation */
	private void showProgress(final GameLaunch launch)
	{
		int msgId = launch.needsGenerating ? R.string.starting : R.string.resuming;
		final boolean returnToChooser = launch.getOrigin().getShouldReturnToChooserOnFail();
		progress = new ProgressDialog(this);
		progress.setMessage(getString(msgId));
		progress.setIndeterminate(true);
		progress.setCancelable(true);
		progress.setCanceledOnTouchOutside(false);
		progress.setOnCancelListener(dialog1 -> abort(null, returnToChooser));
		progress.setButton(DialogInterface.BUTTON_NEGATIVE, getString(android.R.string.cancel), (dialog, which) -> abort(null, returnToChooser));
		if (launch.needsGenerating) {
			final BackendName backend = launch.getWhichBackend();
			final String label = getString(R.string.reset_this_backend, backend.getDisplayName());
			progress.setButton(DialogInterface.BUTTON_NEUTRAL, label, (dialog, which) -> {
				resetBackendState(backend);
				currentBackend = null;  // prevent save undoing our reset
				abort(null, true);
			});
		}
		progress.show();
		if (launch.needsGenerating) {
			progress.getButton(DialogInterface.BUTTON_NEUTRAL).setVisibility(View.GONE);
			final ProgressDialog progressFinal = progress;  // dismissProgress could null this
			final CountDownTimer progressResetRevealer = new CountDownTimer(3000, 3000) {
				public void onTick(long millisUntilFinished) {
				}

				public void onFinish() {
					if (progressFinal.isShowing()) {
						progressFinal.getButton(DialogInterface.BUTTON_NEUTRAL).setVisibility(View.VISIBLE);
					}
				}
			}.start();
			progress.setOnDismissListener(d -> progressResetRevealer.cancel());
		}
	}

	private void resetBackendState(BackendName backend) {
		final SharedPreferences.Editor editor = state.edit();
		editor.remove(PrefsConstants.SAVED_GAME_PREFIX + backend);
		editor.remove(PrefsConstants.SAVED_COMPLETED_PREFIX + backend);
		editor.remove(PrefsConstants.LAST_PARAMS_PREFIX + backend);
		editor.apply();
		prefs.edit().remove(backend.getPreferencesName()).apply();
	}

	private void dismissProgress()
	{
		if (progress == null) return;
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
		gameEngine.serialise(baos);
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
		ed.putString(PrefsConstants.LAST_PARAMS_PREFIX + currentBackend, gameEngine.getCurrentParams());
		ed.apply();
	}

	void gameViewResized()
	{
		if (progress == null && gameView.w > 10 && gameView.h > 10)
			gameEngine.resizeEvent(gameView.wDip, gameView.hDip);
	}

	public float suggestDensity(final int w, final int h) {
		return gameEngine.suggestDensity(w, h);
	}

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		prefs = PreferenceManager.getDefaultSharedPreferences(this);
		prefs.registerOnSharedPreferenceChangeListener(this);
		state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE);
		gameTypesById = new LinkedHashMap<>();
		gameTypesMenu = new MenuEntry[]{};
		gameGenerator = new GameGenerator();

		applyFullscreen(false);  // must precede super.onCreate and setContentView
		cachedFullscreen = startedFullscreen = prefs.getBoolean(PrefsConstants.FULLSCREEN_KEY, false);
		applyStayAwake();
		applyOrientation();
		super.onCreate(savedInstanceState);
		if (GameGenerator.executableIsMissing(this)) {
			finish();
			return;
		}
		_binding = MainBinding.inflate(getLayoutInflater());
		setContentView(_binding.getRoot());
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
		mainLayout = _binding.mainLayout;
		statusBar = _binding.statusBar;
		gameView = _binding.gameView;
		newKeyboard = _binding.newKeyboard;
		newKeyboard.getOnKeyListener().setValue(c -> {
			sendKey(0, 0, c);
			return Unit.INSTANCE;
		});
		newKeyboard.getOnSwapLRListener().setValue(swap -> {
			setSwapLR(swap);
			Utils.toastFirstFewTimes(this, state, SEEN_SWAP_L_R_TOAST, 4,
					swap ? R.string.toast_swap_l_r_on : R.string.toast_swap_l_r_off);
			return Unit.INSTANCE;
		});
		keyboard = findViewById(R.id.keyboard);
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);
		gameView.requestFocus();
		_wasNight = NightModeHelper.isNight(getResources().getConfiguration());
		applyLimitDPI(false);
		if (prefs.getBoolean(PrefsConstants.KEYBOARD_BORDERS_KEY, false)) {
			applyKeyboardBorders();
		}
		applyMouseLongPress();
		applyMouseBackKey();
		getWindow().setBackgroundDrawable(null);
		appStartIntentOnResume = getIntent();
		GameGenerator.cleanUpOldExecutables(prefs, state, new File(getApplicationInfo().dataDir));
	}

	/** work around Android issue 21181 whose bug page has vanished :-( */
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
		// work around Android issue 21181
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
		BackendName backendFromChooser = null;
		// Don't regenerate on resurrecting a URL-bound activity from the recent list
		if ((intent.getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) == 0) {
			String s = intent.getStringExtra("game");
			Uri u = intent.getData();
			if (s != null && s.length() > 0) {
				Log.d(TAG, "starting game from Intent, " + s.length() + " bytes");
				final GameLaunch launch;
				try {
					launch = GameLaunch.ofSavedGameFromIntent(s);
				} catch (IllegalArgumentException e) {
					abort(e.getMessage(), true);  // invalid file
					return;
				}
				startGame(launch);
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
							startGame(GameLaunch.ofGameID(incoming, split[1], GameLaunch.Origin.INTENT_COMPLEX_URI));
						} else {  // params only
							startGame(GameLaunch.toGenerate(incoming, split[1], GameLaunch.Origin.INTENT_COMPLEX_URI));
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
						startGame(GameLaunch.fromContentURI(Utils.readAllOf(getContentResolver().openInputStream(u))));
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

	private void warnOfStateLoss(final BackendName backend, final Runnable continueLoading, final boolean returnToChooser) {
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
			|| backendName == SOLO.INSTANCE  // Solo is square whatever happens so no point
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
			if (itemId == R.id.new_game) {
				startNewGame();
			} else if (itemId == R.id.restart) {
				gameEngine.restartEvent();
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
			gameEngine.solveEvent();
		} catch (IllegalArgumentException e) {
			messageBox(getString(R.string.Error), e.getMessage(), false);
		}
	}

	private final MenuItem.OnMenuItemClickListener TYPE_CLICK_LISTENER = item -> {
		final int itemId = item.getItemId();
		if (itemId == R.id.custom) {
			gameEngine.configEvent(this, CFG_SETTINGS.jni, this, currentBackend);
		} else {
			final String presetParams = orientGameType(currentBackend, Objects.requireNonNull(gameTypesById.get(itemId)));
			Log.d(TAG, "preset: " + itemId + ": " + presetParams);
			startGame(GameLaunch.toGenerate(currentBackend, presetParams, GameLaunch.Origin.BUTTON_OR_MENU_IN_ACTIVITY));
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
			} else if (entry.getSubmenu() != null) {
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
				intent.putExtra(HelpActivity.TOPIC, gameEngine.htmlHelpTopic());
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

	private final ActivityResultLauncher<String> saveLauncher = registerForActivityResult
			(new ActivityResultContracts.CreateDocument(MIME_TYPE), uri -> {
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
			setKeys(gameEngine.requestKeys(currentBackend, gameEngine.getCurrentParams()));
		}
	}

	private void startNewGame()
	{
		startGame(GameLaunch.toGenerate(currentBackend, orientGameType(currentBackend, gameEngine.getCurrentParams()), GameLaunch.Origin.BUTTON_OR_MENU_IN_ACTIVITY));
	}

	public void startGame(@NonNull final GameLaunch launch)
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
		if (isRedo || launch.needsGenerating) {
			gameEngine.purgeStates();
			redoToGame = null;
			previousGame = (currentBackend == null) ? null : saveToString();
		} else {
			previousGame = null;
		}
		showProgress(launch);
		stopGameGeneration();
		startingBackend = launch.getWhichBackend();
		if (launch.needsGenerating) {
			startGameGeneration(launch, previousGame);
		} else if (!launch.getOrigin().isOfLocalState() && launch.getSaved() != null) {
			warnOfStateLoss(launch.getWhichBackend(), () -> startGameConfirmed(launch, previousGame), launch.getOrigin().getShouldReturnToChooserOnFail());
		} else {
			startGameConfirmed(launch, previousGame);
		}
	}

	private void startGameGeneration(final GameLaunch launch, final String previousGame) {
		final List<String> args = new ArrayList<>();
		args.add(launch.getWhichBackend().toString());
		if (launch.getSeed() != null) {
			args.add("--seed");
			args.add(launch.getSeed());
		} else {
			final String params = decideParams(launch);
			args.add(params);
		}
		generationInProgress = gameGenerator.generate(getApplicationInfo(), launch, args, previousGame, this);
	}

	@NonNull
	private String decideParams(final GameLaunch launch) {
		if (launch.getParams() != null) {
			Log.d(TAG, "Using specified params: " + launch.getParams());
			return launch.getParams();
		}
		final String lastParams = getLastParams(launch.getWhichBackend());
		if (lastParams != null) {
			Log.d(TAG, "Using last params: " + lastParams);
			return lastParams;
		}
		final String defaultParams = orientGameType(launch.getWhichBackend(), GameEngineImpl.getDefaultParams(launch.getWhichBackend()));
		Log.d(TAG, "Using default params with orientation: " + defaultParams);
		return defaultParams;
	}

	@Override
	public void gameGeneratorSuccess(final GameLaunch launch, final String previousGame) {
		runOnUiThread(() -> startGameConfirmed(launch, previousGame));
	}

	@Override
	public void gameGeneratorFailure(final Exception e, final GameLaunch launch) {
		runOnUiThread(() -> {
			if (launch.getOrigin().shouldResetBackendStateOnFail() && hasState(launch.getWhichBackend())) {
				resetBackendState(launch.getWhichBackend());
				generationInProgress = null;
				dismissProgress();
				startGame(launch);
			} else {
				abort(e.getMessage(), launch.getOrigin().getShouldReturnToChooserOnFail());  // probably bogus params
			}
		});
	}

	private boolean hasState(final BackendName backend) {
		return state.contains(PrefsConstants.SAVED_GAME_PREFIX + backend)
				|| state.contains(PrefsConstants.SAVED_COMPLETED_PREFIX + backend)
				|| state.contains(PrefsConstants.LAST_PARAMS_PREFIX + backend);

	}

	private void startGameConfirmed(final GameLaunch launch, final String previousGame) {
		if (launch.getSaved() == null && launch.getGameID() == null) {
			throw new IllegalStateException("startGameConfirmed with un-generated game");
		}
		final boolean changingGame = previousGame == null || startingBackend != GameEngineImpl.identifyBackend(previousGame);
		if (previousGame != null && !changingGame && !previousGame.equals(launch.getSaved())) {
			undoToGame = previousGame;
		} else {
			undoToGame = null;
		}

		try {
			gameEngine = GameEngineImpl.fromLaunch(launch, this, gameView, this);
		} catch (IllegalArgumentException e) {
			abort(e.getMessage(), launch.getOrigin().getShouldReturnToChooserOnFail());  // probably bogus params
			return;
		}

		currentBackend = startingBackend;
		refreshColours();
		gameView.resetZoomForClear();
		gameView.clear();
		applyUndoRedoKbd();
		gameView.keysHandled = 0;
		everCompleted = false;

		final String currentParams = orientGameType(currentBackend, gameEngine.getCurrentParams());
		refreshPresets(currentParams);
		gameView.setDragModeFor(currentBackend);
		setTitle(currentBackend.getDisplayName());
		if (getSupportActionBar() != null) {
			getSupportActionBar().setTitle(currentBackend.getTitle());
		}
		final int flags = gameEngine.getUiVisibility();
		changedState((flags & UIVisibility.UNDO.getValue()) > 0, (flags & UIVisibility.REDO.getValue()) > 0);
		customVisible = (flags & UIVisibility.CUSTOM.getValue()) > 0;
		solveEnabled = (flags & UIVisibility.SOLVE.getValue()) > 0;
		setStatusBarVisibility((flags & UIVisibility.STATUS.getValue()) > 0);

		setKeys(gameEngine.requestKeys(currentBackend, currentParams));
		inertiaFollow(false);
		// We have a saved completion flag but completion could have been undone; find out whether
		// it's really completed
		if (launch.getOrigin().shouldHighlightCompletionOnLaunch() && gameEngine.isCompletedNow()) {
			completed();
		}
		final boolean hasArrows = computeArrowMode(currentBackend).hasArrows();
		gameEngine.setCursorVisibility(hasArrows);
		if (changingGame) {
			if (prefs.getBoolean(PrefsConstants.CONTROLS_REMINDERS_KEY, true)) {
				if (hasArrows || !showToastIfExists(currentBackend.getControlsToastNoArrows())) {
					showToastIfExists(currentBackend.getControlsToast());
				}
			}
		}
		dismissProgress();
		gameView.rebuildBitmap();
		if (menu != null) onPrepareOptionsMenu(menu);
		save();
	}

	private boolean showToastIfExists(@StringRes final int reminderId) {
		if (reminderId == 0) {
			return false;
		}
		Toast.makeText(GamePlay.this, reminderId, Toast.LENGTH_LONG).show();
		return true;
	}

	private void refreshPresets(final String currentParams) {
		currentType = -1;
		gameTypesMenu = gameEngine.getPresets();
		populateGameTypesById(gameTypesMenu, currentParams);
	}

	private void populateGameTypesById(final MenuEntry[] menuEntries, final String currentParams) {
		for (final MenuEntry entry : menuEntries) {
			if (entry.getParams() != null) {
				gameTypesById.put(entry.getId(), entry.getParams());
				if (orientGameType(currentBackend, currentParams).equals(orientGameType(currentBackend, entry.getParams()))) {
					currentType = entry.getId();
				}
			} else if (entry.getSubmenu() != null) {
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
		if (appStartIntentOnResume != null) {
			onNewIntent(appStartIntentOnResume);
			appStartIntentOnResume = null;
		}
	}

	@Override
	protected void onDestroy()
	{
		stopGameGeneration();
		gameGenerator.onDestroy();
		gameEngine.onDestroy();
		super.onDestroy();
	}

	@Override
	public void onWindowFocusChanged( boolean f )
	{
		if (f && gameWantsTimer && currentBackend != null
				&& ! handler.hasMessages(MsgType.TIMER.ordinal())) {
			gameEngine.resetTimerBaseline();
			handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()),
					TIMER_INTERVAL);
		}
	}

	public void setSwapLR(boolean swap) {
		if (!currentBackend.getSwapLRNatively()) swapLR = swap;
		currentBackend.putSwapLR(this, swap);
		// temporarily while we have two keyboards:
		keyboard.setSwapLR(swap);
		newKeyboard.getSwapLR().setValue(swap);
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
		gameEngine.keyEvent(x, y, k);
		if (CURSOR_KEYS.contains(k) || (currentBackend == INERTIA.INSTANCE && k == '\n')) {
			gameView.ensureCursorVisible(gameEngine.getCursorLocation());
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
			newKeyboard.getUndoEnabled().setValue(undoEnabled);
			newKeyboard.getRedoEnabled().setValue(redoEnabled);
			RelativeLayout.LayoutParams klp = new RelativeLayout.LayoutParams(
					RelativeLayout.LayoutParams.WRAP_CONTENT,
					RelativeLayout.LayoutParams.WRAP_CONTENT);
			RelativeLayout.LayoutParams nklp = new RelativeLayout.LayoutParams(
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
				nklp.addRule(RelativeLayout.LEFT_OF, R.id.keyboard);
				nklp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
				nklp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
				slp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
				slp.addRule(RelativeLayout.LEFT_OF, R.id.new_keyboard);
				glp.addRule(RelativeLayout.ABOVE, R.id.status_bar);
				glp.addRule(RelativeLayout.LEFT_OF, R.id.new_keyboard);
			} else {
				nklp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
				nklp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
				nklp.addRule(RelativeLayout.ABOVE, R.id.keyboard);
				klp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
				klp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
				klp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
				slp.addRule(RelativeLayout.ABOVE, R.id.new_keyboard);
				glp.addRule(RelativeLayout.ABOVE, R.id.status_bar);
			}
			mainLayout.addView(keyboard, klp);
			mainLayout.updateViewLayout(newKeyboard, nklp);
			mainLayout.updateViewLayout(statusBar, slp);
			mainLayout.updateViewLayout(gameView, glp);
		}
		final SmallKeyboard.ArrowMode arrowMode = computeArrowMode(whichBackend);
		final boolean shouldHaveSwap = (lastArrowMode == SmallKeyboard.ArrowMode.ARROWS_LEFT_RIGHT_CLICK)
				|| whichBackend == PALISADE.INSTANCE
				|| whichBackend == NET.INSTANCE;
		final String maybeSwapLRKey = shouldHaveSwap ? String.valueOf(SmallKeyboard.SWAP_L_R_KEY) : "";
		String keys = shouldShowFullSoftKeyboard(c)
				? filterKeys(arrowMode) + maybeSwapLRKey + maybeUndoRedo
				: maybeSwapLRKey + maybeUndoRedo;
		keyboard.setKeys(keys,
				arrowMode, whichBackend, disableCharacterIcons(whichBackend));
		final boolean swap = whichBackend.getSwapLR(this);
		keyboard.setSwapLR(swap);
		if (!whichBackend.getSwapLRNatively()) swapLR = swap;
		newKeyboard.getBackend().setValue(whichBackend);
		newKeyboard.getKeys().setValue(keys);
		newKeyboard.getArrowMode().setValue(arrowMode);
		newKeyboard.getSwapLR().setValue(swap);
		newKeyboard.getDisableCharacterIcons().setValue(disableCharacterIcons(whichBackend));
		prevLandscape = landscape;
		mainLayout.requestLayout();
	}

	private String disableCharacterIcons(BackendName whichBackend) {
		if (whichBackend != UNDEAD.INSTANCE) return "";
		final String undeadPrefs = GameEngineImpl.getPrefs(this, whichBackend);
		return (undeadPrefs != null && Arrays.asList(undeadPrefs.split("\n")).contains("monsters=letters")) ? "GVZ" : "";
	}

	/** Whether to show data-entry keys, as opposed to undo/redo/swap-L-R which are always shown.
	 *  We show data-entry if we either don't have a real hardware keyboard (we usually don't),
	 *  or we're on the Android SDK's emulator, which has the host's keyboard, but showing the full
	 *  keyboard is useful for UI development and screenshots. */
	private static boolean shouldShowFullSoftKeyboard(final Configuration c) {
		return c.hardKeyboardHidden != Configuration.HARDKEYBOARDHIDDEN_NO
				|| c.keyboard == Configuration.KEYBOARD_NOKEYS || isProbablyEmulator();
	}

	private static boolean isProbablyEmulator() {
		return Build.MODEL.startsWith("sdk_") || Build.MODEL.startsWith("Android SDK");
	}

	static String getArrowKeysPrefName(final BackendName whichBackend, final Configuration c) {
		return whichBackend + PrefsConstants.ARROW_KEYS_KEY_SUFFIX
				+ (hasDpadOrTrackball(c) ? "WithDpad" : "");
	}

	static boolean getArrowKeysDefault(final BackendName whichBackend, final Resources resources) {
		if (hasDpadOrTrackball(resources.getConfiguration()) && !isProbablyEmulator()) return false;
		return whichBackend.isArrowsVisibleByDefault();
	}

	private SmallKeyboard.ArrowMode computeArrowMode(final BackendName whichBackend) {
		final boolean arrowPref = prefs.getBoolean(
				getArrowKeysPrefName(whichBackend, getResources().getConfiguration()),
				getArrowKeysDefault(whichBackend, getResources()));
		return arrowPref ? lastArrowMode : SmallKeyboard.ArrowMode.NO_ARROWS;
	}

	private static boolean hasDpadOrTrackball(Configuration c) {
		return (c.navigation == Configuration.NAVIGATION_DPAD
				|| c.navigation == Configuration.NAVIGATION_TRACKBALL)
				&& (c.navigationHidden != Configuration.NAVIGATIONHIDDEN_YES);
	}

	private String filterKeys(final SmallKeyboard.ArrowMode arrowMode) {
		String filtered = lastKeys;
		if (startingBackend != null) {
			if ((startingBackend == BRIDGES.INSTANCE && !prefs.getBoolean(PrefsConstants.BRIDGES_SHOW_H_KEY, false))
					|| (startingBackend == UNEQUAL.INSTANCE && !prefs.getBoolean(PrefsConstants.UNEQUAL_SHOW_H_KEY, false))) {
				filtered = filtered.replace("H", "");
			}
			if ((startingBackend.isLatin()) && !prefs.getBoolean(PrefsConstants.LATIN_SHOW_M_KEY, true)) {
				filtered = filtered.replace("M", "");
			}
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
		super.onConfigurationChanged(newConfig);
		if (keysAlreadySet) setKeyboardVisibility(startingBackend, newConfig);
		final boolean isNight = NightModeHelper.isNight(newConfig);
		if (isNight != _wasNight) {
			refreshColours();
			_wasNight = isNight;
			statusBar.setTextColor(ResourcesCompat.getColor(getResources(), R.color.status_bar_text, getTheme()));
			statusBar.setBackgroundColor(ResourcesCompat.getColor(getResources(), R.color.game_background, getTheme()));
		}
		rethinkActionBarCapacity();
		supportInvalidateOptionsMenu();  // for orientation of presets in type menu
	}

	/** ActionBar's capacity (width) has probably changed, so work around
	 *  <a href="https://issuetracker.google.com/issues/36933746">Android issue 36933746</a>
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
	public void completed() {
		handler.sendMessageDelayed(handler.obtainMessage(MsgType.COMPLETED.ordinal()), 0);
	}

	@UsedByJNI
	public void inertiaFollow(final boolean isSolved) {
		keyboard.setInertiaFollowEnabled(isSolved || currentBackend != INERTIA.INSTANCE);
		newKeyboard.getHidePrimary().setValue(!isSolved && currentBackend == INERTIA.INSTANCE);
	}

	private void completedInternal() {
		everCompleted = true;
		final boolean copyStatusBar = currentBackend == MINES.INSTANCE || currentBackend == FLOOD.INSTANCE || currentBackend == SAMEGAME.INSTANCE;
		final CharSequence titleText = copyStatusBar ? statusBar.getText() : getString(R.string.COMPLETED);
		if (! prefs.getBoolean(PrefsConstants.COMPLETED_PROMPT_KEY, true)) {
			Toast.makeText(GamePlay.this, titleText, Toast.LENGTH_SHORT).show();
			return;
		}
		final Dialog d = new Dialog(this, R.style.Dialog_Completed);
		WindowManager.LayoutParams lp = d.getWindow().getAttributes();
		lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
		d.getWindow().setAttributes(lp);
		final CompletedDialogBinding dialogBinding = CompletedDialogBinding.inflate(LayoutInflater.from(d.getContext()));
		d.setContentView(dialogBinding.getRoot());
		dialogBinding.completedTitle.setText(titleText);
		d.setCanceledOnTouchOutside(true);
		darkenTopDrawable(dialogBinding.newGame);
		dialogBinding.newGame.setOnClickListener(v -> {
			d.dismiss();
			startNewGame();
		});
		darkenTopDrawable(dialogBinding.typeMenu);
		dialogBinding.typeMenu.setOnClickListener(v -> {
			d.dismiss();
			if (hackForSubmenus == null) openOptionsMenu();
			hackForSubmenus.performIdentifierAction(R.id.type_menu, 0);
		});
		final String style = prefs.getString(PrefsConstants.CHOOSER_STYLE_KEY, "list");
		final boolean useGrid = style.equals("grid");
		dialogBinding.otherGame.setCompoundDrawablesWithIntrinsicBounds(0, useGrid
				? R.drawable.ic_action_view_as_grid
				: R.drawable.ic_action_view_as_list, 0, 0);
		darkenTopDrawable(dialogBinding.otherGame);
		dialogBinding.otherGame.setOnClickListener(v -> {
			d.dismiss();
			startChooserAndFinish();
		});
		d.show();
	}

	private void darkenTopDrawable(Button b) {
		final Drawable drawable = DrawableCompat.wrap(b.getCompoundDrawables()[1].mutate());
		DrawableCompat.setTint(drawable, Color.BLACK);
		b.setCompoundDrawables(null, drawable, null, null);
	}

	@UsedByJNI
	public void setStatus(@NonNull final String status)
	{
		statusBar.setText(status.isEmpty() ? " " : status);  // Ensure consistent height
		statusBar.setImportantForAccessibility(status.isEmpty() ? IMPORTANT_FOR_ACCESSIBILITY_NO : IMPORTANT_FOR_ACCESSIBILITY_YES);
	}

	@UsedByJNI
	public void requestTimer(boolean on)
	{
		if( gameWantsTimer && on ) return;
		gameWantsTimer = on;
		if( on ) handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()), TIMER_INTERVAL);
		else handler.removeMessages(MsgType.TIMER.ordinal());
	}

	@Override
	public void customDialogError(String error) {
		dismissProgress();
		messageBox(getString(R.string.Error), error, false);
	}

	private SmallKeyboard.ArrowMode lastArrowMode = SmallKeyboard.ArrowMode.NO_ARROWS;

	void setKeys(@Nullable final GameEngine.KeysResult result)
	{
		if (result == null) return;
		lastArrowMode = (result.getArrowMode() == null) ? SmallKeyboard.ArrowMode.ARROWS_LEFT_RIGHT_CLICK : result.getArrowMode();
		lastKeys = (result.getKeys() == null) ? "" : result.getKeys();
		lastKeysIfArrows = (result.getKeysIfArrows() == null) ? "" : result.getKeysIfArrows();
		// Guess allows digits, but we don't want them in the virtual keyboard because they're already
		// on screen as the colours (if labels are enabled).
		final String addDigits = (startingBackend == GUESS.INSTANCE) ? "1234567890" : "";
		gameView.setHardwareKeys(lastKeys + lastKeysIfArrows + addDigits);
		setKeyboardVisibility(startingBackend, getResources().getConfiguration());
		keysAlreadySet = true;
	}

	@Override
	public void onSharedPreferenceChanged(SharedPreferences p, String key)
	{
		if (!getLifecycle().getCurrentState().isAtLeast(Lifecycle.State.CREATED)) return;
		if (key == null) return;
		final Configuration configuration = getResources().getConfiguration();
		if (key.equals(currentBackend.getPreferencesName())) {
			gameEngine.loadPrefs(this);
			// might have changed swapLR, or pictures vs letters in Undead
			setKeyboardVisibility(startingBackend, configuration);
			gameViewResized();  // just for redraw
		} else if (key.equals(getArrowKeysPrefName(currentBackend, configuration))) {
			setKeyboardVisibility(startingBackend, configuration);
			gameEngine.setCursorVisibility(computeArrowMode(startingBackend).hasArrows());
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
		} else if (key.equals(PrefsConstants.BRIDGES_SHOW_H_KEY) || key.equals(PrefsConstants.UNEQUAL_SHOW_H_KEY)
				|| key.equals(PrefsConstants.LATIN_SHOW_M_KEY)) {
			applyKeyboardFilters();
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
		if (startingBackend != null) {
			setKeyboardVisibility(startingBackend, getResources().getConfiguration());
		}
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

	private void refreshColours() {
		gameView.night = NightModeHelper.isNight(getResources().getConfiguration());
		if (currentBackend != null) {
			gameView.refreshColours(currentBackend, gameEngine.getColours());
			gameView.clear();
			gameViewResized();  // cheat - we just want a redraw
		}
	}

	private void applyUndoRedoKbd() {
		boolean undoRedoKbd = prefs.getBoolean(PrefsConstants.UNDO_REDO_KBD_KEY, PrefsConstants.UNDO_REDO_KBD_DEFAULT);
		final String wantKbd = undoRedoKbd ? "UR" : "";
		if (!wantKbd.equals(maybeUndoRedo)) {
			maybeUndoRedo = wantKbd;
			if (startingBackend != null)
				setKeyboardVisibility(startingBackend, getResources().getConfiguration());
		}
		rethinkActionBarCapacity();
	}

	private void applyKeyboardFilters() {
		setKeyboardVisibility(startingBackend, getResources().getConfiguration());
	}

	/*@UsedByJNI
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
	}*/

	@UsedByJNI
	public void changedState(final boolean canUndo, final boolean canRedo) {
		undoEnabled = canUndo || undoToGame != null;
		undoIsLoadGame = !canUndo && undoToGame != null;
		redoEnabled = canRedo || redoToGame != null;
		redoIsLoadGame = !canRedo && redoToGame != null;
		if (keyboard != null) {
			keyboard.setUndoRedoEnabled(undoEnabled, redoEnabled);
			newKeyboard.getUndoEnabled().setValue(undoEnabled);
			newKeyboard.getRedoEnabled().setValue(redoEnabled);
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
	public void purgingStates()
	{
		redoToGame = null;
	}

	@UsedByJNI
	public boolean allowFlash()
	{
		return prefs.getBoolean(PrefsConstants.VICTORY_FLASH_KEY, true);
	}

	@NonNull
	@VisibleForTesting  // specifically the screenshots test
	GameEngine getGameEngine() {
		return gameEngine;
	}

	@VisibleForTesting  // specifically the screenshots test
	public GameView getGameView() {
		return _binding.gameView;
	}

	static {
		System.loadLibrary("puzzles");
	}
}
