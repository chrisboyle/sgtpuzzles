package name.boyle.chris.sgtpuzzles;

/**
 * A game the user wants to launch, whether saved, or identified by backend and
 * optional parameters.
 */
public class GameLaunch {
	private String saved;
	private final String whichBackend;
	private final String params;

	private GameLaunch(String whichBackend, String params, String saved) {
		this.whichBackend = whichBackend;
		this.params = params;
		this.saved = saved;
	}
	
	@Override
	public String toString() {
		return "GameLaunch(" + whichBackend + ", " + params + ", " + saved + ")";
	}

	public static GameLaunch ofSavedGame(String saved) {
		return new GameLaunch(null, null, saved);
	}

	public static GameLaunch toGenerate(String whichBackend, String params) {
		return new GameLaunch(whichBackend, params, null);
	}

	public boolean needsGenerating() {
		return saved == null;
	}

	public String getWhichBackend() {
		return whichBackend;
	}

	public String getParams() {
		return params;
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
}
