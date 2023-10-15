package name.boyle.chris.sgtpuzzles

import android.annotation.SuppressLint
import android.app.Dialog
import android.content.ActivityNotFoundException
import android.content.DialogInterface
import android.content.Intent
import android.content.SharedPreferences
import android.content.SharedPreferences.OnSharedPreferenceChangeListener
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.content.res.Configuration.HARDKEYBOARDHIDDEN_NO
import android.content.res.Configuration.KEYBOARD_NOKEYS
import android.content.res.Configuration.NAVIGATIONHIDDEN_YES
import android.content.res.Configuration.NAVIGATION_DPAD
import android.content.res.Configuration.NAVIGATION_TRACKBALL
import android.content.res.Resources
import android.graphics.Color
import android.graphics.PointF
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.Message
import android.provider.OpenableColumns
import android.util.Log
import android.view.Gravity
import android.view.KeyEvent
import android.view.KeyEvent.KEYCODE_MENU
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuItem
import android.view.MenuItem.SHOW_AS_ACTION_ALWAYS
import android.view.MenuItem.SHOW_AS_ACTION_WITH_TEXT
import android.view.View
import android.view.WindowManager
import android.view.WindowManager.BadTokenException
import android.widget.Button
import android.widget.RelativeLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.widget.PopupMenu
import androidx.core.app.NavUtils
import androidx.core.app.ShareCompat.IntentBuilder
import androidx.core.content.FileProvider
import androidx.core.content.res.ResourcesCompat
import androidx.core.graphics.drawable.DrawableCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.GameView.LimitDPIMode.LIMIT_AUTO
import name.boyle.chris.sgtpuzzles.GameView.LimitDPIMode.LIMIT_OFF
import name.boyle.chris.sgtpuzzles.GameView.LimitDPIMode.LIMIT_ON
import name.boyle.chris.sgtpuzzles.NightModeHelper.Companion.isNight
import name.boyle.chris.sgtpuzzles.Utils.readAllOf
import name.boyle.chris.sgtpuzzles.Utils.sendFeedbackDialog
import name.boyle.chris.sgtpuzzles.Utils.toastFirstFewTimes
import name.boyle.chris.sgtpuzzles.Utils.unlikelyBug
import name.boyle.chris.sgtpuzzles.backend.BRIDGES
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.BackendName.Companion.byLowerCase
import name.boyle.chris.sgtpuzzles.backend.FLOOD
import name.boyle.chris.sgtpuzzles.backend.GUESS
import name.boyle.chris.sgtpuzzles.backend.GameEngine
import name.boyle.chris.sgtpuzzles.backend.GameEngine.KeysResult
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl.Companion.fromLaunch
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl.Companion.getDefaultParams
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl.Companion.getPrefs
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl.Companion.identifyBackend
import name.boyle.chris.sgtpuzzles.backend.INERTIA
import name.boyle.chris.sgtpuzzles.backend.MINES
import name.boyle.chris.sgtpuzzles.backend.NET
import name.boyle.chris.sgtpuzzles.backend.PALISADE
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.UNDEAD
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL
import name.boyle.chris.sgtpuzzles.backend.UsedByJNI
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode
import name.boyle.chris.sgtpuzzles.buttons.ButtonsView
import name.boyle.chris.sgtpuzzles.buttons.SEEN_SWAP_L_R_TOAST
import name.boyle.chris.sgtpuzzles.buttons.SWAP_L_R_KEY
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder
import name.boyle.chris.sgtpuzzles.config.ConfigBuilder.Event.CFG_SETTINGS
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.ARROW_KEYS_KEY_SUFFIX
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.AUTO_ORIENT
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.AUTO_ORIENT_DEFAULT
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.BRIDGES_SHOW_H_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CHOOSER_STYLE_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.COMPLETED_PROMPT_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CONTROLS_REMINDERS_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.FULLSCREEN_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.LAST_PARAMS_PREFIX
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.LATIN_M_UNDO_SEEN
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.LATIN_SHOW_M_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.LIMIT_DPI_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.MOUSE_BACK_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.MOUSE_LONG_PRESS_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.ORIENTATION_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.REDO_NEW_GAME_SEEN
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.SAVED_BACKEND
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.SAVED_COMPLETED_PREFIX
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.SAVED_GAME_PREFIX
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.STATE_PREFS_NAME
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.STAY_AWAKE_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.UNDO_NEW_GAME_SEEN
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.UNDO_REDO_KBD_DEFAULT
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.UNDO_REDO_KBD_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.UNEQUAL_SHOW_H_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.VICTORY_FLASH_KEY
import name.boyle.chris.sgtpuzzles.databinding.CompletedDialogBinding
import name.boyle.chris.sgtpuzzles.databinding.MainBinding
import name.boyle.chris.sgtpuzzles.launch.DeprecatedProgressDialog
import name.boyle.chris.sgtpuzzles.launch.GameGenerator
import name.boyle.chris.sgtpuzzles.launch.GameGenerator.Companion.cleanUpOldExecutables
import name.boyle.chris.sgtpuzzles.launch.GameGenerator.Companion.executableIsMissing
import name.boyle.chris.sgtpuzzles.launch.GameLaunch
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.fromContentURI
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.ofGameID
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.ofLocalState
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.ofSavedGameFromIntent
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.toGenerate
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.toGenerateFromChooser
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Companion.undoingOrRedoingNewGame
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Origin.BUTTON_OR_MENU_IN_ACTIVITY
import name.boyle.chris.sgtpuzzles.launch.GameLaunch.Origin.INTENT_COMPLEX_URI
import name.boyle.chris.sgtpuzzles.launch.MenuEntry
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.lang.ref.WeakReference
import java.text.MessageFormat
import java.util.concurrent.Future
import java.util.regex.Pattern
import kotlin.math.roundToInt

