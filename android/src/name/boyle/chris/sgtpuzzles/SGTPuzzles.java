package name.boyle.chris.sgtpuzzles;

import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.StringTokenizer;
import java.util.TreeMap;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.ProgressDialog;
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
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.PorterDuff.Mode;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.style.ForegroundColorSpan;
import android.util.Log;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AbsListView;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
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

public class SGTPuzzles extends Activity implements PuzzlesRuntime.CallJavaCB
{
	ProgressDialog progress;
	TextView txtView;
	GameView gameView;
	String[] games;
	SubMenu typeMenu;
	TreeMap<Integer,String> gameTypes;
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
		ALIGN_HCENTRE = 0x001, ALIGN_HRIGHT = 0x002, C_STRING = 0,
		C_CHOICES = 1, C_BOOLEAN = 2;
	PuzzlesRuntime runtime;
	boolean gameWantsTimer = false, resizeOnDone = false;
	int timerInterval = 20;
	int xarg1, xarg2, xarg3;
	Path path;
	StringBuffer savingState;
	AlertDialog dialog;
	ArrayList<Integer> dialogIds;
	TableLayout dialogLayout;
	String helpTopic;

	enum MsgType { INIT, TIMER, DONE, DIE, SETBG, STATUS, MESSAGEBOX };
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
				runtimeCall("jcallback_timer_func", new int[0]);
				if( gameWantsTimer ) sendMessageDelayed(obtainMessage(MsgType.TIMER.ordinal()), timerInterval);
				break;
			case DONE:
				if( resizeOnDone ) {
					runtimeCall("jcallback_resize", new int[]{gameView.w,gameView.h});
					resizeOnDone = false;
				}
				dismissProgress();
				save();
				break;
			case DIE: die((String)msg.obj); break;
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
		progress.setButton( getResources().getString(R.string.background), new DialogInterface.OnClickListener() {
			public void onClick( DialogInterface d, int which ) {
				// Cheat slightly: just launch home screen
				startActivity(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME));
				// Argh, can't prevent a dismiss at this point, so re-show it
				showProgress(msgId2);
				Toast.makeText(SGTPuzzles.this, R.string.bg_unreliable_warn, Toast.LENGTH_LONG).show();
			}
		});
		progress.setButton2( getResources().getString(android.R.string.cancel), new DialogInterface.OnClickListener() {
			public void onClick( DialogInterface d, int which ) { abort(); }
		});
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
		if( runtime == null || dead ) return;
		savingState = new StringBuffer();
		runtimeCall("jcallback_serialise", new int[] { 0 });
		if( savingState.length() > 0 ) lastSave = savingState.toString();
		savingState = null;
		if( lastSave == null ) return;
		SharedPreferences.Editor ed = prefs.edit();
		ed.remove("engineName");
		ed.putString("savedGame", lastSave);
		ed.commit();
	}

	void load(String state)
	{
		int s = runtime.strdup(state);
		runtimeCall("jcallback_deserialise", new int[]{s});
		runtime.free(s);
		save();
	}

	void gameViewResized()
	{
		if( progress == null && gameView.w > 0 && gameView.h > 0 )
			runtimeCall("jcallback_resize", new int[]{gameView.w,gameView.h});
		else
			resizeOnDone = true;
	}

	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		games = getResources().getStringArray(R.array.games);
		gameTypes = new TreeMap<Integer,String>();
		requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
		setContentView(R.layout.main);
		gameView = new GameView(this);
		((FrameLayout)findViewById(R.id.placeholder)).addView( gameView,
				FrameLayout.LayoutParams.FILL_PARENT, FrameLayout.LayoutParams.FILL_PARENT);
		txtView = (TextView)findViewById(R.id.txtView);
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
			ViewGroup content;
			if( useGrid ) {
				content = gv = new GridView(SGTPuzzles.this);
				((GridView)gv).setNumColumns(GridView.AUTO_FIT);
				((GridView)gv).setColumnWidth(64);
			} else {
				content = new LinearLayout(SGTPuzzles.this);
				((LinearLayout)content).setOrientation(LinearLayout.VERTICAL);
				TextView tv = new TextView(SGTPuzzles.this);
				String part1 = getResources().getString(R.string.listchooser_header1);
				String part2 = getResources().getString(R.string.listchooser_header2);
				tv.setText(part1+", "+part2, TextView.BufferType.SPANNABLE);
				Spannable s = (Spannable)tv.getText();
				s.setSpan(new ForegroundColorSpan(0xFFFF0000), 0, part1.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
				s.setSpan(new ForegroundColorSpan(0xFFFFFF00), part1.length()+2, s.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
				content.addView(tv);
				gv = new ListView(SGTPuzzles.this);
				content.addView(gv);
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
				int flagId = getResources().getIdentifier("flag_"+games[i], "string", getPackageName());
				int descId = getResources().getIdentifier("desc_"+games[i], "string", getPackageName());
				String desc;
				int colour = 0;
				if( nameId > 0 ) desc = getResources().getString(nameId);
				else desc = games[i].substring(0,1).toUpperCase() + games[i].substring(1);
				if( flagId > 0 ) {
					String f = getResources().getString(flagId);
					desc += f;
					colour = f.length() > 1 ? 0xFFFF0000 :
							f.length() > 0 ? 0xFFFFFF00 :
							0;
				}
				desc += ": " + getResources().getString( descId > 0 ? descId : R.string.no_desc );
				map.put( LABEL, desc );
				Drawable d = getResources().getDrawable(getResources().getIdentifier(games[i], "drawable", getPackageName()));
				Bitmap b = Bitmap.createBitmap(d.getIntrinsicWidth(),d.getIntrinsicHeight(), Bitmap.Config.RGB_565);
				Canvas c = new Canvas(b);
				d.setBounds(0,0,d.getIntrinsicWidth(),d.getIntrinsicHeight());
				d.draw(c);
				if( colour != 0 ) {
					Log.d("chooser","painting");
					Paint p = new Paint();
					p.setColor(colour);
					p.setStyle(Paint.Style.STROKE);
					p.setStrokeWidth(8.0f);
					c.drawRect(0,0,d.getIntrinsicWidth()-1,d.getIntrinsicHeight()-1,p);
				}
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
			setContentView(content);
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
		menu.findItem(R.id.thisgame).setTitle("Help on "+this.getTitle());
		return true;
	}
	
	public boolean onOptionsItemSelected(MenuItem item)
	{
		switch(item.getItemId()) {
		case R.id.other:   showDialog(0); break;
		case R.id.newgame:
			showProgress( R.string.starting_new );
			new Thread() { public void run() {
				try {
					runtimeCall("jcallback_key_event", new int[]{ 0, 0, 'n' });
					handler.sendEmptyMessage(MsgType.DONE.ordinal());
				} catch (Exception e) {
					handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)).sendToTarget();
				}
			}}.start();
			break;
		case R.id.restart:  runtimeCall("jcallback_restart_event", new int[0]); break;
		case R.id.undo:     runtimeCall("jcallback_key_event",     new int[]{ 0, 0, 'u' }); break;
		case R.id.redo:     runtimeCall("jcallback_key_event",     new int[]{ 0, 0, 'r' }); break;
		case R.id.solve:    runtimeCall("jcallback_solve_event",   new int[0]); break;
		case R.id.custom:   runtimeCall("jcallback_config_event",  new int[]{ CFG_SETTINGS }); break;
		case R.id.specific: runtimeCall("jcallback_config_event",  new int[]{ CFG_DESC }); break;
		case R.id.seed:     runtimeCall("jcallback_config_event",  new int[]{ CFG_SEED }); break;
		case R.id.about:    runtimeCall("jcallback_about_event",   new int[0]); break;
		case R.id.contents:
			startActivity(new Intent(Intent.ACTION_VIEW,
					Uri.parse("http://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/")));
			break;
		case R.id.thisgame:
			startActivity(new Intent(Intent.ACTION_VIEW,
				Uri.parse("http://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/"+helpTopic+".html")));
			break;
		case R.id.website:
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("http://chris.boyle.name/projects/android-puzzles")));
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
				new Thread() { public void run() {
					try {
						runtimeCall("jcallback_preset_event", new int[]{id});
						handler.sendEmptyMessage(MsgType.DONE.ordinal());
					} catch (Exception e) {
						handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)+"\npreset:"+gameTypes.get(id)).sendToTarget();
					}
				}}.start();
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

	void abort()
	{
		stopRuntime(null);
		dismissProgress();
		showDialog(0);
	}

	OnCancelListener abortListener = new OnCancelListener() {
		public void onCancel(DialogInterface dialog) { abort(); }
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
		alert( "Fatal error", msg, true );
	}

	void startGame(int which, String savedGame)
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
			final String[] args = (savedGame == null) ? new String[]{ Integer.toString(which) } : new String[] { "puzzles", "-s", savedGame };
			final int which2 = which;
			final String save2 = savedGame;
			new Thread() { public void run() {
				try {
					runtime = new PuzzlesRuntime();
					runtime.setCallJavaCB(SGTPuzzles.this);
					runtime.start(args);
					if( runtime.execute() ) throw (runtime.exitException == null) ? new Exception( "Runtime exited" ) : runtime.exitException;
					helpTopic = runtime.cstring(runtimeCall("jcallback_game_htmlhelptopic", new int[0]));
					handler.sendEmptyMessage(MsgType.DONE.ordinal());
				} catch (Exception e) {
					if( ! gameRunning ) return;  // stopRuntime was called (probably user aborted)
					handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)+"\nwhich:"+Integer.toString(which2)+"\nstate:"+((save2==null)?"null":save2)).sendToTarget();
				}
			}}.start();
		} catch (Throwable t) {  // Runtime throws Errors that should really be Exceptions
			alert( "Engine failed to load", getStackTrace(t), false );
		}
	}

	public void stopRuntime(Exception e)
	{
		if( e != null && gameRunning ) die(getStackTrace(e));
		gameRunning = false;
		try { runtime.stop(); } catch(Exception e2) {}
	}

	protected void onPause()
	{
		if( gameRunning ) handler.removeMessages(MsgType.TIMER.ordinal());
		//try { dismissDialog(0); } catch( IllegalArgumentException e ) {}
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

	int runtimeCall(String func, int[] args)
	{
		if( ! gameRunning || runtime == null ) return 0;
		int s = runtime.getState();
		if( s != PuzzlesRuntime.PAUSED && s != PuzzlesRuntime.CALLJAVA ) return 0;
		try {
			return runtime.call(func, args);
		} catch (Exception e) {
			stopRuntime(e);
			return 0;
		}
	}

	void sendKey(int x, int y, int k)
	{
		if( runtimeCall("jcallback_key_event", new int[]{ x, y, k }) == 0 )
			quit(false);
	}

	public int call(int cmd, int arg1, int arg2, int arg3)
	{
		if( ! gameRunning || runtime == null ) return 0;
		try {
			switch( cmd ) {
			case 0: // initialise
				handler.obtainMessage(MsgType.INIT.ordinal(), arg2, 0,
						runtime.cstring(arg1) ).sendToTarget();
				gameView.colours = new int[arg3];
				return 0;
			case 1: // Type menu item
				gameTypes.put(arg2, runtime.cstring(arg1));
				return 0;
			case 2: // MessageBox
				handler.obtainMessage(MsgType.MESSAGEBOX.ordinal(), arg3, 0,
						new String[] {(arg1>0) ? runtime.cstring(arg1) : null,runtime.cstring(arg2)}).sendToTarget();
				// I don't think we need to wait before returning here (and we can't)
				return 0;
			case 3: { // Resize
				// Refuse this, we have a fixed size screen (except for orientation changes)
				// (wait for UI to finish doing layout first)
				if(gameView.w == 0 || gameView.h == 0 ) Thread.sleep(200);
				if(gameView.w > 0 && gameView.h > 0 )
					runtimeCall("jcallback_resize", new int[] {gameView.w,gameView.h});
				return 0; }
			case 4: // drawing tasks
				switch(arg1) {
				case 0: handler.obtainMessage(MsgType.STATUS.ordinal(),runtime.cstring(arg2)).sendToTarget(); break;
				case 1: gameView.setMargins( arg2, arg3 ); break;
				case 2: gameView.postInvalidate(); break;
				case 3: gameView.clipRect(arg2, arg3, xarg1, xarg2); break;
				case 4: gameView.unClip( arg2, arg3 ); break;
				case 5: gameView.fillRect(arg2, arg3, arg2 + xarg1, arg3 + xarg2, xarg3); break;
				case 6: gameView.drawLine(arg2, arg3, xarg1, xarg2, xarg3); break;
				case 7: path = new Path(); break;
				case 8: path.close(); gameView.drawPoly(path, arg2, arg3); break;
				case 9: gameView.drawCircle(xarg1,xarg2,xarg3,arg2,arg3); break;
				case 10: return gameView.blitterAlloc( arg2, arg3 );
				case 11: gameView.blitterFree(arg2); break;
				case 12:
					if( gameWantsTimer ) break;
					gameWantsTimer = true;
					handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal()), timerInterval);
					break;
				case 13:
					gameWantsTimer = false;
					handler.removeMessages(MsgType.TIMER.ordinal());
					break;
				}
				return 0;
			case 5: // more arguments
				xarg1 = arg1;
				xarg2 = arg2;
				xarg3 = arg3;
				return 0;
			case 6: // polygon vertex
				if( arg1 == 0 ) path.moveTo(arg2, arg3);
				else path.lineTo(arg2, arg3);
				return 0;
			case 7:
				gameView.drawText( runtime.cstring(arg3), xarg1, xarg2, arg1,
						(xarg3 & 0x10) != 0 ? Typeface.MONOSPACE : Typeface.DEFAULT, xarg3, arg2);
				return 0;
			case 8: gameView.blitterSave(arg1,arg2,arg3); return 0;
			case 9: gameView.blitterLoad(arg1,arg2,arg3); return 0;
			case 10: // dialog_init
				ScrollView sv = new ScrollView(SGTPuzzles.this);
				dialog = new AlertDialog.Builder(SGTPuzzles.this)
						.setTitle(runtime.cstring(arg1))
						.setView(sv)
						.create();
				sv.addView(dialogLayout = new TableLayout(SGTPuzzles.this));
				dialog.setOnCancelListener(new OnCancelListener() {
					public void onCancel(DialogInterface dialog) {
						runtimeCall("jcallback_config_cancel", new int[0]);
					}
				});
				dialog.setButton(getResources().getString(android.R.string.ok), new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface d, int which) {
						for( Integer i : dialogIds ) {
							View v = dialogLayout.findViewById(i);
							if( v instanceof EditText ) {
								runtimeCall("jcallback_config_set_string", new int[]{i, runtime.strdup(((EditText)v).getText().toString())});
							} else if( v instanceof CheckBox ) {
								runtimeCall("jcallback_config_set_boolean", new int[]{i, ((CheckBox)v).isChecked() ? 1 : 0});
							} else if( v instanceof Spinner ) {
								runtimeCall("jcallback_config_set_choice", new int[]{i, ((Spinner)v).getSelectedItemPosition()});
							}
						}
						dialog.dismiss();
						showProgress( R.string.starting_custom );
						gameView.clear();
						new Thread() { public void run() {
							try {
								runtimeCall("jcallback_config_ok", new int[0]);
								handler.sendEmptyMessage(MsgType.DONE.ordinal());
							} catch (Exception e) {
								handler.obtainMessage(MsgType.DIE.ordinal(),SGTPuzzles.this.getStackTrace(e)).sendToTarget();
							}
						}}.start();
					}
				});
				dialogIds = new ArrayList<Integer>();
				return 0;
			case 11: // dialog_add_control
			{
				String name = runtime.cstring(xarg3);
				switch(xarg2) {
				case C_STRING: {
					dialogIds.add(xarg1);
					EditText et = new EditText(SGTPuzzles.this);
					et.setId(xarg1);
					et.setText(runtime.cstring(arg1));
					TextView tv = new TextView(SGTPuzzles.this);
					tv.setText(name);
					TableRow tr = new TableRow(SGTPuzzles.this);
					tr.addView(tv);
					tr.addView(et);
					dialogLayout.addView(tr);
					break; }
				case C_BOOLEAN: {
					dialogIds.add(xarg1);
					CheckBox c = new CheckBox(SGTPuzzles.this);
					c.setId(xarg1);
					c.setText(name);
					c.setChecked(arg2 != 0);
					dialogLayout.addView(c);
					break; }
				case C_CHOICES: {
					String joined = runtime.cstring(arg1);
					StringTokenizer st = new StringTokenizer(joined.substring(1),joined.substring(0,1));
					ArrayList<String> choices = new ArrayList<String>();
					choices.add(name);
					while(st.hasMoreTokens()) choices.add(st.nextToken());
					dialogIds.add(xarg1);
					Spinner s = new Spinner(SGTPuzzles.this);
					s.setId(xarg1);
					ArrayAdapter<String> a = new ArrayAdapter<String>(SGTPuzzles.this,
							android.R.layout.simple_spinner_item, choices.toArray(new String[0]));
					a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
					s.setAdapter(a);
					s.setSelection(arg2);
					TextView tv = new TextView(SGTPuzzles.this);
					tv.setText(name);
					TableRow tr = new TableRow(SGTPuzzles.this);
					tr.addView(tv);
					tr.addView(s);
					dialogLayout.addView(tr);
					break; }
				}
			}
			return 0;
			case 12:
				dialogLayout.setColumnStretchable(1, true);
				dialog.show();
				return 0;
			case 13: // tick a menu item
				currentType = arg1;
				return 0;
			case 14:
				byte[] buf = new byte[arg3];
				runtime.copyin(arg2, buf, arg3);
				savingState.append(new String(buf));
				return 0;
			default:
				if (cmd >= 1024 && cmd < 2048) gameView.colours[cmd-1024] = Color.rgb(arg1, arg2, arg3);
				if (cmd == 1024)
					handler.sendEmptyMessage(MsgType.SETBG.ordinal());
				return 0;
			}
		} catch( Exception e ) {
			handler.obtainMessage(MsgType.DIE.ordinal(),getStackTrace(e)+"\nargs:"+cmd+","+arg1+","+arg2+","+arg3).sendToTarget();
			return 0;
		}
	}
}
