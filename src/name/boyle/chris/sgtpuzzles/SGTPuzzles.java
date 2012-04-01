package name.boyle.chris.sgtpuzzles;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.text.MessageFormat;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.StringTokenizer;

import name.boyle.chris.sgtpuzzles.compat.ActionBarCompat;
import name.boyle.chris.sgtpuzzles.compat.PrefsSaver;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnCancelListener;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.SharedPreferences.OnSharedPreferenceChangeListener;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Path;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.preference.PreferenceManager;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.view.WindowManager;
import android.view.View.OnClickListener;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;
import android.widget.Toast;

public class SGTPuzzles extends Activity implements OnSharedPreferenceChangeListener
{
	static final String TAG = "SGTPuzzles";
	static final String STATE_PREFS_NAME = "state";
	static final String ARROW_KEYS_KEY = "arrowKeys";
	static final String INERTIA_FORCE_ARROWS_KEY = "inertiaForceArrows";
	static final String FULLSCREEN_KEY = "fullscreen";
	static final String STAY_AWAKE_KEY = "stayAwake";
	static final String PATTERN_SHOW_LENGTHS_KEY = "patternShowLengths";
	static final String COMPLETED_PROMPT_KEY = "completedPrompt";

	ProgressDialog progress;
	TextView statusBar;
	SmallKeyboard keyboard;
	RelativeLayout mainLayout;
	GameView gameView;
	SubMenu typeMenu;
	LinkedHashMap<Integer,String> gameTypes;
	int currentType = 0;
	boolean gameRunning = false;
	boolean solveEnabled = false, customVisible = false, fakeCrash = false,
			undoEnabled = false, redoEnabled = false;
	SharedPreferences prefs, state;
	static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2,
		LEFT_BUTTON = 0x0200, MIDDLE_BUTTON = 0x201, RIGHT_BUTTON = 0x202,
		LEFT_DRAG = 0x203, //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
		LEFT_RELEASE = 0x206, MOD_CTRL = 0x1000,
		MOD_SHFT = 0x2000, MOD_NUM_KEYPAD = 0x4000, ALIGN_VCENTRE = 0x100,
		ALIGN_HCENTRE = 0x001, ALIGN_HRIGHT = 0x002, TEXT_MONO = 0x10,
		C_STRING = 0, C_CHOICES = 1, C_BOOLEAN = 2;
	static final long MAX_SAVE_SIZE = 1000000; // 1MB; we only have 16MB of heap
	boolean gameWantsTimer = false, resizeOnDone = false;
	int timerInterval = 20;
	int xarg1, xarg2, xarg3;
	Path path;
	StringBuffer savingState;
	AlertDialog dialog;
	ArrayList<Integer> dialogIds;
	TableLayout dialogLayout;
	String helpTopic;
	Thread worker;
	String lastKeys = "";
	static final File storageDir = Environment.getExternalStorageDirectory();
	// Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
	boolean configIsCustom = false;
	String[] games;
	Menu menu;
	ActionBarCompat actionBarCompat = null;
	String maybeUndoRedo = "ur";
	String maybeMenu = "";
	PrefsSaver prefsSaver;

	enum MsgType { INIT, TIMER, DONE, ABORT };
	Handler handler = new Handler() {
		public void handleMessage( Message msg ) {
			switch( MsgType.values()[msg.what] ) {
			case TIMER:
				if( progress == null ) timerTick();
				if( gameWantsTimer ) sendMessageDelayed(obtainMessage(MsgType.TIMER.ordinal()), timerInterval);
				break;
			case DONE:
				if( resizeOnDone ) {
					resizeEvent(gameView.w, gameView.h);
					resizeOnDone = false;
				}
				// set ActionBar icon to the one for this puzzle
				if( msg.obj != null && actionBarCompat != null ) {
					int iconId = getResources().getIdentifier(
							(String)msg.obj, "drawable", getPackageName());
					actionBarCompat.setIcon(iconId > 0 ? iconId : R.drawable.icon);
				}
				dismissProgress();
				if( menu != null ) onPrepareOptionsMenu(menu);
				save();
				break;
			case ABORT:
				stopNative();
				dismissProgress();
				startChooser();
				if (msg.obj != null) {
					messageBox(getString(R.string.Error), (String)msg.obj, 1, false);
				} else {
					finish();
				}
				break;
			}
		}
	};

