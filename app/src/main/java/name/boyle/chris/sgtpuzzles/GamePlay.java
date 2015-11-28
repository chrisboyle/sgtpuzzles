package name.boyle.chris.sgtpuzzles;

import android.Manifest;
import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.app.Activity;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnCancelListener;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.PointF;
import android.graphics.PorterDuff;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.nfc.NdefMessage;
import android.nfc.NdefRecord;
import android.nfc.NfcAdapter;
import android.nfc.NfcEvent;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.os.ParcelFileDescriptor;
import android.os.Parcelable;
import android.preference.PreferenceManager;
import android.provider.OpenableColumns;
import android.support.annotation.NonNull;
import android.support.v4.app.NavUtils;
import android.support.v4.app.ShareCompat;
import android.support.v4.content.FileProvider;
import android.support.v4.view.MenuItemCompat;
import android.support.v7.app.ActionBar;
import android.support.v7.app.AlertDialog;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.AppCompatCheckBox;
import android.support.v7.widget.AppCompatEditText;
import android.support.v7.widget.AppCompatSpinner;
import android.support.v7.widget.AppCompatTextView;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.view.View.OnClickListener;
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

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.nio.charset.Charset;
import java.text.MessageFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.StringTokenizer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;
import name.boyle.chris.sgtpuzzles.compat.SysUIVisSetter;

public class GamePlay extends AppCompatActivity implements OnSharedPreferenceChangeListener, NightModeHelper.Parent
{
	static final String TAG = "GamePlay";
	static final String STATE_PREFS_NAME = "state";
	static final String ORIENTATION_KEY = "orientation";
	private static final String ARROW_KEYS_KEY_SUFFIX = "ArrowKeys";
	static final String LIMIT_DPI_KEY = "limitDpi";
	private static final String KEYBOARD_BORDERS_KEY = "keyboardBorders";
	private static final String BRIDGES_SHOW_H_KEY = "bridgesShowH";
	private static final String UNEQUAL_SHOW_H_KEY = "unequalShowH";
	private static final String FULLSCREEN_KEY = "fullscreen";
	private static final String STAY_AWAKE_KEY = "stayAwake";
	private static final String UNDO_REDO_KBD_KEY = "undoRedoOnKeyboard";
	private static final boolean UNDO_REDO_KBD_DEFAULT = true;
	private static final String PATTERN_SHOW_LENGTHS_KEY = "patternShowLengths";
	private static final String COMPLETED_PROMPT_KEY = "completedPrompt";
	private static final String CONTROLS_REMINDERS_KEY = "controlsReminders";
	private static final String OLD_SAVED_COMPLETED = "savedCompleted";
	private static final String OLD_SAVED_GAME = "savedGame";
	public static final String SAVED_BACKEND = "savedBackend";
	private static final String SAVED_COMPLETED_PREFIX = "savedCompleted_";
	static final String SAVED_GAME_PREFIX = "savedGame_";
	public static final String LAST_PARAMS_PREFIX = "last_params_";
	public static final String SWAP_L_R_PREFIX = "swap_l_r_";
	private static final String PUZZLESGEN_LAST_UPDATE = "puzzlesgen_last_update";
	private static final String BLUETOOTH_PACKAGE_PREFIX = "com.android.bluetooth";
	private static final int REQ_CODE_CREATE_DOC = Activity.RESULT_FIRST_USER;
	private static final int REQ_CODE_STORAGE_PERMISSION = Activity.RESULT_FIRST_USER + 1;
	private static final String OUR_SCHEME = "sgtpuzzles";
	static final String MIME_TYPE = "text/prs.sgtatham.puzzles";
	private static final String STORAGE_PERMISSION_EVER_ASKED = "storage_permission_ever_asked";

	private ProgressDialog progress;
	private TextView statusBar;
	private SmallKeyboard keyboard;
	private RelativeLayout mainLayout;
	private GameView gameView;
	private SubMenu typeMenu;
	private Map<String, String> gameTypes;
	private int currentType = 0;
	private boolean workerRunning = false;
	private Process gameGenProcess = null;
	private boolean solveEnabled = false, customVisible = false,
			undoEnabled = false, redoEnabled = false;
	private SharedPreferences prefs, state;
	private static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2,
		C_STRING = 0, C_CHOICES = 1, C_BOOLEAN = 2;
	static final long MAX_SAVE_SIZE = 1000000; // 1MB; we only have 16MB of heap
	private boolean gameWantsTimer = false;
	private static final int TIMER_INTERVAL = 20;
	private StringBuffer savingState;
	private AlertDialog dialog;
	private AlertDialog.Builder dialogBuilder;
	private int dialogEvent;
	private ArrayList<String> dialogIds;
	private TableLayout dialogLayout;
	String currentBackend = null;
	private String startingBackend = null;
	private Thread worker;
	private String lastKeys = "", lastKeysIfArrows = "";
	private static final File storageDir = Environment.getExternalStorageDirectory();
	private String[] games;
	private Menu menu;
	private String maybeUndoRedo = "UR";
	private PrefsSaver prefsSaver;
	private boolean startedFullscreen = false, cachedFullscreen = false;
	private boolean keysAlreadySet = false;
	private boolean everCompleted = false;
	private final Pattern DIMENSIONS = Pattern.compile("(\\d+)( ?)x\\2(\\d+)(.*)");
	private long lastKeySent = 0;
	NightModeHelper nightModeHelper;
	private Intent appStartIntentOnResume = null;
	private boolean swapLR = false;

	enum UIVisibility {
		UNDO(1), REDO(2), CUSTOM(4), SOLVE(8), STATUS(16);
		private final int _flag;
		UIVisibility(final int flag) { _flag = flag; }
		public int getValue() { return _flag; }
	}

	enum MsgType { TIMER }
	static class PuzzlesHandler extends Handler
	{
		final WeakReference<GamePlay> ref;
		public PuzzlesHandler(GamePlay outer) {
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
			if( progress == null ) timerTick();
			if( gameWantsTimer ) {
				handler.sendMessageDelayed(
						handler.obtainMessage(MsgType.TIMER.ordinal()),
						TIMER_INTERVAL);
			}
			break;
		}
	}

	private void showProgress(int msgId, final boolean returnToChooser)
	{
		progress = new ProgressDialog(this);
		progress.setMessage( getString(msgId) );
		progress.setIndeterminate(true);
		progress.setCancelable(true);
		progress.setCanceledOnTouchOutside(false);
		progress.setOnCancelListener(new OnCancelListener() {
			public void onCancel(DialogInterface dialog) {
				abort(null, returnToChooser);
			}
		});
		progress.setButton(DialogInterface.BUTTON_NEGATIVE, getString(android.R.string.cancel), new DialogInterface.OnClickListener() {
			@Override
			public void onClick(DialogInterface dialog, int which) {
				abort(null, returnToChooser);
			}
		});
		progress.show();
	}

