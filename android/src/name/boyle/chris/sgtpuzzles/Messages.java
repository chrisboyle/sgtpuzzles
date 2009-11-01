package name.boyle.chris.sgtpuzzles;

enum Messages {
	// engine to parent
	DIE, INIT, SETBG, STATUS, DONE, MESSAGEBOX, ENABLE_SOLVE, ENABLE_CUSTOM,
	ADDTYPE, SETTYPE, DIALOG_INIT, SAVED,
	// parent to engine
	RESIZE, RESTART, SOLVE, UNDO, REDO, ABOUT, NEWGAME, CONFIG,
	PRESET, KEY, DIALOG_CANCEL, SAVE, LOAD,
	// either direction (arguments may differ)
	QUIT, DIALOG_STRING, DIALOG_BOOL, DIALOG_CHOICE, DIALOG_FINISH,
	// engine to self
	TIMER
};
