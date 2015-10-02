package name.boyle.chris.sgtpuzzles;

import android.content.Intent;
import android.net.Uri;
import android.support.test.espresso.ViewAction;
import android.support.test.espresso.action.CoordinatesProvider;
import android.support.test.espresso.action.GeneralClickAction;
import android.support.test.espresso.action.GeneralSwipeAction;
import android.support.test.espresso.action.Press;
import android.support.test.espresso.action.Swipe;
import android.support.test.espresso.action.Tap;
import android.support.test.rule.ActivityTestRule;
import android.view.View;

import org.hamcrest.Matcher;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameters;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.Set;

import static android.support.test.InstrumentationRegistry.getTargetContext;
import static android.support.test.espresso.Espresso.onView;
import static android.support.test.espresso.action.ViewActions.pressKey;
import static android.support.test.espresso.assertion.ViewAssertions.doesNotExist;
import static android.support.test.espresso.assertion.ViewAssertions.matches;
import static android.support.test.espresso.matcher.RootMatchers.isDialog;
import static android.support.test.espresso.matcher.ViewMatchers.isDisplayed;
import static android.support.test.espresso.matcher.ViewMatchers.withId;
import static android.support.test.espresso.matcher.ViewMatchers.withText;
import static android.view.KeyEvent.KEYCODE_1;
import static android.view.KeyEvent.KEYCODE_2;
import static android.view.KeyEvent.KEYCODE_3;
import static android.view.KeyEvent.KEYCODE_D;
import static android.view.KeyEvent.KEYCODE_DPAD_CENTER;
import static android.view.KeyEvent.KEYCODE_DPAD_DOWN;
import static android.view.KeyEvent.KEYCODE_DPAD_LEFT;
import static android.view.KeyEvent.KEYCODE_DPAD_RIGHT;
import static android.view.KeyEvent.KEYCODE_DPAD_UP;
import static android.view.KeyEvent.KEYCODE_G;
import static android.view.KeyEvent.KEYCODE_SPACE;
import static android.view.KeyEvent.KEYCODE_V;
import static android.view.KeyEvent.KEYCODE_Z;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertNotEquals;

@RunWith(Parameterized.class)
public class GamePlayTest {

	private static final Set<String> _usedBackends = new LinkedHashSet<>();
	private static final Set<Object[]> _params = new LinkedHashSet<>();

	private final String _backend;
	private final String _gameID;
	private final ViewAction[] _viewActions;

	@Rule
	public ActivityTestRule<GamePlay> mActivityRule =
			new ActivityTestRule<>(GamePlay.class, false, false);

