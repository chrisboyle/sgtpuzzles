package name.boyle.chris.sgtpuzzles;

import java.util.ArrayList;
import java.util.StringTokenizer;

import org.ibex.nestedvm.Runtime;

import android.graphics.Color;
import android.graphics.Path;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;

public class Engine extends Thread implements Runtime.CallJavaCB
{
	Runtime runtime;
	GameView gameView;
	Handler handler, toParent;
	/** True when we have a critical error and want to stop servicing any
	 * (probably wrong) calls from the engine or making any calls to it */
	boolean handbrake = true;
	boolean layoutDone = false;
	boolean gameWantsTimer = false;
	int timerInterval = 20;
	int xarg1, xarg2, xarg3;
	Path path;
	StringBuffer savingState;
	String[] args = null;
	static final int CFG_SETTINGS = 0, CFG_SEED = 1, CFG_DESC = 2,
		LEFT_BUTTON = 0x0200, MIDDLE_BUTTON = 0x201, RIGHT_BUTTON = 0x202,
		LEFT_DRAG = 0x203, //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
		LEFT_RELEASE = 0x206, CURSOR_UP = 0x209, CURSOR_DOWN = 0x20a,
		CURSOR_LEFT = 0x20b, CURSOR_RIGHT = 0x20c, MOD_CTRL = 0x1000,
		MOD_SHFT = 0x2000, MOD_NUM_KEYPAD = 0x4000, ALIGN_VCENTRE = 0x100,
		ALIGN_HCENTRE = 0x001, ALIGN_HRIGHT = 0x002, C_STRING = 0,
		C_CHOICES = 1, C_BOOLEAN = 2;


	public Engine(Handler toParent, GameView gameView)
	{
		this.setName("sgtpuzzles.Engine");
		this.toParent = toParent;
		this.gameView = gameView;
	}
	
	public void load(String[] args) throws Throwable
	{
		if( handler != null ) stopRuntime(null);
		// This class will become available later, compiled by NestedVM
		runtime = (Runtime) Class.forName("name.boyle.chris.sgtpuzzles.PuzzlesRuntime").newInstance();
		runtime.setCallJavaCB(this);
		this.args = args;
	}

	public void run()
	{
		Looper.prepare();
		handler = new Handler() {
			public void handleMessage( Message msg ) {
				if( handbrake && msg.what != Messages.QUIT.ordinal() ) return;
				switch( Messages.values()[msg.what] ) {
				case QUIT:     getLooper().quit(); break;
				case RESIZE:
					gameView.stopDrawing = false;
					runtimeCall("jcallback_resize", new int[]{msg.arg1,msg.arg2});
					break;
				case TIMER:    runtimeCall("jcallback_timer_func",    new int[0]);
					if( gameWantsTimer ) sendMessageDelayed(obtainMessage(Messages.TIMER.ordinal()), timerInterval);
					break;
				case NEWGAME:  runtimeCall("jcallback_key_event",     new int[]{ 0, 0, 'n' });
					toParent.sendEmptyMessage(Messages.DONE.ordinal());
					break;
				case RESTART:  runtimeCall("jcallback_restart_event", new int[0]);
					toParent.sendEmptyMessage(Messages.DONE.ordinal());
					break;
				case SOLVE:    runtimeCall("jcallback_solve_event",   new int[0]);
					toParent.sendEmptyMessage(Messages.DONE.ordinal());
					break;
				case UNDO:     runtimeCall("jcallback_key_event",     new int[]{ 0, 0, 'u' }); break;
				case REDO:     runtimeCall("jcallback_key_event",     new int[]{ 0, 0, 'r' }); break;
				case ABOUT:    runtimeCall("jcallback_about_event",   new int[0]); break;
				case CONFIG:   runtimeCall("jcallback_config_event",  new int[]{ msg.arg1 }); break;
				case KEY: case DRAG:  // DRAG is the same as KEY but the distinction allows removing all DRAGs from the queue
					int k = ((Integer)msg.obj).intValue();
					if( runtimeCall("jcallback_key_event", new int[]{ msg.arg1, msg.arg2, k }) == 0 )
						toParent.sendEmptyMessage(Messages.QUIT.ordinal());
					break;
				case PRESET:   runtimeCall("jcallback_preset_event",  new int[]{msg.arg1});
					toParent.sendEmptyMessage(Messages.DONE.ordinal());
					break;
				case DIALOG_STRING:
					runtimeCall("jcallback_config_set_string", new int[]{msg.arg1, runtime.strdup((String)msg.obj)});
					break;
				case DIALOG_BOOL: runtimeCall("jcallback_config_set_boolean", new int[]{msg.arg1, msg.arg2}); break;
				case DIALOG_CHOICE: runtimeCall("jcallback_config_set_choice", new int[]{msg.arg1, msg.arg2}); break;
				case DIALOG_FINISH: runtimeCall("jcallback_config_ok", new int[0]);
					toParent.sendEmptyMessage(Messages.DONE.ordinal());
					break;
				case DIALOG_CANCEL: runtimeCall("jcallback_config_cancel", new int[0]); break;
				case SAVE:
					savingState = new StringBuffer();
					runtimeCall("jcallback_serialise", new int[] { 0 });
					toParent.obtainMessage(Messages.SAVED.ordinal(),savingState.toString()).sendToTarget();
					break;
				case LOAD:
					int s = runtime.strdup((String)msg.obj);
					runtimeCall("jcallback_deserialise", new int[]{s});
					runtime.free(s);
					toParent.sendEmptyMessage(Messages.DONE.ordinal());
					break;
				}
			}
		};
		gameView.toEngine = handler;
		handbrake = false;
		try {
			if( args == null ) runtime.start(); else runtime.start(args);
			if( runtime.execute() ) throw runtime.exitException;
			toParent.sendEmptyMessage(Messages.DONE.ordinal());
			Looper.loop();
		} catch (Exception e) {
			stopRuntime(e);
		}
	}
	
