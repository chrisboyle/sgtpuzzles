package name.boyle.chris.sgtpuzzles;

import androidx.annotation.NonNull;

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
public class GameLaunch {
	private final String saved;
	private final BackendName whichBackend;
	private final String params;
	private final String gameID;
	private final String seed;
	private final boolean fromChooser;
	private final boolean ofLocalState;
	private final boolean undoingOrRedoing;

	private GameLaunch(final BackendName whichBackend, final String params, final String gameID,
					   final String seed, final String saved,
					   final boolean fromChooser, final boolean ofLocalState, boolean undoingOrRedoing) {
		this.whichBackend = whichBackend;
		this.params = params;
		this.gameID = gameID;
		this.seed = seed;
		this.saved = saved;
		this.fromChooser = fromChooser;
		this.ofLocalState = ofLocalState;
		this.undoingOrRedoing = undoingOrRedoing;
	}
	
	@Override
	@NonNull
	public String toString() {
		return "GameLaunch(" + whichBackend + ", " + params + ", " + gameID + ", " + seed + ", " + saved + ")";
	}

	public static GameLaunch ofSavedGame(@NonNull final String saved) {
		return new GameLaunch(null, null, null, null, saved, true, false, false);
	}

	public static GameLaunch undoingOrRedoingNewGame(@NonNull final String saved) {
		return new GameLaunch(null, null, null, null, saved, true, true, true);
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
		return saved == null && gameID == null;
	}

	public BackendName getWhichBackend() {
		return whichBackend;
	}

	public String getParams() {
		return params;
	}

	public String getGameID() {
		return gameID;
	}

	public String getSeed() {
		return seed;
	}

	public GameLaunch finishedGenerating(@NonNull String saved) {
		if (this.saved != null) {
			throw new RuntimeException("finishedGenerating called twice");
		}
		return new GameLaunch(whichBackend, params, gameID, seed, saved, fromChooser, ofLocalState, undoingOrRedoing);
	}

	public String getSaved() {
		return saved;
	}

	public boolean isFromChooser() {
		return fromChooser;
	}

	public boolean isOfLocalState() {
		return ofLocalState;
	}

	public boolean isUndoingOrRedoing() {
		return undoingOrRedoing;
	}
}
