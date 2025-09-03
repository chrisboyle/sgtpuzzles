package name.boyle.chris.sgtpuzzles

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Point
import android.graphics.PointF
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.graphics.drawable.BitmapDrawable
import android.os.Build
import android.os.Build.VERSION_CODES
import android.util.AttributeSet
import android.util.Log
import android.view.GestureDetector
import android.view.HapticFeedbackConstants
import android.view.InputDevice.SOURCE_MOUSE
import android.view.InputDevice.SOURCE_STYLUS
import android.view.KeyEvent
import android.view.KeyEvent.KEYCODE_BACK
import android.view.KeyEvent.KEYCODE_BUTTON_L1
import android.view.KeyEvent.KEYCODE_BUTTON_R1
import android.view.KeyEvent.KEYCODE_BUTTON_X
import android.view.KeyEvent.KEYCODE_DEL
import android.view.KeyEvent.KEYCODE_DPAD_CENTER
import android.view.KeyEvent.KEYCODE_DPAD_DOWN
import android.view.KeyEvent.KEYCODE_DPAD_LEFT
import android.view.KeyEvent.KEYCODE_DPAD_RIGHT
import android.view.KeyEvent.KEYCODE_DPAD_UP
import android.view.KeyEvent.KEYCODE_ENTER
import android.view.KeyEvent.KEYCODE_FOCUS
import android.view.KeyEvent.KEYCODE_MENU
import android.view.KeyEvent.KEYCODE_SPACE
import android.view.KeyEvent.META_ALT_ON
import android.view.KeyEvent.META_SHIFT_ON
import android.view.MotionEvent
import android.view.MotionEvent.ACTION_HOVER_MOVE
import android.view.MotionEvent.BUTTON_SECONDARY
import android.view.MotionEvent.BUTTON_STYLUS_PRIMARY
import android.view.MotionEvent.BUTTON_TERTIARY
import android.view.MotionEvent.TOOL_TYPE_STYLUS
import android.view.ScaleGestureDetector
import android.view.ScaleGestureDetector.SimpleOnScaleGestureListener
import android.view.View
import android.view.ViewConfiguration
import android.widget.EdgeEffect
import android.widget.OverScroller
import androidx.core.graphics.createBitmap
import androidx.core.graphics.withMatrix
import androidx.core.graphics.withRotation
import androidx.annotation.ColorInt
import androidx.annotation.ColorRes
import androidx.annotation.VisibleForTesting
import androidx.core.content.ContextCompat
import androidx.core.view.MotionEventCompat
import name.boyle.chris.sgtpuzzles.GameView.LimitDPIMode.LIMIT_AUTO
import name.boyle.chris.sgtpuzzles.GameView.LimitDPIMode.LIMIT_OFF
import name.boyle.chris.sgtpuzzles.GameView.LimitDPIMode.LIMIT_ON
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.GameEngine.ViewCallbacks
import name.boyle.chris.sgtpuzzles.backend.UsedByJNI
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

class GameView(context: Context, attrs: AttributeSet?) : View(context, attrs), ViewCallbacks {
    private lateinit var parent: GamePlay
    private var bitmap: Bitmap = createBitmap(100, 100, BITMAP_CONFIG) // for safety
    private var canvas: Canvas = Canvas(bitmap)
    private var canvasRestoreJustAfterCreation: Int
    private val paint = Paint().apply {
        isAntiAlias = true
        strokeCap = Paint.Cap.SQUARE
        strokeWidth = 1f // will be scaled with everything else as long as it's non-zero
    }
    private val checkerboardPaint = Paint()
    private val blitters: Array<Bitmap?> = arrayOfNulls(512)

    @ColorInt
    private var colours = IntArray(0)
    private var density = 1f

    enum class LimitDPIMode {
        LIMIT_OFF, LIMIT_AUTO, LIMIT_ON
    }

    var limitDpi = LIMIT_AUTO
    var w = 0
    var h = 0
    var wDip = 0
    var hDip = 0
    var longPressTimeout = ViewConfiguration.getLongPressTimeout()
    private var hardwareKeys = ""
    var night = false
    var hasRightMouse = false
    var alwaysLongPress = false
    var mouseBackSupport = true

    private enum class TouchState {
        IDLE, WAITING_LONG_PRESS, DRAGGING, PINCH
    }

    private var lastDrag: PointF? = null
    private var lastTouch = PointF(0f, 0f)
    private var touchState = TouchState.IDLE
    private var button = 0

    @ColorInt
    private var backgroundColour = defaultBackgroundColour
    private var waitingSpace = false
    private var rightMouseHeld = false
    private var touchStart: PointF? = null
    private var mousePos: PointF? = null
    private val maxDistSq = ViewConfiguration.get(context).scaledTouchSlop.toDouble().pow(2.0)
    var keysHandled = 0 // debug
    private var scaleDetector: ScaleGestureDetector? = null
    private val gestureDetector: GestureDetector
    private var overdrawX = 0
    private var overdrawY = 0
    private val zoomMatrix = Matrix()
    private val zoomInProgressMatrix = Matrix()
    private val inverseZoomMatrix = Matrix()
    private val tempDrawMatrix = Matrix()

    enum class DragMode {
        UNMODIFIED, REVERT_OFF_SCREEN, REVERT_TO_START, PREVENT
    }

