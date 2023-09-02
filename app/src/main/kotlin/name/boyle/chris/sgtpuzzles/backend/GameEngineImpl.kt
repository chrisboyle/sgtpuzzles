package name.boyle.chris.sgtpuzzles.backend

import android.content.Context
import android.graphics.Point
import android.graphics.RectF
import android.util.Log
import androidx.annotation.VisibleForTesting
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.backend.GameEngine.KeysResult
import name.boyle.chris.sgtpuzzles.backend.GameEngine.ViewCallbacks
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder.EngineCallbacks
import name.boyle.chris.sgtpuzzles.config.CustomDialogBuilder
import name.boyle.chris.sgtpuzzles.launch.GameLaunch
import name.boyle.chris.sgtpuzzles.launch.MenuEntry
import java.io.ByteArrayOutputStream

class GameEngineImpl @UsedByJNI private constructor(
    @field:UsedByJNI private val _nativeFrontend: Long,
    val backend: BackendName
) : EngineCallbacks, GameEngine {

    external override fun onDestroy()

    override fun configEvent(
        activityCallbacks: ConfigBuilder.ActivityCallbacks,
        whichEvent: Int,
        context: Context,
        backendName: BackendName
    ) {
        configEvent(
            whichEvent,
            CustomDialogBuilder(context, this, activityCallbacks, whichEvent, backendName)
        )
    }

    external override fun configEvent(whichEvent: Int, builder: ConfigBuilder)

    override fun savePrefs(context: Context) {
        ByteArrayOutputStream().use {
            serialisePrefs(it)
            savePrefs(context, backend, it.toString())
        }
    }

    override fun loadPrefs(context: Context) {
        val toLoad = getPrefs(context, backend)
        Log.d("Prefs", "Loading ${backend.preferencesName}: \"$toLoad\"")
        toLoad?.let { deserialisePrefs(it) }
    }

    private external fun serialisePrefs(baos: ByteArrayOutputStream)
    private external fun deserialisePrefs(prefs: String)
    external override fun configOK(): String
    override val fullGameIDFromDialog: String external get
    override val fullSeedFromDialog: String external get
    external override fun configCancel()
    external override fun configSetString(itemPtr: String, s: String, isPrefs: Boolean)
    external override fun configSetBool(itemPtr: String, selected: Boolean, isPrefs: Boolean)
    external override fun configSetChoice(itemPtr: String, selected: Int, isPrefs: Boolean)

    external override fun requestKeys(backend: BackendName, params: String?): KeysResult?
    external override fun timerTick()
    external override fun htmlHelpTopic(): String?
    external override fun keyEvent(x: Int, y: Int, k: Int)
    external override fun restartEvent()
    external override fun solveEvent()
    external override fun resizeEvent(x: Int, y: Int)
    external override fun serialise(baos: ByteArrayOutputStream)
    override val currentParams: String? external get
    external override fun setCursorVisibility(visible: Boolean)
    override val presets: Array<MenuEntry?> external get
    override val uiVisibility: Int external get
    external override fun resetTimerBaseline()
    external override fun purgeStates()
    override val isCompletedNow: Boolean external get
    override val colours: FloatArray external get
    external override fun suggestDensity(x: Int, y: Int): Float
    override val cursorLocation: RectF external get
    @get:VisibleForTesting
    override val gameSizeInGameCoords: Point external get
    @VisibleForTesting
    external override fun freezePartialRedo()

    companion object {

        @JvmStatic
        fun fromLaunch(
            launch: GameLaunch,
            activityCallbacks: GameEngine.ActivityCallbacks,
            viewCallbacks: ViewCallbacks,
            contextForPrefs: Context
        ): GameEngine {
            return if (launch.saved != null) {
                val initialPrefs = getPrefs(contextForPrefs, identifyBackend(launch.saved))
                fromSavedGame(launch.saved, activityCallbacks, viewCallbacks, initialPrefs)
            } else if (launch.gameID != null) {
                val initialPrefs = getPrefs(contextForPrefs, launch.whichBackend)
                fromGameID(launch.gameID, launch.whichBackend, activityCallbacks, viewCallbacks, initialPrefs)
            } else {
                throw IllegalArgumentException("GameEngine.fromLaunch without saved game or id")
            }
        }

        @JvmStatic
        private external fun fromSavedGame(
            savedGame: String?,
            activityCallbacks: GameEngine.ActivityCallbacks,
            viewCallbacks: ViewCallbacks,
            initialPrefs: String?
        ): GameEngine

        @JvmStatic
        private external fun fromGameID(
            gameID: String?,
            backendName: BackendName,
            activityCallbacks: GameEngine.ActivityCallbacks,
            viewCallbacks: ViewCallbacks,
            initialPrefs: String?
        ): GameEngine

        @JvmStatic
        external fun identifyBackend(savedGame: String?): BackendName

        @JvmStatic
        external fun getDefaultParams(backend: BackendName?): String

        @JvmStatic
        fun forPreferencesOnly(backendName: BackendName, context: Context): GameEngine =
            forPreferencesOnly(backendName, getPrefs(context, backendName))

        @JvmStatic
        external fun forPreferencesOnly(backendName: BackendName?, initialPrefs: String?): GameEngine

        fun savePrefs(context: Context, backend: BackendName, serialised: String) {
            Log.d("Prefs", "Saving ${backend.preferencesName}: \"$serialised\"")
            PreferenceManager.getDefaultSharedPreferences(context).edit()
                .putString(backend.preferencesName, serialised)
                .apply()
        }

        @JvmStatic
        fun getPrefs(context: Context, backend: BackendName): String? {
            var toLoad = PreferenceManager.getDefaultSharedPreferences(context)
                .getString(backend.preferencesName, null)
            if (toLoad != null) {
                // Work around Android bug https://issuetracker.google.com/issues/37032278
                // (incorrectly marked as obsolete even though it still applies on Android 13)
                // in which "foo\n" is read back as "foo\n    ". Remove just the spaces.
                toLoad = toLoad.replaceFirst(" +$".toRegex(), "")
            }
            return toLoad
        }
    }
}