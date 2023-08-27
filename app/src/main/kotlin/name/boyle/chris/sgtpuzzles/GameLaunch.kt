package name.boyle.chris.sgtpuzzles

import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.GameEngineImpl

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
class GameLaunch private constructor(
    val origin: Origin,
    val whichBackend: BackendName,
    val params: String? = null,
    val gameID: String? = null,
    val seed: String? = null,
    val saved: String? = null
) {

    enum class Origin(val shouldReturnToChooserOnFail: Boolean, val isOfLocalState: Boolean) {
        GENERATING_FROM_CHOOSER(true, false),
        RESTORING_LAST_STATE_FROM_CHOOSER(true, true),
        RESTORING_LAST_STATE_APP_START(true, true),
        BUTTON_OR_MENU_IN_ACTIVITY(false, false),
        CUSTOM_DIALOG(false, false),
        UNDO_OR_REDO(false, true),
        CONTENT_URI(true, false),  // probably via Load button & Storage Access Framework
        INTENT_COMPLEX_URI(true, false),  // probably tests
        INTENT_EXTRA(true, false);  // probably tests

        fun shouldHighlightCompletionOnLaunch() = isOfLocalState && this != UNDO_OR_REDO

        fun shouldResetBackendStateOnFail() =
            this in setOf(
                GENERATING_FROM_CHOOSER,
                RESTORING_LAST_STATE_FROM_CHOOSER,
                RESTORING_LAST_STATE_APP_START
            )
    }

    override fun toString() = "GameLaunch($origin, $whichBackend, $params, $gameID, $seed, $saved)"

    @JvmField
    val needsGenerating = saved == null && gameID == null

    fun finishedGenerating(savedNow: String): GameLaunch {
        check(saved == null) { "finishedGenerating called twice" }
        return GameLaunch(origin, whichBackend, params, gameID, seed, savedNow)
    }

    companion object {
        @JvmStatic
        fun fromContentURI(saved: String) = GameLaunch(
            Origin.CONTENT_URI, GameEngineImpl.identifyBackend(saved), saved = saved
        )

        @JvmStatic
        fun ofSavedGameFromIntent(saved: String) = GameLaunch(
            Origin.INTENT_EXTRA, GameEngineImpl.identifyBackend(saved), saved = saved
        )

        @JvmStatic
        fun undoingOrRedoingNewGame(saved: String) = GameLaunch(
            Origin.UNDO_OR_REDO, GameEngineImpl.identifyBackend(saved), saved = saved
        )

        @JvmStatic
        fun ofLocalState(backend: BackendName, saved: String, fromChooser: Boolean) =
            GameLaunch(
                if (fromChooser) Origin.RESTORING_LAST_STATE_FROM_CHOOSER else Origin.RESTORING_LAST_STATE_APP_START,
                backend,
                saved = saved
            )

        @JvmStatic
        fun toGenerate(whichBackend: BackendName, params: String, origin: Origin) =
            GameLaunch(origin, whichBackend, params)

        @JvmStatic
        fun toGenerateFromChooser(whichBackend: BackendName) =
            GameLaunch(Origin.GENERATING_FROM_CHOOSER, whichBackend)

        @JvmStatic
        fun ofGameID(whichBackend: BackendName, gameID: String, origin: Origin): GameLaunch {
            val pos = gameID.indexOf(':')
            require(pos >= 0) { "Game ID invalid: $gameID" }
            return GameLaunch(
                origin, whichBackend, params = gameID.substring(0, pos), gameID = gameID
            )
        }

        @JvmStatic
        fun fromSeed(whichBackend: BackendName, seed: String): GameLaunch {
            val pos = seed.indexOf('#')
            require(pos >= 0) { "Seed invalid: $seed" }
            return GameLaunch(
                Origin.CUSTOM_DIALOG, whichBackend, params = seed.substring(0, pos), seed = seed
            )
        }
    }
}