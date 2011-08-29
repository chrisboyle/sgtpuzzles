package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Region;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

public class GameView extends View
{
	SGTPuzzles parent;
	Bitmap bitmap;
	Canvas canvas;
	Paint paint;
	Bitmap[] blitters;
	int[] colours;
	int w, h;
	int longTimeout = ViewConfiguration.getLongPressTimeout();
	int button;
	boolean waiting = false, waitingSpace = false;
	double startX, startY, maxDistSq;
	static final int DRAG = SGTPuzzles.LEFT_DRAG - SGTPuzzles.LEFT_BUTTON,  // not bit fields, but there's a pattern
			RELEASE = SGTPuzzles.LEFT_RELEASE - SGTPuzzles.LEFT_BUTTON;
	static final String TAG = "GameView";
	int keysHandled = 0;  // debug

	public GameView(Context context, AttributeSet attrs)
	{
		super(context, attrs);
		this.parent = (SGTPuzzles)context;
		setFocusableInTouchMode(true);
		bitmap = Bitmap.createBitmap(100, 100, Bitmap.Config.RGB_565);  // for safety
		canvas = new Canvas(bitmap);
		paint = new Paint();
		blitters = new Bitmap[512];
		maxDistSq = Math.pow(getResources().getDisplayMetrics().density * 8.0f, 2);
	}

	Runnable sendRightClick = new Runnable() {
		public void run() {
			button = SGTPuzzles.RIGHT_BUTTON;
			waiting = false;
			performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
			parent.sendKey((int)startX, (int)startY, button);
		}
	};

	public boolean onTouchEvent(MotionEvent event)
	{
		if (! parent.gameRunning) return false;
		switch( event.getAction() ) {
		case MotionEvent.ACTION_DOWN:
			int meta = event.getMetaState();
			button = ( meta & KeyEvent.META_ALT_ON ) > 0 ? SGTPuzzles.MIDDLE_BUTTON :
				( meta & KeyEvent.META_SHIFT_ON ) > 0  ? SGTPuzzles.RIGHT_BUTTON :
				SGTPuzzles.LEFT_BUTTON;
			startX = event.getX();
			startY = event.getY();
			waiting = true;
			parent.handler.removeCallbacks( sendRightClick );
			parent.handler.postDelayed( sendRightClick, longTimeout );
			return true;
		case MotionEvent.ACTION_MOVE:
			float x = event.getX(), y = event.getY();
			if( waiting ) {
				if( Math.pow(Math.abs(x-startX),2) + Math.pow(Math.abs(y-startY),2) <= maxDistSq ) {
					return true;
				} else {
					parent.sendKey((int)startX, (int)startY, button);
					waiting = false;
					parent.handler.removeCallbacks( sendRightClick );
				}
			}
			parent.sendKey((int)x, (int)y, button + DRAG);
			return true;
		case MotionEvent.ACTION_UP:
			if( waiting ) {
				parent.handler.removeCallbacks( sendRightClick );
				parent.sendKey((int)startX, (int)startY, button);
			}
			parent.sendKey((int)event.getX(), (int)event.getY(), button + RELEASE);
			return true;
		default:
			return false;
		}
	}

	Runnable sendSpace = new Runnable() {
		public void run() {
			waitingSpace = false;
			parent.sendKey(0, 0, ' ');
		}
	};

	public boolean onKeyDown( int keyCode, KeyEvent event )
	{
		int key = 0, repeat = event.getRepeatCount();
		switch( keyCode ) {
		case KeyEvent.KEYCODE_DPAD_UP:    key = SGTPuzzles.CURSOR_UP;    break;
		case KeyEvent.KEYCODE_DPAD_DOWN:  key = SGTPuzzles.CURSOR_DOWN;  break;
		case KeyEvent.KEYCODE_DPAD_LEFT:  key = SGTPuzzles.CURSOR_LEFT;  break;
		case KeyEvent.KEYCODE_DPAD_RIGHT: key = SGTPuzzles.CURSOR_RIGHT; break;
		// dpad center auto-repeats on at least Tattoo, Hero
		case KeyEvent.KEYCODE_DPAD_CENTER:
			if (repeat > 0) return false;
			if ((event.getMetaState() & KeyEvent.META_SHIFT_ON) > 0) {
				key = ' ';
				break;
			}
			startX = startY = 0;
			waitingSpace = true;
			parent.handler.removeCallbacks( sendSpace );
			parent.handler.postDelayed( sendSpace, longTimeout );
			keysHandled++;
			return true;
		case KeyEvent.KEYCODE_ENTER: key = '\n'; break;
		case KeyEvent.KEYCODE_FOCUS: case KeyEvent.KEYCODE_SPACE: key = ' '; break;
		case KeyEvent.KEYCODE_DEL: key = '\b'; break;
		}
		// we probably don't want MOD_NUM_KEYPAD here (numbers are in a line on G1 at least)
		if( key == 0 ) key = event.getMatch("0123456789abcdefghijklqrsux".toCharArray());
		if( key == 0 ) return super.onKeyDown(keyCode, event);  // handles Back etc.
		if( event.isShiftPressed() ) key |= SGTPuzzles.MOD_SHFT;
		if( event.isAltPressed() ) key |= SGTPuzzles.MOD_CTRL;
		parent.sendKey( 0, 0, key );
		keysHandled++;
		return true;
	}

