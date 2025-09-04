package name.boyle.chris.sgtpuzzles.backend

import android.content.Context
import android.graphics.Point
import android.graphics.RectF
import androidx.annotation.VisibleForTesting
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder
import name.boyle.chris.sgtpuzzles.launch.MenuEntry
import java.io.ByteArrayOutputStream

interface GameEngine {

    @UsedByJNI
    interface ActivityCallbacks {
        fun allowFlash(): Boolean
        fun changedState(canUndo: Boolean, canRedo: Boolean)
        fun completed()
        fun inertiaFollow(isSolved: Boolean)
        fun purgingStates()
        fun requestTimer(on: Boolean)
        fun setStatus(status: String)
    }

    @UsedByJNI
    interface ViewCallbacks {
        fun blitterAlloc(w: Int, h: Int): Int
        fun blitterFree(i: Int)
        fun blitterLoad(i: Int, x: Int, y: Int)
        fun blitterSave(i: Int, x: Int, y: Int)
        fun clipRect(x: Int, y: Int, w: Int, h: Int)
        fun drawCircle(
            thickness: Float,
            x: Float,
            y: Float,
            r: Float,
            lineColour: Int,
            fillColour: Int
        )
        fun drawLine(thickness: Float, x1: Float, y1: Float, x2: Float, y2: Float, colour: Int)
        fun drawPoly(thickness: Float, points: IntArray, ox: Int, oy: Int, line: Int, fill: Int)
        fun drawText(x: Int, y: Int, flags: Int, size: Int, colour: Int, text: String)
        fun fillRect(x: Int, y: Int, w: Int, h: Int, colour: Int)
        val defaultBackgroundColour: Int
        fun postInvalidateOnAnimation()
        fun unClip(left: Int, top: Int, right: Int, bottom: Int)
    }

    fun onDestroy()

    fun configEvent(
        activityCallbacks: ConfigBuilder.ActivityCallbacks,
        whichEvent: Int,
        context: Context,
        backendName: BackendName
    )
    fun configEvent(whichEvent: Int, builder: ConfigBuilder)
    fun configSetString(itemPtr: String, s: String, isPrefs: Boolean)
    fun configSetBool(itemPtr: String, selected: Boolean, isPrefs: Boolean)
    fun configSetChoice(itemPtr: String, selected: Int, isPrefs: Boolean)
    fun savePrefs(context: Context)
    fun loadPrefs(context: Context)

    data class KeysResult @UsedByJNI constructor(
        val keys: String?,
        val keysIfArrows: String?,
        val arrowMode: ArrowMode?
    )

    fun requestKeys(backend: BackendName, params: String?, palisadeFullCursor: Boolean): KeysResult?

    fun timerTick()
    fun htmlHelpTopic(): String?
    fun keyEvent(x: Int, y: Int, k: Int)
    fun restartEvent()
    fun solveEvent()
    fun resizeEvent(x: Int, y: Int, bottomInset: Int)
    fun serialise(baos: ByteArrayOutputStream)
    val currentParams: String?
    fun setCursorVisibility(visible: Boolean)
    val presets: Array<MenuEntry>
    val uiVisibility: Int
    fun resetTimerBaseline()
    fun purgeStates()
    val isCompletedNow: Boolean
    val colours: FloatArray
    fun suggestDensity(x: Int, y: Int): Float
    val cursorLocation: RectF
    @get:VisibleForTesting
    val gameSizeInGameCoords: Point
    @VisibleForTesting
    fun freezePartialRedo()
    fun setViewCallbacks(viewCallbacks: ViewCallbacks)

    companion object {

        @JvmField
        val NOT_LOADED_YET: GameEngine = object : GameEngine {
            override fun onDestroy() {}
            override fun configEvent(
                activityCallbacks: ConfigBuilder.ActivityCallbacks,
                whichEvent: Int,
                context: Context,
                backendName: BackendName
            ) {}
            override fun configEvent(whichEvent: Int, builder: ConfigBuilder) {}
            override fun configSetString(itemPtr: String, s: String, isPrefs: Boolean) {}
            override fun configSetBool(itemPtr: String, selected: Boolean, isPrefs: Boolean) {}
            override fun configSetChoice(itemPtr: String, selected: Int, isPrefs: Boolean) {}
            override fun savePrefs(context: Context) {}
            override fun loadPrefs(context: Context) {}
            override fun requestKeys(backend: BackendName, params: String?, palisadeFullCursor: Boolean): KeysResult? = null
            override fun timerTick() {}
            override fun htmlHelpTopic(): String? = null
            override fun keyEvent(x: Int, y: Int, k: Int) {}
            override fun restartEvent() {}
            override fun solveEvent() {}
            override fun resizeEvent(x: Int, y: Int, bottomInset: Int) {}
            override fun serialise(baos: ByteArrayOutputStream) {}
            override val currentParams: String? get() = null
            override fun setCursorVisibility(visible: Boolean) {}
            override val presets: Array<MenuEntry> get() = arrayOf()
            override val uiVisibility: Int get() = 0
            override fun resetTimerBaseline() {}
            override fun purgeStates() {}
            override val isCompletedNow: Boolean get() = false
            override val colours: FloatArray get() = FloatArray(0)
            override fun suggestDensity(x: Int, y: Int): Float = 1f
            override val cursorLocation: RectF get() = RectF(0f, 0f, 1f, 1f)
            override val gameSizeInGameCoords: Point get() = Point(1, 1)
            override fun freezePartialRedo() {}
            override fun setViewCallbacks(viewCallbacks: ViewCallbacks) {}
        }
    }
}