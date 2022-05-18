package name.boyle.chris.sgtpuzzles;

import androidx.annotation.NonNull;

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
public class GameLaunch {
	enum Origin {
		GENERATING_FROM_CHOOSER(true, false),
		RESTORING_LAST_STATE_FROM_CHOOSER(true, true),
		RESTORING_LAST_STATE_APP_START(true, true),
		BUTTON_OR_MENU_IN_ACTIVITY(false, false),
		CUSTOM_DIALOG(false, false),
		UNDO_OR_REDO(false, true),
		CONTENT_URI(true, false),  // probably via Load button & Storage Access Framework
		INTENT_COMPLEX_URI(true, false),  // probably tests
		INTENT_EXTRA(true, false);        // probably tests

		private final boolean _shouldReturnToChooserOnFail;
		private final boolean _isOfLocalState;

		Origin(final boolean shouldReturnToChooserOnFail, final boolean isOfLocalState) {
			_shouldReturnToChooserOnFail = shouldReturnToChooserOnFail;
			_isOfLocalState = isOfLocalState;
		}

		public boolean shouldReturnToChooserOnFail() {
			return _shouldReturnToChooserOnFail;
		}

		public boolean isOfLocalState() {
			return _isOfLocalState;
		}

		public boolean shouldHighlightCompletionOnLaunch() {
			return _isOfLocalState && this != UNDO_OR_REDO;
		}
	}

	private final String _saved;
	@NonNull private final BackendName _whichBackend;
	private final String _params;
	private final String _gameID;
	private final String _seed;
	@NonNull private final Origin _origin;

	private GameLaunch(@NonNull final Origin origin, @NonNull final BackendName whichBackend, final String params, final String gameID,
					   final String seed, final String saved) {
		_whichBackend = whichBackend;
		_params = params;
		_gameID = gameID;
		_seed = seed;
		_saved = saved;
		_origin = origin;
	}
	
	@Override
	@NonNull
	public String toString() {
		return "GameLaunch(" + _origin + ", " + _whichBackend + ", " + _params + ", " + _gameID + ", " + _seed + ", " + _saved + ")";
	}

	public static GameLaunch fromContentURI(@NonNull final String saved) {
		return new GameLaunch(Origin.CONTENT_URI, GameEngineImpl.identifyBackend(saved), null, null, null, saved);
	}

	public static GameLaunch ofSavedGameFromIntent(@NonNull final String saved) {
		return new GameLaunch(Origin.INTENT_EXTRA, GameEngineImpl.identifyBackend(saved), null, null, null, saved);
	}

	public static GameLaunch undoingOrRedoingNewGame(@NonNull final String saved) {
		return new GameLaunch(Origin.UNDO_OR_REDO, GameEngineImpl.identifyBackend(saved), null, null, null, saved);
	}

	public static GameLaunch ofLocalState(@NonNull final BackendName backend, @NonNull final String saved, final boolean fromChooser) {
		return new GameLaunch(fromChooser ? Origin.RESTORING_LAST_STATE_FROM_CHOOSER : Origin.RESTORING_LAST_STATE_APP_START,
				backend, null, null, null, saved);
	}

	public static GameLaunch toGenerate(@NonNull BackendName whichBackend, @NonNull String params, @NonNull final Origin origin) {
		return new GameLaunch(origin, whichBackend, params, null, null, null);
	}

	public static GameLaunch toGenerateFromChooser(@NonNull BackendName whichBackend) {
		return new GameLaunch(Origin.GENERATING_FROM_CHOOSER, whichBackend, null, null, null, null);
	}

	public static GameLaunch ofGameID(@NonNull BackendName whichBackend, @NonNull String gameID, @NonNull final Origin origin) {
		final int pos = gameID.indexOf(':');
		if (pos < 0) throw new IllegalArgumentException("Game ID invalid: " + gameID);
		return new GameLaunch(origin, whichBackend, gameID.substring(0, pos), gameID, null, null);
	}

	public static GameLaunch fromSeed(@NonNull BackendName whichBackend, @NonNull String seed) {
		final int pos = seed.indexOf('#');
		if (pos < 0) throw new IllegalArgumentException("Seed invalid: " + seed);
		return new GameLaunch(Origin.CUSTOM_DIALOG, whichBackend, seed.substring(0, pos), null, seed, null);
	}

	public boolean needsGenerating() {
		return _saved == null && _gameID == null;
	}

	@NonNull
	public BackendName getWhichBackend() {
		return _whichBackend;
	}

	public String getParams() {
		return _params;
	}

	public String getGameID() {
		return _gameID;
	}

	public String getSeed() {
		return _seed;
	}

	public GameLaunch finishedGenerating(@NonNull String saved) {
		if (_saved != null) {
			throw new RuntimeException("finishedGenerating called twice");
		}
		return new GameLaunch(_origin, _whichBackend, _params, _gameID, _seed, saved);
	}

	public String getSaved() {
		return _saved;
	}

	@NonNull
	public Origin getOrigin() {
		return _origin;
	}

}
