package name.boyle.chris.sgtpuzzles;

import android.annotation.TargetApi;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Region;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.support.annotation.NonNull;
import android.support.v4.view.GestureDetectorCompat;
import android.util.AttributeSet;
import android.util.Log;
import android.view.GestureDetector;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewConfiguration;

public class GameView extends View
{
	private GamePlay parent;
	private Bitmap bitmap;
	private final Canvas canvas;
	private final Paint paint;
	private final Bitmap[] blitters;
	int[] colours;
	int w, h;
	private final int tapTimeout = ViewConfiguration.getTapTimeout(),
			longPressTimeout = ViewConfiguration.getLongPressTimeout();
	private enum TouchState { IDLE, WAITING_TAP, WAITING_LONG_PRESS, DRAGGING, SCALING_PINCH, SCALING_DOUBLE_TAP }
	private TouchState touchState = TouchState.IDLE;
	private int button;
	private int backgroundColour;
	private boolean waitingSpace = false;
	private Point touchStart;
	private final double maxDistSq;
	static final int
			LEFT_BUTTON = 0x0200, MIDDLE_BUTTON = 0x201, RIGHT_BUTTON = 0x202,
			LEFT_DRAG = 0x203, //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
			LEFT_RELEASE = 0x206, MOD_CTRL = 0x1000,
			MOD_SHIFT = 0x2000, ALIGN_V_CENTRE = 0x100,
			ALIGN_H_CENTRE = 0x001, ALIGN_H_RIGHT = 0x002, TEXT_MONO = 0x10;
	private static final int DRAG = LEFT_DRAG - LEFT_BUTTON;  // not bit fields, but there's a pattern
    private static final int RELEASE = LEFT_RELEASE - LEFT_BUTTON;
	static final int CURSOR_UP = 0x209, CURSOR_DOWN = 0x20a,
			CURSOR_LEFT = 0x20b, CURSOR_RIGHT = 0x20c, MOD_NUM_KEYPAD = 0x4000;
	int keysHandled = 0;  // debug
	private static final char[] INTERESTING_CHARS = "0123456789abcdefghijklqrsux".toCharArray();
	final boolean hasPinchZoom;
	ScaleGestureDetector scaleDetector = null;
	GestureDetectorCompat gestureDetector;
	private float maxZoom = 30.f;  // blitter size must be scaled by this to prevent jaggies
	private Matrix zoomMatrix = new Matrix(), inverseZoomMatrix = new Matrix();

