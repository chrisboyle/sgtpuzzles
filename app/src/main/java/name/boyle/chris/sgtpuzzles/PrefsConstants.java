package name.boyle.chris.sgtpuzzles;

/** Constants used for preference names/prefixes/values. */
abstract class PrefsConstants {
    private PrefsConstants() {}

    static final String CATEGORY_CHOOSER = "gameChooser";
    static final String CHOOSER_STYLE_KEY = "chooserStyle";
    static final String CATEGORY_THIS_GAME = "thisGame";
    static final String PLACEHOLDER_NO_ARROWS = "arrowKeysUnavailable";
    static final String PLACEHOLDER_SEND_FEEDBACK = "send_feedback";
    static final String SAVED_BACKEND = "savedBackend";
    static final String LAST_PARAMS_PREFIX = "last_params_";
    static final String STATE_PREFS_NAME = "state";
    static final String ORIENTATION_KEY = "orientation";
    static final String ARROW_KEYS_KEY_SUFFIX = "ArrowKeys";
    static final String LIMIT_DPI_KEY = "limitDpi";
    static final String KEYBOARD_BORDERS_KEY = "keyboardBorders";
    static final String BRIDGES_SHOW_H_KEY = "bridgesShowH";
    static final String UNEQUAL_SHOW_H_KEY = "unequalShowH";
    static final String LATIN_SHOW_M_KEY = "latinShowM";
    static final String FULLSCREEN_KEY = "fullscreen";
    static final String STAY_AWAKE_KEY = "stayAwake";
    static final String UNDO_REDO_KBD_KEY = "undoRedoOnKeyboard";
    static final boolean UNDO_REDO_KBD_DEFAULT = true;
    static final String MOUSE_LONG_PRESS_KEY = "extMouseLongPress";
    static final String MOUSE_BACK_KEY = "extMouseBackKey";
    static final String PATTERN_SHOW_LENGTHS_KEY = "patternShowLengths";
    static final String COMPLETED_PROMPT_KEY = "completedPrompt";
    static final String VICTORY_FLASH_KEY = "victoryFlash";
    static final String CONTROLS_REMINDERS_KEY = "controlsReminders";
    static final String OLD_PUZZLESGEN_LAST_UPDATE = "puzzlesgen_last_update";
    static final String SAVED_COMPLETED_PREFIX = "savedCompleted_";
    static final String SAVED_GAME_PREFIX = "savedGame_";
    static final String SWAP_L_R_PREFIX = "swap_l_r_";
    static final String UNDO_NEW_GAME_SEEN = "undoNewGameSeen";
    static final String REDO_NEW_GAME_SEEN = "redoNewGameSeen";
    static final String PUZZLESGEN_CLEANUP_DONE = "puzzlesgen_cleanup_done";
    static final String AUTO_ORIENT = "autoOrient";
    static final boolean AUTO_ORIENT_DEFAULT = true;
    static final String NIGHT_MODE_KEY = "nightMode";
    static final String SEEN_NIGHT_MODE = "seenNightMode";
    static final String SEEN_NIGHT_MODE_SETTING = "seenNightModeSetting";
}
