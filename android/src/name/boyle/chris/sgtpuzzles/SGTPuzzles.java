package name.boyle.chris.sgtpuzzles;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.text.MessageFormat;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.StringTokenizer;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.ProgressDialog;
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
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.text.InputType;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.style.ForegroundColorSpan;
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
	FrameLayout placeholder;
	GameView gameView;
	String[] games;
	SubMenu typeMenu;
	LinkedHashMap<Integer,String> gameTypes;
	int currentType = 0;
	boolean gameRunning = false;
	boolean solveEnabled = false, customVisible = false, dead = false;
	String lastSave;
	SharedPreferences prefs;
	static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2,
		LEFT_BUTTON = 0x0200, MIDDLE_BUTTON = 0x201, RIGHT_BUTTON = 0x202,
		LEFT_DRAG = 0x203, //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
		LEFT_RELEASE = 0x206, CURSOR_UP = 0x209, CURSOR_DOWN = 0x20a,
		CURSOR_LEFT = 0x20b, CURSOR_RIGHT = 0x20c, MOD_CTRL = 0x1000,
		MOD_SHFT = 0x2000, MOD_NUM_KEYPAD = 0x4000, ALIGN_VCENTRE = 0x100,
		ALIGN_HCENTRE = 0x001, ALIGN_HRIGHT = 0x002, TEXT_MONO = 0x10,
		C_STRING = 0, C_CHOICES = 1, C_BOOLEAN = 2;
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

	enum MsgType { INIT, TIMER, DONE, DIE, ABORT, SETBG, STATUS, MESSAGEBOX };
	Handler handler = new Handler() {
		public void handleMessage( Message msg ) {
			switch( MsgType.values()[msg.what] ) {
			case INIT:
				setTitle( (String)msg.obj );
				txtView.setVisibility( ((msg.arg1 & 2) > 0) ? View.VISIBLE : View.GONE );
				if ((msg.arg1 & 1) != 0) customVisible = true;
				if ((msg.arg1 & 4) != 0) solveEnabled = true;
				break;
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
			case DIE: die((String)msg.obj); break;
			case ABORT:
				stopRuntime(null);
				dismissProgress();
				showDialog(0);
				break;
			case SETBG: gameView.setBackgroundColor(gameView.colours[0]); break;
			case STATUS:
				String status = (String)msg.obj;
				if( status.length() == 0 ) status = " ";
				txtView.setText(status);
				break;
			case MESSAGEBOX: {
				dismissProgress();
				String[] strings = (String[]) msg.obj;
				if( strings[0] == null )
					Toast.makeText(SGTPuzzles.this, strings[1], Toast.LENGTH_SHORT).show();
				else new AlertDialog.Builder(SGTPuzzles.this)
						.setTitle( strings[0] )
						.setMessage( strings[1] )
						.setIcon( ( msg.arg1 == 0 )
								? android.R.drawable.ic_dialog_info
								: android.R.drawable.ic_dialog_alert )
						.show();
				break; }
			}
		}
	};

	void showProgress( int msgId )
	{
		progress = new ProgressDialog(this);
		progress.setMessage( getResources().getString(msgId) );
		progress.setIndeterminate( true );
		progress.setCancelable( true );
		progress.setOnCancelListener( abortListener );
		final int msgId2 = msgId;
		progress.setButton( DialogInterface.BUTTON_POSITIVE, getResources().getString(R.string.background), new DialogInterface.OnClickListener() {
			public void onClick( DialogInterface d, int which ) {
				// Cheat slightly: just launch home screen
				startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME));
				// Argh, can't prevent a dismiss at this point, so re-show it
				showProgress(msgId2);
				Toast.makeText(SGTPuzzles.this, R.string.bg_unreliable_warn, Toast.LENGTH_LONG).show();
			}
		});
		progress.setButton( DialogInterface.BUTTON_NEGATIVE, getResources().getString(android.R.string.cancel), handler.obtainMessage(MsgType.ABORT.ordinal()));
		progress.show();
	}

	void dismissProgress()
	{
		if( progress == null ) return;
		progress.dismiss();
		progress = null;
	}

	void save()
	{
		if( dead || ! gameRunning || progress != null ) return;
		savingState = new StringBuffer();
		serialise();  // serialiseWrite() callbacks will happen in here
		String s = savingState.toString();
		if( savingState.length() > 0 ) lastSave = s;
		savingState = null;
		if( lastSave == null ) return;
		SharedPreferences.Editor ed = prefs.edit();
		ed.remove("engineName");
		ed.putString("savedGame", lastSave);
		ed.commit();
	}

	void load(String state)
	{
		deserialise(state);
		save();
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
		super.onCreate(savedInstanceState);
		games = getResources().getStringArray(R.array.games);
		gameTypes = new LinkedHashMap<Integer,String>();
		requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
		setContentView(R.layout.main);
		txtView = (TextView)findViewById(R.id.txtView);
		placeholder = (FrameLayout)findViewById(R.id.placeholder);
		gameView = new GameView(this);
		placeholder.addView( gameView, FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT );
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);

		prefs = getSharedPreferences("state", MODE_PRIVATE);
		if( prefs.contains("savedGame") ) {
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
				if( nameId > 0 ) desc = getResources().getString(nameId);
				else desc = games[i].substring(0,1).toUpperCase() + games[i].substring(1);
				desc += ": " + getResources().getString( descId > 0 ? descId : R.string.no_desc );
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
					getResources().getString(R.string.help_on_game),new Object[]{this.getTitle()}));
		return true;
	}
	
	public boolean onOptionsItemSelected(MenuItem item)
	{
		switch(item.getItemId()) {
		case R.id.other:   showDialog(0); break;
		case R.id.newgame:
			showProgress( R.string.starting_new );
			(worker = new Thread("newGame") { public void run() {
				try {
					sendKey(0, 0, 'n');
					handler.sendEmptyMessage(MsgType.DONE.ordinal());
				} catch (Exception e) {
					handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)).sendToTarget();
				}
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
		case R.id.contents:
			startActivity(new Intent(Intent.ACTION_VIEW,
					Uri.parse(getResources().getString(R.string.help_contents_url))));
			break;
		case R.id.thisgame:
			startActivity(new Intent(Intent.ACTION_VIEW,
				Uri.parse(MessageFormat.format(getResources().getString(R.string.help_game_url),new Object[]{helpTopic}))));
			break;
		case R.id.website:
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(
							getResources().getString(R.string.website_url))));
			break;
		case R.id.email:
			// Damn, Android Email doesn't cope with ?subject=foo yet.
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(getResources().getString(R.string.mailto_author))));
			break;
		/*case R.id.load:
			// TODO: load
			break;
		case R.id.save:
			// TODO: save
			break;*/
		default:
			final int id = item.getItemId();
			if( gameTypes.get(id) != null ) {
				showProgress( R.string.changing_type );
				gameView.clear();
				(worker = new Thread("presetGame") { public void run() {
					try {
						presetEvent(id);
						handler.sendEmptyMessage(MsgType.DONE.ordinal());
					} catch (Exception e) {
						handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)+"\npreset:"+gameTypes.get(id)).sendToTarget();
					}
				}}).start();
			}
			else super.onOptionsItemSelected(item);
			break;
		}
		return true;
	}

	void quit(boolean fromDestroy)
	{
		stopRuntime(null);
		if( ! fromDestroy ) finish();
	}

	OnCancelListener quitListener = new OnCancelListener() {
		public void onCancel(DialogInterface dialog) { quit(false); }
	};

	OnCancelListener abortListener = new OnCancelListener() {
		public void onCancel(DialogInterface dialog) {
			handler.sendEmptyMessage(MsgType.ABORT.ordinal());
		}
	};

	String getStackTrace( Throwable t )
	{
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		t.printStackTrace(new PrintStream(b,true));
		return b.toString();
	}

	void alert( String title, String msg, boolean fatal )
	{
		dismissProgress();
		AlertDialog.Builder b = new AlertDialog.Builder(this)
			.setTitle( title )
			.setIcon(android.R.drawable.ic_dialog_alert)
			.setOnCancelListener( fatal ? quitListener : abortListener );
		final CheckBox c = new CheckBox(SGTPuzzles.this);
		final String msg2 = msg;
		b.setPositiveButton(R.string.report, new DialogInterface.OnClickListener() {
			public void onClick(DialogInterface d, int which) {
				String vName;
				try {
					vName = getPackageManager().getPackageInfo(getPackageName(),0).versionName;
				} catch(Exception e) { vName = "unknown"; }
				if( c.isChecked() ) clearState();
				try {
					startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(
							getResources().getString(R.string.report_url)+
							"?v="+vName+"&e="+URLEncoder.encode(msg2,"UTF-8"))));
				} catch (UnsupportedEncodingException e) { /* It's really not our lucky day...*/ }
				d.cancel();
			}});
		b.setNegativeButton(R.string.close, new DialogInterface.OnClickListener() {
			public void onClick(DialogInterface d, int which) {
				if( c.isChecked() ) clearState();
				d.cancel();
			}
		});
		TextView tv = new TextView(SGTPuzzles.this);
		tv.setText(msg);
		ScrollView sv = new ScrollView(SGTPuzzles.this);
		sv.addView(tv);
		c.setText(R.string.clear_state);
		c.setChecked(true);
		LinearLayout ll = new LinearLayout(SGTPuzzles.this);
		ll.setOrientation(LinearLayout.VERTICAL);
		ll.addView(sv, new LinearLayout.LayoutParams(
					LinearLayout.LayoutParams.FILL_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT, 42 ));
		ll.addView(c, new LinearLayout.LayoutParams(
					LinearLayout.LayoutParams.FILL_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT, 0 ));
		b.setView(ll).show();
	}

	void die( String msg )
	{
		if( dead ) return;
		dead = true;
		alert( getResources().getString(R.string.fatal_error), msg, true );
	}

	void startGame(final int which, final String savedGame)
	{
		showProgress( (savedGame == null) ? R.string.starting : R.string.resuming );
		try {
			if( gameRunning ) {
				gameView.clear();
				stopRuntime(null);
				solveEnabled = false;
				customVisible = false;
				txtView.setVisibility( View.GONE );
			}
			if( typeMenu != null ) for( Integer i : gameTypes.keySet() ) typeMenu.removeItem(i);
			gameTypes.clear();
			gameRunning = true;
			(worker = new Thread("startGame") { public void run() {
				try {
					init(gameView, which, savedGame);
					helpTopic = htmlHelpTopic();
					handler.sendEmptyMessage(MsgType.DONE.ordinal());
				} catch (Exception e) {
					if( ! gameRunning ) return;  // stopRuntime was called (probably user aborted)
					handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)+"\nwhich:"+Integer.toString(which)+"\nstate:"+((savedGame==null)?"null":savedGame)).sendToTarget();
				}
			}}).start();
		} catch (Exception e) {
			alert( getResources().getString(R.string.startgame_fail), getStackTrace(e), false );
		}
	}

	public void stopRuntime(Exception e)
	{
		if( e != null && gameRunning ) die(getStackTrace(e));
		gameRunning = false;
		while(true) { try {
			worker.join();  // we may ANR if native code is spinning - safer than leaving a runaway native thread
			break;
		} catch (InterruptedException i) {} }
	}

	protected void onPause()
	{
		if( gameRunning ) handler.removeMessages(MsgType.TIMER.ordinal());
		save();
		super.onPause();
	}

	void clearState()
	{
		SharedPreferences.Editor ed = prefs.edit();
		ed.clear();
		ed.commit();
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
	
	public boolean onKeyDown( int keyCode, KeyEvent event )
	{
		int key = 0;
		switch( keyCode ) {
		case KeyEvent.KEYCODE_DPAD_UP:    key = CURSOR_UP;    break;
		case KeyEvent.KEYCODE_DPAD_DOWN:  key = CURSOR_DOWN;  break;
		case KeyEvent.KEYCODE_DPAD_LEFT:  key = CURSOR_LEFT;  break;
		case KeyEvent.KEYCODE_DPAD_RIGHT: key = CURSOR_RIGHT; break;
		case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: key = '\n'; break;
		case KeyEvent.KEYCODE_FOCUS: case KeyEvent.KEYCODE_SPACE: key = ' '; break;
		case KeyEvent.KEYCODE_DEL: key = '\b'; break;
		}
		// we probably don't want MOD_NUM_KEYPAD here (numbers are in a line on G1 at least)
		if( key == 0 ) key = event.getMatch("0123456789abcdefghijklqrsux".toCharArray());
		if( key == 0 ) return super.onKeyDown(keyCode, event);  // handles Back etc.
		if( event.isShiftPressed() ) key |= MOD_SHFT;
		if( event.isAltPressed() ) key |= MOD_CTRL;
		if( gameRunning ) sendKey( 0, 0, key );
		return true;
	}

	void sendKey(int x, int y, int k)
	{
		if (keyEvent(x, y, k) == 0) quit(false);
	}

	public void onConfigurationChanged(Configuration newConfig)
	{
		super.onConfigurationChanged(newConfig);
	}

	// Callbacks from native code:

	void gameStarted(String name, int flags, float[] colours)
	{
		gameView.colours = new int[colours.length/3];
		for (int i=0; i<colours.length/3; i++)
			gameView.colours[i] = Color.rgb((int)(colours[i*3]*255),(int)(colours[i*3+1]*255),(int)(colours[i*3+2]*255));
		if (colours.length > 0) handler.sendEmptyMessage(MsgType.SETBG.ordinal());
		handler.obtainMessage(MsgType.INIT.ordinal(), flags, 0, name ).sendToTarget();
	}

	void addTypeItem(int id, String label) { gameTypes.put(id, label); }

	void messageBox(String title, String msg, int flag)
	{
		handler.obtainMessage(MsgType.MESSAGEBOX.ordinal(), flag, 0,
				new String[] {title, msg}).sendToTarget();
		// I don't think we need to wait before returning here (and we can't)
	}

	void requestResize(int x, int y)
	{
		// Refuse this, we have a fixed size screen (except for orientation changes)
		// (wait for UI to finish doing layout first)
		if(gameView.w == 0 || gameView.h == 0 ) try { Thread.sleep(200); } catch( InterruptedException ignored ) {}
		if(gameView.w > 0 && gameView.h > 0 ) resizeEvent(gameView.w, gameView.h);
	}

	void setStatus(String status)
	{
		handler.obtainMessage(MsgType.STATUS.ordinal(), status).sendToTarget();
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

	// TODO: coalesce dialog calls into one call?
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
		dialog.setButton(DialogInterface.BUTTON_POSITIVE, getResources().getString(android.R.string.ok), new DialogInterface.OnClickListener() {
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
					try {
						configOK();
						handler.sendEmptyMessage(MsgType.DONE.ordinal());
					} catch (Exception e) {
						handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)).sendToTarget();
					}
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

	native static void initNative(Class vcls);
	native void init(GameView _gameView, int whichGame, String gameState);
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
	native int deserialise(String s);

	static {
		System.loadLibrary("puzzles");
		initNative(GameView.class);
	}
}
