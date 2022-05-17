package name.boyle.chris.sgtpuzzles;

import androidx.annotation.NonNull;

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
public class GameLaunch {
	private final String _saved;
	@NonNull private final BackendName _whichBackend;
	private final String _params;
	private final String _gameID;
	private final String _seed;
	private final boolean _fromChooser;
	private final boolean _ofLocalState;
	private final boolean _undoingOrRedoing;

	private GameLaunch(@NonNull final BackendName whichBackend, final String params, final String gameID,
					   final String seed, final String saved,
					   final boolean fromChooser, final boolean ofLocalState, boolean undoingOrRedoing) {
		_whichBackend = whichBackend;
		_params = params;
		_gameID = gameID;
		_seed = seed;
		_saved = saved;
		_fromChooser = fromChooser;
		_ofLocalState = ofLocalState;
		_undoingOrRedoing = undoingOrRedoing;
	}
	
	@Override
	@NonNull
	public String toString() {
		return "GameLaunch(" + _whichBackend + ", " + _params + ", " + _gameID + ", " + _seed + ", " + _saved + ")";
	}

	public static GameLaunch ofSavedGame(@NonNull final String saved) {
		return new GameLaunch(GameEngineImpl.identifyBackend(saved), null, null, null, saved, true, false, false);
	}

	public static GameLaunch undoingOrRedoingNewGame(@NonNull final String saved) {
		return new GameLaunch(GameEngineImpl.identifyBackend(saved), null, null, null, saved, true, true, true);
	}

	public static GameLaunch ofLocalState(@NonNull final BackendName backend, @NonNull final String saved, final boolean fromChooser) {
		return new GameLaunch(backend, null, null, null, saved, fromChooser, true, false);
	}

	public static GameLaunch toGenerate(@NonNull BackendName whichBackend, @NonNull String params) {
		return new GameLaunch(whichBackend, params, null, null, null, false, false, false);
	}

	public static GameLaunch toGenerateFromChooser(@NonNull BackendName whichBackend) {
		return new GameLaunch(whichBackend, null, null, null, null, true, false, false);
	}

	public static GameLaunch ofGameID(@NonNull BackendName whichBackend, @NonNull String gameID) {
		final int pos = gameID.indexOf(':');
		if (pos < 0) throw new IllegalArgumentException("Game ID invalid: " + gameID);
		return new GameLaunch(whichBackend, gameID.substring(0, pos), gameID, null, null, false, false, false);
	}

	public static GameLaunch fromSeed(@NonNull BackendName whichBackend, @NonNull String seed) {
		final int pos = seed.indexOf('#');
		if (pos < 0) throw new IllegalArgumentException("Seed invalid: " + seed);
		return new GameLaunch(whichBackend, seed.substring(0, pos), null, seed, null, false, false, false);
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
		return new GameLaunch(_whichBackend, _params, _gameID, _seed, saved, _fromChooser, _ofLocalState, _undoingOrRedoing);
	}

	public String getSaved() {
		return _saved;
	}

	public boolean isFromChooser() {
		return _fromChooser;
	}

	public boolean isOfLocalState() {
		return _ofLocalState;
	}

	public boolean isUndoingOrRedoing() {
		return _undoingOrRedoing;
	}
}