	public void stopRuntime(Exception e)
	{
		if( e != null && ! handbrake ) toParent.obtainMessage(Messages.DIE.ordinal(), e).sendToTarget();
		handbrake = true;
		try { runtime.stop(); } catch(Exception e2) {}
	}
	
	int runtimeCall(String func, int[] args)
	{
		if( handbrake ) return 0;
		int s = runtime.getState();
		if( s != Runtime.PAUSED && s != Runtime.CALLJAVA ) return 0;
		try {
			return runtime.call(func, args);
		} catch (Exception e) {
			stopRuntime(e);
			return 0;
		}
	}
	
	public int call(int cmd, int arg1, int arg2, int arg3)
	{
		if( handbrake ) return 0;
		try {
			switch( cmd ) {
			case 0: // initialise
				boolean hasStatus = (arg2 & 2) > 0;
				toParent.obtainMessage(Messages.INIT.ordinal(), hasStatus ? 1 : 0, 0,
						runtime.cstring(arg1) ).sendToTarget();
				if ((arg2 & 1) != 0) toParent.sendEmptyMessage(Messages.ENABLE_CUSTOM.ordinal());
				if ((arg2 & 4) != 0) toParent.sendEmptyMessage(Messages.ENABLE_SOLVE.ordinal());
				gameView.colours = new int[arg3];
				return 0;
			case 1: // Type menu item
				toParent.obtainMessage(Messages.ADDTYPE.ordinal(),arg2,0,runtime.cstring(arg1)).sendToTarget();
				return 0;
			case 2: // MessageBox
				toParent.obtainMessage(Messages.MESSAGEBOX.ordinal(), arg3, 0,
						new String[] {runtime.cstring(arg1),runtime.cstring(arg2)}).sendToTarget();
				// I don't think we need to wait before returning here
				return 0;
			case 3: { // Resize
				// Refuse this, we have a fixed size screen (except for orientation changes)
				// (wait for UI to finish doing layout first)
				while(gameView.w == 0 || gameView.h == 0 || ! layoutDone) sleep(200);
				runtimeCall("jcallback_resize", new int[] {gameView.w,gameView.h});
				return 0; }
			case 4: // drawing tasks
				switch(arg1) {
				case 0: toParent.obtainMessage(Messages.STATUS.ordinal(),runtime.cstring(arg2)).sendToTarget(); break;
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
					handler.sendMessageDelayed(handler.obtainMessage(Messages.TIMER.ordinal()), timerInterval);
					break;
				case 13:
					gameWantsTimer = false;
					handler.removeMessages(Messages.TIMER.ordinal());
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
				toParent.obtainMessage(Messages.DIALOG_INIT.ordinal(), runtime.cstring(arg1)).sendToTarget();
				return 0;
			case 11: // dialog_add_control
			{
				String name = runtime.cstring(xarg3);
				switch(xarg2) {
				case C_STRING:
					toParent.obtainMessage(Messages.DIALOG_STRING.ordinal(), xarg1, 0,
							new String[]{ name, runtime.cstring(arg1) }).sendToTarget();
					break;
				case C_BOOLEAN:
					toParent.obtainMessage(Messages.DIALOG_BOOL.ordinal(), xarg1, arg2, name).sendToTarget();
					break;
				case C_CHOICES:
					String joined = runtime.cstring(arg1);
					StringTokenizer st = new StringTokenizer(joined.substring(1),joined.substring(0,1));
					ArrayList<String> choices = new ArrayList<String>();
					choices.add(name);
					while(st.hasMoreTokens()) choices.add(st.nextToken());
					toParent.obtainMessage(Messages.DIALOG_CHOICE.ordinal(), xarg1, arg2,
							choices.toArray(new String[0])).sendToTarget();
					break;
				}
			}
			return 0;
			case 12:
				toParent.obtainMessage(Messages.DIALOG_FINISH.ordinal()).sendToTarget();
				return 0;
			case 13: // tick a menu item
				toParent.obtainMessage(Messages.SETTYPE.ordinal(), arg1, 0).sendToTarget();
				return 0;
			case 14:
				byte[] buf = new byte[arg3];
				runtime.copyin(arg2, buf, arg3);
				savingState.append(new String(buf));
				return 0;
			case 15:
				//Log.d("engine",runtime.cstring(arg1));
				return 0;
			default:
				if (cmd >= 1024 && cmd < 2048) gameView.colours[cmd-1024] = Color.rgb(arg1, arg2, arg3);
				if (cmd == 1024) toParent.sendEmptyMessage(Messages.SETBG.ordinal());
				return 0;
			}
		} catch( Exception e ) {
			stopRuntime(e);
			return 0;
		}
	}
}
