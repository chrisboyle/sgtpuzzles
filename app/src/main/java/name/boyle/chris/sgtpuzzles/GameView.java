package name.boyle.chris.sgtpuzzles;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapShader;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.os.Build;

import androidx.annotation.ColorInt;
import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.core.content.ContextCompat;
import androidx.core.view.GestureDetectorCompat;
import androidx.core.view.ScaleGestureDetectorCompat;
import androidx.core.view.ViewCompat;
import android.util.AttributeSet;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.GestureDetector;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewConfiguration;
import android.widget.EdgeEffect;
import android.widget.OverScroller;

import static androidx.core.view.MotionEventCompat.isFromSource;
import static android.view.InputDevice.SOURCE_MOUSE;
import static android.view.InputDevice.SOURCE_STYLUS;
import static android.view.MotionEvent.TOOL_TYPE_STYLUS;

import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;

public class GameView extends View implements GameEngine.ViewCallbacks
{
	private GamePlay parent;
	private Bitmap bitmap;
	private Canvas canvas;
	private int canvasRestoreJustAfterCreation;
	private final Paint paint;
	private final Paint checkerboardPaint = new Paint();
	private final Bitmap[] blitters;
	@ColorInt private int[] colours = new int[0];
	private float density = 1.f;
	enum LimitDPIMode { LIMIT_OFF, LIMIT_AUTO, LIMIT_ON }
	LimitDPIMode limitDpi = LimitDPIMode.LIMIT_AUTO;
	int w, h, wDip, hDip;
	private final int longPressTimeout = ViewConfiguration.getLongPressTimeout();
	@NonNull private String hardwareKeys = "";
	boolean night = false;
	boolean hasRightMouse = false;
	boolean alwaysLongPress = false;
	boolean mouseBackSupport = true;

	private static final String TAG = "GameView";
	private enum TouchState { IDLE, WAITING_LONG_PRESS, DRAGGING, PINCH }
	private PointF lastDrag = null, lastTouch = new PointF(0.f, 0.f);
	private TouchState touchState = TouchState.IDLE;
	private int button;
	@ColorInt private int backgroundColour;
	private boolean waitingSpace = false;
	private boolean rightMouseHeld = false;
	private PointF touchStart;
	private PointF mousePos;
	private final double maxDistSq;
	private static final int LEFT_BUTTON = 0x0200;
	private static final int MIDDLE_BUTTON = 0x201;
	private static final int RIGHT_BUTTON = 0x202;
	private static final int LEFT_DRAG = 0x203; //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
			private static final int LEFT_RELEASE = 0x206;
	static final int FIRST_MOUSE = LEFT_BUTTON, LAST_MOUSE = 0x208;
	private static final int MOD_CTRL = 0x1000;
	private static final int MOD_SHIFT = 0x2000;
	private static final int ALIGN_V_CENTRE = 0x100;
	private static final int ALIGN_H_CENTRE = 0x001;
	private static final int ALIGN_H_RIGHT = 0x002;
	private static final int TEXT_MONO = 0x10;
	private static final int DRAG = LEFT_DRAG - LEFT_BUTTON;  // not bit fields, but there's a pattern
			private static final int RELEASE = LEFT_RELEASE - LEFT_BUTTON;
	static final int CURSOR_UP = 0x209, CURSOR_DOWN = 0x20a,
			CURSOR_LEFT = 0x20b, CURSOR_RIGHT = 0x20c, UI_UNDO = 0x213, UI_REDO = 0x214, MOD_NUM_KEYPAD = 0x4000;
	static final Set<Integer> CURSOR_KEYS = Collections.unmodifiableSet(new LinkedHashSet<>(Arrays.asList(
			CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, MOD_NUM_KEYPAD | '7', MOD_NUM_KEYPAD | '1', MOD_NUM_KEYPAD | '9', MOD_NUM_KEYPAD | '3')));
	int keysHandled = 0;  // debug
	private ScaleGestureDetector scaleDetector = null;
	private final GestureDetectorCompat gestureDetector;
	private static final float MAX_ZOOM = 30.f;
	private static final float ZOOM_OVERDRAW_PROPORTION = 0.25f;  // of a screen-full, in each direction, that you can see before checkerboard
	private int overdrawX, overdrawY;
	private final Matrix zoomMatrix = new Matrix();
	private final Matrix zoomInProgressMatrix = new Matrix();
	private final Matrix inverseZoomMatrix = new Matrix();
	private final Matrix tempDrawMatrix = new Matrix();
	public enum DragMode { UNMODIFIED, REVERT_OFF_SCREEN, REVERT_TO_START, PREVENT }
	private DragMode dragMode = DragMode.UNMODIFIED;
	private final OverScroller mScroller;
	private final EdgeEffect[] edges = new EdgeEffect[4];
	// ARGB_8888 is viewable in Android Studio debugger but very memory-hungry
	private static final Bitmap.Config BITMAP_CONFIG = Bitmap.Config.RGB_565;