	private static void addExamples() {
		addExample("net", "1x2:42", new GeneralClickAction(Tap.SINGLE,
				squareProportions(0, -0.25), Press.FINGER));
		addExample("blackbox", "w3h3m1M1:38727296",
				KEYCODE_DPAD_UP, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER);
		addExample("bridges", "3x3m2:3a2c1b",
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT);
		addExample("cube", "c4x4:0C56,0", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_LEFT);
		addExample("dominosa", "1:011100",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER);
		addExample("fifteen", "2x2:1,2,0,3", KEYCODE_DPAD_LEFT);
		addExample("filling", "2x1:02", KEYCODE_DPAD_UP, KEYCODE_2);
		addExample("flip", "2x2:edb7,d", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("flood", "2x2:1212,6", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("galaxies", "3x3:co",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("guess", "c2p2g2Bm:c2ab",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("inertia", "3x3:wbbSbbgms", KEYCODE_DPAD_DOWN);
		addExample("keen", "3:_baa_3a,m6s1m3a3",
				KEYCODE_DPAD_UP, KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3, KEYCODE_DPAD_RIGHT,
				KEYCODE_2, KEYCODE_DPAD_DOWN, KEYCODE_1, KEYCODE_DPAD_LEFT, KEYCODE_2,
				KEYCODE_DPAD_LEFT, KEYCODE_3, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3);
		addExample("lightup", "2x2:a0b", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		addExample("loopy", "3x3t0:02a2a1c",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER);
		addExample("magnets", "3x2:111,21,111,12,TTTBBB",
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("map", "3x2n5:afa,3120a", KEYCODE_DPAD_UP,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("mines", "4x3:2,0,m5d9",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		addExample("net", "1x2:12", KEYCODE_DPAD_UP, KEYCODE_D);
		addExample("netslide", "2x2:ch116", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		addExample("pattern", "1x2:2/1/1",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		addExample("pearl", "5x5:dBaWaBgWaBeB", KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER);
		addExample("pegs", "4x4:PHPPHPPOPOPOPOPO", KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_DOWN);
		addExample("range", "3x2:b2_4b",
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("rect", "2x2:2a2a", KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("samegame", "2x2c3s2:1,1,3,3", KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER);
		addExample("signpost", "3x2:1ccfcg6a",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER);
		addExample("singles", "2x2:1121", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("sixteen", "2x2:1,4,3,2", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		addExample("slant", "2x2:1c1d", KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER);
		addExample("solo", "2j:1c,b__", KEYCODE_DPAD_RIGHT, KEYCODE_2,
				KEYCODE_DPAD_DOWN, KEYCODE_1, KEYCODE_DPAD_LEFT, KEYCODE_2);
		addExample("tents", "4x4:baj_,1,1,0,1,1,0,2,0", KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER);
		addExample("towers", "3:1/2/3/2/2/1/1/2/2/3/2/1",
				KEYCODE_DPAD_UP, KEYCODE_3, KEYCODE_DPAD_RIGHT, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_LEFT, KEYCODE_3,
				KEYCODE_DPAD_LEFT, KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3);
		addExample("tracks", "4x4:Cm9a,3,2,S3,4,S3,4,3,2",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		addExample("twiddle", "2x2n2:4,3,2,1",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER);
		addExample("undead", "3x3:1,2,2,dLaRLL,2,0,2,2,2,0,1,0,0,2,1,2",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_Z, KEYCODE_DPAD_UP, KEYCODE_Z,
				KEYCODE_DPAD_RIGHT, KEYCODE_G, KEYCODE_DPAD_RIGHT, KEYCODE_V, KEYCODE_DPAD_DOWN,
				KEYCODE_V);
		addExample("unequal", "3:0D,0,0,0,0,0,0R,0,0U,",
				KEYCODE_DPAD_UP, KEYCODE_3, KEYCODE_DPAD_RIGHT, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_LEFT, KEYCODE_3,
				KEYCODE_DPAD_LEFT, KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3);
		addExample("unruly", "6x6:BCCAHgBCga", KEYCODE_DPAD_UP,
				KEYCODE_SPACE, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN,
				KEYCODE_SPACE, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_SPACE, KEYCODE_DPAD_UP,
				KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_SPACE,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
				KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_SPACE,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_SPACE, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER);
		addExample("untangle", "4:0-1,0-2,0-3,1-2,1-3,2-3", new GeneralSwipeAction(Swipe.FAST,
				squareProportions(0, -0.42), squareProportions(0, 0.25), Press.FINGER));
	}

	private static void addExample(final String backend, final String gameID, final int... keystrokes) {
		final ArrayList<ViewAction> actions = new ArrayList<>();
		for (int key : keystrokes) {
			actions.add(pressKey(key));
		}
		addExample(backend, gameID, actions.toArray(new ViewAction[actions.size()]));
	}

	private static void addExample(final String backend, final String gameID, final ViewAction... actions) {
		_usedBackends.add(backend);
		_params.add(new Object[]{backend, gameID, actions});
	}

	@Parameters(name = "{0}:{1}")
	public static Iterable<Object[]> data() {
		Set<String> unusedBackends = new LinkedHashSet<>(Arrays.asList(
				getTargetContext().getResources().getStringArray(R.array.games)));
		addExamples();
		for (String used : _usedBackends) {
			unusedBackends.remove(used);
		}
		for (String unused : unusedBackends) {
			addExample(unused, "", KEYCODE_DPAD_CENTER);  // testGameCompletion will fail appropriately
		}
		return _params;
	}

	public GamePlayTest(final String backend, final String gameID, final ViewAction... viewActions) {
		_backend = backend;
		_gameID = gameID;
		_viewActions = viewActions;
	}

	@Test
	public void testGameCompletion() throws InterruptedException {
		assertNotEquals("Missing test for " + _backend, "", _gameID);
		final Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse("sgtpuzzles:" + _backend + ":" + _gameID));
		final GamePlay activity = mActivityRule.launchActivity(intent);
		assertCompleted(false);
		onView(withId(R.id.game)).perform(_viewActions);
		assertCompleted(true);
		activity.finish();
	}

	private void assertCompleted(final boolean isCompleted) {
		// Note that flood and mines copy the status bar so there is score info before/after "COMPLETED".
		final Matcher<View> titleMatcher = withText(containsString(getTargetContext().getString(R.string.COMPLETED)));
		if (isCompleted) {
			onView(titleMatcher).inRoot(isDialog()).check(matches(isDisplayed()));
		} else {
			onView(titleMatcher).check(doesNotExist());
		}
	}

	public static CoordinatesProvider squareProportions(final double xProp, final double yProp) {
		return new CoordinatesProvider() {
			@Override
			public float[] calculateCoordinates(View view) {
				final int[] screenPos = new int[2];
				view.getLocationOnScreen(screenPos);
				final int squareSz = Math.min(view.getWidth(), view.getHeight());
				final float screenX = (float) (screenPos[0] + (0.5 * view.getWidth()) + xProp * squareSz);
				final float screenY = (float) (screenPos[1] + (0.5 * view.getHeight()) + yProp * squareSz);
				return new float[]{screenX, screenY};
			}
		};
	}
}