	private void dismissProgress()
	{
		if( progress == null ) return;
		try {
			progress.dismiss();
		} catch (IllegalArgumentException ignored) {}  // race condition?
		progress = null;
	}

	String saveToString()
	{
		if (currentBackend == null || progress != null) return null;
		savingState = new StringBuffer();
		serialise();  // serialiseWrite() callbacks will happen in here
		String s = savingState.toString();
		savingState = null;
		return s;
	}

	@SuppressLint("CommitPrefEdits")
	private void save()
	{
		String s = saveToString();
		if (s == null || s.length() == 0) return;
		SharedPreferences.Editor ed = state.edit();
		ed.remove("engineName");
		ed.putString(SAVED_BACKEND, currentBackend);
		ed.putString(SAVED_GAME_PREFIX + currentBackend, s);
		ed.putBoolean(SAVED_COMPLETED_PREFIX + currentBackend, everCompleted);
		ed.putString(LAST_PARAMS_PREFIX + currentBackend, getCurrentParams());
		prefsSaver.save(ed);
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
		state = getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE);
		prefsSaver = PrefsSaver.get(this);
		games = getResources().getStringArray(R.array.games);
		gameTypes = new LinkedHashMap<>();

		applyFullscreen(false);  // must precede super.onCreate and setContentView
		cachedFullscreen = startedFullscreen = prefs.getBoolean(FULLSCREEN_KEY, false);
		applyStayAwake();
		applyOrientation();
		super.onCreate(savedInstanceState);
		setContentView(R.layout.main);
		if (getSupportActionBar() != null) {
			getSupportActionBar().setDisplayHomeAsUpEnabled(true);
			getSupportActionBar().setDisplayUseLogoEnabled(false);
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			getSupportActionBar().addOnMenuVisibilityListener(new ActionBar.OnMenuVisibilityListener() {
				@Override
				public void onMenuVisibilityChanged(boolean visible) {
					// https://code.google.com/p/android/issues/detail?id=69205
					if (!visible) {
						supportInvalidateOptionsMenu();
						rethinkActionBarCapacity();
					}
				}
			});
		}
		mainLayout = (RelativeLayout)findViewById(R.id.mainLayout);
		statusBar = (TextView)findViewById(R.id.statusBar);
		gameView = (GameView)findViewById(R.id.game);
		keyboard = (SmallKeyboard)findViewById(R.id.keyboard);
		dialogIds = new ArrayList<>();
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);
		gameView.requestFocus();
		nightModeHelper = new NightModeHelper(this, this);
		applyLimitDPI(false);
		if (prefs.getBoolean(KEYBOARD_BORDERS_KEY, false)) {
			applyKeyboardBorders();
		}
		refreshStatusBarColours();
		getWindow().setBackgroundDrawable(null);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.ICE_CREAM_SANDWICH) {
			setUpBeam();
		}
		appStartIntentOnResume = getIntent();
	}

	private void refreshStatusBarColours() {
		final boolean night = nightModeHelper.isNight();
		final int foreground = getResources().getColor(night ? R.color.night_status_bar_text : R.color.status_bar_text);
		final int background = getResources().getColor(night ? R.color.night_game_background : R.color.game_background);
		statusBar.setTextColor(foreground);
		statusBar.setBackgroundColor(background);
	}

	@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH)
	private void setUpBeam() {
		NfcAdapter nfcAdapter = NfcAdapter.getDefaultAdapter(this);
		if (nfcAdapter == null) return;  // NFC not available on this device
		nfcAdapter.setNdefPushMessageCallback(new NfcAdapter.CreateNdefMessageCallback() {
			@Override
			public NdefMessage createNdefMessage(NfcEvent event) {
				String saved = saveToString();
				if (saved == null) return null;
				return new NdefMessage(
						new NdefRecord[]{
								createMime(saved),
								NdefRecord.createApplicationRecord(getPackageName())
						});
			}
		}, this);
	}

	@TargetApi(Build.VERSION_CODES.GINGERBREAD)
	private static NdefRecord createMime(final String content) {
		return new NdefRecord(NdefRecord.TNF_MIME_MEDIA,
				GamePlay.MIME_TYPE.getBytes(Charset.forName("US-ASCII")),
				new byte[0], content.getBytes(Charset.forName("US-ASCII")));
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
	protected void onNewIntent(Intent intent)
	{
		if( progress != null ) {
			stopNative();
			dismissProgress();
		}
		migrateToPerPuzzleSave();
		String backendFromChooser = null;
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
					String g = u.getSchemeSpecificPart();
					final String[] split = g.split(":", 2);
					if (split.length > 1) {
						if (split[1].contains(":")) {
							startGame(GameLaunch.ofGameID(split[0], split[1]));
						} else {  // params only
							startGame(GameLaunch.toGenerate(split[0], split[1]));
						}
						return;
					}
					if (games.length < 2) games = getResources().getStringArray(R.array.games);
					for (String game : games) {
						if (game.equals(g)) {
							if (game.equals(currentBackend) && !everCompleted) {
								// already alive & playing incomplete game of that kind; keep it.
								return;
							}
							backendFromChooser = game;
							break;
						}
					}
				}
				if (backendFromChooser == null) {
					final GameLaunch launch = GameLaunch.ofUri(u);
					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
							&& ("file".equalsIgnoreCase(u.getScheme()) || u.getScheme() == null)) {
						if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
							if (shouldShowStoragePermissionRationale()) {
								showRationaleThenRequest();
							} else {
								// first time, or "never ask again" ticked - we could distinguish this with state, if we wanted to show rationale the first time
								requestStoragePermission();
							}
							return;
						} else if (checkPermissionGrantBug(u)) {
							return;
						}
					}
					startGame(launch);
					return;
				}
			} else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.GINGERBREAD && handleNFC(intent)) {
				return;
			}
		}

		if (backendFromChooser != null) {
			final String savedGame = state.getString(SAVED_GAME_PREFIX + backendFromChooser, null);
			final boolean wasCompleted = state.getBoolean(SAVED_COMPLETED_PREFIX + backendFromChooser, false);
			if (savedGame == null || wasCompleted) {
				Log.d(TAG, "generating as requested");
				startGame(GameLaunch.toGenerateFromChooser(backendFromChooser));
				return;
			}
			Log.d(TAG, "restoring last state of " + backendFromChooser);
			startGame(GameLaunch.ofLocalState(backendFromChooser, savedGame, false, true));
		} else {
			final String savedBackend = state.getString(SAVED_BACKEND, null);
			if (savedBackend != null) {
				final String savedGame = state.getString(SAVED_GAME_PREFIX + savedBackend, null);
				if (savedGame == null) {
					Log.e(TAG, "missing state for " + savedBackend);
					startGame(GameLaunch.toGenerateFromChooser(savedBackend));
				} else {
					Log.d(TAG, "normal launch; resuming game of " + savedBackend);
					final boolean wasCompleted = state.getBoolean(SAVED_COMPLETED_PREFIX + savedBackend, false);
					startGame(GameLaunch.ofLocalState(savedBackend, savedGame, wasCompleted, false));
				}
			} else {
				Log.d(TAG, "no state, starting chooser");
				startChooserAndFinish();
			}
		}
	}

	@TargetApi(Build.VERSION_CODES.M)
	/** I want to show rationale the first time, not just when re-asking after a no. */
	private boolean shouldShowStoragePermissionRationale() {
		return !state.getBoolean(STORAGE_PERMISSION_EVER_ASKED, false) || shouldShowRequestPermissionRationale(Manifest.permission.READ_EXTERNAL_STORAGE);
	}

	@TargetApi(Build.VERSION_CODES.M)
	private void showRationaleThenRequest() {
		new AlertDialog.Builder(this)
				.setMessage(R.string.storage_permission_explanation)
				.setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
					@Override
					public void onClick(DialogInterface dialog, int which) {
						requestStoragePermission();
					}
				})
				.create().show();
		state.edit().putBoolean(STORAGE_PERMISSION_EVER_ASKED, true).apply();
	}

	@TargetApi(Build.VERSION_CODES.M)
	private void requestStoragePermission() {
		requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, REQ_CODE_STORAGE_PERMISSION);
	}

	@Override
	@TargetApi(Build.VERSION_CODES.M)
	public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
		if (requestCode != REQ_CODE_STORAGE_PERMISSION) return;
		if (grantResults.length < 1) {  // dialog interrupted
			finish();
			return;
		}
		final Uri uri = getIntent().getData();
		if (grantResults[0] != PackageManager.PERMISSION_GRANTED) {
			new AlertDialog.Builder(this)
					.setMessage(MessageFormat.format(getString(R.string.storage_permission_denied), uri.getPath()))
					.setOnDismissListener(new DialogInterface.OnDismissListener() {
						@Override
						public void onDismiss(DialogInterface dialog) {
							finish();
						}
					}).create().show();
			return;
		}
		if (checkPermissionGrantBug(uri)) return;
		startGame(GameLaunch.ofUri(uri));
	}

	@TargetApi(Build.VERSION_CODES.M)
	private boolean checkPermissionGrantBug(Uri uri) {
		// Work around https://code.google.com/p/android-developer-preview/issues/detail?id=2982
		// We know it's a file:// URI.
		if (new File(uri.getPath()).canRead()) {
			return false;
		}
		new AlertDialog.Builder(this)
				.setMessage(R.string.storage_permission_bug)
				.setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
					@Override
					public void onClick(DialogInterface dialog, int which) {
						// do nothing
					}
				})
				.setNeutralButton(R.string.storage_permission_bug_more, new DialogInterface.OnClickListener() {
					@Override
					public void onClick(DialogInterface dialog, int which) {
						startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(getString(R.string.storage_permission_bug_url))));
					}
				})
				.setOnDismissListener(new DialogInterface.OnDismissListener() {
					@Override
					public void onDismiss(DialogInterface dialog) {
						finish();
					}
				}).create().show();
		return true;
	}

	@TargetApi(Build.VERSION_CODES.GINGERBREAD)
	private boolean handleNFC(Intent intent) {
		if (intent == null || !NfcAdapter.ACTION_NDEF_DISCOVERED.equals(intent.getAction())) return false;
		Parcelable[] rawMessages = intent.getParcelableArrayExtra(NfcAdapter.EXTRA_NDEF_MESSAGES);
		if (rawMessages.length == 0) return false;
		NdefMessage msg = (NdefMessage) rawMessages[0];
		if (msg.getRecords().length == 0) return false;
		startGame(GameLaunch.ofSavedGame(new String(msg.getRecords()[0].getPayload())));
		return true;
	}

	private void warnOfStateLoss(String newGame, final Runnable continueLoading, final boolean returnToChooser) {
		final String backend;
		try {
			backend = games[identifyBackend(newGame)];
		} catch (IllegalArgumentException ignored) {
			// It won't replace an existing game if it's invalid (we'll handle this later during load).
			continueLoading.run();
			return;
		}
		boolean careAboutOldGame = !state.getBoolean(SAVED_COMPLETED_PREFIX + backend, true);
		if (careAboutOldGame) {
			final String savedGame = state.getString(SAVED_GAME_PREFIX + backend, null);
			if (savedGame == null || savedGame.contains("NSTATES :1:1")) {
				careAboutOldGame = false;
			}
		}
		if (careAboutOldGame) {
			final String title = getString(getResources().getIdentifier("name_" + backend, "string", getPackageName()));
			runOnUiThread(new Runnable() {
				@Override
				public void run() {
					new AlertDialog.Builder(GamePlay.this)
							.setMessage(MessageFormat.format(getString(R.string.replaceGame), title))
							.setPositiveButton(android.R.string.yes, new DialogInterface.OnClickListener() {
								@Override
								public void onClick(DialogInterface dialog, int which) {
									continueLoading.run();
								}
							})
							.setNegativeButton(android.R.string.no, new DialogInterface.OnClickListener() {
								@Override
								public void onClick(DialogInterface dialog, int which) {
									abort(null, returnToChooser);
								}
							}).create().show();
				}
			});
		} else {
			continueLoading.run();
		}
	}

	@SuppressLint("CommitPrefEdits")
	private void migrateToPerPuzzleSave() {
		final String oldSave = state.getString(OLD_SAVED_GAME, null);
		if (oldSave != null) {
			final boolean oldCompleted = state.getBoolean(OLD_SAVED_COMPLETED, false);
			SharedPreferences.Editor ed = state.edit();
			ed.remove(OLD_SAVED_GAME);
			ed.remove(OLD_SAVED_COMPLETED);
			try {
				final String oldBackend = games[identifyBackend(oldSave)];
				ed.putString(SAVED_BACKEND, oldBackend);
				ed.putString(SAVED_GAME_PREFIX + oldBackend, oldSave);
				ed.putBoolean(SAVED_COMPLETED_PREFIX + oldBackend, oldCompleted);
			} catch (IllegalArgumentException ignored) {}
			prefsSaver.save(ed);
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
		final MenuItem solveItem = menu.findItem(R.id.solve);
		solveItem.setEnabled(solveEnabled);
		solveItem.setVisible(solveEnabled);
		updateUndoRedoEnabled();
		final MenuItem typeItem = menu.findItem(R.id.type_menu);
		final boolean enableType = workerRunning || !gameTypes.isEmpty() || customVisible;
		typeItem.setEnabled(enableType);
		typeItem.setVisible(enableType);
		typeMenu = typeItem.getSubMenu();
		int i = 0;
		for(String title : gameTypes.values()) {
			if( menu.findItem(i) == null ) {
				typeMenu.add(R.id.typeGroup, i, Menu.NONE, orientGameType(title));
			}
			i++;
		}
		final MenuItem customItem = menu.findItem(R.id.custom);
		customItem.setVisible(customVisible);
		typeMenu.setGroupCheckable(R.id.typeGroup, true, true);
		if( currentType < 0 ) customItem.setChecked(true);
		else if( currentType < gameTypes.size() ) menu.findItem(currentType).setChecked(true);
		menu.findItem(R.id.this_game).setTitle(MessageFormat.format(
					getString(R.string.how_to_play_game),new Object[]{this.getTitle()}));
		return true;
	}

	private String orientGameType(String type) {
		if (type == null) return null;
		boolean screenLandscape = (getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE);
		final Matcher matcher = DIMENSIONS.matcher(type);
		if (matcher.matches()) {
			int w = Integer.parseInt(matcher.group(1));
			int h = Integer.parseInt(matcher.group(3));
			boolean typeLandscape = (w > h);
			if (typeLandscape != screenLandscape) {
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
		switch(itemId) {
		case android.R.id.home:
			startChooserAndFinish();
			break;
		case R.id.settings:
			final Intent prefsIntent = new Intent(this, PrefsActivity.class);
			prefsIntent.putExtra(PrefsActivity.BACKEND_EXTRA, currentBackend);
			startActivity(prefsIntent);
			break;
		case R.id.newgame:
			startNewGame();
			break;
		case R.id.restart:  restartEvent(); break;
		case R.id.undo:     sendKey(0, 0, 'U'); break;
		case R.id.redo:     sendKey(0, 0, 'R'); break;
		case R.id.solve:
			try {
				solveEvent();
			} catch (IllegalArgumentException e) {
				messageBox(getString(R.string.Error), e.getMessage(), false);
			}
			break;
		case R.id.custom:
			configEvent(CFG_SETTINGS);
			break;
		case R.id.this_game:
			Intent intent = new Intent(this, HelpActivity.class);
			intent.putExtra(HelpActivity.TOPIC, htmlHelpTopic());
			startActivity(intent);
			break;
		case R.id.email:
			startActivity(new Intent(this, SendFeedbackActivity.class));
			break;
		case R.id.save:
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
				Intent saver = new Intent(Intent.ACTION_CREATE_DOCUMENT);
				saver.addCategory(Intent.CATEGORY_OPENABLE);
				saver.setType(MIME_TYPE);
				try {
					startActivityForResult(saver, REQ_CODE_CREATE_DOC);
				} catch (ActivityNotFoundException ignored) {
					SendFeedbackActivity.promptToReport(GamePlay.this, R.string.saf_missing_desc, R.string.saf_missing_short);
				}
			} else {
				new FilePicker(this, storageDir, true).show();
			}
			break;
		case R.id.share:
			share();
			break;
		default:
			if (itemId < gameTypes.size()) {
				String presetParams = orientGameType((String)gameTypes.keySet().toArray()[itemId]);
				Log.d(TAG, "preset: " + itemId + ": " + presetParams);
				startGame(GameLaunch.toGenerate(currentBackend, presetParams));
			} else {
				ret = super.onOptionsItemSelected(item);
			}
			break;
		}
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			// https://code.google.com/p/android/issues/detail?id=69205
			supportInvalidateOptionsMenu();
			rethinkActionBarCapacity();
		}
		return ret;
	}

	private void share() {
		final Uri uriWithMimeType, bluetoothUri;
		final String saved = saveToString();
		try {
			uriWithMimeType = writeCacheFile("puzzle.sgtp", saved);
			bluetoothUri = writeCacheFile("bluetooth-puzzle.sgtp", saved);  // gets text/plain in FixedTypeFileProvider
		} catch (IOException e) {
			SendFeedbackActivity.promptToReport(this, R.string.cache_fail_desc, R.string.cache_fail_short);
			return;
		}
		final ShareCompat.IntentBuilder intentBuilder = ShareCompat.IntentBuilder.from(this)
				.setStream(uriWithMimeType)
				.setType(GamePlay.MIME_TYPE);
		final Intent template = intentBuilder.getIntent();
		List<ResolveInfo> candidates = this.getPackageManager().queryIntentActivities(template, 0);

		// If Bluetooth isn't around, just do the standard chooser
		boolean needBluetoothHack = false;
		for (ResolveInfo candidate : candidates) {
			if (candidate.activityInfo.packageName.startsWith(BLUETOOTH_PACKAGE_PREFIX)) {
				needBluetoothHack = true;
				break;
			}
		}
		if (!needBluetoothHack) {
			startActivity(intentBuilder.createChooserIntent());
			return;
		}

		// Fix Bluetooth sharing: the closest type it will accept is text/plain, so
		// give it that (see FixedTypeFileProvider) and rely on handling *.sgtp
		Collections.sort(candidates, new ResolveInfo.DisplayNameComparator(getPackageManager()));
		List<Intent> targets = new ArrayList<>();
		for (ResolveInfo candidate : candidates) {
			String packageName = candidate.activityInfo.packageName;
			final boolean isBluetooth = packageName.startsWith(BLUETOOTH_PACKAGE_PREFIX);
			final Uri uri = isBluetooth ? bluetoothUri : uriWithMimeType;
			Intent target = ShareCompat.IntentBuilder.from(this)
					.setStream(uri)
					.setType(isBluetooth ? "text/plain" : GamePlay.MIME_TYPE)
					.getIntent()
					.setPackage(packageName)
					.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
			grantUriPermission(packageName, uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
			targets.add(target);
		}
		Intent chooser = Intent.createChooser(targets.remove(targets.size() - 1), getString(R.string.share_title));
		chooser.putExtra(Intent.EXTRA_INITIAL_INTENTS, targets.toArray(new Parcelable[targets.size()]));
		startActivity(chooser);
	}

	private Uri writeCacheFile(final String cacheFile, final String content) throws IOException {
		Uri uri;
		final File file = new File(getCacheDir(), cacheFile);
		FileOutputStream out = new FileOutputStream(file);
		out.write(content.getBytes());
		out.close();
		uri = FileProvider.getUriForFile(this, getPackageName() + ".fileprovider", file);
		return uri;
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent dataIntent) {
		if (requestCode != REQ_CODE_CREATE_DOC || resultCode != Activity.RESULT_OK || dataIntent == null) return;
		FileOutputStream fileOutputStream = null;
		ParcelFileDescriptor pfd = null;
		try {
			final String s = saveToString();
			pfd = getContentResolver().openFileDescriptor(dataIntent.getData(), "w");
			fileOutputStream = new FileOutputStream(pfd.getFileDescriptor());
			fileOutputStream.write(s.getBytes());
		} catch (IOException e) {
			messageBox(getString(R.string.Error), getString(R.string.save_failed_prefix) + e.getMessage(), false);
		} finally {
			Utils.closeQuietly(fileOutputStream);
			Utils.closeQuietly(pfd);
		}
	}

	private void abort(final String why, final boolean returnToChooser)
	{
		workerRunning = false;
		runOnUiThread(new Runnable() {
			@Override
			public void run() {
				stopNative();
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
		});

	}

	private String generateGame(final List<String> args) throws IllegalArgumentException, IOException {
		String game;
		startGameGenProcess(args);
		OutputStream stdin = gameGenProcess.getOutputStream();
		stdin.close();
		game = Utils.readAllOf(gameGenProcess.getInputStream());
		if (game.length() == 0) game = null;
		int exitStatus = Utils.waitForProcess(gameGenProcess);
		if (exitStatus != 0) {
			String error = game;
			if (error != null && error.length() > 0) {  // probably bogus params
				throw new IllegalArgumentException(error);
			} else if (workerRunning) {
				error = "Game generation exited with status "+exitStatus;
				Log.e(TAG, error);
				throw new IOException(error);
			}
			// else cancelled
		}
		if( !workerRunning) return null;  // cancelled
		return game;
	}

	@SuppressLint("CommitPrefEdits")
	private void startGameGenProcess(final List<String> args) throws IOException {
		final ApplicationInfo applicationInfo = getApplicationInfo();
		final File dataDir = new File(applicationInfo.dataDir);
		final File libDir;
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.GINGERBREAD) {
			libDir = new File(applicationInfo.nativeLibraryDir);
		} else {
			libDir = new File(dataDir, "lib");
		}
		final boolean canRunPIE = Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN;
		final String suffix = canRunPIE ? "-with-pie" : "-no-pie";
		final String baseName = "libpuzzlesgen" + suffix + ".so";
		File installablePath = new File(libDir, baseName);
		final File SYS_LIB = new File("/system/lib");
		final File altPath = new File(SYS_LIB, baseName);
		if (!installablePath.exists() && altPath.exists()) installablePath = altPath;
		File executablePath = new File(dataDir, "puzzlesgen" + suffix);
		if (!executablePath.exists() || (prefs.getInt(PUZZLESGEN_LAST_UPDATE, 0) < BuildConfig.VERSION_CODE)) {
			copyFile(installablePath, executablePath);
			prefsSaver.save(prefs.edit().putInt(PUZZLESGEN_LAST_UPDATE, BuildConfig.VERSION_CODE));
		}
		Utils.setExecutable(executablePath);
		final String[] cmdLine = new String[args.size() + 1];
		cmdLine[0] = executablePath.getAbsolutePath();
		int i = 1;
		for (String arg : args) cmdLine[i++] = arg;
		Log.d(TAG, "exec: " + Arrays.toString(cmdLine));
		File libPuzDir = libDir;
		final String SO = "libpuzzles.so";
		if (! new File(libPuzDir, SO).exists() && new File(SYS_LIB, SO).exists()) libPuzDir = SYS_LIB;
		gameGenProcess = Runtime.getRuntime().exec(cmdLine,
				new String[]{"LD_LIBRARY_PATH="+libPuzDir}, libPuzDir);
	}

	private void copyFile(File src, File dst) throws IOException {
		InputStream in = new FileInputStream(src);
		OutputStream out = new FileOutputStream(dst);
		byte[] buf = new byte[8192];
		int len;
		while ((len = in.read(buf)) > 0) {
			out.write(buf, 0, len);
		}
		in.close();
		out.close();
	}

	private void startNewGame()
	{
		startGame(GameLaunch.toGenerate(currentBackend, orientGameType(getCurrentParams())));
	}

	private void startGame(final GameLaunch launch)
	{
		Log.d(TAG, "startGame: " + launch);
		if (progress != null) {
			throw new RuntimeException("startGame while already starting!");
		}
		showProgress(launch.needsGenerating() ? R.string.starting : R.string.resuming, launch.isFromChooser());
		stopNative();
		startGameThread(launch);
	}

	private void startGameThread(final GameLaunch launch) {
		workerRunning = true;
		(worker = new Thread(launch.needsGenerating() ? "generateAndLoadGame" : "loadGame") { public void run() {
			try {
				Uri uri = launch.getUri();
				if (uri != null) {
					if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
						checkSize(uri);
					}  // else just wish really hard that it isn't too big :-p
					launch.finishedGenerating(Utils.readAllOf(getContentResolver().openInputStream(uri)));
				}
				String backend = launch.getWhichBackend();
				if (backend == null) {
					try {
						backend = games[identifyBackend(launch.getSaved())];
					} catch (IllegalArgumentException e) {
						abort(e.getMessage(), launch.isFromChooser());  // invalid file
						return;
					}
				}
				startingBackend = backend;
				final boolean generating = launch.needsGenerating();
				if (generating) {
					String whichBackend = launch.getWhichBackend();
					String params = launch.getParams();
					final List<String> args = new ArrayList<>();
					args.add(whichBackend);
					if (launch.getSeed() != null) {
						args.add("--seed");
						args.add(launch.getSeed());
					} else {
						if (params == null) {
							params = getLastParams(whichBackend);
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
					final String finalParams = params;
					runOnUiThread(new Runnable() {
						@Override
						public void run() {
							requestKeys(startingBackend, finalParams);
						}
					});
					String generated = generateGame(args);
					if (generated != null) {
						launch.finishedGenerating(generated);
					} else if (workerRunning) {
						throw new IOException("Internal error generating game: result is blank");
					}
					startGameConfirmed(true, launch);
				} else if (launch.isOfNonLocalState() && launch.getSaved() != null) {
					warnOfStateLoss(launch.getSaved(), new Runnable() {
						@Override
						public void run() {
							startGameConfirmed(false, launch);
						}
					}, launch.isFromChooser());
				} else {
					startGameConfirmed(false, launch);
				}
			} catch (IllegalArgumentException e) {
				abort(e.getMessage(), launch.isFromChooser());  // probably bogus params
			} catch (IOException e) {
				e.printStackTrace();
				abort(e.getMessage(), launch.isFromChooser());  // internal error :-(
			}
		}}).start();
	}

	private void startGameConfirmed(final boolean generating, final GameLaunch launch) {
		final String toPlay = launch.getSaved();
		final String gameID = launch.getGameID();
		if (toPlay == null && gameID == null) {
			Log.d(TAG, "startGameThread: null game, presumably cancelled");
			return;
		}
		final boolean changingGame;
		if (currentBackend == null) {
			if (launch.isFromChooser()) {
				final String savedBackend = state.getString(SAVED_BACKEND, null);
				changingGame = savedBackend == null || !savedBackend.equals(startingBackend);
			} else {
				changingGame = true;  // launching app
			}
		} else {
			changingGame = ! currentBackend.equals(startingBackend);
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

		if (! workerRunning) return;
		runOnUiThread(new Runnable() {
			@Override
			public void run() {
				currentBackend = startingBackend;
				gameView.refreshColours(currentBackend);
				gameView.resetZoomForClear();
				gameView.clear();
				applyUndoRedoKbd();
				gameView.keysHandled = 0;
				everCompleted = false;

				final String currentParams = orientGameType(getCurrentParams());
				refreshPresets(currentParams);
				gameView.setDragModeFor(currentBackend);
				final String title = getString(getResources().getIdentifier("name_" + currentBackend, "string", getPackageName()));
				setTitle(title);
				if (getSupportActionBar() != null) {
					getSupportActionBar().setTitle(title);
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
				if (launch.isKnownCompleted()) {
					completed();
				}
				final boolean hasArrows = computeArrowMode(currentBackend).hasArrows();
				setCursorVisibility(hasArrows);
				if (changingGame) {
					if (prefs.getBoolean(CONTROLS_REMINDERS_KEY, true)) {
						if (hasArrows || !showToastIfExists("toast_no_arrows_" + currentBackend)) {
							showToastIfExists("toast_" + currentBackend);
						}
					}
				}
				dismissProgress();
				gameView.rebuildBitmap();
				if (menu != null) onPrepareOptionsMenu(menu);
				save();
			}
		});
	}

	private boolean showToastIfExists(final String name) {
		final int reminderId = getResources().getIdentifier(name, "string", getPackageName());
		if (reminderId <= 0) {
			return false;
		}
		Toast.makeText(GamePlay.this, reminderId, Toast.LENGTH_LONG).show();
		return true;
	}

	private void refreshPresets(String currentParams) {
		if (typeMenu != null) {
			while (typeMenu.size() > 1)
				typeMenu.removeItem(typeMenu.getItem(0).getItemId());
		}
		gameTypes.clear();
		currentType = -1;
		final String[] presets = getPresets();
		for (int i = 0; i < presets.length/2; i++) {
			final String encoded = presets[2 * i];
			final String name = presets[(2 * i) + 1];
			gameTypes.put(encoded, name);
			if (currentParams.equals(orientGameType(encoded))) {
				currentType = i;
				// TODO if it's only equal modulo orientation; should we put a star by it or something?
			}
		}
	}

	@TargetApi(Build.VERSION_CODES.JELLY_BEAN)
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

	private String getLastParams(final String whichBackend) {
		return orientGameType(state.getString(LAST_PARAMS_PREFIX + whichBackend, null));
	}

	private void stopNative()
	{
		workerRunning = false;
		if (gameGenProcess != null) {
			gameGenProcess.destroy();
			gameGenProcess = null;
		}
		if (worker != null) {
			while(true) { try {
				worker.join();  // we may ANR if native code is spinning - safer than leaving a runaway native thread
				break;
			} catch (InterruptedException ignored) {} }
		}
	}

	@Override
	protected void onPause()
	{
		handler.removeMessages(MsgType.TIMER.ordinal());
		nightModeHelper.onPause();
		save();
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
		}
		else {
			nightModeHelper.onResume();
		}
		if (appStartIntentOnResume != null) {
			onNewIntent(appStartIntentOnResume);
			appStartIntentOnResume = null;
		}
	}

	@Override
	protected void onDestroy()
	{
		stopNative();
		super.onDestroy();
	}

	@Override
	public void onWindowFocusChanged( boolean f )
	{
		if( f && gameWantsTimer && currentBackend != null
				&& ! handler.hasMessages(MsgType.TIMER.ordinal()) )
			handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()),
					TIMER_INTERVAL);
	}

	@SuppressLint("CommitPrefEdits")
	public void setSwapLR(boolean swap) {
		swapLR = swap;
		prefsSaver.save(prefs.edit().putBoolean(SWAP_L_R_PREFIX + currentBackend, swap));
	}

	void sendKey(PointF p, int k)
	{
		sendKey(Math.round(p.x), Math.round(p.y), k);
	}

	void sendKey(int x, int y, int k)
	{
		if (progress != null || currentBackend == null) return;
		if (k == '\f') {
			// menu button hack
			openOptionsMenu();
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
		gameView.requestFocus();
		if (startedFullscreen) {
			lightsOut(true);
		}
		lastKeySent = System.nanoTime();
	}

	private boolean prevLandscape = false;
	private void setKeyboardVisibility(final String whichBackend, final Configuration c)
	{
		boolean landscape = (c.orientation == Configuration.ORIENTATION_LANDSCAPE);
		if (landscape != prevLandscape || keyboard == null) {
			// Must recreate KeyboardView on orientation change because it
			// caches the x,y for its preview popups
			// http://code.google.com/p/android/issues/detail?id=4559
			if (keyboard != null) mainLayout.removeView(keyboard);
			final boolean showBorders = prefs.getBoolean(KEYBOARD_BORDERS_KEY, false);
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
		final String maybeSwapLRKey = (lastArrowMode == SmallKeyboard.ArrowMode.ARROWS_LEFT_RIGHT_CLICK)
				? String.valueOf(SmallKeyboard.SWAP_L_R_KEY) : "";
		keyboard.setKeys((c.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO)
				? maybeSwapLRKey + maybeUndoRedo
				: filterKeys(arrowMode) + maybeSwapLRKey + maybeUndoRedo,
				arrowMode, whichBackend);
		swapLR = prefs.getBoolean(SWAP_L_R_PREFIX + whichBackend, false);
		keyboard.setSwapLR(swapLR);
		prevLandscape = landscape;
		mainLayout.requestLayout();
	}

	static String getArrowKeysPrefName(final String whichBackend, final Configuration c) {
		return whichBackend + GamePlay.ARROW_KEYS_KEY_SUFFIX
				+ (hasDpadOrTrackball(c) ? "WithDpad" : "");
	}

	static boolean getArrowKeysDefault(final String whichBackend, final Resources resources, final String packageName) {
		if (hasDpadOrTrackball(resources.getConfiguration())) return false;
		final int defaultId = resources.getIdentifier(
				whichBackend + "_arrows_default", "bool", packageName);
		return defaultId > 0 && resources.getBoolean(defaultId);
	}

	private SmallKeyboard.ArrowMode computeArrowMode(final String whichBackend) {
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
				((startingBackend.equals("bridges") && !prefs.getBoolean(BRIDGES_SHOW_H_KEY, false))
				|| (startingBackend.equals("unequal") && !prefs.getBoolean(UNEQUAL_SHOW_H_KEY, false)))) {
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
	public void onConfigurationChanged(Configuration newConfig)
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
		int state = MenuItemCompat.SHOW_AS_ACTION_ALWAYS;
		if (screenWidthDIP >= 480) {
			state |= MenuItemCompat.SHOW_AS_ACTION_WITH_TEXT;
		}
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.game_menu), state);
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.type_menu), state);
		MenuItemCompat.setShowAsAction(menu.findItem(R.id.help_menu), state);
		final boolean undoRedoKbd = prefs.getBoolean(UNDO_REDO_KBD_KEY, UNDO_REDO_KBD_DEFAULT);
		final MenuItem undoItem = menu.findItem(R.id.undo);
		undoItem.setVisible(!undoRedoKbd);
		final MenuItem redoItem = menu.findItem(R.id.redo);
		redoItem.setVisible(!undoRedoKbd);
		if (!undoRedoKbd) {
			MenuItemCompat.setShowAsAction(undoItem, state);
			MenuItemCompat.setShowAsAction(redoItem, state);
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
				.setOnCancelListener(returnToChooser ? new DialogInterface.OnCancelListener() {
					@Override
					public void onCancel(DialogInterface dialog) {
						startChooserAndFinish();
					}
				} : null)
				.show();
	}

	@UsedByJNI
	void showToast(final String msg, final boolean fromPattern) {
		if (fromPattern && ! prefs.getBoolean(PATTERN_SHOW_LENGTHS_KEY, false)) return;
		runOnUiThread(new Runnable() {
			public void run() {
				Toast.makeText(GamePlay.this, msg, Toast.LENGTH_SHORT).show();
			}
		});
	}

	public void zoomedIn() {
		// GameView was at 1x zoom and is now zoomed in
		if (prefs.getBoolean(CONTROLS_REMINDERS_KEY, true)) {
			Toast.makeText(this, R.string.how_to_scroll, Toast.LENGTH_SHORT).show();
		}
	}

	@UsedByJNI
	void completed() {
		handler.postDelayed(new Runnable() {
			@Override
			public void run() {
				try {
					completedInternal();
				} catch (WindowManager.BadTokenException activityWentAway) {
					// fine, nothing we can do here
				}
			}
		}, 0);
	}

	@UsedByJNI
	void inertiaFollow(final boolean isSolved) {
		keyboard.setInertiaFollowEnabled(isSolved || !"inertia".equals(currentBackend));
	}

	private void completedInternal() {
		everCompleted = true;
		final boolean copyStatusBar = "mines".equals(currentBackend) || "flood".equals(currentBackend);
		final CharSequence titleText = copyStatusBar ? statusBar.getText() : getString(R.string.COMPLETED);
		if (! prefs.getBoolean(COMPLETED_PROMPT_KEY, true)) {
			Toast.makeText(GamePlay.this, titleText, Toast.LENGTH_SHORT).show();
			return;
		}
		final Dialog d = new Dialog(this, R.style.Dialog_Completed);
		WindowManager.LayoutParams lp = d.getWindow().getAttributes();
		lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
		d.getWindow().setAttributes(lp);
		d.setContentView(R.layout.completed);
		final TextView title = (TextView) d.findViewById(R.id.completedTitle);
		title.setText(titleText);
		d.setCanceledOnTouchOutside(true);
		final Button newButton = (Button) d.findViewById(R.id.newgame);
		darkenTopDrawable(newButton);
		newButton.setOnClickListener(new OnClickListener() {
			@Override
			public void onClick(View v) {
				d.dismiss();
				startNewGame();
			}
		});
		final Button typeButton = (Button) d.findViewById(R.id.type_menu);
		darkenTopDrawable(typeButton);
		typeButton.setOnClickListener(new OnClickListener() {
			@Override
			public void onClick(View v) {
				d.dismiss();
				if (hackForSubmenus == null) openOptionsMenu();
				hackForSubmenus.performIdentifierAction(R.id.type_menu, 0);
			}
		});
		final String style = prefs.getString(GameChooser.CHOOSER_STYLE_KEY, "list");
		final boolean useGrid = style.equals("grid");
		final Button chooserButton = (Button) d.findViewById(R.id.other);
		chooserButton.setCompoundDrawablesWithIntrinsicBounds(0, useGrid
				? R.drawable.ic_action_view_as_grid
				: R.drawable.ic_action_view_as_list, 0, 0);
		darkenTopDrawable(chooserButton);
		chooserButton.setOnClickListener(new OnClickListener() {
			@Override
			public void onClick(View v) {
				d.dismiss();
				startChooserAndFinish();
			}
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
		runOnUiThread(new Runnable() {
			public void run() {
				statusBar.setText(status.length() == 0 ? " " : status);
			}
		});
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
				.setOnCancelListener(new OnCancelListener() {
					public void onCancel(DialogInterface dialog) {
						configCancel();
					}
				})
				.setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface d, int whichButton) {
						// do nothing - but must have listener to ensure button is shown
						// (listener is overridden in dialogShow to prevent dismiss)
					}
				});
		ScrollView sv = new ScrollView(dialogBuilder.getContext());
		dialogBuilder.setView(sv);
		if (whichEvent == CFG_SETTINGS) {
			dialogBuilder.setNegativeButton(R.string.Game_ID_, new DialogInterface.OnClickListener() {
				@Override
				public void onClick(DialogInterface dialog, int which) {
					configCancel();
					configEvent(CFG_DESC);
				}
			})
			.setNeutralButton(R.string.Seed_, new DialogInterface.OnClickListener() {
				@Override
				public void onClick(DialogInterface dialog, int which) {
					configCancel();
					configEvent(CFG_SEED);
				}
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
	void dialogAdd(int whichEvent, int type, String name, String value, int selection)
	{
		final Context context = dialogBuilder.getContext();
		switch(type) {
		case C_STRING: {
			dialogIds.add(name);
			AppCompatEditText et = new AppCompatEditText(context);
			// TODO: C_INT, C_UINT, C_UDOUBLE, C_DOUBLE
			// Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
			// Uglier temporary-er hack: Black Box must accept a range for ball count.
			if (whichEvent == CFG_SETTINGS && !currentBackend.equals("blackbox")) {
				et.setInputType(InputType.TYPE_CLASS_NUMBER
								| InputType.TYPE_NUMBER_FLAG_DECIMAL
								| InputType.TYPE_NUMBER_FLAG_SIGNED);
			}
			et.setTag(name);
			et.setText(value);
			et.setWidth(getResources().getDimensionPixelSize((whichEvent == CFG_SETTINGS)
					? R.dimen.dialog_edit_text_width : R.dimen.dialog_long_edit_text_width));
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
			break; }
		case C_BOOLEAN: {
			dialogIds.add(name);
			AppCompatCheckBox c = new AppCompatCheckBox(context);
			c.setTag(name);
			c.setText(name);
			c.setChecked(selection != 0);
			dialogLayout.addView(c);
			break; }
		case C_CHOICES: {
			StringTokenizer st = new StringTokenizer(value.substring(1),value.substring(0,1));
			ArrayList<String> choices = new ArrayList<>();
			while(st.hasMoreTokens()) choices.add(st.nextToken());
			dialogIds.add(name);
			AppCompatSpinner s = new AppCompatSpinner(context);
			s.setTag(name);
			ArrayAdapter<String> a = new ArrayAdapter<>(context,
					android.R.layout.simple_spinner_item, choices.toArray(new String[choices.size()]));
			a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
			s.setAdapter(a);
			s.setSelection(selection);
			s.setLayoutParams(new TableRow.LayoutParams(
					getResources().getDimensionPixelSize(R.dimen.dialog_spinner_width),
					TableRow.LayoutParams.WRAP_CONTENT));
			TextView tv = new TextView(context);
			tv.setText(name);
			tv.setPadding(0, 0, getResources().getDimensionPixelSize(R.dimen.dialog_padding_horizontal), 0);
			tv.setGravity(Gravity.END);
			TableRow tr = new TableRow(context);
			tr.addView(tv);
			tr.addView(s);
			tr.setGravity(Gravity.CENTER_VERTICAL);
			dialogLayout.addView(tr);
			break; }
		}
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
		dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View button) {
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
			}
		});
	}

	@UsedByJNI
	void serialiseWrite(byte[] buffer)
	{
		savingState.append(new String(buffer));
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
		} else if (key.equals(FULLSCREEN_KEY)) {
			applyFullscreen(true);  // = already started
		} else if (key.equals(STAY_AWAKE_KEY)) {
			applyStayAwake();
		} else if (key.equals(LIMIT_DPI_KEY)) {
			applyLimitDPI(true);
		} else if (key.equals(ORIENTATION_KEY)) {
			applyOrientation();
		} else if (key.equals(UNDO_REDO_KBD_KEY)) {
			applyUndoRedoKbd();
		} else if (key.equals(KEYBOARD_BORDERS_KEY)) {
			applyKeyboardBorders();
		} else if (key.equals(BRIDGES_SHOW_H_KEY) || key.equals(UNEQUAL_SHOW_H_KEY)) {
			applyShowH();
		}
	}

	private void applyKeyboardBorders() {
		if (keyboard != null) {
			mainLayout.removeView(keyboard);
		}
		keyboard = null;
		setKeyboardVisibility(startingBackend, getResources().getConfiguration());
	}

	private void applyLimitDPI(final boolean alreadyStarted) {
		final String pref = prefs.getString(LIMIT_DPI_KEY, "auto");
		gameView.limitDpi = "auto".equals(pref) ? GameView.LimitDPIMode.LIMIT_AUTO :
				"off".equals(pref) ? GameView.LimitDPIMode.LIMIT_OFF :
						GameView.LimitDPIMode.LIMIT_ON;
		if (alreadyStarted) {
			gameView.rebuildBitmap();
		}
	}

	private void applyFullscreen(boolean alreadyStarted) {
		cachedFullscreen = prefs.getBoolean(FULLSCREEN_KEY, false);
		final boolean hasLightsOut = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB);
		if (cachedFullscreen) {
			if (hasLightsOut) {
				runOnUiThread(new Runnable() {
					public void run() {
						lightsOut(true);
					}
				});
			} else if (alreadyStarted) {
				// This is the only way to change the theme
				if (! startedFullscreen) restartOnResume = true;
			} else {
				setTheme(R.style.SolidActionBar_Gameplay_FullScreen);
			}
		} else {
			if (hasLightsOut) {
				final boolean fAlreadyStarted = alreadyStarted;
				runOnUiThread(new Runnable() {
					public void run() {
						lightsOut(false);
						// This shouldn't be necessary but is on Galaxy Tab 10.1
						if (fAlreadyStarted && startedFullscreen) restartOnResume = true;
					}
				});
			} else if (alreadyStarted && startedFullscreen) {
				// This is the only way to change the theme
				restartOnResume = true;
			}  // else leave it as default non-fullscreen
		}
	}

	private void lightsOut(final boolean fullScreen) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.ICE_CREAM_SANDWICH) {
			if (fullScreen) {
				getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
			} else {
				getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
			}
		} else if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
			SysUIVisSetter.set(gameView, fullScreen);
		}
	}

	private void applyStayAwake()
	{
		if (prefs.getBoolean(STAY_AWAKE_KEY, false)) {
			getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		} else {
			getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		}
	}

	@SuppressLint("InlinedApi")
	private void applyOrientation() {
		final String orientationPref = prefs.getString(ORIENTATION_KEY, "unspecified");
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

		private void applyUndoRedoKbd() {
		boolean undoRedoKbd = prefs.getBoolean(UNDO_REDO_KBD_KEY, UNDO_REDO_KBD_DEFAULT);
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
			String ret = "";
			for (String choice : choices) ret += ":"+gettext(choice);
			return ret;
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
		runOnUiThread(new Runnable() {
			@Override
			public void run() {
				undoEnabled = canUndo;
				redoEnabled = canRedo;
				if (keyboard != null) {
					keyboard.setUndoRedoEnabled(canUndo, canRedo);
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
		});
	}

	native void startPlaying(GameView _gameView, String savedGame);
	native void startPlayingGameID(GameView _gameView, String whichBackend, String gameID);
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
	native void serialise();
	native static int identifyBackend(String savedGame);
	native String getCurrentParams();
	native void requestKeys(String backend, String params);
	native void setCursorVisibility(boolean visible);
	native String[] getPresets();
	native int getUIVisibility();

	static {
		System.loadLibrary("puzzles");
	}
}
