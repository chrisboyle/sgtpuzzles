package name.boyle.chris.sgtpuzzles.backend

import android.content.Context
import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.core.content.ContextCompat
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.GameView.DragMode.PREVENT
import name.boyle.chris.sgtpuzzles.GameView.DragMode.REVERT_OFF_SCREEN
import name.boyle.chris.sgtpuzzles.GameView.DragMode.REVERT_TO_START
import name.boyle.chris.sgtpuzzles.GameView.DragMode.UNMODIFIED
import name.boyle.chris.sgtpuzzles.config.PrefsConstants

sealed class BackendName(
    val sourceName: String,
    val displayName: String,
    val title: String,
    @DrawableRes val icon: Int,
    @StringRes val description: Int,
    @StringRes val controlsToast: Int,
    @StringRes val controlsToastNoArrows: Int,
    private val keyIcons: Map<String, Int>,
    @ColorRes val nightColours: Array<Int>
) {

    override fun toString() = sourceName

    val preferencesName = "backend/$sourceName"

    fun icon(context: Context) = ContextCompat.getDrawable(context, icon)!!

    fun keyIcon(k: String) = keyIcons[k.removePrefix("sym_key_")]

    val isArrowsCapable by lazy { this != UNTANGLE }

    /**
     * Should the game have arrows by default? Usually not.
     * This is for puzzles where the default preset has small squares, or diagonals are needed.
     * Note that if a hardware D-pad or trackball is present, these are ignored.
     */
    val isArrowsVisibleByDefault by lazy {
        this in setOf(BLACKBOX, FLOOD, INERTIA, MINES, PATTERN, SLANT, TRACKS)
    }

    /**
     * Is this a Latin-square puzzle? Currently only affects whether to offer
     * a preference that hides the "M" button that inserts all pencil marks.
     */
    val isLatin by lazy {
        this in setOf(KEEN, SOLO, TOWERS, UNEQUAL)
    }

    /** How to handle drag and revert for each game. */
    val dragMode by lazy {
        when (this) {
            // By default, and for games where a touch means move in that direction, allow a swipe
            // to do the same and don't try to revert anything: if the user tries to swipe in the
            // direction they want to move, it's better to have a move happen towards where they
            // started, so they learn, than to have nothing happen.
            CUBE, FIFTEEN, FILLING, MOSAIC, NET, TWIDDLE -> UNMODIFIED

            // For games where a drag can be canceled by going off-screen, do that.
            GALAXIES, MAP, MINES, PATTERN, PEGS, RECT, TENTS, TRACKS, UNTANGLE -> REVERT_OFF_SCREEN

            // For games where dragging something off the board would be harmful, revert by
            // dragging back to the start of the drag instead.
            BRIDGES, GUESS, PEARL, SIGNPOST -> REVERT_TO_START

            // For many games with a cursor and no drag facilities, just ignore drags completely.
            // The reason we don't just enable one-finger pan in these games is it would be an
            // inconsistent UI since all the other games can't.
            BLACKBOX, DOMINOSA, FLIP, FLOOD, INERTIA, KEEN, LIGHTUP, LOOPY, MAGNETS, NETSLIDE,
            PALISADE, RANGE, SAMEGAME, SINGLES, SIXTEEN, SLANT, SOLO, TOWERS, UNDEAD, UNEQUAL,
            UNRULY -> PREVENT
        }
    }

    private val swapLRName by lazy { PrefsConstants.SWAP_L_R_PREFIX + this }

    val swapLRNatively by lazy { this in SWAP_L_R_NATIVE_PREFS }

    fun getSwapLR(context: Context): Boolean {
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        SWAP_L_R_NATIVE_PREFS[this]?.let { pref ->
            val nativeOn = GameEngineImpl.getPrefs(context, this)?.split("\n")
                ?.contains("${pref.kw}=${pref.onVal}")
            nativeOn?.let { return it }
            // Native pref not present; migrate from Android-specific name
            if (prefs.contains(swapLRName)) {
                val oldVal = prefs.getBoolean(swapLRName, false)
                putSwapLR(context, oldVal)
                prefs.edit().remove(swapLRName).apply()
                return oldVal
            }
        }
        return prefs.getBoolean(swapLRName, false)
    }

    fun putSwapLR(context: Context, swap: Boolean) {
        SWAP_L_R_NATIVE_PREFS[this]?.let { pref ->
            val otherLines = GameEngineImpl.getPrefs(context, this)?.split("\n")
                ?.filterNot { it.startsWith("${pref.kw}=") } ?: listOf()
            val newPrefs = "${otherLines.joinToString("\n")}${pref.kw}=${if (swap) pref.onVal else pref.offVal}\n"
            GameEngineImpl.savePrefs(context, this, newPrefs)
            return
        }
        PreferenceManager.getDefaultSharedPreferences(context)
            .edit()
            .putBoolean(swapLRName, swap)
            .apply()
    }

    companion object {
        @JvmStatic
        val all: Set<BackendName> by lazy {
            BackendName::class.sealedSubclasses.map { it.objectInstance!! }.toSet()
        }

        private val BY_DISPLAY_NAME: Map<String, BackendName> by lazy {
            all.associateBy { it.displayName }
        }

        @UsedByJNI
        @JvmStatic
        fun byDisplayName(name: String?) = BY_DISPLAY_NAME[name]

        private val BY_LOWERCASE: Map<String, BackendName> by lazy {
            all.associateBy { it.sourceName }
        }

        @JvmStatic
        fun byLowerCase(name: String?) = BY_LOWERCASE[name]

        data class NativeSwapPref(val kw: String, val offVal: String, val onVal: String)

        private val SWAP_L_R_NATIVE_PREFS = mapOf(
            RANGE to NativeSwapPref("left-mouse-button", "fill", "dot"),
            SLANT to NativeSwapPref("left-button", "\\", "/"),
        )
    }
}