class GamePlay : ActivityWithLoadButton(), OnSharedPreferenceChangeListener, GameGenerator.Callback,
    ConfigBuilder.ActivityCallbacks, GameEngine.ActivityCallbacks {
    private lateinit var _binding: MainBinding
    private lateinit var statusBar: TextView
    private lateinit var newKeyboard: ButtonsView
    private lateinit var mainLayout: RelativeLayout
    private lateinit var gameView: GameView
    private lateinit var prefs: SharedPreferences
    private lateinit var state: SharedPreferences
    private lateinit var gameGenerator: GameGenerator
    private var progress: DeprecatedProgressDialog? = null
    private val gameTypesById: MutableMap<Int, String> = linkedMapOf()
    private var gameTypesMenu = arrayOf<MenuEntry>()
    private var currentType = 0
    private var generationInProgress: Future<*>? = null
    private var solveEnabled = false
    private var customVisible = false
    private var undoEnabled = false
    private var redoEnabled = false
    private var undoIsLoadGame = false
    private var redoIsLoadGame = false
    private var undoToGame: String? = null
    private var redoToGame: String? = null
    private var gameWantsTimer = false

    // specifically the screenshots test
    @get:VisibleForTesting
    var gameEngine = GameEngine.NOT_LOADED_YET
        private set
    var currentBackend: BackendName? = null
    private var startingBackend: BackendName? = null
    private var lastKeys = ""
    private var lastKeysIfArrows = ""
    private var menu: Menu? = null
    private var maybeUndoRedo = "" + GameView.UI_UNDO.toChar() + GameView.UI_REDO.toChar()
    private var everCompleted = false
    private var _wasNight = false
    private var appStartIntentOnResume: Intent? = null
    private var swapLROn = false

    private enum class UIVisibility(val value: Int) {
        UNDO(1), REDO(2), CUSTOM(4), SOLVE(8), STATUS(16)
    }

    private enum class MsgType {
        TIMER, COMPLETED, BACK_TIME_ELAPSED
    }

    private val onBackPressedCallback = object : OnBackPressedCallback(false) {
        override fun handleOnBackPressed() {
            // Consume the press and do nothing, because the user pressed a key very recently
            // so it was probably accidental. If they meant it, they can try again, by which time
            // this handler will be disabled.
        }
    }

    private class PuzzlesHandler(outer: GamePlay) : Handler(Looper.getMainLooper()) {
        val ref: WeakReference<GamePlay>

        init {
            ref = WeakReference(outer)
        }

        override fun handleMessage(msg: Message) {
            val outer = ref.get()
            outer?.handleMessage(msg)
        }
    }

    val handler: Handler = PuzzlesHandler(this)

    private fun handleMessage(msg: Message) {
        when (MsgType.values()[msg.what]) {
            MsgType.TIMER -> {
                if (progress == null) {
                    gameEngine.timerTick()
                    if (currentBackend === INERTIA) {
                        gameView.ensureCursorVisible(gameEngine.cursorLocation)
                    }
                }
                if (gameWantsTimer) {
                    handler.sendMessageDelayed(
                        handler.obtainMessage(MsgType.TIMER.ordinal),
                        TIMER_INTERVAL.toLong()
                    )
                }
            }

            MsgType.COMPLETED -> {
                try {
                    completedInternal()
                } catch (activityWentAway: BadTokenException) {
                    // fine, nothing we can do here
                    Log.d(TAG, "completed failed!", activityWentAway)
                }
            }

            MsgType.BACK_TIME_ELAPSED -> {
                onBackPressedCallback.isEnabled = false
            }
        }
    }

    private fun showProgress(launch: GameLaunch) {
        progress = DeprecatedProgressDialog(this, launch, {
            abort(null, launch.origin.shouldReturnToChooserOnFail)
        }, {
            resetBackendState(launch.whichBackend)
            currentBackend = null // prevent save undoing our reset
            abort(null, true)
        })
    }

    private fun resetBackendState(backend: BackendName) {
        state.edit().apply {
            remove(SAVED_GAME_PREFIX + backend)
            remove(SAVED_COMPLETED_PREFIX + backend)
            remove(LAST_PARAMS_PREFIX + backend)
            apply()
        }
        prefs.edit().remove(backend.preferencesName).apply()
    }

    private fun dismissProgress() {
        try {
            progress?.dismiss()
        } catch (ignored: IllegalArgumentException) {
        } // race condition?
        progress = null
    }

    private fun saveToString(): String {
        check(currentBackend != null && progress == null) { "saveToString in invalid state" }
        val baos = ByteArrayOutputStream()
        gameEngine.serialise(baos)
        val saved = baos.toString()
        check(saved.isNotEmpty()) { "serialise returned empty string" }
        return saved
    }

    private fun save() {
        if (currentBackend == null) return
        val saved = saveToString()
        state.edit().apply {
            remove("engineName")
            putString(SAVED_BACKEND, currentBackend.toString())
            putString(SAVED_GAME_PREFIX + currentBackend, saved)
            putBoolean(SAVED_COMPLETED_PREFIX + currentBackend, everCompleted)
            putString(LAST_PARAMS_PREFIX + currentBackend, gameEngine.currentParams)
            apply()
        }
    }

    fun gameViewResized() {
        if (progress == null && gameView.w > 10 && gameView.h > 10)
            gameEngine.resizeEvent(gameView.wDip, gameView.hDip)
    }

    fun suggestDensity(w: Int, h: Int): Float {
        return gameEngine.suggestDensity(w, h)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        prefs = PreferenceManager.getDefaultSharedPreferences(this)
        prefs.registerOnSharedPreferenceChangeListener(this)
        state = getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE)
        gameGenerator = GameGenerator()
        applyStayAwake()
        applyOrientation()
        super.onCreate(savedInstanceState)
        if (executableIsMissing(this)) {
            finish()
            return
        }
        inflateContent()
        applyFullscreen()
        setDefaultKeyMode(DEFAULT_KEYS_SHORTCUT)
        onBackPressedDispatcher.addCallback(onBackPressedCallback)
        appStartIntentOnResume = intent
        cleanUpOldExecutables(prefs, state, File(applicationInfo.dataDir))
    }

    // Also called from onConfigurationChanged()
    private fun inflateContent() {
        _binding = MainBinding.inflate(layoutInflater)
        setContentView(_binding.root)
        supportActionBar?.apply {
            setDisplayHomeAsUpEnabled(true)
            setDisplayUseLogoEnabled(false)
            addOnMenuVisibilityListener { visible: Boolean ->
                // https://code.google.com/p/android/issues/detail?id=69205
                if (!visible) {
                    this@GamePlay.invalidateOptionsMenu()
                    rethinkActionBarCapacity()
                }
            }
        }
        mainLayout = _binding.mainLayout
        statusBar = _binding.statusBar
        gameView = _binding.gameView
        newKeyboard = _binding.newKeyboard.apply {
            onKeyListener.value = { c: Int, isRepeat: Boolean ->
                val ch = c.toChar()
                val maybeLowercase =
                    if (!swapLROn && currentBackend === PALISADE && "HJKL".indexOf(ch) > -1) ch.lowercaseChar()
                    else ch
                runOnUiThread {
                    sendKey(0, 0, maybeLowercase.code, isRepeat)
                }
            }
            onSwapLRListener.value = { swap: Boolean ->
                runOnUiThread {
                    setSwapLR(swap)
                    toastFirstFewTimes(
                        this@GamePlay, state, SEEN_SWAP_L_R_TOAST, 4,
                        if (swap) R.string.toast_swap_l_r_on else R.string.toast_swap_l_r_off
                    )
                }
            }
        }
        gameView.requestFocus()
        _wasNight = isNight(resources.configuration)
        applyLimitDPI(false)
        applyMouseLongPress()
        applyMouseBackKey()
        window.setBackgroundDrawable(null)
    }

    /** work around Android issue 21181 whose bug page has vanished :-(  */
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return progress == null && (gameView.onKeyDown(keyCode, event)
                || super.onKeyDown(keyCode, event))
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KEYCODE_MENU) {
            if (hackForSubmenus == null) openOptionsMenu()
            hackForSubmenus!!.performIdentifierAction(R.id.game_menu, 0)
            return true
        }
        // work around Android issue 21181
        return progress == null && (gameView.onKeyUp(keyCode, event)
                || super.onKeyUp(keyCode, event))
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        // Only delegate for MENU key, delegating for other keys might break something(?)
        if (progress == null && event.keyCode == KEYCODE_MENU && gameView.dispatchKeyEvent(event))
            true
        else
            super.dispatchKeyEvent(event)

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        progress?.let {
            stopGameGeneration()
            dismissProgress()
        }
        var backendFromChooser: BackendName? = null
        // Don't regenerate on resurrecting a URL-bound activity from the recent list
        if (intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY == 0) {
            val s = intent.getStringExtra("game")
            val u = intent.data
            if (!s.isNullOrEmpty()) {
                Log.d(TAG, "starting game from Intent, " + s.length + " bytes")
                val launch: GameLaunch = try {
                    ofSavedGameFromIntent(s)
                } catch (e: IllegalArgumentException) {
                    abort(e.message, true) // invalid file
                    return
                }
                startGame(launch)
                return
            } else if (u != null) {
                Log.d(TAG, "URI is: \"$u\"")
                if (OUR_SCHEME == u.scheme) {
                    val split = u.schemeSpecificPart.split(":".toRegex(), limit = 2).toTypedArray()
                    val incoming = byLowerCase(split[0])
                    if (incoming == null) {
                        abort("Unrecognised game in URI: $u", true)
                        return
                    }
                    if (split.size > 1) {
                        if (split[1].contains(":")) {
                            startGame(ofGameID(incoming, split[1], INTENT_COMPLEX_URI))
                        } else {  // params only
                            startGame(toGenerate(incoming, split[1], INTENT_COMPLEX_URI))
                        }
                        return
                    }
                    if (incoming == currentBackend && !everCompleted) {
                        // already alive & playing incomplete game of that kind; keep it.
                        return
                    }
                    backendFromChooser = incoming
                }
                if (backendFromChooser == null) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                        && ("file".equals(u.scheme, ignoreCase = true) || u.scheme == null)
                    ) {
                        unlikelyBug(this, R.string.old_file_manager)
                        return
                    }
                    try {
                        checkSize(u)
                        startGame(fromContentURI(readAllOf(contentResolver.openInputStream(u))))
                    } catch (e: IllegalArgumentException) {
                        e.printStackTrace()
                        abort(e.message, true)
                    } catch (e: IOException) {
                        e.printStackTrace()
                        abort(e.message, true)
                    }
                    return
                }
            }
        }
        if (backendFromChooser != null) {
            val savedGame =
                state.getString(SAVED_GAME_PREFIX + backendFromChooser, null)
            // We have a saved game, and if it's completed the user probably wants a fresh one.
            // Theoretically we could silently load it and ask midend_status() but remembering is
            // still faster and some people play large games.
            val wasCompleted = state.getBoolean(
                SAVED_COMPLETED_PREFIX + backendFromChooser,
                false
            )
            if (savedGame == null || wasCompleted) {
                Log.d(TAG, "generating as requested")
                startGame(toGenerateFromChooser(backendFromChooser))
                return
            }
            Log.d(TAG, "restoring last state of $backendFromChooser")
            startGame(ofLocalState(backendFromChooser, savedGame, true))
        } else {
            val savedBackend = byLowerCase(state.getString(SAVED_BACKEND, null))
            if (savedBackend != null) {
                val savedGame =
                    state.getString(SAVED_GAME_PREFIX + savedBackend, null)
                if (savedGame == null) {
                    Log.e(TAG, "missing state for $savedBackend")
                    startGame(toGenerateFromChooser(savedBackend))
                } else {
                    Log.d(TAG, "normal launch; resuming game of $savedBackend")
                    startGame(ofLocalState(savedBackend, savedGame, false))
                }
            } else {
                Log.d(TAG, "no state, starting chooser")
                startChooserAndFinish()
            }
        }
    }

    private fun warnOfStateLoss(
        backend: BackendName,
        continueLoading: Runnable,
        returnToChooser: Boolean
    ) {
        var careAboutOldGame =
            !state.getBoolean(SAVED_COMPLETED_PREFIX + backend, true)
        if (careAboutOldGame) {
            val savedGame = state.getString(SAVED_GAME_PREFIX + backend, null)
            if (savedGame == null || savedGame.contains("NSTATES :1:1")) {
                careAboutOldGame = false
            }
        }
        if (careAboutOldGame) {
            AlertDialog.Builder(this@GamePlay)
                .setMessage(
                    MessageFormat.format(getString(R.string.replaceGame), backend.displayName)
                )
                .setPositiveButton(android.R.string.ok) { _: DialogInterface?, _: Int ->
                    continueLoading.run()
                }
                .setNegativeButton(android.R.string.cancel) { _: DialogInterface?, _: Int ->
                    abort(null, returnToChooser)
                }
                .show()
        } else {
            continueLoading.run()
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        super.onCreateOptionsMenu(menu)
        this.menu = menu
        menuInflater.inflate(R.menu.main, menu)
        applyUndoRedoKbd()
        return true
    }

    private var hackForSubmenus: Menu? = null
    override fun onPrepareOptionsMenu(menu: Menu): Boolean {
        super.onPrepareOptionsMenu(menu)
        hackForSubmenus = menu
        updateUndoRedoEnabled()
        val enableType = generationInProgress != null || gameTypesById.isNotEmpty() || customVisible
        menu.findItem(R.id.type_menu).apply {
            isEnabled = enableType
            isVisible = enableType
        }
        return true
    }

    private fun orientGameType(backendName: BackendName?, type: String): String {
        if (!prefs.getBoolean(AUTO_ORIENT, AUTO_ORIENT_DEFAULT)
            || backendName === SOLO  // Solo is square whatever happens so no point
        ) {
            return type
        }
        val viewLandscape = gameView.w > gameView.h
        val matcher = DIMENSIONS.matcher(type)
        if (matcher.matches()) {
            with(matcher) {
                val w = group(1)!!.toInt()
                val h = group(3)!!.toInt()
                val typeLandscape = w > h
                if (typeLandscape != viewLandscape) {
                    return group(3)!! + group(2)!! + "x" + group(2)!! + group(1) + group(4)!!
                }
            }
        }
        return type
    }

    private fun updateUndoRedoEnabled() {
        val undoItem = menu!!.findItem(R.id.undo)
        val redoItem = menu!!.findItem(R.id.redo)
        undoItem.isEnabled = undoEnabled
        redoItem.isEnabled = redoEnabled
        undoItem.setIcon(if (undoEnabled) R.drawable.ic_action_undo else R.drawable.ic_action_undo_disabled)
        redoItem.setIcon(if (redoEnabled) R.drawable.ic_action_redo else R.drawable.ic_action_redo_disabled)
    }

    private fun startChooserAndFinish() {
        NavUtils.navigateUpFromSameTask(this)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        val itemId = item.itemId
        var ret = true
        // these all build menus on demand because we had to wait for anchor buttons to exist
        when (itemId) {
            R.id.game_menu -> doGameMenu()
            R.id.type_menu -> doTypeMenu()
            R.id.help_menu -> doHelpMenu()
            android.R.id.home -> startChooserAndFinish()
            R.id.undo -> sendKey(0, 0, GameView.UI_UNDO)
            R.id.redo -> sendKey(0, 0, GameView.UI_REDO)
            else -> ret = super.onOptionsItemSelected(item)
        }
        // https://code.google.com/p/android/issues/detail?id=69205
        invalidateOptionsMenu()
        rethinkActionBarCapacity()
        return ret
    }

    private fun doGameMenu() {
        val gameMenu = popupMenuWithIcons()
        gameMenu.menuInflater.inflate(R.menu.game_menu, gameMenu.menu)
        val solveItem = gameMenu.menu.findItem(R.id.solve)
        solveItem.isEnabled = solveEnabled
        solveItem.isVisible = solveEnabled
        gameMenu.setOnMenuItemClickListener { item: MenuItem ->
            when (item.itemId) {
                R.id.new_game -> startNewGame()
                R.id.restart -> gameEngine.restartEvent()
                R.id.solve -> solveMenuItemClicked()
                R.id.load -> loadGame()
                R.id.save -> try {
                    saveLauncher.launch(suggestFilenameForShare())
                } catch (e: ActivityNotFoundException) {
                    unlikelyBug(this, R.string.saf_missing_short)
                }
                R.id.share -> share()
                R.id.settings -> startActivity(Intent(this@GamePlay, PrefsActivity::class.java)
                    .putExtra(PrefsActivity.PrefsMainFragment.BACKEND_EXTRA, currentBackend.toString()))
                else -> return@setOnMenuItemClickListener false
            }
            true
        }
        gameMenu.show()
    }

    private fun solveMenuItemClicked() {
        try {
            gameEngine.solveEvent()
        } catch (e: IllegalArgumentException) {
            messageBox(getString(R.string.Error), e.message, false)
        }
    }

    private val typeClickListener = MenuItem.OnMenuItemClickListener { item: MenuItem ->
        val itemId = item.itemId
        if (itemId == R.id.custom) {
            gameEngine.configEvent(this, CFG_SETTINGS.jni, this, currentBackend!!)
        } else {
            val presetParams = orientGameType(
                currentBackend, gameTypesById[itemId]!!
            )
            Log.d(TAG, "preset: $itemId: $presetParams")
            startGame(
                toGenerate(currentBackend!!, presetParams, BUTTON_OR_MENU_IN_ACTIVITY)
            )
        }
        true
    }

    private fun doTypeMenu(
        menuEntries: Array<MenuEntry> = gameTypesMenu,
        includeCustom: Boolean = true
    ) {
        val typeMenu = PopupMenu(this@GamePlay, findViewById(R.id.type_menu))
        typeMenu.menuInflater.inflate(R.menu.type_menu, typeMenu.menu)
        for (entry in menuEntries) {
            val added = typeMenu.menu.add(
                R.id.typeGroup,
                entry.id,
                Menu.NONE,
                orientGameType(currentBackend, entry.title)
            )
            if (entry.params != null) {
                added.setOnMenuItemClickListener(typeClickListener)
                if (currentType == entry.id) {
                    added.isChecked = true
                }
            } else if (entry.submenu != null) {
                if (menuContainsCurrent(entry.submenu)) {
                    added.isChecked = true
                }
                added.setOnMenuItemClickListener {
                    doTypeMenu(entry.submenu, false)
                    true
                }
            }
        }
        typeMenu.menu.setGroupCheckable(R.id.typeGroup, true, true)
        if (includeCustom) {
            typeMenu.menu.findItem(R.id.custom).apply {
                isVisible = customVisible
                setOnMenuItemClickListener(typeClickListener)
                if (currentType < 0) isChecked = true
            }
        }
        typeMenu.show()
    }

    private fun menuContainsCurrent(submenu: Array<MenuEntry>?): Boolean {
        for (entry in submenu!!) {
            if (entry.id == currentType) {
                return true
            }
            if (entry.submenu != null && menuContainsCurrent(entry.submenu)) {
                return true
            }
        }
        return false
    }

    private fun doHelpMenu() {
        PopupMenu(this@GamePlay, findViewById(R.id.help_menu)).apply {
            setForceShowIcon(true)
            menuInflater.inflate(R.menu.help_menu, menu)
            menu.findItem(R.id.solve).apply {
                isEnabled = solveEnabled
                isVisible = solveEnabled
            }
            menu.findItem(R.id.this_game).title = MessageFormat.format(
                getString(R.string.how_to_play_game), this@GamePlay.title
            )
            setOnMenuItemClickListener { item: MenuItem ->
                when (item.itemId) {
                    R.id.this_game -> {
                        startActivity(Intent(this@GamePlay, HelpActivity::class.java)
                                .putExtra(HelpActivity.TOPIC, gameEngine.htmlHelpTopic()))
                        return@setOnMenuItemClickListener true
                    }
                    R.id.solve -> {
                        solveMenuItemClicked()
                        return@setOnMenuItemClickListener true
                    }
                    R.id.feedback -> {
                        sendFeedbackDialog(this@GamePlay)
                        return@setOnMenuItemClickListener true
                    }
                    else -> false
                }
            }
            show()
        }
    }

    private fun popupMenuWithIcons(): PopupMenu =
        PopupMenu(this, findViewById(R.id.game_menu)).apply {
            setForceShowIcon(true)
        }

    private fun share() {
        val saved = saveToString()
        val uriWithMimeType: Uri = try {
            writeCacheFile(saved)
        } catch (e: IOException) {
            unlikelyBug(this, R.string.cache_fail_short)
            return
        }
        val intentBuilder = IntentBuilder(this)
            .setStream(uriWithMimeType)
            .setType(MIME_TYPE)
        startActivity(intentBuilder.createChooserIntent())
    }

    @Throws(IOException::class)
    private fun writeCacheFile(content: String): Uri {
        val shareDir = File(cacheDir, "share").apply { mkdir() }
        val file = File(shareDir, suggestFilenameForShare())
        FileOutputStream(file).use {
            it.write(content.toByteArray())
        }
        return FileProvider.getUriForFile(this, "$packageName.fileprovider", file)
    }

    private fun suggestFilenameForShare(): String = currentBackend!!.displayName + ".sgtp"

    private val saveLauncher =
        registerForActivityResult<String, Uri>(ActivityResultContracts.CreateDocument(MIME_TYPE)) { uri: Uri? ->
            if (uri == null) return@registerForActivityResult
            try {
                val saved = saveToString()
                contentResolver.openFileDescriptor(uri, "w").use { pfd ->
                    if (pfd == null) throw IOException("Could not open $uri")
                    FileOutputStream(pfd.fileDescriptor).use { fos ->
                        fos.write(saved.toByteArray())
                    }
                }
            } catch (e: IOException) {
                messageBox(
                    getString(R.string.Error),
                    getString(R.string.save_failed_prefix) + e.message,
                    false
                )
            }
        }

    private fun abort(why: String?, returnToChooser: Boolean) {
        stopGameGeneration()
        dismissProgress()
        if (!why.isNullOrEmpty()) {
            messageBox(getString(R.string.Error), why, returnToChooser)
        } else if (returnToChooser) {
            startChooserAndFinish()
            return
        }
        startingBackend = currentBackend
        currentBackend?.let {
            setKeys(gameEngine.requestKeys(it, gameEngine.currentParams))
        }
    }

    private fun startNewGame() {
        startGame(
            toGenerate(
                currentBackend!!,
                orientGameType(currentBackend, gameEngine.currentParams!!),
                BUTTON_OR_MENU_IN_ACTIVITY
            )
        )
    }

    override fun startGame(launch: GameLaunch) {
        startGame(launch, false)
    }

    private fun startGame(launch: GameLaunch, isRedo: Boolean) {
        Log.d(TAG, "startGame: $launch")
        check(progress == null) { "startGame while already starting!" }
        val previousGame: String?
        if (isRedo || launch.needsGenerating) {
            gameEngine.purgeStates()
            redoToGame = null
            previousGame = if (currentBackend == null) null else saveToString()
        } else {
            previousGame = null
        }
        showProgress(launch)
        stopGameGeneration()
        startingBackend = launch.whichBackend
        if (launch.needsGenerating) {
            startGameGeneration(launch, previousGame)
        } else if (!launch.origin.isOfLocalState && launch.saved != null) {
            warnOfStateLoss(
                launch.whichBackend,
                { startGameConfirmed(launch, previousGame) },
                launch.origin.shouldReturnToChooserOnFail
            )
        } else {
            startGameConfirmed(launch, previousGame)
        }
    }

    private fun startGameGeneration(launch: GameLaunch, previousGame: String?) {
        val args = listOf(launch.whichBackend.toString()) +
                (launch.seed?.let { listOf("--seed", it) } ?: listOf(decideParams(launch)))
        generationInProgress =
            gameGenerator.generate(applicationInfo, launch, args, previousGame, this)
    }

    private fun decideParams(launch: GameLaunch): String {
        launch.params?.let {
            Log.d(TAG, "Using specified params: $it")
            return it
        }
        getLastParams(launch.whichBackend)?.let {
            Log.d(TAG, "Using last params: $it")
            return it
        }
        orientGameType(launch.whichBackend, getDefaultParams(launch.whichBackend)).let {
            Log.d(TAG, "Using default params with orientation: $it")
            return it
        }
    }

    override fun gameGeneratorSuccess(launch: GameLaunch, previousGame: String?) {
        runOnUiThread { startGameConfirmed(launch, previousGame) }
    }

    override fun gameGeneratorFailure(e: Exception, launch: GameLaunch) {
        runOnUiThread {
            if (launch.origin.shouldResetBackendStateOnFail() && hasState(launch.whichBackend)) {
                resetBackendState(launch.whichBackend)
                generationInProgress = null
                dismissProgress()
                startGame(launch)
            } else {
                abort(e.message, launch.origin.shouldReturnToChooserOnFail)  // probably bogus params
            }
        }
    }

    private fun hasState(backend: BackendName): Boolean =
        state.run {
            contains(SAVED_GAME_PREFIX + backend)
                || contains(SAVED_COMPLETED_PREFIX + backend)
                || contains(LAST_PARAMS_PREFIX + backend)
        }

    private fun startGameConfirmed(launch: GameLaunch, previousGame: String?) {
        check(!(launch.saved == null && launch.gameID == null)) { "startGameConfirmed with un-generated game" }
        val changingGame = previousGame == null || startingBackend !== identifyBackend(previousGame)
        undoToGame = if (previousGame != null && !changingGame && previousGame != launch.saved) {
            previousGame
        } else {
            null
        }
        gameEngine = try {
            fromLaunch(launch, this, gameView, this)
        } catch (e: IllegalArgumentException) {
            abort(e.message, launch.origin.shouldReturnToChooserOnFail) // probably bogus params
            return
        }
        currentBackend = startingBackend
        refreshColours()
        gameView.resetZoomForClear()
        gameView.clear()
        applyUndoRedoKbd()
        gameView.keysHandled = 0
        everCompleted = false
        val currentParams = orientGameType(currentBackend, gameEngine.currentParams!!)
        refreshPresets(currentParams)
        gameView.setDragModeFor(currentBackend)
        title = currentBackend!!.displayName
        supportActionBar?.title = currentBackend!!.title
        val flags = gameEngine.uiVisibility
        changedState(
            flags and UIVisibility.UNDO.value > 0,
            flags and UIVisibility.REDO.value > 0
        )
        customVisible = flags and UIVisibility.CUSTOM.value > 0
        solveEnabled = flags and UIVisibility.SOLVE.value > 0
        setStatusBarVisibility(flags and UIVisibility.STATUS.value > 0)
        setKeys(gameEngine.requestKeys(currentBackend!!, currentParams))
        inertiaFollow(false)
        // We have a saved completion flag but completion could have been undone; find out whether
        // it's really completed
        if (launch.origin.shouldHighlightCompletionOnLaunch() && gameEngine.isCompletedNow) {
            completed()
        }
        val hasArrows = computeArrowMode(currentBackend).hasArrows()
        gameEngine.setCursorVisibility(hasArrows)
        if (changingGame) {
            if (prefs.getBoolean(CONTROLS_REMINDERS_KEY, true)) {
                if (hasArrows || !showToastIfExists(currentBackend!!.controlsToastNoArrows)) {
                    showToastIfExists(currentBackend!!.controlsToast)
                }
            }
        }
        dismissProgress()
        gameView.rebuildBitmap()
        menu?.let { onPrepareOptionsMenu(it) }
        save()
    }

    private fun showToastIfExists(@StringRes reminderId: Int): Boolean {
        if (reminderId == 0) {
            return false
        }
        Toast.makeText(this@GamePlay, reminderId, Toast.LENGTH_LONG).show()
        return true
    }

    private fun refreshPresets(currentParams: String) {
        currentType = -1
        gameTypesMenu = gameEngine.presets
        gameTypesById.clear()
        populateGameTypesById(gameTypesMenu, currentParams)
    }

    private fun populateGameTypesById(menuEntries: Array<MenuEntry>, currentParams: String) {
        for (entry in menuEntries) {
            entry.params?.let {
                gameTypesById[entry.id] = it
                if (orientGameType(currentBackend, currentParams)
                        == orientGameType(currentBackend, it)) {
                    currentType = entry.id
                }
            }
            entry.submenu?.let {
                populateGameTypesById(it, currentParams)
            }
        }
    }

    private fun checkSize(uri: Uri) {
        val cursor =
            contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null, null)
        cursor?.use {
            it.moveToFirst()
            val sizeIndex = it.getColumnIndex(OpenableColumns.SIZE)
            if (it.isNull(sizeIndex)) return
            require(it.getInt(sizeIndex) <= MAX_SAVE_SIZE) { getString(R.string.file_too_big) }
        }
    }

    private fun getLastParams(whichBackend: BackendName): String? =
        state.getString(LAST_PARAMS_PREFIX + whichBackend, null)?.let {
            orientGameType(whichBackend, it)
        }

    private fun stopGameGeneration() {
        generationInProgress?.run {
            cancel(true)
            generationInProgress = null
        }
    }

    override fun onPause() {
        handler.removeMessages(MsgType.TIMER.ordinal)
        if (progress == null) save()
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        appStartIntentOnResume?.let {
            onNewIntent(it)
            appStartIntentOnResume = null
        }
    }

    override fun onDestroy() {
        stopGameGeneration()
        gameGenerator.onDestroy()
        gameEngine.onDestroy()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(f: Boolean) {
        if (f && gameWantsTimer && currentBackend != null && !handler.hasMessages(MsgType.TIMER.ordinal)) {
            gameEngine.resetTimerBaseline()
            handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal), TIMER_INTERVAL.toLong())
        }
    }

    private fun setSwapLR(swap: Boolean) {
        if (!currentBackend!!.swapLRNatively) swapLROn = swap
        currentBackend!!.putSwapLR(this, swap)
        newKeyboard.swapLR.value = swap
    }

    fun sendKey(p: PointF, k: Int) {
        sendKey(p.x.roundToInt(), p.y.roundToInt(), k)
    }

    @JvmOverloads
    fun sendKey(x: Int, y: Int, k: Int, isRepeat: Boolean = false) {
        if (progress != null || currentBackend == null) return
        if (k == GameView.UI_UNDO && undoIsLoadGame) {
            if (!isRepeat) {
                toastFirstFewTimes(this, state, UNDO_NEW_GAME_SEEN, 3, R.string.undo_new_game_toast)
                val launchUndo = undoingOrRedoingNewGame(undoToGame!!)
                redoToGame = saveToString()
                startGame(launchUndo)
            }
            return
        }
        if (k == GameView.UI_REDO && redoIsLoadGame) {
            if (!isRepeat) {
                toastFirstFewTimes(this, state, REDO_NEW_GAME_SEEN, 3, R.string.redo_new_game_toast)
                val launchRedo = undoingOrRedoingNewGame(redoToGame!!)
                redoToGame = null
                startGame(launchRedo, true)
            }
            return
        }
        gameEngine.keyEvent(x, y, maybeSwapMouse(k))
        if (GameView.CURSOR_KEYS.contains(k) || currentBackend === INERTIA && k == '\n'.code) {
            gameView.ensureCursorVisible(gameEngine.cursorLocation)
        }
        if (k == 'M'.code && currentBackend!!.isLatin) {
            toastFirstFewTimes(
                this, state, LATIN_M_UNDO_SEEN, 3,
                R.string.latin_m_undo_toast
            )
        }
        gameView.requestFocus()
        onBackPressedCallback.isEnabled = true
        handler.sendMessageDelayed(
            handler.obtainMessage(MsgType.BACK_TIME_ELAPSED.ordinal),
            PREVENT_BACK_AFTER_KEY_PRESS_MILLIS.toLong()
        )
    }

    private fun maybeSwapMouse(k: Int): Int {
        if (swapLROn && k >= GameView.FIRST_MOUSE && k <= GameView.LAST_MOUSE) {
            val whichButton: Int = (k - GameView.FIRST_MOUSE) % 3
            if (whichButton == 0) {
                return k + 2 // left; send right
            } else if (whichButton == 2) {
                return k - 2 // right; send left
            }
        }
        return k
    }

    private fun setKeyboardVisibility(whichBackend: BackendName?, c: Configuration) {
        val newArrowMode = computeArrowMode(whichBackend)
        val shouldHaveSwap =
            lastArrowMode === ArrowMode.ARROWS_LEFT_RIGHT_CLICK || whichBackend === PALISADE || whichBackend === NET
        val maybeSwapLRKey = if (shouldHaveSwap) SWAP_L_R_KEY.toString() else ""
        val newKeys =
            if (shouldShowFullSoftKeyboard(c)) filterKeys(newArrowMode) + maybeSwapLRKey + maybeUndoRedo else maybeSwapLRKey + maybeUndoRedo
        val swap = whichBackend!!.getSwapLR(this)
        if (!whichBackend.swapLRNatively) swapLROn = swap
        with (newKeyboard) {
            backend.value = whichBackend
            keys.value = newKeys
            arrowMode.value = newArrowMode
            swapLR.value = swap
            disableCharacterIcons.value = disableCharacterIcons(whichBackend)
            undoEnabled.value = this@GamePlay.undoEnabled
            redoEnabled.value = this@GamePlay.redoEnabled
        }
    }

    private fun disableCharacterIcons(whichBackend: BackendName?): String {
        if (whichBackend !== UNDEAD) return ""
        val undeadPrefs = getPrefs(this, whichBackend)
        return if (undeadPrefs?.split("\n".toRegex())?.contains("monsters=letters") == true) "GVZ" else ""
    }

    private fun computeArrowMode(whichBackend: BackendName?): ArrowMode {
        val arrowPref = prefs.getBoolean(
            getArrowKeysPrefName(whichBackend, resources.configuration),
            getArrowKeysDefault(whichBackend, resources)
        )
        return if (arrowPref) lastArrowMode else ArrowMode.NO_ARROWS
    }

    private fun filterKeys(arrowMode: ArrowMode): String {
        var filtered = lastKeys
        startingBackend?.let {
            if (it === BRIDGES && !prefs.getBoolean(BRIDGES_SHOW_H_KEY, false)
                    || it === UNEQUAL && !prefs.getBoolean(UNEQUAL_SHOW_H_KEY, false)) {
                filtered = filtered.replace("H", "")
            }
            if (it.isLatin && !prefs.getBoolean(LATIN_SHOW_M_KEY, true)) {
                filtered = filtered.replace("M", "")
            }
        }
        if (arrowMode.hasArrows()) {
            filtered = lastKeysIfArrows + filtered
        } else if (filtered.length == 1 && filtered[0] == '\b') {
            filtered = ""
        }
        return filtered
    }

    private fun setStatusBarVisibility(visible: Boolean) {
        if (!visible) statusBar.text = ""
        mainLayout.updateViewLayout(statusBar, statusBar.layoutParams.apply {
            height = if (visible) RelativeLayout.LayoutParams.WRAP_CONTENT else 0
        })
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        val statusBarText = statusBar.text
        inflateContent()
        statusBar.text = statusBarText
        gameEngine.setViewCallbacks(gameView)
        setKeyboardVisibility(startingBackend, newConfig)
        refreshColours()
        val isNight = isNight(newConfig)
        if (isNight != _wasNight) {
            _wasNight = isNight
            statusBar.setTextColor(
                ResourcesCompat.getColor(resources, R.color.status_bar_text, theme)
            )
            statusBar.setBackgroundColor(
                ResourcesCompat.getColor(resources, R.color.game_background, theme)
            )
        }
        rethinkActionBarCapacity()
        invalidateOptionsMenu() // for orientation of presets in type menu
    }

    /** ActionBar's capacity (width) has probably changed, so work around
     * [Android issue 36933746](https://issuetracker.google.com/issues/36933746)
     * (invalidateOptionsMenu() does not help here)  */
    private fun rethinkActionBarCapacity() {
        menu?.apply {
            val dm = resources.displayMetrics
            val screenWidthDIP = (dm.widthPixels.toDouble() / dm.density).roundToInt()
            val state =
                if (screenWidthDIP >= 480) (SHOW_AS_ACTION_ALWAYS or SHOW_AS_ACTION_WITH_TEXT) else SHOW_AS_ACTION_ALWAYS
            for (i in listOf(R.id.game_menu, R.id.type_menu, R.id.help_menu)) {
                findItem(i).setShowAsAction(state)
            }
            val undoRedoKbd = prefs.getBoolean(UNDO_REDO_KBD_KEY, UNDO_REDO_KBD_DEFAULT)
            val undoItem = findItem(R.id.undo)
            val redoItem = findItem(R.id.redo)
            undoItem.isVisible = !undoRedoKbd
            redoItem.isVisible = !undoRedoKbd
            if (!undoRedoKbd) {
                undoItem.setShowAsAction(state)
                redoItem.setShowAsAction(state)
                updateUndoRedoEnabled()
            }
            // emulator at 598 dip looks bad with title+undo; GT-N7100 at 640dip looks good
            supportActionBar?.setDisplayShowTitleEnabled(screenWidthDIP > 620 || undoRedoKbd)
        }
    }

    private fun messageBox(title: String, msg: String?, returnToChooser: Boolean) {
        AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(msg)
            .setIcon(android.R.drawable.ic_dialog_alert)
            .setOnCancelListener(if (returnToChooser) DialogInterface.OnCancelListener { startChooserAndFinish() } else null)
            .show()
    }

    @UsedByJNI
    override fun completed() {
        handler.sendMessageDelayed(handler.obtainMessage(MsgType.COMPLETED.ordinal), 0)
    }

    @UsedByJNI
    override fun inertiaFollow(isSolved: Boolean) {
        newKeyboard.hidePrimary.value = !isSolved && currentBackend === INERTIA
    }

    private fun completedInternal() {
        everCompleted = true
        val copyStatusBar = currentBackend in setOf(MINES, FLOOD,  SAMEGAME)
        val titleText = if (copyStatusBar) statusBar.text else getString(R.string.COMPLETED)
        if (!prefs.getBoolean(COMPLETED_PROMPT_KEY, true)) {
            Toast.makeText(this@GamePlay, titleText, Toast.LENGTH_SHORT).show()
            return
        }
        val d = Dialog(this, R.style.Dialog_Completed)
        val lp = d.window!!.attributes
        lp.gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
        d.window!!.attributes = lp
        val dialogBinding = CompletedDialogBinding.inflate(LayoutInflater.from(d.context))
        d.setContentView(dialogBinding.root)
        dialogBinding.completedTitle.text = titleText
        d.setCanceledOnTouchOutside(true)
        darkenTopDrawable(dialogBinding.newGame)
        dialogBinding.newGame.setOnClickListener {
            d.dismiss()
            startNewGame()
        }
        darkenTopDrawable(dialogBinding.typeMenu)
        dialogBinding.typeMenu.setOnClickListener {
            d.dismiss()
            if (hackForSubmenus == null) openOptionsMenu()
            hackForSubmenus!!.performIdentifierAction(R.id.type_menu, 0)
        }
        val style = prefs.getString(CHOOSER_STYLE_KEY, "list")
        val useGrid = style == "grid"
        dialogBinding.otherGame.setCompoundDrawablesWithIntrinsicBounds(
            0,
            if (useGrid) R.drawable.ic_action_view_as_grid else R.drawable.ic_action_view_as_list,
            0,
            0
        )
        darkenTopDrawable(dialogBinding.otherGame)
        dialogBinding.otherGame.setOnClickListener {
            d.dismiss()
            startChooserAndFinish()
        }
        d.show()
    }

    private fun darkenTopDrawable(b: Button) {
        val drawable = DrawableCompat.wrap(b.compoundDrawables[1].mutate())
        DrawableCompat.setTint(drawable, Color.BLACK)
        b.setCompoundDrawables(null, drawable, null, null)
    }

    @UsedByJNI
    override fun setStatus(status: String) {
        statusBar.apply {
            text = status.ifEmpty { " " } // Ensure consistent height
            importantForAccessibility =
                if (status.isEmpty()) View.IMPORTANT_FOR_ACCESSIBILITY_NO else View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }
    }

    @UsedByJNI
    override fun requestTimer(on: Boolean) {
        if (gameWantsTimer && on) return
        gameWantsTimer = on
        if (on) handler.sendMessageDelayed(handler.obtainMessage(MsgType.TIMER.ordinal), TIMER_INTERVAL.toLong())
        else handler.removeMessages(MsgType.TIMER.ordinal)
    }

    override fun customDialogError(error: String?) {
        dismissProgress()
        messageBox(getString(R.string.Error), error, false)
    }

    private var lastArrowMode = ArrowMode.NO_ARROWS

    private fun setKeys(result: KeysResult?) {
        if (result == null) return
        lastArrowMode = result.arrowMode ?: ArrowMode.ARROWS_LEFT_RIGHT_CLICK
        lastKeys = result.keys ?: ""
        lastKeysIfArrows = result.keysIfArrows ?: ""
        // Guess allows digits, but we don't want them in the virtual keyboard because they're already
        // on screen as the colours (if labels are enabled).
        val addDigits = if (startingBackend === GUESS) "1234567890" else ""
        gameView.setHardwareKeys(lastKeys + lastKeysIfArrows + addDigits)
        setKeyboardVisibility(startingBackend, resources.configuration)
    }

    override fun onSharedPreferenceChanged(p: SharedPreferences, key: String?) {
        if (!lifecycle.currentState.isAtLeast(Lifecycle.State.CREATED)) return
        if (key == null) return
        val configuration = resources.configuration
        // = already started
        when (key) {
            currentBackend!!.preferencesName -> {
                gameEngine.loadPrefs(this)
                // might have changed swapLR, or pictures vs letters in Undead
                setKeyboardVisibility(startingBackend, configuration)
                gameViewResized() // just for redraw
            }
            getArrowKeysPrefName(currentBackend, configuration) -> {
                setKeyboardVisibility(startingBackend, configuration)
                gameEngine.setCursorVisibility(computeArrowMode(startingBackend).hasArrows())
                gameViewResized() // cheat - we just want a redraw in case size unchanged
            }
            FULLSCREEN_KEY -> applyFullscreen()
            STAY_AWAKE_KEY -> applyStayAwake()
            LIMIT_DPI_KEY -> applyLimitDPI(true)
            ORIENTATION_KEY -> applyOrientation()
            UNDO_REDO_KBD_KEY -> applyUndoRedoKbd()
            BRIDGES_SHOW_H_KEY, UNEQUAL_SHOW_H_KEY, LATIN_SHOW_M_KEY -> applyKeyboardFilters()
            MOUSE_LONG_PRESS_KEY -> applyMouseLongPress()
            MOUSE_BACK_KEY -> applyMouseBackKey()
        }
    }

    private fun applyMouseLongPress() {
        val pref = prefs.getString(MOUSE_LONG_PRESS_KEY, "auto")
        gameView.alwaysLongPress = "always" == pref
        gameView.hasRightMouse = "never" == pref
    }

    private fun applyMouseBackKey() {
        gameView.mouseBackSupport = prefs.getBoolean(MOUSE_BACK_KEY, true)
    }

    private fun applyLimitDPI(alreadyStarted: Boolean) {
        val pref = prefs.getString(LIMIT_DPI_KEY, "auto")
        gameView.limitDpi =
            if ("auto" == pref) LIMIT_AUTO else if ("off" == pref) LIMIT_OFF else LIMIT_ON
        if (alreadyStarted) {
            gameView.rebuildBitmap()
        }
    }

    private fun applyFullscreen() {
        if (prefs.getBoolean(FULLSCREEN_KEY, false)) {
            WindowCompat.setDecorFitsSystemWindows(window, false)
            WindowInsetsControllerCompat(window, mainLayout).let { controller ->
                controller.hide(WindowInsetsCompat.Type.systemBars())
                controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            WindowCompat.setDecorFitsSystemWindows(window, true)
            WindowInsetsControllerCompat(window, mainLayout).show(WindowInsetsCompat.Type.systemBars())
        }
    }

    private fun applyStayAwake() {
        if (prefs.getBoolean(STAY_AWAKE_KEY, false)) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    @SuppressLint("SourceLockedOrientationActivity") // This is only done at the user's explicit request
    private fun applyOrientation() {
        val orientationPref = prefs.getString(ORIENTATION_KEY, "unspecified")
        requestedOrientation = when (orientationPref) {
            "landscape" -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            "portrait" -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
            else -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
    }

    private fun refreshColours() {
        gameView.night = isNight(resources.configuration)
        currentBackend?.let {
            gameView.refreshColours(it, gameEngine.colours)
            gameView.clear()
            gameViewResized() // cheat - we just want a redraw
        }
    }

    private fun applyUndoRedoKbd() {
        val undoRedoKbd = prefs.getBoolean(UNDO_REDO_KBD_KEY, UNDO_REDO_KBD_DEFAULT)
        val wantKbd = if (undoRedoKbd) "UR" else ""
        if (wantKbd != maybeUndoRedo) {
            maybeUndoRedo = wantKbd
            startingBackend?.let {
                setKeyboardVisibility(it, resources.configuration)
            }
        }
        rethinkActionBarCapacity()
    }

    private fun applyKeyboardFilters() {
        setKeyboardVisibility(startingBackend, resources.configuration)
    }

    @UsedByJNI
    override fun changedState(canUndo: Boolean, canRedo: Boolean) {
        undoEnabled = canUndo || undoToGame != null
        undoIsLoadGame = !canUndo && undoToGame != null
        redoEnabled = canRedo || redoToGame != null
        redoIsLoadGame = !canRedo && redoToGame != null
        newKeyboard.undoEnabled.value = undoEnabled
        newKeyboard.redoEnabled.value = redoEnabled
        menu?.apply {
            findItem(R.id.undo)?.apply {
                isEnabled = undoEnabled
                setIcon(if (undoEnabled) R.drawable.ic_action_undo else R.drawable.ic_action_undo_disabled)
            }
            findItem(R.id.redo)?.apply {
                isEnabled = redoEnabled
                setIcon(if (redoEnabled) R.drawable.ic_action_redo else R.drawable.ic_action_redo_disabled)
            }
        }
    }

    @UsedByJNI
    override fun purgingStates() {
        redoToGame = null
    }

    @UsedByJNI
    override fun allowFlash(): Boolean {
        return prefs.getBoolean(VICTORY_FLASH_KEY, true)
    }

    @VisibleForTesting // specifically the screenshots test
    fun getGameView(): GameView {
        return _binding.gameView
    }

    companion object {
        private const val TAG = "GamePlay"
        const val OUR_SCHEME = "sgtpuzzles"
        const val MIME_TYPE = "text/prs.sgtatham.puzzles"
        private val DIMENSIONS = Pattern.compile("(\\d+)( ?)x\\2(\\d+)(.*)")
        const val MAX_SAVE_SIZE: Long = 1000000 // 1MB; we only have 16MB of heap
        private const val TIMER_INTERVAL = 20
        private const val PREVENT_BACK_AFTER_KEY_PRESS_MILLIS = 600

        /** Whether to show data-entry keys, as opposed to undo/redo/swap-L-R which are always shown.
         * We show data-entry if we either don't have a real hardware keyboard (we usually don't),
         * or we're on the Android SDK's emulator, which has the host's keyboard, but showing the full
         * keyboard is useful for UI development and screenshots.  */
        private fun shouldShowFullSoftKeyboard(c: Configuration): Boolean {
            return c.hardKeyboardHidden != HARDKEYBOARDHIDDEN_NO || c.keyboard == KEYBOARD_NOKEYS || isProbablyEmulator
        }

        private val isProbablyEmulator: Boolean
            get() = Build.MODEL.startsWith("sdk_") || Build.MODEL.startsWith("Android SDK")

        fun getArrowKeysPrefName(whichBackend: BackendName?, c: Configuration): String =
            (whichBackend.toString() + ARROW_KEYS_KEY_SUFFIX
                    + if (hasDpadOrTrackball(c)) "WithDpad" else "")

        fun getArrowKeysDefault(whichBackend: BackendName?, resources: Resources): Boolean =
            if (hasDpadOrTrackball(resources.configuration) && !isProbablyEmulator) false else whichBackend!!.isArrowsVisibleByDefault

        private fun hasDpadOrTrackball(c: Configuration): Boolean =
            (c.navigation == NAVIGATION_DPAD || c.navigation == NAVIGATION_TRACKBALL) && c.navigationHidden != NAVIGATIONHIDDEN_YES

        init {
            System.loadLibrary("puzzles")
        }
    }
}
