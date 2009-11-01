package name.boyle.chris.sgtpuzzles;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.util.ArrayList;
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
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TableLayout;
import android.widget.TableRow;
import android.widget.TextView;
import android.widget.AdapterView.OnItemClickListener;

public class SGTPuzzles extends Activity
{
	ProgressDialog progress;
	TextView txtView;
	GameView gameView;
	Engine engine;
	String[] games;
	String engineName;
	SubMenu typeMenu;
	TreeMap<Integer,String> gameTypes;
	int currentType = 0;
	boolean solveEnabled = false, customVisible = false, dead = false;
	String lastSave;
	SharedPreferences prefs;

	Handler handler = new Handler() {
		AlertDialog dialog;
		ArrayList<Integer> dialogIds;
		TableLayout dialogLayout;
		public void handleMessage( Message msg ) {
			switch(Messages.values()[msg.what]) {
			case QUIT: quit(); break;
			case DIE: die(getStackTrace((Throwable)msg.obj)); break;
			case INIT:
				setTitle( (String)msg.obj );
				txtView.setVisibility( msg.arg1 > 0 ? View.VISIBLE : View.GONE );
				engine.layoutDone = true;
				break;
			case SETBG:  gameView.setBackgroundColor(gameView.colours[0]); break;
			case STATUS:
				String status = (String)msg.obj;
				if( status.length() == 0 ) status = " ";
				txtView.setText(status);
				break;
			case DONE:
				if( progress != null ) progress.dismiss();
				engine.handler.sendEmptyMessage(Messages.SAVE.ordinal());
				break;
			case MESSAGEBOX: {
				if( progress != null ) progress.dismiss();
				String[] strings = (String[]) msg.obj;
				new AlertDialog.Builder(SGTPuzzles.this)
						.setTitle( strings[0] )
						.setMessage( strings[1] )
						.setIcon( ( msg.arg1 == 0 )
								? android.R.drawable.ic_dialog_info
								: android.R.drawable.ic_dialog_alert )
						.show();
				break; }
			case DIALOG_INIT:
				ScrollView sv = new ScrollView(SGTPuzzles.this);
				dialog = new AlertDialog.Builder(SGTPuzzles.this)
						.setTitle((String)msg.obj)
						.setView(sv)
						.create();
				sv.addView(dialogLayout = new TableLayout(SGTPuzzles.this));
				dialog.setOnCancelListener(new OnCancelListener() {
					public void onCancel(DialogInterface dialog) {
						engine.handler.sendEmptyMessage(Messages.DIALOG_CANCEL.ordinal());
					}
				});
				dialog.setButton(getResources().getString(android.R.string.ok), new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface d, int which) {
						for( Integer i : dialogIds ) {
							View v = dialogLayout.findViewById(i);
							if( v instanceof EditText ) {
								engine.handler.obtainMessage(Messages.DIALOG_STRING.ordinal(), i, 0,
										((EditText)v).getText().toString()).sendToTarget();
							} else if( v instanceof CheckBox ) {
								engine.handler.obtainMessage(Messages.DIALOG_BOOL.ordinal(), i,
										((CheckBox)v).isChecked() ? 1 : 0).sendToTarget();
							} else if( v instanceof Spinner ) {
								engine.handler.obtainMessage(Messages.DIALOG_CHOICE.ordinal(), i,
										((Spinner)v).getSelectedItemPosition()).sendToTarget();
							}
						}
						dialog.dismiss();
						progress = ProgressDialog.show( SGTPuzzles.this, null, getResources().getString(R.string.starting_custom),
								true, true, quitListener );
						engine.handler.sendEmptyMessage(Messages.DIALOG_FINISH.ordinal());
					}
				});
				dialogIds = new ArrayList<Integer>();
				break;
			case DIALOG_STRING: {
				String[] s = (String[])msg.obj;
				dialogIds.add(msg.arg1);
				EditText et = new EditText(SGTPuzzles.this);
				et.setId(msg.arg1);
				et.setText(s[1]);
				TextView tv = new TextView(SGTPuzzles.this);
				tv.setText(s[0]);
				TableRow tr = new TableRow(SGTPuzzles.this);
				tr.addView(tv);
				tr.addView(et);
				dialogLayout.addView(tr);
				break; }
			case DIALOG_BOOL: {
				dialogIds.add(msg.arg1);
				CheckBox c = new CheckBox(SGTPuzzles.this);
				c.setId(msg.arg1);
				c.setText((String)msg.obj);
				c.setChecked(msg.arg2 != 0);
				dialogLayout.addView(c);
				break; }
			case DIALOG_CHOICE: {
				dialogIds.add(msg.arg1);
				Spinner s = new Spinner(SGTPuzzles.this);
				s.setId(msg.arg1);
				String[] nameAndChoices = (String[])msg.obj;
				String[] choices = new String[nameAndChoices.length - 1];
				System.arraycopy(nameAndChoices, 1, choices, 0, choices.length);
				ArrayAdapter<String> a = new ArrayAdapter<String>(SGTPuzzles.this,
						android.R.layout.simple_spinner_item, choices);
				a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
				s.setAdapter(a);
				s.setSelection(msg.arg2);
				TextView tv = new TextView(SGTPuzzles.this);
				tv.setText(nameAndChoices[0]);
				TableRow tr = new TableRow(SGTPuzzles.this);
				tr.addView(tv);
				tr.addView(s);
				dialogLayout.addView(tr);
				break; }
			case DIALOG_FINISH:
				dialogLayout.setColumnStretchable(1, true);
				dialog.show();
				break;
			case ENABLE_SOLVE: solveEnabled = true; break;
			case ENABLE_CUSTOM: customVisible = true; break;
			case ADDTYPE: gameTypes.put(msg.arg1, (String)msg.obj); break;
			case SETTYPE: currentType = msg.arg1; break;
			case SAVED:
				String save = (String)msg.obj;
				if( save.length() > 0 ) lastSave = save;
				break;
			}
		}
	};

	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);
		games = getResources().getStringArray(R.array.games);
		gameTypes = new TreeMap<Integer,String>();
		requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
		setContentView(R.layout.main);
		gameView = new GameView(this);
		engine = new Engine(handler, gameView);
		((FrameLayout)findViewById(R.id.placeholder)).addView( gameView,
				new ViewGroup.LayoutParams(ViewGroup.LayoutParams.FILL_PARENT,ViewGroup.LayoutParams.FILL_PARENT));
		txtView = (TextView)findViewById(R.id.txtView);
		setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT);

		prefs = getSharedPreferences("state", MODE_PRIVATE);
		if( prefs.contains("savedGame") ) {
			String name = prefs.getString("engineName", games[0]);
			startGame(name, new String[]{ name, "-s", prefs.getString("savedGame","")});
		} else {
			new AlertDialog.Builder(this)
				.setMessage(R.string.welcome)
				.setOnCancelListener(quitListener)
				.setPositiveButton(android.R.string.yes, new DialogInterface.OnClickListener() {
					public void onClick(DialogInterface d, int which) { showDialog(0); }})
				.show();
		}
	}

	public void onConfigurationChanged(Configuration newConfig)
	{
		//TODO: gameView.stopDrawing = true;  // engine must reset this reliably
		super.onConfigurationChanged(newConfig);
	}

	public Dialog onCreateDialog(int id)
	{
		Dialog d = new Dialog(this);
		d.setTitle(R.string.chooser_title);
		d.setCancelable(true);
		GridView gv = new GridView(this);
		gv.setNumColumns(GridView.AUTO_FIT);
		gv.setColumnWidth(50);
		gv.setOnItemClickListener(new OnItemClickListener() {
			public void onItemClick(AdapterView<?> arg0, View arg1, int which, long arg3) {
				String savedEngine = games[which];
				startGame(savedEngine, new String[]{ savedEngine });
				SGTPuzzles.this.dismissDialog(0);
			}
		});
		gv.setAdapter(new ArrayAdapter<String>(SGTPuzzles.this, 0, games) {
			public View getView(int position, View convertView, ViewGroup parent) {
				ImageView i;
				if (convertView == null) {
					i = new ImageView(getContext());
					i.setScaleType(ImageView.ScaleType.CENTER_INSIDE);
					i.setPadding(4, 4, 4, 4);
				} else {
					i = (ImageView) convertView;
				}
				i.setImageDrawable(getResources().getDrawable(
						getResources().getIdentifier(games[position]+"_48d24", "drawable",
								SGTPuzzles.this.getPackageName())));
				return i;
			}
		});
		d.setContentView(gv);
		return d;
	}
	
	public void onPrepareDialog(int id, Dialog d)
	{
		d.setOnCancelListener(engine.isAlive() ? null : quitListener);
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
		Messages m;
		int arg1 = 0;
		// Are we affecting game state without triggering a DONE message?
		boolean needSave = false;
		switch(item.getItemId()) {
		case R.id.other:   showDialog(0); return true;
		case R.id.newgame:
			progress = ProgressDialog.show( this, null, getResources().getString(R.string.starting_new), true, true, quitListener );
			m = Messages.NEWGAME;
			break;
		case R.id.restart:  m = Messages.RESTART; needSave = true; break;
		case R.id.undo:     m = Messages.UNDO; needSave = true; break;
		case R.id.redo:     m = Messages.REDO; needSave = true; break;
		case R.id.solve:    m = Messages.SOLVE; break;
		case R.id.custom:   m = Messages.CONFIG; arg1 = Engine.CFG_SETTINGS; break;
		case R.id.specific: m = Messages.CONFIG; arg1 = Engine.CFG_DESC; break;
		case R.id.seed:     m = Messages.CONFIG; arg1 = Engine.CFG_SEED; break;
		case R.id.contents:
			startActivity(new Intent(Intent.ACTION_VIEW,
					Uri.parse("http://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/")));
			return true;
		case R.id.thisgame:
			startActivity(new Intent(Intent.ACTION_VIEW,
				Uri.parse("http://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/"+engineName+".html")));
			return true;
		case R.id.website:
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("http://chris.boyle.name/projects/android-puzzles")));
			return true;
		case R.id.email:
			// Damn, Android Email doesn't cope with ?subject=foo yet.
			/*String vName;
			try {
				vName = getPackageManager().getPackageInfo(getPackageName(),0).versionName;
			} catch(Exception e) { die(e.toString()); return true; }*/
			startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("mailto:chris@boyle.name")));
			return true;
		case R.id.about: m = Messages.ABOUT; break;
		/*case R.id.load:
			// TODO: load
			return true;
		case R.id.save:
			// TODO: save
			return true;*/
		default:
			int id = item.getItemId();
			if( gameTypes.get(id) != null ) {
				progress = ProgressDialog.show( this, null, getResources().getString(R.string.changing_type), true, true, quitListener );
				engine.handler.obtainMessage(Messages.PRESET.ordinal(),id,0).sendToTarget();
			}
			else super.onOptionsItemSelected(item);
			return true;
		}
		if( arg1 == 0 ) engine.handler.sendEmptyMessage(m.ordinal());
		else engine.handler.obtainMessage(m.ordinal(),arg1,0).sendToTarget();
		if( needSave ) engine.handler.sendEmptyMessage(Messages.SAVE.ordinal());
		return true;
	}

	void quit()
	{
		engine.stopRuntime(null);
		if( engine.isAlive() && engine.handler != null ) engine.handler.sendEmptyMessage(Messages.QUIT.ordinal());
		finish();
	}
	
	OnCancelListener quitListener = new OnCancelListener() {
		public void onCancel(DialogInterface dialog) { quit(); }
	};

	String getStackTrace( Throwable t )
	{
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		t.printStackTrace(new PrintStream(b,true));
		return b.toString();
	}

	void alert( String title, String msg, boolean fatal )
	{
		if( progress != null ) progress.dismiss();
		AlertDialog.Builder b = new AlertDialog.Builder(this)
			.setTitle( title )
			.setMessage( msg )
			.setIcon(android.R.drawable.ic_dialog_alert)
			.setOnCancelListener( fatal ? quitListener :
					new OnCancelListener() { public void onCancel(DialogInterface d) {
						if( progress != null ) progress.dismiss();
						showDialog(0);
					}});
		if( fatal ) {
			b.setPositiveButton(android.R.string.yes, new DialogInterface.OnClickListener() {
				public void onClick(DialogInterface d, int which) { d.cancel(); }});
			b.setNegativeButton(android.R.string.no, new DialogInterface.OnClickListener() {
				public void onClick(DialogInterface d, int which) {
					SharedPreferences.Editor ed = prefs.edit();
					ed.clear();
					ed.commit();
					d.cancel();
				}
			});
		}
		b.show();
	}

	void die( String msg )
	{
		if( dead ) return;
		dead = true;
		alert( "Died. Keep saved game?", msg, true );
	}

	void startGame(String engineName, String[] args)
	{
		progress = ProgressDialog.show( this, null, getResources().getString( (args.length < 2)
				? R.string.starting : R.string.resuming), true, true, quitListener );
		try {
			if( engine.isAlive() ) {
				gameView.clear();
				engine.stopRuntime(null);
				if( engine.handler != null ) engine.handler.sendEmptyMessage(Messages.QUIT.ordinal());
				solveEnabled = false;
				customVisible = false;
				txtView.setVisibility( View.GONE );
				engine = new Engine(handler, gameView);
			}
			if( typeMenu != null ) for( Integer i : gameTypes.keySet() ) typeMenu.removeItem(i);
			gameTypes.clear();
			engine.load(args);
			this.engineName = engineName;
			engine.start();
		} catch (Throwable t) {  // Runtime throws Errors that should really be Exceptions
			alert( "Engine failed to load", getStackTrace(t), false );
		}
	}
	
	protected void onPause()
	{
		if( engine.handler != null ) engine.handler.removeMessages(Messages.TIMER.ordinal());
		try { dismissDialog(0); } catch( IllegalArgumentException e ) {}
		super.onPause();
		if( lastSave == null ) return;
		SharedPreferences.Editor ed = prefs.edit();
		ed.putString("engineName", engineName);
		ed.putString("savedGame", lastSave);
		ed.commit();
	}

	protected void onDestroy()
	{
		quit();
		super.onDestroy();
	}

	public void onWindowFocusChanged( boolean f )
	{
		if( f && engine.gameWantsTimer && engine.handler != null
				&& ! engine.handler.hasMessages(Messages.TIMER.ordinal() ) )
			engine.handler.sendMessageDelayed(engine.handler.obtainMessage(Messages.TIMER.ordinal()),
					engine.timerInterval);
	}
	
	public boolean onKeyDown( int keyCode, KeyEvent event )
	{
		int key;
		switch( keyCode ) {
		case KeyEvent.KEYCODE_DPAD_UP:    key = Engine.CURSOR_UP;    break;
		case KeyEvent.KEYCODE_DPAD_DOWN:  key = Engine.CURSOR_DOWN;  break;
		case KeyEvent.KEYCODE_DPAD_LEFT:  key = Engine.CURSOR_LEFT;  break;
		case KeyEvent.KEYCODE_DPAD_RIGHT: key = Engine.CURSOR_RIGHT; break;
		case KeyEvent.KEYCODE_0: case KeyEvent.KEYCODE_1: case KeyEvent.KEYCODE_2: case KeyEvent.KEYCODE_3:
		case KeyEvent.KEYCODE_4: case KeyEvent.KEYCODE_5: case KeyEvent.KEYCODE_6: case KeyEvent.KEYCODE_7:
		case KeyEvent.KEYCODE_8: case KeyEvent.KEYCODE_9:
			// we probably don't want Engine.MOD_NUM_KEYPAD here (numbers are in a line on G1 at least)
			key = event.getNumber();
			break;
		case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: key = '\n'; break;
		case KeyEvent.KEYCODE_FOCUS: case KeyEvent.KEYCODE_SPACE: key = ' '; break;
		case KeyEvent.KEYCODE_Q: key = 'q'; break;
		default: return super.onKeyDown(keyCode, event);
		}
		if( event.isShiftPressed() ) key |= Engine.MOD_SHFT;
		if( event.isAltPressed() ) key |= Engine.MOD_CTRL;
		if( engine.handler != null ) {
			engine.handler.obtainMessage(Messages.KEY.ordinal(), new Integer(key)).sendToTarget();
			engine.handler.sendEmptyMessage(Messages.SAVE.ordinal());
		}
		return true;
	}
}
