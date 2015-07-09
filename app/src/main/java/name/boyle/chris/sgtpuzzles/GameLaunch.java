package name.boyle.chris.sgtpuzzles;

import android.net.Uri;

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
public class GameLaunch {
	private String saved;
	private final Uri uri;
	private final String whichBackend;
	private final String params;
	private final String gameID;
	private final String seed;
	private final boolean knownCompleted;
	private final boolean fromChooser;
	private final boolean ofLocalState;

	private GameLaunch(final String whichBackend, final String params, final String gameID,
				final String seed, final Uri uri, final String saved,
				final boolean knownCompleted, final boolean fromChooser, final boolean ofLocalState) {
		this.whichBackend = whichBackend;
		this.params = params;
		this.gameID = gameID;
		this.seed = seed;
		this.uri = uri;
		this.saved = saved;
		this.knownCompleted = knownCompleted;
		this.fromChooser = fromChooser;
		this.ofLocalState = ofLocalState;
	}
	
	@Override
	public String toString() {
		if (uri != null) return "GameLaunch.ofUri(" + uri + ")";
		return "GameLaunch(" + whichBackend + ", " + params + ", " + gameID + ", " + seed + ", " + saved + ")";
	}

	public static GameLaunch ofSavedGame(final String saved) {
		return new GameLaunch(null, null, null, null, null, saved, false, true, false);
	}

	public static GameLaunch ofLocalState(final String saved, final boolean knownCompleted, final boolean fromChooser) {
		return new GameLaunch(null, null, null, null, null, saved, knownCompleted, fromChooser, true);
	}

	public static GameLaunch toGenerate(String whichBackend, String params) {
		return new GameLaunch(whichBackend, params, null, null, null, null, false, false, false);
	}

	public static GameLaunch toGenerateFromChooser(String whichBackend) {
		return new GameLaunch(whichBackend, null, null, null, null, null, false, true, false);
	}

	public static GameLaunch ofGameID(String whichBackend, String gameID) {
		final int pos = gameID.indexOf(':');
		if (pos < 0) throw new IllegalArgumentException("Game ID invalid: " + gameID);
		return new GameLaunch(whichBackend, gameID.substring(0, pos), gameID, null, null, null, false, false, false);
	}

	public static GameLaunch fromSeed(String whichBackend, String seed) {
		final int pos = seed.indexOf('#');
		if (pos < 0) throw new IllegalArgumentException("Seed invalid: " + seed);
		return new GameLaunch(whichBackend, seed.substring(0, pos), null, seed, null, null, false, false, false);
	}

	public static GameLaunch ofUri(final Uri uri) {
		return new GameLaunch(null, null, null, null, uri, null, false, true, false);
	}

	public boolean needsGenerating() {
		return saved == null && gameID == null && uri == null;
	}

	public String getWhichBackend() {
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

	public Uri getUri() {
		return uri;
	}

	public void finishedGenerating(String saved) {
		if (this.saved != null) {
			throw new RuntimeException("finishedGenerating called twice");
		}
		this.saved = saved;
	}

	public String getSaved() {
		return saved;
	}

	public boolean isKnownCompleted() {
		return knownCompleted;
	}

	public boolean isFromChooser() {
		return fromChooser;
	}

	public boolean isOfNonLocalState() {
		return !ofLocalState;
	}
}