	void showProgress( int msgId )
	{
		progress = new ProgressDialog(this);
		progress.setMessage( getString(msgId) );
		progress.setIndeterminate( true );
		progress.setCancelable( true );
		progress.setOnCancelListener( abortListener );
		final int msgId2 = msgId;
		progress.setButton( DialogInterface.BUTTON_POSITIVE, getString(R.string.background), new DialogInterface.OnClickListener() {
			public void onClick( DialogInterface d, int which ) {
				// Cheat slightly: just launch home screen
				startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME));
				// Argh, can't prevent a dismiss at this point, so re-show it
				showProgress(msgId2);
				Toast.makeText(SGTPuzzles.this, R.string.bg_unreliable_warn, Toast.LENGTH_LONG).show();
			}
		});
		progress.setButton( DialogInterface.BUTTON_NEGATIVE, getString(android.R.string.cancel), handler.obtainMessage(MsgType.ABORT.ordinal()));
		progress.show();
	}

	void dismissProgress()
	{
		if( progress == null ) return;
		try {
			progress.dismiss();
		} catch (IllegalArgumentException e) {}  // race condition?
		progress = null;
	}

	String saveToString()
	{
		if( ! gameRunning || progress != null ) return null;
		savingState = new StringBuffer();
		serialise();  // serialiseWrite() callbacks will happen in here
		String s = savingState.toString();
		savingState = null;
		return s;
	}

	void save()
	{
		String s = saveToString();
		if (s == null || s.length() == 0) return;
		SharedPreferences.Editor ed = state.edit();
		ed.remove("engineName");
		ed.putString("savedGame", s);
		prefsSaver.save(ed);
	}

	void gameViewResized()
	{
		if( ! gameRunning ) return;
		if( progress == null && gameView.w > 10 && gameView.h > 10 )
			resizeEvent(gameView.w, gameView.h);
		else
			resizeOnDone = true;
	}

	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler(){
			/** Somewhat controversially, make life much worse: crash in native code.
			 *  This is so that "debuggerd" will get us a native trace, which we may
			 *  need, because we're quite often in Java-in-native-in-Java (e.g. drawing).
			 *  It also leads to an exit(1) (all threads) which we want. */
			public void uncaughtException(Thread t, Throwable e) {
				e.printStackTrace();
				crashMeHarder();  // see you in nativeCrashed()
			}
		});

		prefs = PreferenceManager.getDefaultSharedPreferences(this);
		prefs.registerOnSharedPreferenceChangeListener(this);
		state = getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE);
		prefsSaver = PrefsSaver.get(this);
		games = getResources().getStringArray(R.array.games);
		gameTypes = new LinkedHashMap<Integer,String>();

		applyFullscreen(false);  // must precede super.onCreate and setContentView
		applyStayAwake();
		super.onCreate(savedInstanceState);
		setContentView(R.layout.main);
		mainLayout = (RelativeLayout)findViewById(R.id.mainLayout);
		statusBar = (TextView)findViewById(R.id.statusBar);
		gameView = (GameView)findViewById(R.id.game);
		keyboard = (SmallKeyboard)findViewById(R.id.keyboard);
		actionBarCompat = ActionBarCompat.get(this);
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);
		gameView.requestFocus();
		onNewIntent(getIntent());
	}

	/** work around http://code.google.com/p/android/issues/detail?id=21181 */
	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if (progress != null) return false;
		if (gameView.onKeyDown(keyCode, event)) return true;
		else return super.onKeyDown(keyCode, event);
	}

	/** work around http://code.google.com/p/android/issues/detail?id=21181 */
	@Override
	public boolean onKeyUp(int keyCode, KeyEvent event) {
		if (progress != null) return false;
		if (gameView.onKeyUp(keyCode, event)) return true;
		else return super.onKeyUp(keyCode, event);
	}

	@Override
	protected void onNewIntent(Intent intent)
	{
		if( progress != null ) {
			// Sorry, cancelling is hard. :-p Require explicit user action.
			return;
		}
		String s = intent.getStringExtra("game");
		Uri u = intent.getData();
		if (s != null && s.length() > 0) {
			startGame(-1, s);
			return;
		} else if (u != null) {
			String g = u.getSchemeSpecificPart();
			if (games.length < 2) games = getResources().getStringArray(R.array.games);
			for (int i=0; i<games.length; i++) {
				if (games[i].equals(g)) {
					startGame(i,null);
					return;
				}
			}
			Log.e(TAG, "Unhandled URL! \""+u+"\" -> g = \""+g+"\", games = "+games);
			// TODO! Other URLs, including game states...
		}
		if( state.contains("savedGame") && state.getString("savedGame","").length() > 0 ) {
			startGame(-1, state.getString("savedGame",""));
		} else {
			startChooser();
			finish();
		}
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		this.menu = menu;
		getMenuInflater().inflate(R.menu.main, menu);
		menu.findItem(R.id.undo).setVisible(actionBarCompat != null);
		menu.findItem(R.id.redo).setVisible(actionBarCompat != null);
		return true;
	}

	Menu hackForSubmenus;
	@Override
	public boolean onPrepareOptionsMenu(Menu menu)
	{
		super.onPrepareOptionsMenu(menu);
		hackForSubmenus = menu;
		if( progress != null && (actionBarCompat == null || actionBarCompat.hasMenuButton()) ) return false;  // not safe/useful until game is loaded
		MenuItem item;
		item = menu.findItem(R.id.solve);
		item.setEnabled(solveEnabled);
		if (actionBarCompat != null) item.setVisible(solveEnabled);
		MenuItem undoItem = menu.findItem(R.id.undo), redoItem = menu.findItem(R.id.redo);
		undoItem.setEnabled(undoEnabled);
		redoItem.setEnabled(redoEnabled);
		undoItem.setIcon(undoEnabled ? R.drawable.sym_keyboard_undo : R.drawable.sym_keyboard_undo_disabled);
		redoItem.setIcon(redoEnabled ? R.drawable.sym_keyboard_redo : R.drawable.sym_keyboard_redo_disabled);
		item = menu.findItem(R.id.type);
		item.setEnabled(! gameTypes.isEmpty() || customVisible);
		if (actionBarCompat != null) item.setVisible(item.isEnabled());
		typeMenu = item.getSubMenu();
		for( Integer i : gameTypes.keySet() ) {
			if( menu.findItem(i) == null ) typeMenu.add(R.id.typeGroup, i, Menu.NONE, gameTypes.get(i) );
		}
		MenuItem customItem = menu.findItem(R.id.custom);
		customItem.setVisible(customVisible);
		typeMenu.setGroupCheckable(R.id.typeGroup, true, true);
		if( currentType < 0 ) customItem.setChecked(true);
		else if( currentType < gameTypes.size() ) menu.findItem((Integer)gameTypes.keySet().toArray()[currentType]).setChecked(true);
		menu.findItem(R.id.thisgame).setTitle(MessageFormat.format(
					getString(R.string.help_on_game),new Object[]{this.getTitle()}));
		return true;
	}

	public void showHelp(String topic)
	{
		final Dialog d = new Dialog(this,android.R.style.Theme);
		final WebView wv = new WebView(this);
		d.setOnKeyListener(new DialogInterface.OnKeyListener(){ public boolean onKey(DialogInterface di, int key, KeyEvent evt) {
			if (evt.getAction() != KeyEvent.ACTION_DOWN || key != KeyEvent.KEYCODE_BACK) return false;
			if (wv.canGoBack()) wv.goBack(); else d.cancel();
			return true;
		}});
		d.setContentView(wv);
		wv.setWebChromeClient(new WebChromeClient(){
			public void onReceivedTitle(WebView w, String title) { d.setTitle(title); }
		});
		wv.getSettings().setBuiltInZoomControls(true);
		wv.loadUrl(MessageFormat.format(getString(R.string.docs_url), new Object[]{topic}));
		d.show();
	}

	void startChooser()
	{
		startActivity(new Intent(this, GameChooser.class));
	}

	void startNewGame()
	{
		if(! gameRunning || progress != null) return;
		showProgress( R.string.starting_new );
		changedState(false, false);
		(worker = new Thread("newGame") { public void run() {
			keyEvent(0, 0, 'n');
			handler.sendEmptyMessage(MsgType.DONE.ordinal());
		}}).start();
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item)
	{
		switch(item.getItemId()) {
		case R.id.other:
			startChooser();
			break;
		case R.id.settings:
			startActivity(new Intent(this, PrefsActivity.class));
			break;
		case R.id.newgame:
			startNewGame();
			break;
		case R.id.restart:  restartEvent(); break;
		case R.id.undo:     sendKey(0, 0, 'u'); break;
		case R.id.redo:     sendKey(0, 0, 'r'); break;
		case R.id.solve:    solveEvent(); break;
		case R.id.custom:   configIsCustom = true; configEvent( CFG_SETTINGS ); break;
		case R.id.specific: configIsCustom = false; configEvent( CFG_DESC ); break;
		case R.id.seed:     configIsCustom = false; configEvent( CFG_SEED ); break;
		case R.id.about:    about(); break;
		case R.id.contents: showHelp("index"); break;
		case R.id.thisgame: showHelp(helpTopic); break;
		case R.id.website:
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(
							getString(R.string.website_url))));
			break;
		case R.id.email:
			tryEmailAuthor(this, false, null);
			break;
		case R.id.load:
			new FilePicker(this, storageDir, false).show();
			break;
		case R.id.save:
			new FilePicker(this, storageDir, true).show();
			break;
		default:
			final int id = item.getItemId();
			if( gameTypes.get(id) != null ) {
				showProgress( R.string.changing_type );
				gameView.clear();
				Log.d(TAG, "preset: "+id+": "+gameTypes.get(id));
				(worker = new Thread("presetGame") { public void run() {
					presetEvent(id);
					handler.sendEmptyMessage(MsgType.DONE.ordinal());
				}}).start();
			}
			else super.onOptionsItemSelected(item);
			break;
		}
		return true;
	}

	private void about()
	{
		String title = String.format(getString(R.string.About_X), getTitle());
		String msg = String.format(getString(R.string.From_Simon_Tatham_s_Portable_Puzzle_Collection_Revision_X),
				getVersion(this));
		messageBox(title, msg, 0, false);
	}

	static String getEmailSubject(Context c, boolean isCrash)
	{
		String modVer = "";
		try {
			Process p = Runtime.getRuntime().exec(new String[]{"getprop","ro.modversion"});
			modVer = readAllOf(p.getInputStream()).trim();
		} catch (Exception e) {}
		if (modVer.length() == 0) modVer = "original";
		return MessageFormat.format(c.getString(
				isCrash ? R.string.crash_subject : R.string.email_subject),
				getVersion(c), Build.MODEL, modVer, Build.FINGERPRINT);
	}

	static boolean tryEmailAuthor(Context c, boolean isCrash, String body)
	{
		Intent i = new Intent(Intent.ACTION_SEND);
		i.putExtra(Intent.EXTRA_EMAIL, new String[]{c.getString(R.string.author_email)});
		i.putExtra(Intent.EXTRA_SUBJECT, getEmailSubject(c, isCrash));
		i.setType("message/rfc822");
		i.putExtra(Intent.EXTRA_TEXT, body!=null ? body : "");
		try {
			c.startActivity(i);
			return true;
		} catch (ActivityNotFoundException e) {
			try {
				// Get the OS to present a nicely formatted, translated error
				c.startActivity(Intent.createChooser(i,null));
			} catch (Exception e2) {
				e2.printStackTrace();
				Toast.makeText(c, e2.toString(), Toast.LENGTH_LONG).show();
			}
			return false;
		}
	}

	void quit(boolean fromDestroy)
	{
		stopNative();
		if( ! fromDestroy ) finish();
	}

	void abort(String why)
	{
		gameRunning = false;
		handler.obtainMessage(MsgType.ABORT.ordinal(), why).sendToTarget();
	}

	OnCancelListener abortListener = new OnCancelListener() {
		public void onCancel(DialogInterface dialog) { abort(null); }
	};

	String getStackTrace( Throwable t )
	{
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		t.printStackTrace(new PrintStream(b,true));
		return b.toString();
	}

	static String getVersion(Context c)
	{
		try {
			return c.getPackageManager().getPackageInfo(c.getPackageName(),0).versionName;
		} catch(Exception e) { return c.getString(R.string.unknown_version); }
	}

	void startGame(final int which, final String savedGame)
	{
		Log.d(TAG, "startGame: "+which+", "+((savedGame==null)?"null":(savedGame.length()+" bytes")));
		if (progress != null) {
			Log.e(TAG, "startGame while already starting!");
			return;
		}
		showProgress( (savedGame == null) ? R.string.starting : R.string.resuming );
		if( gameRunning ) {
			gameView.clear();
			stopNative();
			solveEnabled = false;
			changedState(false, false);
			customVisible = false;
			setStatusBarVisibility(false);
		}
		if (ActionBarCompat.earlyHasActionBar()) {
			maybeUndoRedo = "";
		}
		setKeys("", SmallKeyboard.ARROWS_LEFT_RIGHT_CLICK);
		if( typeMenu != null ) for( Integer i : gameTypes.keySet() ) typeMenu.removeItem(i);
		gameTypes.clear();
		gameRunning = true;
		gameView.keysHandled = 0;
		(worker = new Thread("startGame") { public void run() {
			init(gameView, which, savedGame);
			if( ! gameRunning ) return;  // stopNative or abort was called
			helpTopic = htmlHelpTopic();
			handler.obtainMessage(MsgType.DONE.ordinal(), htmlHelpTopic()).sendToTarget();
		}}).start();
	}

	public void stopNative()
	{
		gameRunning = false;
		cancel();  // set flag in native code
		if (worker != null) {
			while(true) { try {
				worker.join();  // we may ANR if native code is spinning - safer than leaving a runaway native thread
				break;
			} catch (InterruptedException i) {} }
		}
	}

	@Override
	protected void onPause()
	{
		if( gameRunning ) handler.removeMessages(MsgType.TIMER.ordinal());
		save();
		super.onPause();
	}

	boolean restartOnResume = false;

	@Override
	protected void onResume()
	{
		super.onResume();
		if (restartOnResume) {
			startActivity(new Intent(this, RestartActivity.class));
			finish();
		}
	}

	@Override
	protected void onDestroy()
	{
		quit(true);
		super.onDestroy();
	}

	@Override
	public void onWindowFocusChanged( boolean f )
	{
		if( f && gameWantsTimer && gameRunning
				&& ! handler.hasMessages(MsgType.TIMER.ordinal()) )
			handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()),
					timerInterval);
	}

	void sendKey(int x, int y, int k)
	{
		if(! gameRunning || progress != null) return;
		if (k == '\f') {
			// menu button hack
			openOptionsMenu();
			return;
		}
		keyEvent(x, y, k);
	}

	boolean prevLandscape = false;
	void setKeyboardVisibility(Configuration c)
	{
		boolean landscape = (c.orientation == Configuration.ORIENTATION_LANDSCAPE);
		if (landscape != prevLandscape || keyboard == null) {
			// Must recreate KeyboardView on orientation change because it
			// caches the x,y for its preview popups
			// http://code.google.com/p/android/issues/detail?id=4559
			if (keyboard != null) mainLayout.removeView(keyboard);
			keyboard = new SmallKeyboard(this, undoEnabled, redoEnabled);
			keyboard.setId(R.id.keyboard);
			RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
					RelativeLayout.LayoutParams.WRAP_CONTENT,
					RelativeLayout.LayoutParams.WRAP_CONTENT);
			RelativeLayout.LayoutParams glp = new RelativeLayout.LayoutParams(
					RelativeLayout.LayoutParams.WRAP_CONTENT,
					RelativeLayout.LayoutParams.WRAP_CONTENT);
			if (landscape) {
				lp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
				lp.addRule(RelativeLayout.ALIGN_PARENT_TOP);
				lp.addRule(RelativeLayout.ABOVE, R.id.statusBar);
				glp.addRule(RelativeLayout.ABOVE, R.id.statusBar);
				glp.addRule(RelativeLayout.LEFT_OF, R.id.keyboard);
			} else {
				lp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
				lp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
				lp.addRule(RelativeLayout.ABOVE, R.id.statusBar);
				glp.addRule(RelativeLayout.ABOVE, R.id.keyboard);
			}
			mainLayout.updateViewLayout(gameView, glp);
			mainLayout.addView(keyboard, lp);
		}
		keyboard.setKeys( (c.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO)
				? (maybeUndoRedo+maybeMenu) : lastKeys, lastArrowMode);
		prevLandscape = landscape;
		mainLayout.requestLayout();
	}

	void setStatusBarVisibility(boolean visible)
	{
		if (!visible) statusBar.setText("");
		RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(
				RelativeLayout.LayoutParams.FILL_PARENT,
				visible ? RelativeLayout.LayoutParams.WRAP_CONTENT : 0);
		lp.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
		lp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
		mainLayout.updateViewLayout(statusBar, lp);
	}

	@Override
	public void onConfigurationChanged(Configuration newConfig)
	{
		setKeyboardVisibility(newConfig);
		super.onConfigurationChanged(newConfig);
		if (actionBarCompat != null) {
			// ActionBar's capacity (width) has probably changed, so work around
			// http://code.google.com/p/android/issues/detail?id=20493
			// (invalidateOptionsMenu() does not help here)
			if (actionBarCompat != null) {
				// Just cautiously fix the common case: if >850dip then force
				// show everything, else let the platform decide
				DisplayMetrics dm = getResources().getDisplayMetrics();
				int screenWidthDIP = (int)Math.round(((double)dm.widthPixels) / dm.density);
				boolean reallyWide = screenWidthDIP > 850;
				int state =  reallyWide ? ActionBarCompat.SHOW_AS_ACTION_ALWAYS
						: ActionBarCompat.SHOW_AS_ACTION_IF_ROOM;
				actionBarCompat.menuItemSetShowAsAction(menu.findItem(R.id.settings), state);
				actionBarCompat.menuItemSetShowAsAction(menu.findItem(R.id.solve), state);
				actionBarCompat.menuItemSetShowAsAction(menu.findItem(R.id.help), state);
				state |= ActionBarCompat.SHOW_AS_ACTION_WITH_TEXT;
				actionBarCompat.menuItemSetShowAsAction(menu.findItem(R.id.game), state);
				actionBarCompat.menuItemSetShowAsAction(menu.findItem(R.id.type), state);
				actionBarCompat.menuItemSetShowAsAction(menu.findItem(R.id.other), state);
			}
		}
	}

	// Callbacks from native code:

	void gameStarted(final String title, final boolean hasCustom, final boolean hasStatus, final boolean canSolve, float[] colours)
	{
		gameView.colours = new int[colours.length/3];
		for (int i=0; i<colours.length/3; i++)
			gameView.colours[i] = Color.rgb((int)(colours[i*3]*255),(int)(colours[i*3+1]*255),(int)(colours[i*3+2]*255));
		customVisible = hasCustom;
		solveEnabled = canSolve;
		runOnUiThread(new Runnable(){public void run(){
			if (gameView.colours.length > 0) gameView.setBackgroundColor(gameView.colours[0]);
			setTitle(title);
			setStatusBarVisibility(hasStatus);
		}});
	}

	void addTypeItem(int id, String label) { gameTypes.put(id, label); }

	void messageBox(final String title, final String msg, final int flag, boolean fromPattern)
	{
		if (fromPattern &&
				! prefs.getBoolean(PATTERN_SHOW_LENGTHS_KEY, false)) {
			return;
		}
		runOnUiThread(new Runnable(){public void run(){
			dismissProgress();
			if( title == null )
				Toast.makeText(SGTPuzzles.this, msg, Toast.LENGTH_SHORT).show();
			else new AlertDialog.Builder(SGTPuzzles.this)
					.setTitle(title)
					.setMessage(msg)
					.setIcon( (flag == 0)
							? android.R.drawable.ic_dialog_info
							: android.R.drawable.ic_dialog_alert )
					.setOnCancelListener((flag == 0) ? null : new OnCancelListener() {
						public void onCancel(DialogInterface dialog) {
							finish();
						}})
					.show();
		}});
		// I don't think we need to wait before returning here (and we can't)
	}

	void completed()
	{
		if (! prefs.getBoolean(COMPLETED_PROMPT_KEY, true)) {
			Toast.makeText(SGTPuzzles.this, getString(R.string.COMPLETED), Toast.LENGTH_SHORT).show();
			return;
		}
		final Dialog d = new Dialog(this, android.R.style.Theme_Panel);
		WindowManager.LayoutParams lp = d.getWindow().getAttributes();
		lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
		d.getWindow().setAttributes(lp);
		d.setContentView(R.layout.completed);
		d.setCanceledOnTouchOutside(true);
		d.findViewById(R.id.newgame).setOnClickListener(new OnClickListener() {
			@Override public void onClick(View v) {
				d.dismiss();
				startNewGame();
			}
		});
		d.findViewById(R.id.type).setOnClickListener(new OnClickListener() {
			@Override public void onClick(View v) {
				d.dismiss();
				if (hackForSubmenus == null) openOptionsMenu();
				hackForSubmenus.performIdentifierAction(R.id.type, 0);
			}
		});
		d.findViewById(R.id.other).setOnClickListener(new OnClickListener() {
			@Override public void onClick(View v) {
				d.dismiss();
				startChooser();
			}
		});
		d.show();
	}

	void requestResize(int x, int y)
	{
		gameView.clear();
		gameViewResized();
	}

	void setStatus(final String status)
	{
		runOnUiThread(new Runnable(){public void run(){
			statusBar.setText(status.length() == 0 ? " " : status);
		}});
	}

	void requestTimer(boolean on)
	{
		if( gameWantsTimer && on ) return;
		gameWantsTimer = on;
		if( on ) handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()), timerInterval);
		else handler.removeMessages(MsgType.TIMER.ordinal());
	}

	void drawPoly(int[] points, int ox, int oy, int line, int fill)
	{
		path = new Path();
		path.moveTo(points[0]+ox, points[1]+oy);
		for( int i=1; i < points.length/2; i++ )
			path.lineTo(points[2*i]+ox, points[2*i+1]+oy);
		path.close();
		gameView.drawPoly(path, line, fill);
	}

	void dialogInit(String title)
	{
		ScrollView sv = new ScrollView(SGTPuzzles.this);
		dialog = new AlertDialog.Builder(SGTPuzzles.this)
				.setTitle(title)
				.setView(sv)
				.create();
		sv.addView(dialogLayout = new TableLayout(SGTPuzzles.this));
		dialog.setOnCancelListener(new OnCancelListener() {
			public void onCancel(DialogInterface dialog) {
				configCancel();
			}
		});
		dialog.setButton(DialogInterface.BUTTON_POSITIVE, getString(android.R.string.ok), new DialogInterface.OnClickListener() {
			public void onClick(DialogInterface d, int which) {
				for( Integer i : dialogIds ) {
					View v = dialogLayout.findViewById(i);
					if( v instanceof EditText ) {
						configSetString(i, ((EditText)v).getText().toString());
					} else if( v instanceof CheckBox ) {
						configSetBool(i, ((CheckBox)v).isChecked() ? 1 : 0);
					} else if( v instanceof Spinner ) {
						configSetChoice(i, ((Spinner)v).getSelectedItemPosition());
					}
				}
				dialog.dismiss();
				showProgress( R.string.starting_custom );
				gameView.clear();
				(worker = new Thread("startCustomGame") { public void run() {
					configOK();
					handler.sendEmptyMessage(MsgType.DONE.ordinal());
				}}).start();
			}
		});
		dialogIds = new ArrayList<Integer>();
	}

	void dialogAdd(int id, int type, String name, String value, int selection)
	{
		switch(type) {
		case C_STRING: {
			dialogIds.add(id);
			EditText et = new EditText(SGTPuzzles.this);
			// TODO: C_INT, C_UINT, C_UDOUBLE, C_DOUBLE
			// Ugly temporary hack: in custom game dialog, all text boxes are numeric, in the other two dialogs they aren't.
			// Uglier temporary-er hack: Black Box must accept a range for ball count.
			if (configIsCustom && !helpTopic.equals("blackbox")) et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL | InputType.TYPE_NUMBER_FLAG_SIGNED);
			et.setId(id);
			et.setText(value);
			TextView tv = new TextView(SGTPuzzles.this);
			tv.setText(name);
			tv.setPadding(2,2,2,2);
			TableRow tr = new TableRow(SGTPuzzles.this);
			tr.addView(tv);
			tr.addView(et);
			dialogLayout.addView(tr);
			break; }
		case C_BOOLEAN: {
			dialogIds.add(id);
			CheckBox c = new CheckBox(SGTPuzzles.this);
			c.setId(id);
			c.setText(name);
			c.setChecked(selection != 0);
			dialogLayout.addView(c);
			break; }
		case C_CHOICES: {
			StringTokenizer st = new StringTokenizer(value.substring(1),value.substring(0,1));
			ArrayList<String> choices = new ArrayList<String>();
			while(st.hasMoreTokens()) choices.add(st.nextToken());
			dialogIds.add(id);
			Spinner s = new Spinner(SGTPuzzles.this);
			s.setId(id);
			ArrayAdapter<String> a = new ArrayAdapter<String>(SGTPuzzles.this,
					android.R.layout.simple_spinner_item, choices.toArray(new String[0]));
			a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
			s.setAdapter(a);
			s.setSelection(selection);
			TextView tv = new TextView(SGTPuzzles.this);
			tv.setText(name);
			TableRow tr = new TableRow(SGTPuzzles.this);
			tr.addView(tv);
			tr.addView(s);
			dialogLayout.addView(tr);
			break; }
		}
	}

	void dialogShow()
	{
		dialogLayout.setColumnShrinkable(0, true);
		dialogLayout.setColumnShrinkable(1, true);
		dialogLayout.setColumnStretchable(1, true);
		dialog.show();
	}

	void tickTypeItem(int which)
	{
		currentType = which;
	}

	void serialiseWrite(byte[] buffer)
	{
		savingState.append(new String(buffer));
	}

	int lastArrowMode = 0;
	void setKeys(String keys, int arrowMode)
	{
		lastArrowMode = arrowMode;
		if( keys == null ) return;
		lastKeys = keys + maybeUndoRedo + maybeMenu;
		runOnUiThread(new Runnable(){public void run(){
			setKeyboardVisibility(getResources().getConfiguration());
		}});
	}

	@Override
	public void onSharedPreferenceChanged(SharedPreferences p, String key)
	{
		if (key.equals("arrowKeys") || key.equals("inertiaForceArrows")) {
			setKeyboardVisibility(getResources().getConfiguration());
		} else if (key.equals(FULLSCREEN_KEY)) {
			applyFullscreen(true);  // = already started
		} else if (key.equals(STAY_AWAKE_KEY)) {
			applyStayAwake();
		}
	}

	void applyFullscreen(boolean alreadyStarted) {
		boolean hasActionBar = ActionBarCompat.earlyHasActionBar();
		if (prefs.getBoolean(FULLSCREEN_KEY, false)) {
			if (hasActionBar) {
				handler.post(new Runnable(){ public void run() {
					actionBarCompat.lightsOut(gameView, true);
				}});
			} else if (alreadyStarted) {
				// This is the only way to change the theme
				restartOnResume = true;
			} else {
				setTheme(android.R.style.Theme_NoTitleBar_Fullscreen);
			}
		} else {
			if (hasActionBar) {
				final boolean fAlreadyStarted = alreadyStarted;
				handler.post(new Runnable(){ public void run() {
					actionBarCompat.lightsOut(gameView, false);
					// This shouldn't be necessary but is on Galaxy Tab 10.1
					if (fAlreadyStarted) restartOnResume = true;
				}});
			} else if (alreadyStarted) {
				// This is the only way to change the theme
				restartOnResume = true;
			}  // else leave it as default non-fullscreen
		}
	}

	void applyStayAwake()
	{
		if (prefs.getBoolean(STAY_AWAKE_KEY, false)) {
			getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		} else {
			getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		}
	}

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
				.replaceAll("[^A-Za-z0-9_]+","_");
		if( id.endsWith("_") ) id = id.substring(0,id.length()-1);
		int resId = getResources().getIdentifier(id, "string", getPackageName());
		if( resId > 0 ) {
			String ret = getString(resId);
			//Log.d(TAG,"gettext: "+s+" -> "+id+" -> "+ret);
			return ret;
		}
		Log.i(TAG,"gettext: NO TRANSLATION: "+s+" -> "+id+" -> ???");
		return s;
	}

	void changedState(boolean canUndo, boolean canRedo)
	{
		undoEnabled = canUndo;
		redoEnabled = canRedo;
		if (keyboard != null) {
			keyboard.setUndoRedoEnabled(false, canUndo);
			keyboard.setUndoRedoEnabled(true, canRedo);
		}
		if (actionBarCompat != null && menu != null) {
			runOnUiThread(new Runnable() {
				@Override public void run() {
					MenuItem mi;
					mi = menu.findItem(R.id.undo);
					if (mi != null) {
						mi.setEnabled(undoEnabled);
						mi.setIcon(undoEnabled ? R.drawable.sym_keyboard_undo : R.drawable.sym_keyboard_undo_disabled);
					}
					mi = menu.findItem(R.id.redo);
					if (mi != null) {
						mi.setEnabled(redoEnabled);
						mi.setIcon(redoEnabled ? R.drawable.sym_keyboard_redo : R.drawable.sym_keyboard_redo_disabled);
					}
				}
			});
		}
	}

	/** A signal handler in native code has been triggered. As our last gasp,
	 * launch the crash handler (in its own process), because when we return
	 * from this function the process will soon exit. */
	void nativeCrashed()
	{
		if (state != null) {
			try {
				Log.d(TAG, "saved game was:\n"+state.getString("savedGame",""));
			} catch(Exception e) {
				Log.d(TAG, "couldn't report saved game because: "+e.toString());
				e.printStackTrace();
			}
		}
		try {
			if (gameView == null) Log.d(TAG, "GameView is null");
			else Log.d(TAG, "GameView has seen "+gameView.keysHandled+" keys since init");
		} catch(Exception e) {
			Log.d(TAG, "couldn't report key count because: "+e.toString());
		}
		new RuntimeException("crashed here (native trace should follow after the Java trace)").printStackTrace();
		startActivity(new Intent(this, CrashHandler.class));
		finish();
	}

	static String readAllOf(InputStream s) throws IOException
	{
		BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(s),8096);
		String line;
		StringBuilder log = new StringBuilder();
		while ((line = bufferedReader.readLine()) != null) {
			log.append(line);
			log.append("\n");
		}
		return log.toString();
	}

	native void init(GameView _gameView, int whichGame, String gameState);
	native void cancel();
	native void timerTick();
	native String htmlHelpTopic();
	native void keyEvent(int x, int y, int k);
	native void restartEvent();
	native void solveEvent();
	native void resizeEvent(int x, int y);
	native void presetEvent(int id);
	native void configEvent(int which);
	native void configOK();
	native void configCancel();
	native void configSetString(int item_ptr, String s);
	native void configSetBool(int item_ptr, int selected);
	native void configSetChoice(int item_ptr, int selected);
	native void serialise();
	native void crashMeHarder();

	static {
		System.loadLibrary("puzzles");
	}
}