    private var dragMode = DragMode.UNMODIFIED
    private val mScroller = OverScroller(context)
    private val edges = Array(4) { EdgeEffect(context) }
    private val currentScroll: PointF
        get() = viewToGame(PointF(w.toFloat() / 2, h.toFloat() / 2))
    private fun animateScroll() {
        mScroller.computeScrollOffset()
        val currentScroll: PointF = currentScroll
        scrollBy(mScroller.currX - currentScroll.x, mScroller.currY - currentScroll.y)
        if (mScroller.isFinished) {
            postOnAnimation {
                redrawForInitOrZoomChange()
                for (edge in edges) edge.onRelease()
            }
        } else {
            postOnAnimation(::animateScroll)
        }
    }

    fun setDragModeFor(whichBackend: BackendName?) {
        dragMode = whichBackend?.dragMode ?: DragMode.UNMODIFIED
    }

    private fun revertDragInProgress(here: PointF?) {
        if (touchState == TouchState.DRAGGING) {
            val offScreen = PointF(-1f, -1f)
            val dragTo = when (dragMode) {
                DragMode.REVERT_OFF_SCREEN -> offScreen
                DragMode.REVERT_TO_START -> viewToGame(touchStart ?: offScreen)
                else -> viewToGame(here ?: touchStart ?: offScreen)
            }
            parent.sendKey(dragTo, button + DRAG)
            parent.sendKey(dragTo, button + RELEASE)
        }
    }

    override fun scrollBy(x: Int, y: Int) {
        scrollBy(x.toFloat(), y.toFloat())
    }

    private fun scrollBy(distanceX: Float, distanceY: Float) {
        zoomInProgressMatrix.postTranslate(-distanceX, -distanceY)
        zoomMatrixUpdated(true)
        postInvalidateOnAnimation()
    }

    fun ensureCursorVisible(cursorLocation: RectF?) {
        val topLeft = viewToGame(PointF(0f, 0f))
        val bottomRight = viewToGame(PointF(w.toFloat(), h.toFloat()))
        if (cursorLocation == null) {
            postInvalidateOnAnimation()
            return
        }
        val cursorWithMargin = RectF(
            cursorLocation.left - cursorLocation.width() / 4,
            cursorLocation.top - cursorLocation.height() / 4,
            cursorLocation.right + cursorLocation.width() / 4,
            cursorLocation.bottom + cursorLocation.height() / 4
        )
        var dx = 0f
        var dy = 0f
        if (cursorWithMargin.left < topLeft.x) {
            dx = topLeft.x - cursorWithMargin.left
        }
        if (cursorWithMargin.top < topLeft.y) {
            dy = topLeft.y - cursorWithMargin.top
        }
        if (cursorWithMargin.right > bottomRight.x) {
            dx = bottomRight.x - cursorWithMargin.right
        }
        if (cursorWithMargin.bottom > bottomRight.y) {
            dy = bottomRight.y - cursorWithMargin.bottom
        }
        if (dx != 0f || dy != 0f) {
            Log.d(TAG, "dx $dx dy $dy")
            val scale = getXScale(zoomMatrix)
            zoomInProgressMatrix.postTranslate(dx * scale, dy * scale)
            redrawForInitOrZoomChange()
        }
        postInvalidateOnAnimation()
    }

    private fun zoomMatrixUpdated(userAction: Boolean) {
        // Constrain scrolling to game bounds
        invertZoomMatrix() // needed for viewToGame
        val topLeft = viewToGame(PointF(0f, 0f))
        val bottomRight = viewToGame(PointF(w.toFloat(), h.toFloat()))
        if (topLeft.x < 0) {
            zoomInProgressMatrix.preTranslate(topLeft.x * density, 0f)
            if (userAction) hitEdge(3, -topLeft.x / wDip, 1 - lastTouch.y / h)
        } else if (exceedsTouchSlop(topLeft.x)) {
            edges[3].onRelease()
        }
        if (bottomRight.x > wDip) {
            zoomInProgressMatrix.preTranslate((bottomRight.x - wDip) * density, 0f)
            if (userAction) hitEdge(1, (bottomRight.x - wDip) / wDip, lastTouch.y / h)
        } else if (exceedsTouchSlop(wDip - bottomRight.x)) {
            edges[1].onRelease()
        }
        if (topLeft.y < 0) {
            zoomInProgressMatrix.preTranslate(0f, topLeft.y * density)
            if (userAction) hitEdge(0, -topLeft.y / hDip, lastTouch.x / w)
        } else if (exceedsTouchSlop(topLeft.y)) {
            edges[0].onRelease()
        }
        if (bottomRight.y > hDip) {
            zoomInProgressMatrix.preTranslate(0f, (bottomRight.y - hDip) * density)
            if (userAction) hitEdge(2, (bottomRight.y - hDip) / hDip, 1 - lastTouch.x / w)
        } else if (exceedsTouchSlop(hDip - bottomRight.y)) {
            edges[2].onRelease()
        }
        canvas.setMatrix(zoomMatrix)
        invertZoomMatrix() // now with our changes
    }

