package name.boyle.chris.sgtpuzzles;

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
public class GameLaunch {
	private String saved;
	private final String whichBackend;
	private final String params;
	private final String gameID;
	private final String seed;
	private final boolean knownCompleted;
	private final boolean fromChooser;

	private GameLaunch(final String whichBackend, final String params, final String gameID,
				final String seed, final String saved,
				final boolean knownCompleted, final boolean fromChooser) {
		this.whichBackend = whichBackend;
		this.params = params;
		this.gameID = gameID;
		this.seed = seed;
		this.saved = saved;
		this.knownCompleted = knownCompleted;
		this.fromChooser = fromChooser;
	}
	
	@Override
	public String toString() {
		return "GameLaunch(" + whichBackend + ", " + params + ", " + saved + ")";
	}

	public static GameLaunch ofSavedGame(final String saved, final boolean knownCompleted, final boolean fromChooser) {
		return new GameLaunch(null, null, null, null, saved, knownCompleted, fromChooser);
	}

	public static GameLaunch toGenerate(String whichBackend, String params) {
		return new GameLaunch(whichBackend, params, null, null, null, false, false);
	}

	public static GameLaunch toGenerateFromChooser(String whichBackend) {
		return new GameLaunch(whichBackend, null, null, null, null, false, true);
	}

	public static GameLaunch ofGameID(String whichBackend, String gameID) {
		final int pos = gameID.indexOf(':');
		if (pos < 0) throw new IllegalArgumentException("Game ID invalid: " + gameID);
		return new GameLaunch(whichBackend, gameID.substring(0, pos), gameID, null, null, false, false);
	}

	public static GameLaunch fromSeed(String whichBackend, String seed) {
		final int pos = seed.indexOf('#');
		if (pos < 0) throw new IllegalArgumentException("Seed invalid: " + seed);
		return new GameLaunch(whichBackend, seed.substring(0, pos), null, seed, null, false, false);
	}

	public boolean needsGenerating() {
		return saved == null && gameID == null;
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
}