	public GameView(Context context, AttributeSet attrs)
	{
		super(context, attrs);
		if (! isInEditMode())
			this.parent = (GamePlay)context;
		bitmap = Bitmap.createBitmap(100, 100, Bitmap.Config.RGB_565);  // for safety
		canvas = new Canvas(bitmap);
		paint = new Paint();
		paint.setStrokeWidth(1.f);  // will be scaled with everything else as long as it's non-zero
		blitters = new Bitmap[512];
		maxDistSq = Math.pow(ViewConfiguration.get(context).getScaledTouchSlop(), 2);
		backgroundColour = getDefaultBackgroundColour();
		gestureDetector = new GestureDetectorCompat(getContext(), new GestureDetector.SimpleOnGestureListener() {
			@Override
			public boolean onDown(MotionEvent event) {
				Log.d(GamePlay.TAG, "onDown");
				touchState = TouchState.WAITING_TAP;
				int meta = event.getMetaState();
				button = ( meta & KeyEvent.META_ALT_ON ) > 0 ? MIDDLE_BUTTON :
						( meta & KeyEvent.META_SHIFT_ON ) > 0  ? RIGHT_BUTTON :
								LEFT_BUTTON;
				touchStart = pointFromEvent(event);
				parent.handler.removeCallbacks(setPressed);
				parent.handler.postDelayed(setPressed, tapTimeout);
				return true;
			}

			@Override
			public boolean onDoubleTapEvent(MotionEvent e) {
				Log.d(GamePlay.TAG, "onDoubleTapEvent");
				parent.handler.removeCallbacks(setPressed);
				parent.handler.removeCallbacks(sendLongPress);
				touchState = TouchState.SCALING_DOUBLE_TAP;
				return true;
			}

			@Override
			public boolean onSingleTapConfirmed(MotionEvent event) {
				Log.d(GamePlay.TAG, "onSingleTapConfirmed");
				parent.sendKey(viewToGame(touchStart), button);
				parent.sendKey(viewToGame(pointFromEvent(event)), button + RELEASE);
				return true;
			}

			@Override
			public boolean onScroll(MotionEvent downEvent, MotionEvent event, float distanceX, float distanceY) {
				if (hasPinchZoom && isScaleInProgress()) {
					if (touchState == TouchState.SCALING_DOUBLE_TAP) {
						return false;
					} else if (touchState == TouchState.DRAGGING) {
						// try to drag back to start
						Point p = viewToGame(touchStart);
						parent.sendKey(p, button + DRAG);
						parent.sendKey(p, button + RELEASE);
						Log.d(GamePlay.TAG, "scale started");
					} else if (touchState == TouchState.WAITING_TAP || touchState == TouchState.WAITING_LONG_PRESS) {
						parent.handler.removeCallbacks(setPressed);
						parent.handler.removeCallbacks(sendLongPress);
						Log.d(GamePlay.TAG, "scale started");
					}
					touchState = TouchState.SCALING_PINCH;
					scrollBy(distanceX, distanceY);
					return true;
				} else if (event.getPointerCount() > 1) {  // 2 fingers but not moved together/apart
					Log.d(GamePlay.TAG, "scroll only");
					scrollBy(distanceX, distanceY);
					return true;
				}
				float x = event.getX(), y = event.getY();
				if (touchState == TouchState.WAITING_LONG_PRESS && movedPastTouchSlop(x, y)) {
					Log.d(GamePlay.TAG, "drag start");
					parent.sendKey(viewToGame(touchStart), button);
					parent.handler.removeCallbacks(sendLongPress);
					touchState = TouchState.DRAGGING;
				}
				if (touchState == TouchState.DRAGGING) {
					Log.d(GamePlay.TAG, "drag");
					parent.sendKey(viewToGame(pointFromEvent(event)), button + DRAG);
					return true;
				}
				return false;
			}

			// TODO add fling detection; onFling doesn't happen with two fingers
		});
		// We do our own long-press detection to capture movement afterwards
		gestureDetector.setIsLongpressEnabled(false);
		hasPinchZoom = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.FROYO);
		if (hasPinchZoom) {
			enablePinchZoom();
		}
	}

	private void scrollBy(float distanceX, float distanceY) {
		Log.d(GamePlay.TAG, "scroll");
		zoomMatrix.postTranslate(-distanceX, -distanceY);
		zoomMatrixUpdated();
		forceRedraw();
	}

	private void zoomMatrixUpdated() {
		// Constrain scrolling to game bounds
		invertZoomMatrix();  // needed for viewToGame
		final Point topLeft = viewToGame(new Point(0, 0));
		final Point bottomRight = viewToGame(new Point(w, h));
		// TODO trigger EdgeEffectCompat animation on appropriate edge(s)
		if (topLeft.x < 0) {
			zoomMatrix.postTranslate(topLeft.x, 0);
		} else if (bottomRight.x > w) {
			zoomMatrix.postTranslate(bottomRight.x - w, 0);
		}
		if (topLeft.y < 0) {
			zoomMatrix.postTranslate(0, topLeft.y);
		} else if (bottomRight.y > h) {
			zoomMatrix.postTranslate(0, bottomRight.y - h);
		}
		canvas.setMatrix(zoomMatrix);
		invertZoomMatrix();  // now with our changes
	}

	private boolean movedPastTouchSlop(float x, float y) {
		return Math.pow(Math.abs(x - touchStart.x), 2)
				+ Math.pow(Math.abs(y - touchStart.y) ,2)
				> maxDistSq;
	}

	@TargetApi(Build.VERSION_CODES.FROYO)
	private boolean isScaleInProgress() {
		return scaleDetector.isInProgress();
	}

	@TargetApi(Build.VERSION_CODES.FROYO)
	private void enablePinchZoom() {
		scaleDetector = new ScaleGestureDetector(getContext(), new ScaleGestureDetector.SimpleOnScaleGestureListener() {
			@Override
			public boolean onScale(ScaleGestureDetector detector) {
				float factor = detector.getScaleFactor();
				Log.d(GamePlay.TAG, "scale! " + factor
						+ " @" + detector.getFocusX() + "," + detector.getFocusY());
				final float scale = getXScale(zoomMatrix);
				final float nextScale = scale * factor;
				if (nextScale < 1.0f) {
					factor /= nextScale;
				} else if (nextScale > maxZoom) {
					factor *= maxZoom/nextScale;
				}
				zoomMatrix.postScale(factor, factor, detector.getFocusX(), detector.getFocusY());
				zoomMatrixUpdated();
				forceRedraw();
				return true;
			}
		});
	}

	private void invertZoomMatrix() {
		if (!zoomMatrix.invert(inverseZoomMatrix)) {
			throw new RuntimeException("zoom not invertible");
		}
	}

	private void forceRedraw() {
		canvas.setMatrix(zoomMatrix);
		if (parent != null) {
			clear();
			parent.forceRedraw();
		}
		invalidate();
	}

	private float getXScale(Matrix m) {
		float[] values = new float[9];
		m.getValues(values);
		return values[Matrix.MSCALE_X];
	}

	Point pointFromEvent(MotionEvent event) {
		return new Point(Math.round(event.getX()), Math.round(event.getY()));
	}

	Point viewToGame(Point point) {
		float[] f = { point.x, point.y };
		inverseZoomMatrix.mapPoints(f);
		return new Point(Math.round(f[0]), Math.round(f[1]));
	}

	@TargetApi(Build.VERSION_CODES.FROYO)
	private boolean checkPinchZoom(MotionEvent event) {
		return scaleDetector.onTouchEvent(event);
	}

	private final Runnable setPressed = new Runnable() {
		public void run() {
			Log.d(GamePlay.TAG, "setPressed");
			if (hasPinchZoom && isScaleInProgress()) return;
			touchState = TouchState.WAITING_LONG_PRESS;
			parent.handler.removeCallbacks(sendLongPress);
			parent.handler.postDelayed(sendLongPress, longPressTimeout);
		}
	};

	private final Runnable sendLongPress = new Runnable() {
		public void run() {
			Log.d(GamePlay.TAG, "sendLongPress");
			if (hasPinchZoom && isScaleInProgress()) return;
			button = RIGHT_BUTTON;
			touchState = TouchState.DRAGGING;
			performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
			parent.sendKey(viewToGame(touchStart), button);
		}
	};

	@Override
	public boolean onTouchEvent(@NonNull MotionEvent event)
	{
		if (parent.currentBackend == null) return false;
		boolean sdRet = hasPinchZoom && checkPinchZoom(event);
		boolean gdRet = gestureDetector.onTouchEvent(event);
		if (event.getAction() == MotionEvent.ACTION_UP) {
			Log.d(GamePlay.TAG, "onUp");
			parent.handler.removeCallbacks(setPressed);
			parent.handler.removeCallbacks(sendLongPress);
			if (touchState == TouchState.WAITING_TAP && movedPastTouchSlop(event.getX(), event.getY())) {
				parent.sendKey(viewToGame(touchStart), button);
			}
			if (touchState == TouchState.WAITING_TAP || touchState == TouchState.DRAGGING) {
				parent.sendKey(viewToGame(pointFromEvent(event)), button + RELEASE);
			}
			touchState = TouchState.IDLE;
			return true;
		} else {
			return sdRet || gdRet;
		}
	}

	private final Runnable sendSpace = new Runnable() {
		public void run() {
			waitingSpace = false;
			parent.sendKey(0, 0, ' ');
		}
	};

	@Override
	public boolean onKeyDown(int keyCode, @NonNull KeyEvent event)
	{
		int key = 0, repeat = event.getRepeatCount();
		switch( keyCode ) {
		case KeyEvent.KEYCODE_DPAD_UP:    key = CURSOR_UP;    break;
		case KeyEvent.KEYCODE_DPAD_DOWN:  key = CURSOR_DOWN;  break;
		case KeyEvent.KEYCODE_DPAD_LEFT:  key = CURSOR_LEFT;  break;
		case KeyEvent.KEYCODE_DPAD_RIGHT: key = CURSOR_RIGHT; break;
		// dpad center auto-repeats on at least Tattoo, Hero
		case KeyEvent.KEYCODE_DPAD_CENTER:
			if (repeat > 0) return false;
			if ((event.getMetaState() & KeyEvent.META_SHIFT_ON) > 0) {
				key = ' ';
				break;
			}
			touchStart = new Point(0, 0);
			waitingSpace = true;
			parent.handler.removeCallbacks( sendSpace );
			parent.handler.postDelayed( sendSpace, longPressTimeout);
			keysHandled++;
			return true;
		case KeyEvent.KEYCODE_ENTER: key = '\n'; break;
		case KeyEvent.KEYCODE_FOCUS: case KeyEvent.KEYCODE_SPACE: key = ' '; break;
		case KeyEvent.KEYCODE_DEL: key = '\b'; break;
		}
		// we probably don't want MOD_NUM_KEYPAD here (numbers are in a line on G1 at least)
		if( key == 0 ) key = event.getMatch(INTERESTING_CHARS);
		if( key == 0 ) return super.onKeyDown(keyCode, event);  // handles Back etc.
		if( event.isShiftPressed() ) key |= MOD_SHIFT;
		if( event.isAltPressed() ) key |= MOD_CTRL;
		parent.sendKey( 0, 0, key );
		keysHandled++;
		return true;
	}

	@Override
	public boolean onKeyUp( int keyCode, KeyEvent event )
	{
		if (keyCode != KeyEvent.KEYCODE_DPAD_CENTER || ! waitingSpace)
			return super.onKeyUp(keyCode, event);
		parent.handler.removeCallbacks( sendSpace );
		parent.sendKey(0, 0, '\n');
		return true;
	}

	@Override
	protected void onDraw( Canvas c )
	{
		if( bitmap == null ) return;
		c.drawBitmap(bitmap, 0, 0, null);
	}

	@Override
	protected void onSizeChanged(int w, int h, int oldW, int oldH)
	{
		if( w <= 0 ) w = 1;
		if( h <= 0 ) h = 1;
		bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565);
		clear();
		canvas.setBitmap(bitmap);
		this.w = w; this.h = h;
		zoomMatrixUpdated();
		if (parent != null) parent.gameViewResized();
		if (isInEditMode()) {
			// Draw a little placeholder to aid UI editing
			Drawable d = getResources().getDrawable(R.drawable.net);
			int s = w<h ? w : h;
			int mx = (w-s)/2, my = (h-s)/2;
			d.setBounds(new Rect(mx,my,mx+s,my+s));
			d.draw(canvas);
		}
	}

	public void clear()
	{
		bitmap.eraseColor(backgroundColour);
	}

	@Override
	public void setBackgroundColor(int colour) {
		super.setBackgroundColor(colour);
		backgroundColour = colour;
	}

	@UsedByJNI
	int getDefaultBackgroundColour() {
		return getResources().getColor(R.color.game_background);
	}

	@UsedByJNI
	void setMargins( int x, int y )
	{
		if (x == 0 && y == 0) return;
		int w = getWidth(), h = getHeight();
		canvas.clipRect(new Rect(x, y, w - x - 1, h - y - 1), Region.Op.REPLACE);
	}

	@UsedByJNI
	void clipRect(int x, int y, int w, int h)
	{
		canvas.clipRect(new Rect(x,y,x+w,y+h), Region.Op.REPLACE);
	}

	@UsedByJNI
	void unClip(int x, int y)
	{
		int w = getWidth(), h = getHeight();
		if (x == 0 && y == 0) {
			paint.setStyle(Paint.Style.FILL);
			paint.setColor(colours[0]);
			canvas.drawRect(0, 0, w, h, paint);
		} else {
			canvas.clipRect(new Rect(x, y, w - x - 1, h - y - 1), Region.Op.REPLACE);
		}
	}

	@UsedByJNI
	void fillRect(int x, int y, int w, int h, int colour)
	{
		paint.setColor(colours[colour]);
		paint.setStyle(Paint.Style.FILL);
		canvas.drawRect(x, y, x+w, y+h, paint);
	}

	@UsedByJNI
	void drawLine(int x1, int y1, int x2, int y2, int colour)
	{
		paint.setColor(colours[colour]);
		paint.setAntiAlias( true );
		canvas.drawLine(x1, y1, x2, y2, paint);
		paint.setAntiAlias( false );
	}

	@UsedByJNI
	void drawPoly(int[] points, int ox, int oy, int line, int fill)
	{
		Path path = new Path();
		path.moveTo(points[0] + ox, points[1] + oy);
		for( int i=1; i < points.length/2; i++ )
			path.lineTo(points[2 * i] + ox, points[2 * i + 1] + oy);
		path.close();
		drawPoly(path, line, fill);
	}

	private void drawPoly(Path p, int lineColour, int fillColour)
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

	@UsedByJNI
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

	@UsedByJNI
	void drawText(int x, int y, int flags, int size, int colour, String text)
	{
		paint.setColor(colours[colour]);
		paint.setStyle(Paint.Style.FILL);
		paint.setTypeface( (flags & TEXT_MONO) != 0 ? Typeface.MONOSPACE : Typeface.DEFAULT );
		paint.setTextSize(size);
		Paint.FontMetrics fm = paint.getFontMetrics();
		float asc = Math.abs(fm.ascent), desc = Math.abs(fm.descent);
		if ((flags & ALIGN_V_CENTRE) != 0) y += asc - (asc+desc)/2;
		if ((flags & ALIGN_H_CENTRE) != 0) paint.setTextAlign( Paint.Align.CENTER );
		else if ((flags & ALIGN_H_RIGHT) != 0) paint.setTextAlign( Paint.Align.RIGHT );
		else paint.setTextAlign( Paint.Align.LEFT );
		paint.setAntiAlias( true );
		canvas.drawText( text, x, y, paint );
		paint.setAntiAlias( false );
	}

	@UsedByJNI
	int blitterAlloc(int w, int h)
	{
		for(int i=0; i<blitters.length; i++) {
			if (blitters[i] == null) {
				blitters[i] = Bitmap.createBitmap(Math.round(maxZoom * w), Math.round(maxZoom * h), Bitmap.Config.RGB_565);
				return i;
			}
		}
		throw new RuntimeException("No free blitter found!");
	}

	@UsedByJNI
	void blitterFree(int i)
	{
		if( blitters[i] == null ) return;
		blitters[i].recycle();
		blitters[i] = null;
	}

	@UsedByJNI
	void blitterSave(int i, int x, int y)
	{
		if( blitters[i] == null ) return;
		Canvas c = new Canvas(blitters[i]);
		Matrix m = new Matrix(inverseZoomMatrix);
		m.postTranslate(-x, -y);
		m.postScale(maxZoom, maxZoom);
		c.drawBitmap(bitmap, m, null);
	}

	@UsedByJNI
	void blitterLoad(int i, int x, int y)
	{
		if( blitters[i] == null ) return;
		Matrix m = new Matrix();
		m.postScale(1/maxZoom, 1/maxZoom);
		m.postTranslate(x, y);
		canvas.drawBitmap(blitters[i], m, null);
	}
}
