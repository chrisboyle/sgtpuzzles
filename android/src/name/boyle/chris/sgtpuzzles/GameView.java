package name.boyle.chris.sgtpuzzles;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Region;
import android.graphics.Typeface;
import android.os.Handler;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

class GameView extends View
{
	Handler toEngine;
	Bitmap bitmap;
	Canvas canvas;
	Paint paint;
	Bitmap[] blitters;
	int[] colours;
	int w, h;
	int longTimeout = ViewConfiguration.getLongPressTimeout();
	int button;
	boolean waiting = false;
	boolean stopDrawing = false;
	float startX, startY, maxDist = 5.0f;
	static final int DRAG = Engine.LEFT_DRAG - Engine.LEFT_BUTTON,  // not bit fields, but there's a pattern
			RELEASE = Engine.LEFT_RELEASE - Engine.LEFT_BUTTON;

	GameView(SGTPuzzles parent)
	{
		super(parent);
		canvas = new Canvas();
		paint = new Paint();
		blitters = new Bitmap[512];
	}

	Runnable sendRightClick = new Runnable() {
		public void run() {
			button = Engine.RIGHT_BUTTON;
			toEngine.obtainMessage(Messages.KEY.ordinal(), (int)startX, (int)startY,
					new Integer(button)).sendToTarget();
			waiting = false;
		}
	};

	public boolean onTouchEvent(MotionEvent event)
	{
		if( toEngine == null ) return false;
		switch( event.getAction() ) {
		case MotionEvent.ACTION_DOWN:
			int meta = event.getMetaState();
			button = ( meta & KeyEvent.META_ALT_ON ) > 0 ? Engine.MIDDLE_BUTTON :
				( meta & KeyEvent.META_SHIFT_ON ) > 0  ? Engine.RIGHT_BUTTON :
				Engine.LEFT_BUTTON;
			startX = event.getX();
			startY = event.getY();
			waiting = true;
			toEngine.postDelayed( sendRightClick, longTimeout );
			return true;
		case MotionEvent.ACTION_MOVE:
			float x = event.getX(), y = event.getY();
			if( waiting ) {
				if( Math.abs(x-startX) <= maxDist && Math.abs(y-startY) <= maxDist ) {
					return true;
				} else {
					toEngine.obtainMessage(Messages.KEY.ordinal(), (int)startX, (int)startY,
							new Integer(button)).sendToTarget();
					waiting = false;
					toEngine.removeCallbacks( sendRightClick );
				}
			}
			// DRAG is the same as KEY but the distinction allows removing all DRAGs from the queue
			toEngine.removeMessages(Messages.DRAG.ordinal());
			toEngine.obtainMessage(Messages.DRAG.ordinal(), (int)x, (int)y,
					new Integer(button + DRAG)).sendToTarget();
			return true;
		case MotionEvent.ACTION_UP:
			if( waiting ) {
				toEngine.removeCallbacks( sendRightClick );
				toEngine.obtainMessage(Messages.KEY.ordinal(), (int)startX, (int)startY,
						new Integer(button)).sendToTarget();
			}
			toEngine.obtainMessage(Messages.KEY.ordinal(), (int)event.getX(), (int)event.getY(),
				new Integer(button + RELEASE)).sendToTarget();
			toEngine.sendEmptyMessage(Messages.SAVE.ordinal());
			return true;
		default:
			return false;
		}
	}

	protected void onDraw( Canvas c )
	{
		if( bitmap == null ) return;
		c.drawBitmap(bitmap, 0, 0, null);
	}

	protected void onSizeChanged( int w, int h, int oldw, int oldh )
	{
		bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565);
		canvas.setBitmap(bitmap);
		clear();
		this.w = w; this.h = h;
		if( toEngine == null ) return;
		if( oldw != 0 ) toEngine.obtainMessage(Messages.RESIZE.ordinal(), w, h).sendToTarget();
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
	void fillRect(int x1, int y1, int x2, int y2, int colour)
	{
		if( stopDrawing ) return;
		paint.setColor(colours[colour]);
		paint.setStyle(Paint.Style.FILL);
		canvas.drawRect(x1, y1, x2, y2, paint);
	}
	void drawLine(int x1, int y1, int x2, int y2, int colour)
	{
		if( stopDrawing ) return;
		paint.setColor(colours[colour]);
		paint.setAntiAlias( true );
		canvas.drawLine(x1, y1, x2, y2, paint);
		paint.setAntiAlias( false );
	}
	void drawPoly(Path p, int lineColour, int fillColour)
	{
		if( stopDrawing ) return;
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
		if( stopDrawing ) return;
		if (fillColour != -1) {
			paint.setColor(colours[fillColour]);
			paint.setStyle(Paint.Style.FILL);
			canvas.drawOval(new RectF(x-r, y-r, x+r, y+r), paint);
		}
		paint.setColor(colours[lineColour]);
		paint.setStyle(Paint.Style.STROKE);
		canvas.drawOval(new RectF(x-r, y-r, x+r, y+r), paint);
	}
	void drawText(String text, int x, int y, int size, Typeface tf, int align, int colour)
	{
		if( stopDrawing ) return;
		paint.setColor(colours[colour]);
		paint.setTypeface( tf );
		paint.setTextSize(size);
		Paint.FontMetrics fm = paint.getFontMetrics();
		float asc = Math.abs(fm.ascent), desc = Math.abs(fm.descent);
		if ((align & Engine.ALIGN_VCENTRE) != 0) y += asc - (asc+desc)/2;
		else y += asc;
		if ((align & Engine.ALIGN_HCENTRE) != 0) paint.setTextAlign( Paint.Align.CENTER );
		else if ((align & Engine.ALIGN_HRIGHT) != 0) paint.setTextAlign( Paint.Align.RIGHT );
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