    private fun hitEdge(edge: Int, delta: Float, displacement: Float) {
        if (!mScroller.isFinished) {
            edges[edge].onAbsorb(mScroller.currVelocity.roundToInt())
            mScroller.abortAnimation()
        } else {
            val deltaDistance = (delta * 1.5f).coerceAtMost(1f)
            edges[edge].onPull(deltaDistance, displacement)
        }
    }

    private fun exceedsTouchSlop(dist: Float): Boolean {
        return dist.toDouble().pow(2.0) > maxDistSq
    }

    private fun movedPastTouchSlop(x: Float, y: Float): Boolean {
        touchStart?.let {
            return (abs(x - it.x).toDouble().pow(2.0)
                    + abs(y - it.y).toDouble().pow(2.0)
                    > maxDistSq)
        }
        return false
    }

    private val isScaleInProgress: Boolean
        get() = scaleDetector?.isInProgress ?: false

    private fun enablePinchZoom() {
        scaleDetector = ScaleGestureDetector(context, object : SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                var factor = detector.scaleFactor
                val scale = getXScale(zoomMatrix) * getXScale(zoomInProgressMatrix)
                val nextScale = scale * factor
                val wasZoomedOut = scale == density
                if (nextScale < density + 0.01f) {
                    if (!wasZoomedOut) {
                        resetZoomMatrix()
                        redrawForInitOrZoomChange()
                    }
                } else {
                    if (nextScale > MAX_ZOOM) {
                        factor = MAX_ZOOM / scale
                    }
                    zoomInProgressMatrix.postScale(
                        factor, factor,
                        overdrawX + detector.focusX,
                        overdrawY + detector.focusY
                    )
                }
                zoomMatrixUpdated(true)
                postInvalidateOnAnimation()
                return true
            }
        }).also {
            it.isQuickScaleEnabled = false
            if (Build.VERSION.SDK_INT >= VERSION_CODES.M) {
                it.isStylusScaleEnabled = false
            }
        }
    }

    fun resetZoomForClear() {
        resetZoomMatrix()
        canvas.setMatrix(zoomMatrix)
        invertZoomMatrix()
    }

    private fun resetZoomMatrix() {
        zoomMatrix.reset()
        zoomMatrix.postTranslate(overdrawX.toFloat(), overdrawY.toFloat())
        zoomMatrix.postScale(
            density, density,
            overdrawX.toFloat(), overdrawY.toFloat()
        )
        zoomInProgressMatrix.reset()
    }

    private fun invertZoomMatrix() {
        val copy = Matrix(zoomMatrix)
        copy.postConcat(zoomInProgressMatrix)
        copy.postTranslate(-overdrawX.toFloat(), -overdrawY.toFloat())
        if (!copy.invert(inverseZoomMatrix)) {
            error("zoom not invertible")
        }
    }

    private fun redrawForInitOrZoomChange() {
        zoomMatrixUpdated(false) // constrains zoomInProgressMatrix
        zoomMatrix.postConcat(zoomInProgressMatrix)
        zoomInProgressMatrix.reset()
        canvas.setMatrix(zoomMatrix)
        invertZoomMatrix()
        if (!isInEditMode) {
            clear()
            parent.gameViewResized() // not just forceRedraw() - need to reallocate blitters
        }
        postInvalidateOnAnimation()
    }

    private fun getXScale(m: Matrix): Float {
        val values = FloatArray(9)
        m.getValues(values)
        return values[Matrix.MSCALE_X]
    }

    private val MotionEvent.point: PointF?
        get() = if (x.isNaN() or y.isNaN()) null else PointF(x, y)

    private fun viewToGame(point: PointF): PointF {
        val f = floatArrayOf(point.x, point.y)
        inverseZoomMatrix.mapPoints(f)
        return PointF(f[0], f[1])
    }

    private fun checkPinchZoom(event: MotionEvent): Boolean {
        return scaleDetector?.onTouchEvent(event) ?: false
    }

    private val sendLongPress: Runnable = object : Runnable {
        override fun run() {
            if (isScaleInProgress) return
            button = RIGHT_BUTTON
            touchState = TouchState.DRAGGING
            performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            touchStart?.let { parent.sendKey(viewToGame(it), button) }
        }
    }

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (MotionEventCompat.isFromSource(event, SOURCE_MOUSE)
                && event.actionMasked == ACTION_HOVER_MOVE
        ) {
            mousePos = event.point
            if (rightMouseHeld && touchState == TouchState.DRAGGING) {
                event.action = MotionEvent.ACTION_MOVE
                return handleTouchEvent(event, false)
            }
        }
        return super.onGenericMotionEvent(event)
    }

    @SuppressLint("ClickableViewAccessibility") // Not a simple enough view to just "click the view"
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (parent.currentBackend == null) return false
        val evAction = event.action
        if (MotionEventCompat.isFromSource(event, SOURCE_STYLUS)
                && evAction >= 211 && evAction <= 213) {
            event.action = evAction - 211
        }
        val sdRet = checkPinchZoom(event)
        val gdRet = gestureDetector.onTouchEvent(event)
        return handleTouchEvent(event, sdRet || gdRet)
    }

    private fun handleTouchEvent(event: MotionEvent, consumedAsScrollOrGesture: Boolean): Boolean {
        event.point?.let { lastTouch = it }
        return when (event.action) {
            MotionEvent.ACTION_UP -> {
                parent.handler.removeCallbacks(sendLongPress)
                if (touchState == TouchState.PINCH && mScroller.isFinished) {
                    redrawForInitOrZoomChange()
                    for (edge in edges) edge.onRelease()
                } else if (touchState == TouchState.WAITING_LONG_PRESS) {
                    touchStart?.let { parent.sendKey(viewToGame(it), button) }
                    touchState = TouchState.DRAGGING
                }
                if (touchState == TouchState.DRAGGING) {
                    event.point?.let { parent.sendKey(viewToGame(it), button + RELEASE) }
                }
                touchState = TouchState.IDLE
                true
            }
            MotionEvent.ACTION_MOVE -> {
                // 2nd clause is 2 fingers a constant distance apart
                if (isScaleInProgress || event.pointerCount > 1) {
                    return consumedAsScrollOrGesture
                }
                val x = event.x
                val y = event.y
                if (touchState == TouchState.WAITING_LONG_PRESS && movedPastTouchSlop(x, y)) {
                    parent.handler.removeCallbacks(sendLongPress)
                    touchState = if (dragMode == DragMode.PREVENT) {
                        TouchState.IDLE
                    } else {
                        touchStart?.let { parent.sendKey(viewToGame(it), button) }
                        TouchState.DRAGGING
                    }
                }
                if (touchState == TouchState.DRAGGING) {
                    lastDrag = event.point
                    lastDrag?.let { parent.sendKey(viewToGame(it), button + DRAG) }
                    return true
                }
                false
            }
            else -> {
                consumedAsScrollOrGesture
            }
        }
    }

    private val sendSpace = Runnable {
        waitingSpace = false
        parent.sendKey(0, 0, ' '.code)
    }

    init {
        if (!isInEditMode) {
            parent = context as GamePlay
        }
        canvasRestoreJustAfterCreation = canvas.save()
        if (Build.VERSION.SDK_INT >= VERSION_CODES.O) {
            defaultFocusHighlightEnabled = false
        }
        gestureDetector =
            GestureDetector(getContext(), object : GestureDetector.OnGestureListener {
                override fun onDown(event: MotionEvent): Boolean {
                    val meta = event.metaState
                    val buttonState = event.buttonState
                    if (meta and META_ALT_ON > 0 ||
                        buttonState == BUTTON_TERTIARY
                    ) {
                        button = MIDDLE_BUTTON
                    } else if (meta and META_SHIFT_ON > 0
                            || buttonState == BUTTON_SECONDARY
                            || buttonState == BUTTON_STYLUS_PRIMARY) {
                        button = RIGHT_BUTTON
                        hasRightMouse = true
                    } else {
                        button = LEFT_BUTTON
                    }
                    touchStart = event.point
                    if (touchStart == null) return false
                    parent.handler.removeCallbacks(sendLongPress)
                    if ((MotionEventCompat.isFromSource(event, SOURCE_MOUSE)
                            || MotionEventCompat.isFromSource(event, SOURCE_STYLUS)
                            || event.getToolType(event.actionIndex) == TOOL_TYPE_STYLUS) &&
                        (hasRightMouse && !alwaysLongPress || button != LEFT_BUTTON)
                    ) {
                        touchStart?.let { parent.sendKey(viewToGame(it), button) }
                        touchState = if (dragMode == DragMode.PREVENT) {
                            TouchState.IDLE
                        } else {
                            TouchState.DRAGGING
                        }
                    } else {
                        touchState = TouchState.WAITING_LONG_PRESS
                        parent.handler.postDelayed(sendLongPress, longPressTimeout.toLong())
                    }
                    return true
                }

                override fun onScroll(
                    downEvent: MotionEvent?,
                    event: MotionEvent,
                    distanceX: Float,
                    distanceY: Float
                ): Boolean {
                    // 2nd clause is 2 fingers a constant distance apart
                    if (isScaleInProgress || event.pointerCount > 1) {
                        revertDragInProgress(event.point)
                        if (touchState == TouchState.WAITING_LONG_PRESS) {
                            parent.handler.removeCallbacks(sendLongPress)
                        }
                        touchState = TouchState.PINCH
                        scrollBy(distanceX, distanceY)
                        return true
                    }
                    return false
                }

                override fun onSingleTapUp(event: MotionEvent): Boolean {
                    return true
                }

                override fun onShowPress(e: MotionEvent) {}
                override fun onLongPress(e: MotionEvent) {}
                override fun onFling(
                    e1: MotionEvent?,
                    e2: MotionEvent,
                    velocityX: Float,
                    velocityY: Float
                ): Boolean {
                    if (touchState != TouchState.PINCH) {  // require 2 fingers
                        return false
                    }
                    val scale = getXScale(zoomMatrix) * getXScale(zoomInProgressMatrix)
                    val currentScroll: PointF = currentScroll
                    mScroller.fling(
                        currentScroll.x.roundToInt(),
                        currentScroll.y.roundToInt(),
                        -(velocityX / scale).roundToInt(),
                        -(velocityY / scale).roundToInt(),
                        0,
                        wDip,
                        0,
                        hDip
                    )
                    animateScroll()
                    return true
                }
            })
        // We do our own long-press detection to capture movement afterwards
        @Suppress("UsePropertyAccessSyntax")
        gestureDetector.setIsLongpressEnabled(false)
        enablePinchZoom()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        var key = 0
        val repeat = event.repeatCount
        when (keyCode) {
            KEYCODE_DPAD_UP -> key = CURSOR_UP
            KEYCODE_DPAD_DOWN -> key = CURSOR_DOWN
            KEYCODE_DPAD_LEFT -> key = CURSOR_LEFT
            KEYCODE_DPAD_RIGHT -> key = CURSOR_RIGHT
            KEYCODE_DPAD_CENTER -> {
                if (repeat > 0) return false
                if (event.isShiftPressed) {
                    key = ' '.code
                } else {
                    touchStart = PointF(0f, 0f)
                    waitingSpace = true
                    parent.handler.removeCallbacks(sendSpace)
                    parent.handler.postDelayed(sendSpace, longPressTimeout.toLong())
                    keysHandled++
                    return true
                }
            }

            KEYCODE_ENTER -> key = '\n'.code
            KEYCODE_FOCUS, KEYCODE_SPACE, KEYCODE_BUTTON_X -> key = ' '.code

            KEYCODE_BUTTON_L1 -> key = UI_UNDO
            KEYCODE_BUTTON_R1 -> key = UI_REDO
            KEYCODE_DEL -> key = '\b'.code
        }
        if (preAndroid11MouseBackDown(keyCode, event)) return true
        if (key == CURSOR_UP || key == CURSOR_DOWN || key == CURSOR_LEFT || key == CURSOR_RIGHT) {
            // "only apply to cursor keys"
            // http://www.chiark.greenend.org.uk/~sgtatham/puzzles/devel/backend.html#backend-interpret-move
            if (event.isShiftPressed) key = key or MOD_SHIFT
            if (event.isAltPressed) key = key or MOD_CTRL
        }
        // we probably don't want MOD_NUM_KEYPAD here (numbers are in a line on G1 at least)
        if (key == 0) {
            val exactKey = event.unicodeChar
            if (exactKey >= 'A'.code && exactKey <= 'Z'.code || hardwareKeys.indexOf(exactKey.toChar()) >= 0) {
                key = exactKey
            } else {
                key = event.getMatch(hardwareKeys.toCharArray()).code
                if (key == 0 && (exactKey == 'u'.code || exactKey == 'r'.code)) key = exactKey
            }
        }
        if (key == 0) return super.onKeyDown(keyCode, event) // handles Back etc.
        parent.sendKey(0, 0, key, repeat > 0)
        keysHandled++
        return true
    }

    private fun preAndroid11MouseBackDown(keyCode: Int, event: KeyEvent): Boolean {
        if (Build.VERSION.SDK_INT < VERSION_CODES.R
                && keyCode == KEYCODE_BACK
                && mouseBackSupport
                && event.source == SOURCE_MOUSE) {
            if (rightMouseHeld) {
                return true
            }
            rightMouseHeld = true
            hasRightMouse = true
            touchStart = mousePos
            button = RIGHT_BUTTON
            touchStart?.let { parent.sendKey(viewToGame(it), button) }
            touchState = if (dragMode == DragMode.PREVENT) {
                TouchState.IDLE
            } else {
                TouchState.DRAGGING
            }
            return true
        }
        return false
    }

    fun setHardwareKeys(hardwareKeys: String) {
        this.hardwareKeys = hardwareKeys
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (preAndroid11MouseBackUp(event, keyCode)) return true
        if (keyCode == KEYCODE_DPAD_CENTER && waitingSpace) {
            parent.handler.removeCallbacks(sendSpace)
            parent.sendKey(0, 0, '\n'.code)
            return true
        }
        return super.onKeyUp(
            keyCode,
            event
        )
    }

    @SuppressLint("GestureBackNavigation")
    private fun preAndroid11MouseBackUp(event: KeyEvent, keyCode: Int): Boolean {
        if (Build.VERSION.SDK_INT < VERSION_CODES.R
                && mouseBackSupport
                && event.source == SOURCE_MOUSE) {
            if (keyCode == KEYCODE_BACK && rightMouseHeld) {
                rightMouseHeld = false
                if (touchState == TouchState.DRAGGING) {
                    mousePos?.let { parent.sendKey(viewToGame(it), button + RELEASE) }
                }
                touchState = TouchState.IDLE
            }
            return true
        }
        return false
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        // Mouse right-click-and-hold sends MENU as a "keyboard" on at least Galaxy S7, ignore
        if (event.keyCode == KEYCODE_MENU && rightMouseHeld)
            true
        else
            super.dispatchKeyEvent(event)

    override fun onDraw(c: Canvas) {
        tempDrawMatrix.reset()
        tempDrawMatrix.preTranslate(-overdrawX.toFloat(), -overdrawY.toFloat())
        tempDrawMatrix.preConcat(zoomInProgressMatrix)
        c.withMatrix(tempDrawMatrix) {
            val f = floatArrayOf(0f, 0f, bitmap.width.toFloat(), bitmap.height.toFloat())
            tempDrawMatrix.mapPoints(f)
            if (f[0] > 0 || f[1] < w || f[2] < 0 || f[3] > h) {
                c.drawPaint(checkerboardPaint)
            }
            c.drawBitmap(bitmap, 0f, 0f, null)
        }
        var keepAnimating = false
        for (i in 0..3) {
            if (!edges[i].isFinished) {
                keepAnimating = true
                c.withRotation((i * 90).toFloat()) {
                    when (i) {
                        1 -> c.translate(0f, -w.toFloat())
                        2 -> c.translate(-w.toFloat(), -h.toFloat())
                        3 -> c.translate(-h.toFloat(), 0f)
                    }
                    val flip = i % 2 > 0
                    edges[i].setSize(if (flip) h else w, if (flip) w else h)
                    edges[i].draw(c)
                }
            }
        }
        if (keepAnimating) {
            postInvalidateOnAnimation()
        }
    }

    override fun onSizeChanged(viewW: Int, viewH: Int, oldW: Int, oldH: Int) {
        lastDrag?.let { revertDragInProgress(it) }
        w = viewW.coerceAtLeast(1)
        h = viewH.coerceAtLeast(1)
        Log.d("GameView", "onSizeChanged: $w, $h")
        rebuildBitmap()
        if (isInEditMode) {
            // Draw a little placeholder to aid UI editing
            val d = checkNotNull(ContextCompat.getDrawable(context, R.drawable.net)) { "Missing R.drawable.net" }
            val s = min(w, h)
            val mx = (w - s) / 2
            val my = (h - s) / 2
            d.bounds = Rect(mx, my, mx + s, my + s)
            d.draw(canvas)
        }
    }

    fun rebuildBitmap() {
        density = if (isInEditMode) {
            1f
        } else {
            when (limitDpi) {
                LIMIT_OFF -> 1f
                LIMIT_AUTO -> min(parent.suggestDensity(w, h), resources.displayMetrics.density)
                LIMIT_ON -> resources.displayMetrics.density
            }
        }
        Log.d("GameView", "density: $density")
        wDip = (w.toFloat() / density).roundToInt().coerceAtLeast(1)
        hDip = (h.toFloat() / density).roundToInt().coerceAtLeast(1)
        overdrawX = ((ZOOM_OVERDRAW_PROPORTION * wDip).roundToInt() * density).roundToInt()
        overdrawY = ((ZOOM_OVERDRAW_PROPORTION * hDip).roundToInt() * density).roundToInt()
        // texture size limit, see http://stackoverflow.com/a/7523221/6540
        val maxTextureSize = maxTextureSize
        // Assumes maxTextureSize >= (w,h) otherwise you get checkerboard edges
        // https://github.com/chrisboyle/sgtpuzzles/issues/199
        overdrawX = min(overdrawX, (maxTextureSize.x - w) / 2)
        overdrawY = min(overdrawY, (maxTextureSize.y - h) / 2)
        bitmap.recycle()
        bitmap = createBitmap(
            (w + 2 * overdrawX).coerceAtLeast(1),
            (h + 2 * overdrawY).coerceAtLeast(1), BITMAP_CONFIG)
        clear()
        canvas = Canvas(bitmap)
        canvasRestoreJustAfterCreation = canvas.save()
        resetZoomForClear()
        redrawForInitOrZoomChange()
    }

    private val maxTextureSize: Point
        get() {
            val maxW = canvas.maximumBitmapWidth
            val maxH = canvas.maximumBitmapHeight
            if (maxW < 2048 || maxH < 2048) {
                return Point(maxW, maxH)
            }
            // maxW/maxH are otherwise likely a lie, and we should be careful of OOM risk anyway
            // https://github.com/chrisboyle/sgtpuzzles/issues/195
            val metrics = resources.displayMetrics
            val largestDimension = max(metrics.widthPixels, metrics.heightPixels)
            return if (largestDimension > 2048) Point(4096, 4096) else Point(2048, 2048)
        }

    fun clear() {
        bitmap.eraseColor(backgroundColour)
    }

    fun refreshColours(whichBackend: BackendName, newColours: FloatArray) {
        val checkerboardDrawable = checkNotNull(ContextCompat.getDrawable(
            context,
            if (night) R.drawable.checkerboard_night else R.drawable.checkerboard
        )) { "Missing R.drawable.checkerboard" }
        val checkerboard = (checkerboardDrawable as BitmapDrawable).bitmap
        checkerboardPaint.shader =
            BitmapShader(checkerboard, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT)
        colours = IntArray(newColours.size / 3)
        for (i in 0 until newColours.size / 3) {
            val colour = Color.rgb(
                (newColours[i * 3] * 255).toInt(),
                (newColours[i * 3 + 1] * 255).toInt(),
                (newColours[i * 3 + 2] * 255).toInt()
            )
            colours[i] = colour
        }
        if (night) {
            // Only replace colours[0] at night: Untangle uses a darker grey to distinguish the play area
            colours[0] = ContextCompat.getColor(context, R.color.game_background)
            @ColorRes val nightColours = whichBackend.nightColours // doesn't include background
            for (i in 1 until colours.size) {
                //Log.d("GameView", "\t<color name=\"" + resourceName + "\">" + String.format("#%06x", (0xFFFFFF & colours[i])) + "</color>");
                if (nightColours.size >= i) {
                    @ColorRes val nightRes = nightColours[i - 1]
                    if (nightRes != 0) {
                        colours[i] = ContextCompat.getColor(context, nightRes)
                    }
                }  // else GenerateBackendsTask is broken, but don't crash
            }
        }
        if (colours.isNotEmpty()) {
            setBackgroundColor(colours[0])
        } else {
            setBackgroundColor(defaultBackgroundColour)
        }
    }

    override fun setBackgroundColor(@ColorInt colour: Int) {
        super.setBackgroundColor(colour)
        backgroundColour = colour
    }

    @get:ColorInt
    @get:UsedByJNI
    override val defaultBackgroundColour: Int
        /** Unfortunately backends do things like setting other colours as fractions of this, so
         * e.g. black (night mode) would make all of Undead's monsters white - but we replace all
         * the colours in night mode anyway.  */
        get() = ContextCompat.getColor(context, R.color.fake_game_background_to_derive_colours_from)

    @UsedByJNI
    override fun clipRect(x: Int, y: Int, w: Int, h: Int) {
        canvas.restoreToCount(canvasRestoreJustAfterCreation)
        canvasRestoreJustAfterCreation = canvas.save()
        canvas.setMatrix(zoomMatrix)
        canvas.clipRect(RectF(x - 0.5f, y - 0.5f, x + w - 0.5f, y + h - 0.5f))
    }

    @UsedByJNI
    override fun unClip(left: Int, top: Int, right: Int, bottom: Int) {
        canvas.restoreToCount(canvasRestoreJustAfterCreation)
        canvasRestoreJustAfterCreation = canvas.save()
        canvas.setMatrix(zoomMatrix)
        canvas.clipRect(
            left - 0.5f,
            top - 0.5f,
            right - 1.5f,
            bottom - 1.5f
        )
    }

    @UsedByJNI
    override fun fillRect(x: Int, y: Int, w: Int, h: Int, colour: Int) {
        paint.color = colours[colour]
        paint.style = Paint.Style.FILL
        paint.isAntiAlias = false // required for regions in Map to look continuous (and by API)
        if (w == 1 && h == 1) {
            canvas.drawPoint(x.toFloat(), y.toFloat(), paint)
        } else if ((w == 1) xor (h == 1)) {
            canvas.drawLine(
                x.toFloat(),
                y.toFloat(),
                (x + w - 1).toFloat(),
                (y + h - 1).toFloat(),
                paint
            )
        } else {
            canvas.drawRect(x - 0.5f, y - 0.5f, x + w - 0.5f, y + h - 0.5f, paint)
        }
        paint.isAntiAlias = true
    }

    @UsedByJNI
    override fun drawLine(
        thickness: Float,
        x1: Float,
        y1: Float,
        x2: Float,
        y2: Float,
        colour: Int
    ) {
        paint.color = colours[colour]
        paint.strokeWidth = thickness.coerceAtLeast(1f)
        canvas.drawLine(x1, y1, x2, y2, paint)
        paint.strokeWidth = 1f
    }

    @UsedByJNI
    override fun drawPoly(
        thickness: Float,
        points: IntArray,
        ox: Int,
        oy: Int,
        line: Int,
        fill: Int
    ) {
        val path = Path()
        path.moveTo((points[0] + ox).toFloat(), (points[1] + oy).toFloat())
        for (i in 1 until points.size / 2) {
            path.lineTo((points[2 * i] + ox).toFloat(), (points[2 * i + 1] + oy).toFloat())
        }
        path.close()
        // cheat slightly: polygons up to square look prettier without (and adjacent squares want to
        // look continuous in lightup)
        val disableAntiAlias = points.size <= 8 // 2 per point
        if (disableAntiAlias) paint.isAntiAlias = false
        drawPoly(thickness, path, line, fill)
        paint.isAntiAlias = true
    }

    private fun drawPoly(thickness: Float, p: Path, lineColour: Int, fillColour: Int) {
        if (fillColour != -1) {
            paint.color = colours[fillColour]
            paint.style = Paint.Style.FILL
            canvas.drawPath(p, paint)
        }
        paint.color = colours[lineColour]
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = thickness.coerceAtLeast(1f)
        canvas.drawPath(p, paint)
        paint.strokeWidth = 1f
    }

    @UsedByJNI
    override fun drawCircle(
        thickness: Float,
        x: Float,
        y: Float,
        r: Float,
        lineColour: Int,
        fillColour: Int
    ) {
        drawCircleInternal(
            thickness,
            x,
            y,
            r.coerceAtLeast(0.4f),
            lineColour,
            if (r <= 0.5f) lineColour else fillColour
        )
    }

    private fun drawCircleInternal(
        thickness: Float,
        x: Float,
        y: Float,
        r: Float,
        lineColour: Int,
        fillColour: Int
    ) {
        if (fillColour != -1) {
            paint.color = colours[fillColour]
            paint.style = Paint.Style.FILL
            canvas.drawCircle(x, y, r, paint)
        }
        paint.color = colours[lineColour]
        paint.style = Paint.Style.STROKE
        if (thickness > 1f) {
            paint.strokeWidth = thickness
        }
        canvas.drawCircle(x, y, r, paint)
        paint.strokeWidth = 1f
    }

    @UsedByJNI
    override fun drawText(x: Int, y: Int, flags: Int, size: Int, colour: Int, text: String) {
        paint.color = colours[colour]
        paint.style = Paint.Style.FILL
        paint.typeface =
            if (flags and TEXT_MONO != 0) Typeface.MONOSPACE else Typeface.DEFAULT
        paint.textSize = size.toFloat()
        val fm = paint.fontMetrics
        val asc = abs(fm.ascent)
        val desc = abs(fm.descent)
        if (flags and ALIGN_H_CENTRE != 0) paint.textAlign =
            Paint.Align.CENTER else if (flags and ALIGN_H_RIGHT != 0) paint.textAlign =
            Paint.Align.RIGHT else paint.textAlign = Paint.Align.LEFT
        val alignedY = if (flags and ALIGN_V_CENTRE != 0) y + (asc - (asc + desc) / 2).toInt() else y
        canvas.drawText(text, x.toFloat(), alignedY.toFloat(), paint)
    }

    @UsedByJNI
    override fun blitterAlloc(w: Int, h: Int): Int {
        for (i in blitters.indices) {
            if (blitters[i] == null) {
                val zoom = getXScale(zoomMatrix)
                blitters[i] =
                    createBitmap((zoom * w).roundToInt(), (zoom * h).roundToInt(), BITMAP_CONFIG)
                return i
            }
        }
        error("No free blitter found!")
    }

    @UsedByJNI
    override fun blitterFree(i: Int) {
        blitters[i]?.recycle()
        blitters[i] = null
    }

    private fun blitterPosition(x: Int, y: Int, save: Boolean): PointF {
        val f = floatArrayOf(x.toFloat(), y.toFloat())
        zoomMatrix.mapPoints(f)
        f[0] = floor(f[0].toDouble()).toFloat()
        f[1] = floor(f[1].toDouble()).toFloat()
        if (save) {
            f[0] *= -1f
            f[1] *= -1f
        }
        return PointF(f[0], f[1])
    }

    @UsedByJNI
    override fun blitterSave(i: Int, x: Int, y: Int) {
        blitters[i]?.let {
            val blitterPosition = blitterPosition(x, y, true)
            Canvas(it).drawBitmap(bitmap, blitterPosition.x, blitterPosition.y, null)
        }
    }

    @UsedByJNI
    override fun blitterLoad(i: Int, x: Int, y: Int) {
        blitters[i]?.let {
            val blitterPosition = blitterPosition(x, y, false)
            Canvas(bitmap).drawBitmap(it, blitterPosition.x, blitterPosition.y, null)
        }
    }

    @VisibleForTesting
    fun screenshot(gameCoords: Rect, gameSizeInGameCoords: Point): Bitmap {
        val offX = (wDip - gameSizeInGameCoords.x) / 2
        val offY = (hDip - gameSizeInGameCoords.y) / 2
        val r = RectF(
            (gameCoords.left + offX).toFloat(),
            (gameCoords.top + offY).toFloat(),
            (gameCoords.right + offX).toFloat(),
            (gameCoords.bottom + offY).toFloat()
        )
        zoomMatrix.mapRect(r)
        return Bitmap.createBitmap(
            bitmap,
            r.left.toInt(),
            r.top.toInt(),
            (r.right - r.left).toInt(),
            (r.bottom - r.top).toInt()
        )
    }

    companion object {
        private const val TAG = "GameView"
        private const val LEFT_BUTTON = 0x0200
        private const val MIDDLE_BUTTON = 0x201
        private const val RIGHT_BUTTON = 0x202
        private const val LEFT_DRAG = 0x203 //MIDDLE_DRAG = 0x204, RIGHT_DRAG = 0x205,
        private const val LEFT_RELEASE = 0x206
        const val FIRST_MOUSE = LEFT_BUTTON
        const val LAST_MOUSE = 0x208
        private const val MOD_CTRL = 0x1000
        private const val MOD_SHIFT = 0x2000
        private const val ALIGN_V_CENTRE = 0x100
        private const val ALIGN_H_CENTRE = 0x001
        private const val ALIGN_H_RIGHT = 0x002
        private const val TEXT_MONO = 0x10
        private const val DRAG = LEFT_DRAG - LEFT_BUTTON // not bit fields, but there's a pattern
        private const val RELEASE = LEFT_RELEASE - LEFT_BUTTON
        const val CURSOR_UP = 0x209
        const val CURSOR_DOWN = 0x20a
        const val CURSOR_LEFT = 0x20b
        const val CURSOR_RIGHT = 0x20c
        const val UI_UNDO = 0x213
        const val UI_REDO = 0x214
        const val MOD_NUM_KEYPAD = 0x4000
        val CURSOR_KEYS = setOf(
            CURSOR_UP,
            CURSOR_DOWN,
            CURSOR_LEFT,
            CURSOR_RIGHT,
            MOD_NUM_KEYPAD or '7'.code,
            MOD_NUM_KEYPAD or '1'.code,
            MOD_NUM_KEYPAD or '9'.code,
            MOD_NUM_KEYPAD or '3'.code
        )
        private const val MAX_ZOOM = 30f
        private const val ZOOM_OVERDRAW_PROPORTION =
            0.25f // of a screen-full, in each direction, that you can see before checkerboard

        // ARGB_8888 is viewable in Android Studio debugger but very memory-hungry
        private val BITMAP_CONFIG = Bitmap.Config.RGB_565
    }
}
