package name.boyle.chris.sgtpuzzles;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintStream;
import java.io.RandomAccessFile;
import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.text.MessageFormat;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.StringTokenizer;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.DialogInterface.OnCancelListener;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.drawable.Drawable;
import android.graphics.PorterDuff.Mode;
import android.net.MailTo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.text.Editable;
import android.text.InputType;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.style.ForegroundColorSpan;
import android.text.TextWatcher;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.widget.AbsListView;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.SimpleAdapter;
import android.widget.Spinner;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.AdapterView.OnItemClickListener;

public class SGTPuzzles extends Activity
{
	private static final String TAG = "SGTPuzzles";
	ProgressDialog progress;
	TextView txtView;
	SmallKeyboard keyboard;
	LinearLayout gameAndKeys;
	GameView gameView;
	String[] games;
	SubMenu typeMenu;
	LinkedHashMap<Integer,String> gameTypes;
	int currentType = 0;
	boolean gameRunning = false;
	boolean solveEnabled = false, customVisible = false, fakeCrash = false,
			undoEnabled = false, redoEnabled = false;
	SharedPreferences prefs;
	static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2,
		LEFT_BUTTON = 0x0200, MIDDLE_BUTTON = 0x201, RIGHT_BUTTON = 0x202,
		LEFT_DRAG = 0x203, //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
		LEFT_RELEASE = 0x206, CURSOR_UP = 0x209, CURSOR_DOWN = 0x20a,
		CURSOR_LEFT = 0x20b, CURSOR_RIGHT = 0x20c, MOD_CTRL = 0x1000,
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
				dismissProgress();
				save();
				break;
			case ABORT:
				stopNative();
				dismissProgress();
				showDialog(0);
				if (msg.obj != null) {
					messageBox(getString(R.string.Error), (String)msg.obj, 1);
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
		progress.dismiss();
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
		SharedPreferences.Editor ed = prefs.edit();
		ed.remove("engineName");
		ed.putString("savedGame", s);
		ed.commit();
	}