	public GameView(Context context, AttributeSet attrs)
	{
		super(context, attrs);
		if (! isInEditMode()) {
			this.parent = (GamePlay) context;
		}
		bitmap = Bitmap.createBitmap(100, 100, BITMAP_CONFIG);  // for safety
		canvas = new Canvas(bitmap);
		canvasRestoreJustAfterCreation = canvas.save();
		paint = new Paint();
		paint.setAntiAlias(true);
		paint.setStrokeCap(Paint.Cap.SQUARE);
		paint.setStrokeWidth(1.f);  // will be scaled with everything else as long as it's non-zero
		blitters = new Bitmap[512];
		maxDistSq = Math.pow(ViewConfiguration.get(context).getScaledTouchSlop(), 2);
		backgroundColour = getDefaultBackgroundColour();
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			setDefaultFocusHighlightEnabled(false);
		}
		mScroller = new OverScroller(context);
		for (int i = 0; i < 4; i++) {
			edges[i] = new EdgeEffect(context);
		}
		gestureDetector = new GestureDetectorCompat(getContext(), new GestureDetector.OnGestureListener() {
			@Override
			public boolean onDown(MotionEvent event) {
				int meta = event.getMetaState();
				int buttonState = event.getButtonState();
				if ((meta & KeyEvent.META_ALT_ON) > 0  ||
						buttonState == MotionEvent.BUTTON_TERTIARY)  {
					button = MIDDLE_BUTTON;
				} else if ((meta & KeyEvent.META_SHIFT_ON) > 0  ||
						buttonState == MotionEvent.BUTTON_SECONDARY ||
						buttonState == MotionEvent.BUTTON_STYLUS_PRIMARY) {
					button = RIGHT_BUTTON;
					hasRightMouse = true;
				} else {
					button = LEFT_BUTTON;
				}
				touchStart = pointFromEvent(event);
				parent.handler.removeCallbacks(sendLongPress);
				if ((isFromSource(event, SOURCE_MOUSE) || isFromSource(event, SOURCE_STYLUS)
						|| event.getToolType(event.getActionIndex()) == TOOL_TYPE_STYLUS) &&
						((hasRightMouse && !alwaysLongPress) || button != LEFT_BUTTON)) {
					parent.sendKey(viewToGame(touchStart), button);
					if (dragMode == DragMode.PREVENT) {
						touchState = TouchState.IDLE;
					} else {
						touchState = TouchState.DRAGGING;
					}
				} else {
					touchState = TouchState.WAITING_LONG_PRESS;
					parent.handler.postDelayed(sendLongPress, longPressTimeout);
				}
				return true;
			}

			@Override
			public boolean onScroll(MotionEvent downEvent, MotionEvent event, float distanceX, float distanceY) {
				// 2nd clause is 2 fingers a constant distance apart
				if (isScaleInProgress() || event.getPointerCount() > 1) {
					revertDragInProgress(pointFromEvent(event));
					if (touchState == TouchState.WAITING_LONG_PRESS) {
						parent.handler.removeCallbacks(sendLongPress);
					}
					touchState = TouchState.PINCH;
					scrollBy(distanceX, distanceY);
					return true;
				}
				return false;
			}

			@Override public boolean onSingleTapUp(MotionEvent event) { return true; }
			@Override public void onShowPress(MotionEvent e) {}
			@Override public void onLongPress(MotionEvent e) {}

			@Override
			public boolean onFling(MotionEvent e1, MotionEvent e2, float velocityX, float velocityY) {
				if (touchState != TouchState.PINCH) {  // require 2 fingers
					return false;
				}
				final float scale = getXScale(zoomMatrix) * getXScale(zoomInProgressMatrix);
				final PointF currentScroll = getCurrentScroll();
				mScroller.fling(Math.round(currentScroll.x), Math.round(currentScroll.y),
						-Math.round(velocityX / scale), -Math.round(velocityY / scale), 0, wDip, 0, hDip);
				animateScroll.run();
				return true;
			}
		});
		// We do our own long-press detection to capture movement afterwards
		gestureDetector.setIsLongpressEnabled(false);
		enablePinchZoom();
	}

	private PointF getCurrentScroll() {
		return viewToGame(new PointF((float)w/2, (float)h/2));
	}

	private final Runnable animateScroll = new Runnable() {
		@Override
		public void run() {
			mScroller.computeScrollOffset();
			final PointF currentScroll = getCurrentScroll();
			scrollBy(mScroller.getCurrX() - currentScroll.x, mScroller.getCurrY() - currentScroll.y);
			if (mScroller.isFinished()) {
				ViewCompat.postOnAnimation(GameView.this, () -> {
					redrawForInitOrZoomChange();
					for (EdgeEffect edge : edges) edge.onRelease();
				});
			} else {
				ViewCompat.postOnAnimation(GameView.this, animateScroll);
			}
		}
	};

	public void setDragModeFor(final BackendName whichBackend) {
		dragMode = whichBackend.getDragMode();
	}

	private void revertDragInProgress(final PointF here) {
		if (touchState == TouchState.DRAGGING) {
			final PointF dragTo = switch (dragMode) {
				case REVERT_OFF_SCREEN -> new PointF(-1, -1);
				case REVERT_TO_START -> viewToGame(touchStart);
				default -> viewToGame(here);
			};
			parent.sendKey(dragTo, button + DRAG);
			parent.sendKey(dragTo, button + RELEASE);
		}
	}

	@Override
	public void scrollBy(int x, int y) {
		scrollBy((float) x, (float) y);
	}

	private void scrollBy(float distanceX, float distanceY) {
		zoomInProgressMatrix.postTranslate(-distanceX, -distanceY);
		zoomMatrixUpdated(true);
		postInvalidateOnAnimation();
	}

	public void ensureCursorVisible(final RectF cursorLocation) {
		final PointF topLeft = viewToGame(new PointF(0, 0));
		final PointF bottomRight = viewToGame(new PointF(w, h));
		if (cursorLocation == null) {
			postInvalidateOnAnimation();
			return;
		}
		final RectF cursorWithMargin = new RectF(
				cursorLocation.left - cursorLocation.width()/4,
				cursorLocation.top - cursorLocation.height()/4,
				cursorLocation.right + cursorLocation.width()/4,
				cursorLocation.bottom + cursorLocation.height()/4);
		float dx = 0, dy = 0;
		if (cursorWithMargin.left < topLeft.x) {
			dx = topLeft.x - cursorWithMargin.left;
		}
		if (cursorWithMargin.top < topLeft.y) {
			dy = topLeft.y - cursorWithMargin.top;
		}
		if (cursorWithMargin.right > bottomRight.x) {
			dx = bottomRight.x - cursorWithMargin.right;
		}
		if (cursorWithMargin.bottom > bottomRight.y) {
			dy = bottomRight.y - cursorWithMargin.bottom;
		}
		if (dx != 0 || dy != 0) {
			Log.d(TAG, "dx " + dx + " dy " + dy);
			final float scale = getXScale(zoomMatrix);
			zoomInProgressMatrix.postTranslate(dx * scale, dy * scale);
			redrawForInitOrZoomChange();
		}
		postInvalidateOnAnimation();
	}

	private void zoomMatrixUpdated(final boolean userAction) {
		// Constrain scrolling to game bounds
		invertZoomMatrix();  // needed for viewToGame
		final PointF topLeft = viewToGame(new PointF(0, 0));
		final PointF bottomRight = viewToGame(new PointF(w, h));
		if (topLeft.x < 0) {
			zoomInProgressMatrix.preTranslate(topLeft.x * density, 0);
			if (userAction) hitEdge(3, -topLeft.x / wDip, 1 - (lastTouch.y / h));
		} else if (exceedsTouchSlop(topLeft.x)) {
			edges[3].onRelease();
		}
		if (bottomRight.x > wDip) {
			zoomInProgressMatrix.preTranslate((bottomRight.x - wDip) * density, 0);
			if (userAction) hitEdge(1, (bottomRight.x - wDip) / wDip, lastTouch.y / h);
		} else if (exceedsTouchSlop(wDip - bottomRight.x)) {
			edges[1].onRelease();
		}
		if (topLeft.y < 0) {
			zoomInProgressMatrix.preTranslate(0, topLeft.y * density);
			if (userAction) hitEdge(0, -topLeft.y / hDip, lastTouch.x / w);
		} else if (exceedsTouchSlop(topLeft.y)) {
			edges[0].onRelease();
		}
		if (bottomRight.y > hDip) {
			zoomInProgressMatrix.preTranslate(0, (bottomRight.y - hDip) * density);
			if (userAction) hitEdge(2, (bottomRight.y - hDip) / hDip, 1 - (lastTouch.x / w));
		} else if (exceedsTouchSlop(hDip - bottomRight.y)) {
			edges[2].onRelease();
		}
		canvas.setMatrix(zoomMatrix);
		invertZoomMatrix();  // now with our changes
	}

	private void hitEdge(int edge, float delta, float displacement) {
		if (!mScroller.isFinished()) {
			edges[edge].onAbsorb(Math.round(mScroller.getCurrVelocity()));
			mScroller.abortAnimation();
		} else {
			final float deltaDistance = Math.min(1.f, delta * 1.5f);
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
				edges[edge].onPull(deltaDistance, displacement);
			} else {
				edges[edge].onPull(deltaDistance);
			}
		}
	}

	private boolean exceedsTouchSlop(float dist) {
		return Math.pow(dist, 2) > maxDistSq;
	}

	private boolean movedPastTouchSlop(float x, float y) {
		return Math.pow(Math.abs(x - touchStart.x), 2)
				+ Math.pow(Math.abs(y - touchStart.y) ,2)
				> maxDistSq;
	}

	private boolean isScaleInProgress() {
		return scaleDetector.isInProgress();
	}

	private void enablePinchZoom() {
		scaleDetector = new ScaleGestureDetector(getContext(), new ScaleGestureDetector.SimpleOnScaleGestureListener() {
			@Override
			public boolean onScale(ScaleGestureDetector detector) {
				float factor = detector.getScaleFactor();
				final float scale = getXScale(zoomMatrix) * getXScale(zoomInProgressMatrix);
				final float nextScale = scale * factor;
				final boolean wasZoomedOut = (scale == density);
				if (nextScale < density + 0.01f) {
					if (! wasZoomedOut) {
						resetZoomMatrix();
						redrawForInitOrZoomChange();
					}
				} else {
					if (nextScale > MAX_ZOOM) {
						factor = MAX_ZOOM / scale;
					}
					zoomInProgressMatrix.postScale(factor, factor,
							overdrawX + detector.getFocusX(),
							overdrawY + detector.getFocusY());
				}
				zoomMatrixUpdated(true);
				postInvalidateOnAnimation();
				return true;
			}
		});
		ScaleGestureDetectorCompat.setQuickScaleEnabled(scaleDetector, false);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			scaleDetector.setStylusScaleEnabled(false);
		}
	}

	void resetZoomForClear() {
		resetZoomMatrix();
		canvas.setMatrix(zoomMatrix);
		invertZoomMatrix();
	}

	private void resetZoomMatrix() {
		zoomMatrix.reset();
		zoomMatrix.postTranslate(overdrawX, overdrawY);
		zoomMatrix.postScale(density, density,
				overdrawX, overdrawY);
		zoomInProgressMatrix.reset();
	}

	private void invertZoomMatrix() {
		final Matrix copy = new Matrix(zoomMatrix);
		copy.postConcat(zoomInProgressMatrix);
		copy.postTranslate(-overdrawX, -overdrawY);
		if (!copy.invert(inverseZoomMatrix)) {
			throw new RuntimeException("zoom not invertible");
		}
	}

	private void redrawForInitOrZoomChange() {
		zoomMatrixUpdated(false);  // constrains zoomInProgressMatrix
		zoomMatrix.postConcat(zoomInProgressMatrix);
		zoomInProgressMatrix.reset();
		canvas.setMatrix(zoomMatrix);
		invertZoomMatrix();
		if (parent != null) {
			clear();
			parent.gameViewResized();  // not just forceRedraw() - need to reallocate blitters
		}
		postInvalidateOnAnimation();
	}

	private float getXScale(Matrix m) {
		float[] values = new float[9];
		m.getValues(values);
		return values[Matrix.MSCALE_X];
	}

	private PointF pointFromEvent(MotionEvent event) {
		return new PointF(event.getX(), event.getY());
	}

	private PointF viewToGame(PointF point) {
		float[] f = { point.x, point.y };
		inverseZoomMatrix.mapPoints(f);
		return new PointF(f[0], f[1]);
	}

	private boolean checkPinchZoom(MotionEvent event) {
		return scaleDetector.onTouchEvent(event);
	}

	private final Runnable sendLongPress = new Runnable() {
		public void run() {
			if (isScaleInProgress()) return;
			button = RIGHT_BUTTON;
			touchState = TouchState.DRAGGING;
			performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
			parent.sendKey(viewToGame(touchStart), button);
		}
	};

	@Override
	public boolean onGenericMotionEvent(@NonNull MotionEvent event)
	{
		if (isFromSource(event, SOURCE_MOUSE) && event.getActionMasked() == MotionEvent.ACTION_HOVER_MOVE) {
			mousePos = pointFromEvent(event);
			if (rightMouseHeld && touchState == TouchState.DRAGGING) {
				event.setAction(MotionEvent.ACTION_MOVE);
				return handleTouchEvent(event, false);
			}
		}
		return super.onGenericMotionEvent(event);
	}

	@SuppressLint("ClickableViewAccessibility")  // Not a simple enough view to just "click the view"
	@Override
	public boolean onTouchEvent(@NonNull MotionEvent event)
	{
		if (parent.currentBackend == null) return false;

		int evAction = event.getAction();
		if (isFromSource(event, SOURCE_STYLUS) && (evAction>=211) && (evAction<=213)) {
			event.setAction(evAction-211);
		}

		boolean sdRet = checkPinchZoom(event);
		boolean gdRet = gestureDetector.onTouchEvent(event);
		return handleTouchEvent(event, sdRet || gdRet);
	}

	private boolean handleTouchEvent(@NonNull MotionEvent event, boolean consumedAsScrollOrGesture)
	{
		lastTouch = pointFromEvent(event);
		if (event.getAction() == MotionEvent.ACTION_UP) {
			parent.handler.removeCallbacks(sendLongPress);
			if (touchState == TouchState.PINCH && mScroller.isFinished()) {
				redrawForInitOrZoomChange();
				for (EdgeEffect edge : edges) edge.onRelease();
			} else if (touchState == TouchState.WAITING_LONG_PRESS) {
				parent.sendKey(viewToGame(touchStart), button);
				touchState = TouchState.DRAGGING;
			}
			if (touchState == TouchState.DRAGGING) {
				parent.sendKey(viewToGame(pointFromEvent(event)), button + RELEASE);
			}
			touchState = TouchState.IDLE;
			return true;
		} else if (event.getAction() == MotionEvent.ACTION_MOVE) {
			// 2nd clause is 2 fingers a constant distance apart
			if (isScaleInProgress() || event.getPointerCount() > 1) {
				return consumedAsScrollOrGesture;
			}
			float x = event.getX(), y = event.getY();
			if (touchState == TouchState.WAITING_LONG_PRESS && movedPastTouchSlop(x, y)) {
				parent.handler.removeCallbacks(sendLongPress);
				if (dragMode == DragMode.PREVENT) {
					touchState = TouchState.IDLE;
				} else {
					parent.sendKey(viewToGame(touchStart), button);
					touchState = TouchState.DRAGGING;
				}
			}
			if (touchState == TouchState.DRAGGING) {
				lastDrag = pointFromEvent(event);
				parent.sendKey(viewToGame(lastDrag), button + DRAG);
				return true;
			}
			return false;
		} else {
			return consumedAsScrollOrGesture;
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
			if (event.isShiftPressed()) {
				key = ' ';
				break;
			}
			touchStart = new PointF(0, 0);
			waitingSpace = true;
			parent.handler.removeCallbacks( sendSpace );
			parent.handler.postDelayed( sendSpace, longPressTimeout);
			keysHandled++;
			return true;
		case KeyEvent.KEYCODE_ENTER: key = '\n'; break;
		case KeyEvent.KEYCODE_FOCUS: case KeyEvent.KEYCODE_SPACE: case KeyEvent.KEYCODE_BUTTON_X:
			key = ' '; break;
		case KeyEvent.KEYCODE_BUTTON_L1: key = UI_UNDO; break;
		case KeyEvent.KEYCODE_BUTTON_R1: key = UI_REDO; break;
		case KeyEvent.KEYCODE_DEL: key = '\b'; break;
		// Mouse right-click = BACK auto-repeats on at least Galaxy S7
		case KeyEvent.KEYCODE_BACK:
			if (mouseBackSupport && event.getSource() == SOURCE_MOUSE) {
				if (rightMouseHeld) {
					return true;
				}
				rightMouseHeld = true;
				hasRightMouse = true;
				touchStart = mousePos;
				button = RIGHT_BUTTON;
				parent.sendKey(viewToGame(touchStart), button);
				if (dragMode == DragMode.PREVENT) {
					touchState = TouchState.IDLE;
				} else {
					touchState = TouchState.DRAGGING;
				}
				return true;
			}
			break;
		}
		if (key == CURSOR_UP || key == CURSOR_DOWN || key == CURSOR_LEFT || key == CURSOR_RIGHT) {
			// "only apply to cursor keys"
			// http://www.chiark.greenend.org.uk/~sgtatham/puzzles/devel/backend.html#backend-interpret-move
			if( event.isShiftPressed() ) key |= MOD_SHIFT;
			if( event.isAltPressed() ) key |= MOD_CTRL;
		}
		// we probably don't want MOD_NUM_KEYPAD here (numbers are in a line on G1 at least)
		if (key == 0) {
			int exactKey = event.getUnicodeChar();
			if ((exactKey >= 'A' && exactKey <= 'Z') || hardwareKeys.indexOf(exactKey) >= 0) {
				key = exactKey;
			} else {
				key = event.getMatch(hardwareKeys.toCharArray());
				if (key == 0 && (exactKey == 'u' || exactKey == 'r')) key = exactKey;
			}
		}
		if( key == 0 ) return super.onKeyDown(keyCode, event);  // handles Back etc.
		parent.sendKey(0, 0, key, repeat > 0);
		keysHandled++;
		return true;
	}

	public void setHardwareKeys(@NonNull String hardwareKeys) {
		this.hardwareKeys = hardwareKeys;
	}

	@Override
	public boolean onKeyUp( int keyCode, KeyEvent event )
	{
		if (mouseBackSupport && event.getSource() == SOURCE_MOUSE) {
			if (keyCode == KeyEvent.KEYCODE_BACK && rightMouseHeld) {
				rightMouseHeld = false;
				if (touchState == TouchState.DRAGGING) {
					parent.sendKey(viewToGame(mousePos), button + RELEASE);
				}
				touchState = TouchState.IDLE;
			}
			return true;
		}
		if (keyCode != KeyEvent.KEYCODE_DPAD_CENTER || ! waitingSpace)
			return super.onKeyUp(keyCode, event);
		parent.handler.removeCallbacks(sendSpace);
		parent.sendKey(0, 0, '\n');
		return true;
	}

	@Override
	public boolean dispatchKeyEvent(@NonNull KeyEvent event) {
		int keyCode = event.getKeyCode();
		// Mouse right-click-and-hold sends MENU as a "keyboard" on at least Galaxy S7, ignore
		if (keyCode == KeyEvent.KEYCODE_MENU && rightMouseHeld) {
			return true;
		}
		return super.dispatchKeyEvent(event);
	}

	@Override
	protected void onDraw( Canvas c )
	{
		if( bitmap == null ) return;
		tempDrawMatrix.reset();
		tempDrawMatrix.preTranslate(-overdrawX, -overdrawY);
		tempDrawMatrix.preConcat(zoomInProgressMatrix);
		final int restore = c.save();
		c.concat(tempDrawMatrix);
		float[] f = { 0, 0, bitmap.getWidth(), bitmap.getHeight() };
		tempDrawMatrix.mapPoints(f);
		if (f[0] > 0 || f[1] < w || f[2] < 0 || f[3] > h) {
			c.drawPaint(checkerboardPaint);
		}
		c.drawBitmap(bitmap, 0, 0, null);
		c.restoreToCount(restore);
		boolean keepAnimating = false;
		for (int i = 0; i < 4; i++) {
			if (!edges[i].isFinished()) {
				keepAnimating = true;
				final int restoreTo = c.save();
				c.rotate(i * 90);
				if (i == 1) {
					c.translate(0, -w);
				} else if (i == 2) {
					c.translate(-w, -h);
				} else if (i == 3) {
					c.translate(-h, 0);
				}
				final boolean flip = (i % 2) > 0;
				edges[i].setSize(flip ? h : w, flip ? w : h);
				edges[i].draw(c);
				c.restoreToCount(restoreTo);
			}
		}
		if (keepAnimating) {
			postInvalidateOnAnimation();
		}
	}

	@Override
	protected void onSizeChanged(int viewW, int viewH, int oldW, int oldH)
	{
		if (lastDrag != null) revertDragInProgress(lastDrag);
		w = Math.max(1, viewW); h = Math.max(1, viewH);
		Log.d("GameView", "onSizeChanged: " + w + ", " + h);
		rebuildBitmap();
		if (isInEditMode()) {
			// Draw a little placeholder to aid UI editing
			final Drawable d = ContextCompat.getDrawable(getContext(), R.drawable.net);
			if (d == null) throw new RuntimeException("Missing R.drawable.net");
			int s = Math.min(w, h);
			int mx = (w-s)/2, my = (h-s)/2;
			d.setBounds(new Rect(mx,my,mx+s,my+s));
			d.draw(canvas);
		}
	}

	void rebuildBitmap() {
		density = switch (limitDpi) {
			case LIMIT_OFF -> 1.f;
			case LIMIT_AUTO -> Math.min(parent.suggestDensity(w, h), getResources().getDisplayMetrics().density);
			case LIMIT_ON -> getResources().getDisplayMetrics().density;
		};
		Log.d("GameView", "density: " + density);
		wDip = Math.max(1, Math.round((float) w / density));
		hDip = Math.max(1, Math.round((float) h / density));
		if (bitmap != null) bitmap.recycle();
		overdrawX = Math.round(Math.round(ZOOM_OVERDRAW_PROPORTION * wDip) * density);
		overdrawY = Math.round(Math.round(ZOOM_OVERDRAW_PROPORTION * hDip) * density);
		// texture size limit, see http://stackoverflow.com/a/7523221/6540
		final Point maxTextureSize = getMaxTextureSize();
		// Assumes maxTextureSize >= (w,h) otherwise you get checkerboard edges
		// https://github.com/chrisboyle/sgtpuzzles/issues/199
		overdrawX = Math.min(overdrawX, (maxTextureSize.x - w) / 2);
		overdrawY = Math.min(overdrawY, (maxTextureSize.y - h) / 2);
		bitmap = Bitmap.createBitmap(Math.max(1, w + 2 * overdrawX), Math.max(1, h + 2 * overdrawY), BITMAP_CONFIG);
		clear();
		canvas = new Canvas(bitmap);
		canvasRestoreJustAfterCreation = canvas.save();
		resetZoomForClear();
		redrawForInitOrZoomChange();
	}

	private Point getMaxTextureSize() {
		final int maxW = canvas.getMaximumBitmapWidth();
		final int maxH = canvas.getMaximumBitmapHeight();
		if (maxW < 2048 || maxH < 2048) {
			return new Point(maxW, maxH);
		}
		// maxW/maxH are otherwise likely a lie, and we should be careful of OOM risk anyway
		// https://github.com/chrisboyle/sgtpuzzles/issues/195
		final DisplayMetrics metrics = getResources().getDisplayMetrics();
		final int largestDimension = Math.max(metrics.widthPixels, metrics.heightPixels);
		return (largestDimension > 2048) ? new Point(4096, 4096) : new Point(2048, 2048);
	}

	public void clear()
	{
		bitmap.eraseColor(backgroundColour);
	}

	void refreshColours(final BackendName whichBackend, final float[] newColours) {
		final Drawable checkerboardDrawable = ContextCompat.getDrawable(getContext(), night ? R.drawable.checkerboard_night : R.drawable.checkerboard);
		if (checkerboardDrawable == null) throw new RuntimeException("Missing R.drawable.checkerboard");
		final Bitmap checkerboard = ((BitmapDrawable) checkerboardDrawable).getBitmap();
		checkerboardPaint.setShader(new BitmapShader(checkerboard, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT));
		colours = new int[newColours.length / 3];
		for (int i = 0; i < newColours.length / 3; i++) {
			final int colour = Color.rgb(
					(int) (newColours[i * 3] * 255),
					(int) (newColours[i * 3 + 1] * 255),
					(int) (newColours[i * 3 + 2] * 255));
			colours[i] = colour;
		}
		colours[0] = ContextCompat.getColor(getContext(), R.color.game_background);  // modified by night
		if (night) {
			final String[] colourNames = whichBackend.getColours().toArray(new String[0]);
			for (int i = 1; i < colours.length; i++) {
				final boolean noName = i - 1 >= colourNames.length;
				String colourName = noName ? "unnamed_" + (i - 1) : colourNames[i - 1];
				if (whichBackend == SIGNPOST.INSTANCE && noName) {
					int offset = i - (colourNames.length);
					int category = offset / 16;
					int chain = offset % 16;
					colourName = ((category == 0) ? "b" :
							(category == 1) ? "m" :
							(category == 2) ? "d" : "x") + chain;
				}
				final String resourceName = whichBackend + "_night_colour_" + colourName;
				//Log.d("GameView", "\t<color name=\"" + resourceName + "\">" + String.format("#%06x", (0xFFFFFF & colours[i])) + "</color>");
				final int nightColourId = getResources().getIdentifier(resourceName, "color", parent.getPackageName());
				if (nightColourId > 0) {
					colours[i] = ContextCompat.getColor(getContext(), nightColourId);
				}
			}
		}
		if (colours.length > 0) {
			setBackgroundColor(colours[0]);
		} else {
			setBackgroundColor(getDefaultBackgroundColour());
		}
	}

	@Override
	public void setBackgroundColor(@ColorInt int colour) {
		super.setBackgroundColor(colour);
		backgroundColour = colour;
	}

	/** Unfortunately backends do things like setting other colours as fractions of this, so
	 *  e.g. black (night mode) would make all of Undead's monsters white - but we replace all
	 *  the colours in night mode anyway. */
	@UsedByJNI
	@ColorInt
	public int getDefaultBackgroundColour() {
		return ContextCompat.getColor(getContext(), R.color.fake_game_background_to_derive_colours_from);
	}

	@UsedByJNI
	public void clipRect(int x, int y, int w, int h) {
		canvas.restoreToCount(canvasRestoreJustAfterCreation);
		canvasRestoreJustAfterCreation = canvas.save();
		canvas.setMatrix(zoomMatrix);
		canvas.clipRect(new RectF(x - 0.5f, y - 0.5f, x + w - 0.5f, y + h - 0.5f));
	}

	@UsedByJNI
	public void unClip(int marginX, int marginY)
	{
		canvas.restoreToCount(canvasRestoreJustAfterCreation);
		canvasRestoreJustAfterCreation = canvas.save();
		canvas.setMatrix(zoomMatrix);
		canvas.clipRect(marginX - 0.5f, marginY - 0.5f, wDip - marginX - 1.5f, hDip - marginY - 1.5f);
	}

	@UsedByJNI
	public void fillRect(final int x, final int y, final int w, final int h, final int colour)
	{
		paint.setColor(colours[colour]);
		paint.setStyle(Paint.Style.FILL);
		paint.setAntiAlias(false);  // required for regions in Map to look continuous (and by API)
		if (w == 1 && h == 1) {
			canvas.drawPoint(x, y, paint);
		} else if ((w == 1) ^ (h == 1)) {
			canvas.drawLine(x, y, x + w - 1, y + h - 1, paint);
		} else {
			canvas.drawRect(x - 0.5f, y - 0.5f, x + w - 0.5f, y + h - 0.5f, paint);
		}
		paint.setAntiAlias(true);
	}

	@UsedByJNI
	public void drawLine(float thickness, float x1, float y1, float x2, float y2, int colour)
	{
		paint.setColor(colours[colour]);
		paint.setStrokeWidth(Math.max(thickness, 1.f));
		canvas.drawLine(x1, y1, x2, y2, paint);
		paint.setStrokeWidth(1.f);
	}

	@UsedByJNI
	public void drawPoly(float thickness, int[] points, int ox, int oy, int line, int fill)
	{
		Path path = new Path();
		path.moveTo(points[0] + ox, points[1] + oy);
		for(int i=1; i < points.length/2; i++) {
			path.lineTo(points[2 * i] + ox, points[2 * i + 1] + oy);
		}
		path.close();
		// cheat slightly: polygons up to square look prettier without (and adjacent squares want to
		// look continuous in lightup)
		boolean disableAntiAlias = points.length <= 8;  // 2 per point
		if (disableAntiAlias) paint.setAntiAlias(false);
		drawPoly(thickness, path, line, fill);
		paint.setAntiAlias(true);
	}

	private void drawPoly(float thickness, Path p, int lineColour, int fillColour)
	{
		if (fillColour != -1) {
			paint.setColor(colours[fillColour]);
			paint.setStyle(Paint.Style.FILL);
			canvas.drawPath(p, paint);
		}
		paint.setColor(colours[lineColour]);
		paint.setStyle(Paint.Style.STROKE);
		paint.setStrokeWidth(Math.max(thickness, 1.f));
		canvas.drawPath(p, paint);
		paint.setStrokeWidth(1.f);
	}

	@UsedByJNI
	public void drawCircle(float thickness, float x, float y, float r, int lineColour, int fillColour)
	{
		if (r <= 0.5f) fillColour = lineColour;
		r = Math.max(r, 0.4f);
		if (fillColour != -1) {
			paint.setColor(colours[fillColour]);
			paint.setStyle(Paint.Style.FILL);
			canvas.drawCircle(x, y, r, paint);
		}
		paint.setColor(colours[lineColour]);
		paint.setStyle(Paint.Style.STROKE);
		if (thickness > 1.f) {
			paint.setStrokeWidth(thickness);
		}
		canvas.drawCircle(x, y, r, paint);
		paint.setStrokeWidth(1.f);
	}

	@UsedByJNI
	public void drawText(int x, int y, int flags, int size, int colour, String text)
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
		canvas.drawText(text, x, y, paint);
	}

	@UsedByJNI
	public int blitterAlloc(int w, int h)
	{
		for(int i=0; i<blitters.length; i++) {
			if (blitters[i] == null) {
				float zoom = getXScale(zoomMatrix);
				blitters[i] = Bitmap.createBitmap(Math.round(zoom * w), Math.round(zoom * h), BITMAP_CONFIG);
				return i;
			}
		}
		throw new RuntimeException("No free blitter found!");
	}

	@UsedByJNI
	public void blitterFree(int i)
	{
		if( blitters[i] == null ) return;
		blitters[i].recycle();
		blitters[i] = null;
	}

	private PointF blitterPosition(int x, int y, boolean save) {
		float[] f = { x, y };
		zoomMatrix.mapPoints(f);
		f[0] = (float) Math.floor(f[0]);
		f[1] = (float) Math.floor(f[1]);
		if (save) {
			f[0] *= -1f;
			f[1] *= -1f;
		}
		return new PointF(f[0], f[1]);
	}

	@UsedByJNI
	public void blitterSave(int i, int x, int y)
	{
		if( blitters[i] == null ) return;
		final PointF blitterPosition = blitterPosition(x, y, true);
		new Canvas(blitters[i]).drawBitmap(bitmap, blitterPosition.x, blitterPosition.y, null);
	}

	@UsedByJNI
	public void blitterLoad(int i, int x, int y)
	{
		if( blitters[i] == null ) return;
		final PointF blitterPosition = blitterPosition(x, y, false);
		new Canvas(bitmap).drawBitmap(blitters[i], blitterPosition.x, blitterPosition.y, null);
	}

	@VisibleForTesting
	Bitmap screenshot(final Rect gameCoords, final Point gameSizeInGameCoords) {
		int offX = (wDip - gameSizeInGameCoords.x) / 2;
		int offY = (hDip - gameSizeInGameCoords.y) / 2;
		final RectF r = new RectF(gameCoords.left + offX, gameCoords.top + offY, gameCoords.right + offX, gameCoords.bottom + offY);
		zoomMatrix.mapRect(r);
		return Bitmap.createBitmap(bitmap, (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top));
	}
}