	public boolean onKeyUp( int keyCode, KeyEvent event )
	{
		if (keyCode != KeyEvent.KEYCODE_DPAD_CENTER || ! waitingSpace)
			return super.onKeyUp(keyCode, event);
		parent.handler.removeCallbacks( sendSpace );
		parent.sendKey(0, 0, '\n');
		return true;
	}

	protected void onDraw( Canvas c )
	{
		if( bitmap == null ) return;
		c.drawBitmap(bitmap, 0, 0, null);
	}

	protected void onSizeChanged( int w, int h, int oldw, int oldh )
	{
		if( w <= 0 ) w = 1;
		if( h <= 0 ) h = 1;
		bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565);
		canvas.setBitmap(bitmap);
		clear();
		this.w = w; this.h = h;
		parent.gameViewResized();
	}

	public void setBackgroundColor( int colour )
	{
		super.setBackgroundColor(colour);
		//if( canvas != null ) canvas.drawColor( colour );
	}

	public void clear()
	{
		if( canvas != null ) canvas.drawColor( Color.BLACK );
	}

	void setMargins( int x, int y )
	{
		if (x == 0 && y == 0) return;
		int w = getWidth(), h = getHeight();
		/*paint.setColor(Color.BLACK);
		paint.setStyle(Paint.Style.FILL);
		canvas.drawRect(0, 0, x, h, paint);
		canvas.drawRect(0, 0, w, y, paint);
		canvas.drawRect(w - x, 0, x, h, paint);
		canvas.drawRect(0, h - y, w, y, paint);*/
		canvas.clipRect(new Rect(x, y, w - x, h - y), Region.Op.REPLACE);
	}
	void clipRect(int x, int y, int w, int h)
	{
		canvas.clipRect(new Rect(x,y,x+w,y+h), Region.Op.REPLACE);
	}
	void unClip(int x, int y)
	{
		int w = getWidth(), h = getHeight();
		if (x == 0 && y == 0) {
			paint.setStyle(Paint.Style.FILL);
			paint.setColor(colours[0]);
			canvas.drawRect(0, 0, w, h, paint);
		} else {
			canvas.clipRect(new Rect(x, y, w - x, h - y), Region.Op.REPLACE);
		}
	}
	void fillRect(int x, int y, int w, int h, int colour)
	{
		paint.setColor(colours[colour]);
		paint.setStyle(Paint.Style.FILL);
		canvas.drawRect(x, y, x+w, y+h, paint);
	}
	void drawLine(int x1, int y1, int x2, int y2, int colour)
	{
		paint.setColor(colours[colour]);
		paint.setAntiAlias( true );
		canvas.drawLine(x1, y1, x2, y2, paint);
		paint.setAntiAlias( false );
	}
	void drawPoly(Path p, int lineColour, int fillColour)
	{
		if (fillColour != -1) {
			paint.setColor(colours[fillColour]);
			paint.setStyle(Paint.Style.FILL);
			canvas.drawPath(p, paint);
		}
		paint.setColor(colours[lineColour]);
		paint.setStyle(Paint.Style.STROKE);
		canvas.drawPath(p, paint);
	}
	void drawCircle(int x, int y, int r, int lineColour, int fillColour)
	{
		if (fillColour != -1) {
			paint.setColor(colours[fillColour]);
			paint.setStyle(Paint.Style.FILL);
			canvas.drawOval(new RectF(x-r, y-r, x+r, y+r), paint);
		}
		paint.setColor(colours[lineColour]);
		paint.setStyle(Paint.Style.STROKE);
		canvas.drawOval(new RectF(x-r, y-r, x+r, y+r), paint);
	}
	void drawText(int x, int y, int flags, int size, int colour, String text)
	{
		paint.setColor(colours[colour]);
		paint.setTypeface( (flags & SGTPuzzles.TEXT_MONO) != 0 ? Typeface.MONOSPACE : Typeface.DEFAULT );
		paint.setTextSize(size);
		Paint.FontMetrics fm = paint.getFontMetrics();
		float asc = Math.abs(fm.ascent), desc = Math.abs(fm.descent);
		if ((flags & SGTPuzzles.ALIGN_VCENTRE) != 0) y += asc - (asc+desc)/2;
		if ((flags & SGTPuzzles.ALIGN_HCENTRE) != 0) paint.setTextAlign( Paint.Align.CENTER );
		else if ((flags & SGTPuzzles.ALIGN_HRIGHT) != 0) paint.setTextAlign( Paint.Align.RIGHT );
		else paint.setTextAlign( Paint.Align.LEFT );
		paint.setAntiAlias( true );
		canvas.drawText( text, x, y, paint );
		paint.setAntiAlias( false );
	}
	int blitterAlloc(int w, int h)
	{
		for(int i=0; i<blitters.length; i++) {
			if (blitters[i] == null) {
				blitters[i] = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565);
				return i;
			}
		}
		throw new RuntimeException("No free blitter found!");
	}
	void blitterFree(int i)
	{
		if( blitters[i] == null ) return;
		blitters[i].recycle();
		blitters[i] = null;
	}
	void blitterSave(int i, int x, int y)
	{
		if( blitters[i] == null ) return;
		Canvas c = new Canvas(blitters[i]);
		c.drawBitmap(bitmap, -x, -y, null);
	}
	void blitterLoad(int i, int x, int y)
	{
		if( blitters[i] == null ) return;
		canvas.drawBitmap(blitters[i], x, y, null);
	}
}