	void gameViewResized()
	{
		if( progress == null && gameView.w > 0 && gameView.h > 0 )
			resizeEvent(gameView.w, gameView.h);
		else
			resizeOnDone = true;
	}

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
		super.onCreate(savedInstanceState);
		games = getResources().getStringArray(R.array.games);
		gameTypes = new LinkedHashMap<Integer,String>();
		requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
		setContentView(R.layout.main);
		txtView = (TextView)findViewById(R.id.txtView);
		gameAndKeys = (LinearLayout)findViewById(R.id.gameAndKeys);
		gameView = (GameView)findViewById(R.id.game);
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);
		gameView.requestFocus();

		prefs = getSharedPreferences("state", MODE_PRIVATE);
		if( prefs.contains("savedGame") && prefs.getString("savedGame","").length() > 0 ) {
			startGame(-1, prefs.getString("savedGame",""));
		} else {
			new AlertDialog.Builder(this)
				.setMessage(R.string.welcome)
				.setOnCancelListener(quitListener)
				.setPositiveButton(android.R.string.yes, new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface d, int which) { showDialog(0); }})
				.show();
		}
	}

	class GameChooser extends Dialog
	{
		boolean useGrid = prefs.getString("chooserStyle","list").equals("grid");
		AbsListView gv;
		final String LABEL = "LABEL";
		final String ICON = "ICON";
		GameChooser()
		{
			super(SGTPuzzles.this, android.R.style.Theme);  // full screen
			setTitle(R.string.chooser_title);
			setCancelable(true);
			rebuild();
		}

		public boolean onCreateOptionsMenu(Menu menu)
		{
			super.onCreateOptionsMenu(menu);
			getMenuInflater().inflate(R.menu.chooser, menu);
			return true;
		}

		public boolean onPrepareOptionsMenu(Menu menu)
		{
			super.onPrepareOptionsMenu(menu);
			menu.findItem(useGrid ? R.id.gridchooser : R.id.listchooser).setChecked(true);
			return true;
		}

		/** Possible android bug: onOptionsItemSelected(MenuItem item)
		 *  wasn't being called? */
		public boolean onMenuItemSelected(int f, MenuItem item)
		{
			boolean newGrid;
			switch(item.getItemId()) {
				case R.id.listchooser: newGrid = false; break;
				case R.id.gridchooser: newGrid = true; break;
				case R.id.load: SGTPuzzles.this.showLoadPrompt(); return true;
				default: return super.onMenuItemSelected(f,item);
			}
			if( useGrid == newGrid ) return true;
			useGrid = newGrid;
			rebuild();
			SharedPreferences.Editor ed = prefs.edit();
			ed.putString("chooserStyle", useGrid ? "grid" : "list");
			ed.commit();
			return true;
		}

		void rebuild()
		{
			if( useGrid ) {
				gv = new GridView(SGTPuzzles.this);
				((GridView)gv).setNumColumns(GridView.AUTO_FIT);
				((GridView)gv).setColumnWidth(64);
			} else {
				gv = new ListView(SGTPuzzles.this);
			}
			gv.setOnItemClickListener(new OnItemClickListener() {
				public void onItemClick(AdapterView<?> arg0, View arg1, int which, long arg3) {
					startGame(which, null);
					SGTPuzzles.this.dismissDialog(0);
				}
			});
			List<Map<String,Object>> gameDescs = new ArrayList<Map<String,Object> >( games.length );
			for( int i = 0; i < games.length; i++ ) {
				Map<String,Object> map = new HashMap<String,Object>();
				int nameId = getResources().getIdentifier("name_"+games[i], "string", getPackageName());
				int descId = getResources().getIdentifier("desc_"+games[i], "string", getPackageName());
				String desc;
				if( nameId > 0 ) desc = getString(nameId);
				else desc = games[i].substring(0,1).toUpperCase() + games[i].substring(1);
				desc += ": " + getString( descId > 0 ? descId : R.string.no_desc );
				map.put( LABEL, desc );
				Drawable d = getResources().getDrawable(getResources().getIdentifier(games[i], "drawable", getPackageName()));
				Bitmap b = Bitmap.createBitmap(d.getIntrinsicWidth(),d.getIntrinsicHeight(), Bitmap.Config.RGB_565);
				Canvas c = new Canvas(b);
				d.setBounds(0,0,d.getIntrinsicWidth(),d.getIntrinsicHeight());
				d.draw(c);
				map.put( ICON, b );
				gameDescs.add( map );
			}
			SimpleAdapter s = new SimpleAdapter(SGTPuzzles.this, gameDescs,
						useGrid ? R.layout.grid_item : R.layout.list_item,
						useGrid ? new String[]{ ICON } : new String[]{ LABEL, ICON },
						useGrid ? new int[]{ android.R.id.icon } : new int[]{ android.R.id.text1, android.R.id.icon });
			s.setViewBinder( new SimpleAdapter.ViewBinder() {
				public boolean setViewValue(View v, Object o, String t) {
					if( ! ( v instanceof ImageView ) ) return false;
					((ImageView)v).setImageBitmap((Bitmap)o);
					return true;
				}
			});
			gv.setAdapter(s);
			setContentView(gv);
		}

		void prepare()
		{
			setOnCancelListener(gameRunning ? null : quitListener);
		}
	}

	public Dialog onCreateDialog(int id)
	{
		return new GameChooser();
	}
	
	public void onPrepareDialog(int id, Dialog d)
	{
		((GameChooser)d).prepare();
	}
	
	public boolean onCreateOptionsMenu(Menu menu)
	{
		super.onCreateOptionsMenu(menu);
		getMenuInflater().inflate(R.menu.main, menu);
		return true;
	}
	
	public boolean onPrepareOptionsMenu(Menu menu)
	{
		super.onPrepareOptionsMenu(menu);
		menu.findItem(R.id.solve).setEnabled(solveEnabled);
		menu.findItem(R.id.undo).setEnabled(undoEnabled);
		menu.findItem(R.id.redo).setEnabled(redoEnabled);
		MenuItem typeItem = menu.findItem(R.id.type);
		typeItem.setEnabled(! gameTypes.isEmpty() || customVisible);
		typeMenu = typeItem.getSubMenu();
		for( Integer i : gameTypes.keySet() ) {
			if( menu.findItem(i) == null ) typeMenu.add(R.id.typeGroup, i, Menu.NONE, gameTypes.get(i) );
		}
		MenuItem customItem = menu.findItem(R.id.custom);
		customItem.setVisible(customVisible);
		typeMenu.setGroupCheckable(R.id.typeGroup, true, true);
		if( currentType < 0 ) customItem.setChecked(true);
		else menu.findItem((Integer)gameTypes.keySet().toArray()[currentType]).setChecked(true);
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
		String lang = Locale.getDefault().getLanguage();
		if (lang == null || lang.equals("")) lang = "en";
		wv.loadUrl(MessageFormat.format(getString(R.string.docs_url),
				new Object[]{lang,topic}));
		d.show();
	}

	public boolean onOptionsItemSelected(MenuItem item)
	{
		switch(item.getItemId()) {
		case R.id.other:   showDialog(0); break;
		case R.id.newgame:
			showProgress( R.string.starting_new );
			changedState(false, false);
			(worker = new Thread("newGame") { public void run() {
				sendKey(0, 0, 'n');
				handler.sendEmptyMessage(MsgType.DONE.ordinal());
			}}).start();
			break;
		case R.id.restart:  restartEvent(); break;
		case R.id.undo:     sendKey(0, 0, 'u'); break;
		case R.id.redo:     sendKey(0, 0, 'r'); break;
		case R.id.solve:    solveEvent(); break;
		case R.id.custom:   configEvent( CFG_SETTINGS ); break;
		case R.id.specific: configEvent( CFG_DESC ); break;
		case R.id.seed:     configEvent( CFG_SEED ); break;
		case R.id.about:    aboutEvent(); break;
		case R.id.contents: showHelp("index"); break;
		case R.id.thisgame: showHelp(helpTopic); break;
		case R.id.website:
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(
							getString(R.string.website_url))));
			break;
		case R.id.email:
			tryEmailAuthor(this, false, null);
			break;
		case R.id.load: showLoadPrompt(); break;
		case R.id.save:
			if (! Environment.getExternalStorageState().equals(Environment.MEDIA_MOUNTED)) {
				Toast.makeText(SGTPuzzles.this, R.string.storage_not_ready, Toast.LENGTH_SHORT).show();
				break;
			}
			new FilePicker(storageDir,true).show();
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

	static boolean tryEmailAuthor(Context c, boolean isCrash, String body)
	{
		String addr = c.getString(R.string.author_email);
		Intent i = new Intent(Intent.ACTION_SEND);
		// second empty address because of http://code.google.com/p/k9mail/issues/detail?id=589
		i.putExtra(Intent.EXTRA_EMAIL, new String[]{addr, ""});
		i.putExtra(Intent.EXTRA_SUBJECT, MessageFormat.format(c.getString(
					isCrash ? R.string.crash_subject : R.string.email_subject),
					getVersion(c), Build.MODEL, Build.FINGERPRINT));
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

	void showLoadPrompt()
	{
		if (! Environment.getExternalStorageState().equals(Environment.MEDIA_MOUNTED)) {
			Toast.makeText(SGTPuzzles.this, R.string.storage_not_ready, Toast.LENGTH_SHORT).show();
			return;
		}
		new FilePicker(storageDir,false).show();
	}

	class FilePicker extends Dialog
	{
		String[] files;
		ListView lv;
		FilePicker parent;
		void dismissAll()
		{
			try {
				SGTPuzzles.this.dismissDialog(0);
			} catch (Exception e) {}
			dismiss();
			FilePicker fp = this.parent;
			while (fp != null) {
				fp.dismiss();
				fp = fp.parent;
			}
		}
		void load(final File f) throws IOException
		{
			byte[] b = new byte[(int)f.length()];
			new RandomAccessFile(f,"r").readFully(b);
			String s = new String(b);
			SGTPuzzles.this.startGame(-1,s);
			dismissAll();
		}
		void save(final File f)
		{
			if (f.exists()) {
				AlertDialog.Builder b = new AlertDialog.Builder(SGTPuzzles.this)
					.setMessage(R.string.file_exists)
					.setCancelable(true)
					.setIcon(android.R.drawable.ic_dialog_alert);
				b.setPositiveButton(android.R.string.yes, new OnClickListener(){ public void onClick(DialogInterface d, int which) {
					try {
						f.delete();
						save(f);
					} catch (Exception e) {
						Toast.makeText(SGTPuzzles.this, e.toString(), Toast.LENGTH_LONG).show();
					}
					d.dismiss();
				}});
				b.setNegativeButton(android.R.string.no, new OnClickListener(){ public void onClick(DialogInterface d, int which) {
					d.cancel();
				}});
				b.show();
				return;
			}
			try {
				String s = saveToString();
				FileWriter w = new FileWriter(f);
				w.write(s,0,s.length());
				w.close();
				Toast.makeText(SGTPuzzles.this, MessageFormat.format(
						getString(R.string.file_saved),new Object[]{f.getPath()}),
						Toast.LENGTH_LONG).show();
				dismissAll();
			} catch (Exception e) {
				Toast.makeText(SGTPuzzles.this, e.toString(), Toast.LENGTH_LONG).show();
			}
		}
		FilePicker(final File path, final boolean isSave) { this(path, isSave, null); }
		FilePicker(final File path, final boolean isSave, FilePicker parent)
		{
			super(SGTPuzzles.this, android.R.style.Theme);  // full screen
			this.parent = parent;
			this.files = path.list();
			Arrays.sort(this.files);
			setTitle(path.getName());
			setCancelable(true);
			setContentView(isSave ? R.layout.file_save : R.layout.file_load);
			lv = (ListView)findViewById(R.id.filelist);
			lv.setAdapter(new ArrayAdapter<String>(SGTPuzzles.this, android.R.layout.simple_list_item_1, files));
			lv.setOnItemClickListener(new OnItemClickListener() {
				public void onItemClick(AdapterView<?> arg0, View arg1, int which, long arg3) {
					File f = new File(path,files[which]);
					if (f.isDirectory()) {
						new FilePicker(f,isSave,FilePicker.this).show();
						return;
					}
					if (isSave) {
						save(f);
						return;
					}
					try {
						if (f.length() > MAX_SAVE_SIZE) {
							Toast.makeText(SGTPuzzles.this, R.string.file_too_big, Toast.LENGTH_LONG).show();
							return;
						}
						load(f);
					} catch (Exception e) {
						Toast.makeText(SGTPuzzles.this, e.toString(), Toast.LENGTH_LONG).show();
					}
				}
			});
			if (!isSave) return;
			final EditText et = (EditText)findViewById(R.id.savebox);
			et.addTextChangedListener(new TextWatcher(){
				public void onTextChanged(CharSequence s,int a,int b, int c){}
				public void beforeTextChanged(CharSequence s,int a,int b, int c){}
				public void afterTextChanged(Editable s) {
					lv.setFilterText(s.toString());
				}
			});
			et.setOnEditorActionListener(new TextView.OnEditorActionListener() { public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
				Log.d(TAG,"actionId: "+actionId+", event: "+event);
				if (actionId == EditorInfo.IME_ACTION_DONE) return false;
				if ((event != null && event.getAction() != KeyEvent.ACTION_DOWN)
						|| et.length() == 0) return true;
				save(new File(path,et.getText().toString()));
				return true;
			}});
			final Button saveButton = (Button)findViewById(R.id.savebutton);
			saveButton.setOnClickListener(new View.OnClickListener(){public void onClick(View v){
				save(new File(path,et.getText().toString()));
			}});
			et.requestFocus();
		}
	}

	void quit(boolean fromDestroy)
	{
		stopNative();
		if( ! fromDestroy ) finish();
	}

	OnCancelListener quitListener = new OnCancelListener() {
		public void onCancel(DialogInterface dialog) { quit(false); }
	};

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
		showProgress( (savedGame == null) ? R.string.starting : R.string.resuming );
		if( gameRunning ) {
			gameView.clear();
			stopNative();
			solveEnabled = false;
			changedState(false, false);
			customVisible = false;
			txtView.setVisibility( View.GONE );
		}
		setKeys("");
		if( typeMenu != null ) for( Integer i : gameTypes.keySet() ) typeMenu.removeItem(i);
		gameTypes.clear();
		gameRunning = true;
		(worker = new Thread("startGame") { public void run() {
			init(gameView, which, savedGame);
			if( ! gameRunning ) return;  // stopNative or abort was called
			helpTopic = htmlHelpTopic();
			handler.sendEmptyMessage(MsgType.DONE.ordinal());
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

	protected void onPause()
	{
		if( gameRunning ) handler.removeMessages(MsgType.TIMER.ordinal());
		save();
		super.onPause();
	}

	protected void onDestroy()
	{
		quit(true);
		super.onDestroy();
	}

	public void onWindowFocusChanged( boolean f )
	{
		if( f && gameWantsTimer && gameRunning
				&& ! handler.hasMessages(MsgType.TIMER.ordinal()) )
			handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()),
					timerInterval);
	}
	
	void sendKey(int x, int y, int k)
	{
		if(! gameRunning) return;
		if (keyEvent(x, y, k) == 0) quit(false);
	}

	boolean prevLandscape = false;
	void setKeyboardVisibility(Configuration c)
	{
		boolean landscape = (c.orientation == Configuration.ORIENTATION_LANDSCAPE);
		gameAndKeys.setOrientation( landscape ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL );
		if (landscape != prevLandscape || keyboard == null) {
			// Must recreate KeyboardView on orientation change because it caches the x,y for its preview popups
			// http://code.google.com/p/android/issues/detail?id=4559
			if (keyboard != null) gameAndKeys.removeView(keyboard);
			keyboard = new SmallKeyboard(this, undoEnabled, redoEnabled);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT, 0);
			lp.gravity = Gravity.CENTER;
			gameAndKeys.addView(keyboard, lp);
		}
		//keyboard.setKeys(lastKeys, landscape);
		//keyboard.setVisibility( (c.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO)
		//		? View.GONE : View.VISIBLE );
		keyboard.setKeys( (c.hardKeyboardHidden == Configuration.HARDKEYBOARDHIDDEN_NO)
				? "ur" : lastKeys, landscape );
		prevLandscape = landscape;
	}

	public void onConfigurationChanged(Configuration newConfig)
	{
		setKeyboardVisibility(newConfig);
		super.onConfigurationChanged(newConfig);
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
			txtView.setVisibility( hasStatus ? View.VISIBLE : View.GONE );
		}});
	}

	void addTypeItem(int id, String label) { gameTypes.put(id, label); }

	void messageBox(final String title, final String msg, final int flag)
	{
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
					.show();
		}});
		// I don't think we need to wait before returning here (and we can't)
	}

	void requestResize(int x, int y)
	{
		gameView.clear();
		gameViewResized();
	}

	void setStatus(final String status)
	{
		runOnUiThread(new Runnable(){public void run(){
			txtView.setText(status.length() == 0 ? " " : status);
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
			et.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL | InputType.TYPE_NUMBER_FLAG_SIGNED);
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

	void setKeys(String keys)
	{
		if( keys == null ) return;
		lastKeys = keys + "ur";
		runOnUiThread(new Runnable(){public void run(){
			setKeyboardVisibility(getResources().getConfiguration());
		}});
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
		if (keyboard == null) return;
		keyboard.setUndoRedoEnabled(false, canUndo);
		keyboard.setUndoRedoEnabled(true, canRedo);
	}

	/** A signal handler in native code has been triggered. As our last gasp,
	 * launch the crash handler (in its own process), because when we return
	 * from this function the process will soon exit. */
	void nativeCrashed()
	{
		if (prefs != null) {
			try {
				System.err.println("saved game was:\n"+prefs.getString("savedGame",""));
			} catch(Exception e) {}
		}
		new RuntimeException("crashed here (native trace should follow after the Java trace)").printStackTrace();
		startActivity(new Intent(this, CrashHandler.class));
	}

	native void init(GameView _gameView, int whichGame, String gameState);
	native void cancel();
	native void timerTick();
	native String htmlHelpTopic();
	native int keyEvent(int x, int y, int k);
	native int menuKeyEvent(int k);
	native void restartEvent();
	native void solveEvent();
	native void aboutEvent();
	native void resizeEvent(int x, int y);
	native void presetEvent(int id);
	native void configEvent(int which);
	native void configOK();
	native void configCancel();
	native void configSetString(int item_ptr, String s);
	native void configSetBool(int item_ptr, int selected);
	native void configSetChoice(int item_ptr, int selected);
	native void serialise();
	native String deserialise(String s);
	native void crashMeHarder();

	static {
		System.loadLibrary("puzzles");
	}
}
