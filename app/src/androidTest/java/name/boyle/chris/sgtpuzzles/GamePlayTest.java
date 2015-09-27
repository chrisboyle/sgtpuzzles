package name.boyle.chris.sgtpuzzles;

import android.content.Intent;
import android.net.Uri;
import android.support.test.espresso.ViewAction;
import android.support.test.espresso.action.CoordinatesProvider;
import android.support.test.espresso.action.GeneralClickAction;
import android.support.test.espresso.action.Press;
import android.support.test.espresso.action.Tap;
import android.support.test.rule.ActivityTestRule;
import android.support.test.runner.AndroidJUnit4;
import android.view.View;

import org.hamcrest.Matcher;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

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
import static android.test.MoreAsserts.assertEmpty;
import static android.view.KeyEvent.KEYCODE_1;
import static android.view.KeyEvent.KEYCODE_2;
import static android.view.KeyEvent.KEYCODE_3;
import static android.view.KeyEvent.KEYCODE_A;
import static android.view.KeyEvent.KEYCODE_DPAD_CENTER;
import static android.view.KeyEvent.KEYCODE_DPAD_DOWN;
import static android.view.KeyEvent.KEYCODE_DPAD_LEFT;
import static android.view.KeyEvent.KEYCODE_DPAD_RIGHT;
import static android.view.KeyEvent.KEYCODE_DPAD_UP;
import static android.view.KeyEvent.KEYCODE_G;
import static android.view.KeyEvent.KEYCODE_V;
import static android.view.KeyEvent.KEYCODE_Z;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.startsWith;

@RunWith(AndroidJUnit4.class)
public class GamePlayTest {

	@Rule
	public ActivityTestRule<GamePlay> mActivityRule =
			new ActivityTestRule<>(GamePlay.class, false, false);

	@Test
	public void testPlayOneOfEverything() throws InterruptedException {
		final LinkedHashSet<String> backends = new LinkedHashSet<>(Arrays.asList(
				getTargetContext().getResources().getStringArray(R.array.games)));
		// not currently testable: no keyboard control, drags need too much precision
		backends.remove("untangle");
		// all other backends should be tested
		assertCompletesGame(backends, "blackbox", "w3h3m1M1:38727296",
				KEYCODE_DPAD_UP, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "bridges", "3x3m2:3a2c1b",
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT);
		backends.remove("cube");  // TODO
		assertCompletesGame(backends, "dominosa", "1:011100",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "fifteen", "2x2:1,2,0,3", KEYCODE_DPAD_LEFT);
		assertCompletesGame(backends, "filling", "2x1:02", KEYCODE_DPAD_UP, KEYCODE_2);
		assertCompletesGame(backends, "flip", "2x2:edb7,d", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "flood", "2x2:1212,6", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "galaxies", "3x3:co",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "guess", "c2p2g2Bm:c2ab",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "inertia", "3x3:wbbSbbgms", KEYCODE_DPAD_DOWN);
		assertCompletesGame(backends, "keen", "3:_baa_3a,m6s1m3a3",
				KEYCODE_DPAD_UP, KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3, KEYCODE_DPAD_RIGHT,
				KEYCODE_2, KEYCODE_DPAD_DOWN, KEYCODE_1, KEYCODE_DPAD_LEFT, KEYCODE_2,
				KEYCODE_DPAD_LEFT, KEYCODE_3, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3);
		assertCompletesGame(backends, "lightup", "2x2:a0b", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "loopy", "3x3t0:02a2a1c",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "magnets", "3x2:111,21,111,12,TTTBBB",
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "map", "3x2n5:afa,3120a", KEYCODE_DPAD_UP,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "mines", "4x3:2,0,m5d9",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "net", "1x2:42", KEYCODE_DPAD_UP, KEYCODE_A);
		backends.remove("netslide");  // TODO
		assertCompletesGame(backends, "pattern", "1x2:2/1/1",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "pearl", "5x5:dBaWaBgWaBeB", KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "pegs", "4x4:PHPPHPPOPOPOPOPO", KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_DOWN);
		assertCompletesGame(backends, "range", "3x2:b2_4b",
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "rect", "2x2:2a2a", KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "samegame", "2x2c3s2:1,1,3,3", KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "signpost", "3x2:1ccfcg6a",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "singles", "2x2:1121", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "sixteen", "2x2:1,4,3,2", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "slant", "2x2:1c1d", KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "solo", "2j:1c,b__", KEYCODE_DPAD_RIGHT, KEYCODE_2,
				KEYCODE_DPAD_DOWN, KEYCODE_1, KEYCODE_DPAD_LEFT, KEYCODE_2);
		assertCompletesGame(backends, "tents", "4x4:baj_,1,1,0,1,1,0,2,0", KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "towers", "3:1/2/3/2/2/1/1/2/2/3/2/1",
				KEYCODE_DPAD_UP, KEYCODE_3, KEYCODE_DPAD_RIGHT, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_LEFT, KEYCODE_3,
				KEYCODE_DPAD_LEFT, KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3);
		assertCompletesGame(backends, "tracks", "4x4:Cm9a,3,2,S3,4,S3,4,3,2",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "twiddle", "2x2n2:4,3,2,1",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER);
		assertCompletesGame(backends, "undead", "3x3:1,2,2,dLaRLL,2,0,2,2,2,0,1,0,0,2,1,2",
				KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_Z, KEYCODE_DPAD_UP, KEYCODE_Z,
				KEYCODE_DPAD_RIGHT, KEYCODE_G, KEYCODE_DPAD_RIGHT, KEYCODE_V, KEYCODE_DPAD_DOWN,
				KEYCODE_V);
		assertCompletesGame(backends, "unequal", "3:0D,0,0,0,0,0,0R,0,0U,",
				KEYCODE_DPAD_UP, KEYCODE_3, KEYCODE_DPAD_RIGHT, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_LEFT, KEYCODE_3,
				KEYCODE_DPAD_LEFT, KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
				KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3);
		backends.remove("unruly");  // TODO
		assertEmpty("Backends untested", backends);
	}

	@Test
	public void testPlayMinimalNetByTouch() throws InterruptedException {
		assertCompletesGame(null, "net", "1x2:42", tapXYProportion(0.5, 0.25));
	}

	private void assertCompletesGame(final Set<String> backends, final String backend, final String gameID, final int... keystrokes) {
		final ArrayList<ViewAction> actions = new ArrayList<>();
		for (int key : keystrokes) {
			actions.add(pressKey(key));
		}
		assertCompletesGame(backends, backend, gameID, actions.toArray(new ViewAction[actions.size()]));
	}

	private void assertCompletesGame(final Set<String> backends, final String backend, final String gameID, final ViewAction... actions) {
		if (backends != null) backends.remove(backend);
		final GamePlay activity = launchGameID(backend + ":" + gameID);
		assertCompleted(false);
		onView(withId(R.id.game)).perform(actions);
		assertCompleted(true);
		activity.finish();
	}

	private GamePlay launchGameID(final String gameID) {
		return mActivityRule.launchActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("sgtpuzzles:" + gameID)));
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

	public static ViewAction tapXYProportion(final double x, final double y) {
		return new GeneralClickAction(
				Tap.SINGLE,
				new CoordinatesProvider() {
					@Override
					public float[] calculateCoordinates(View view) {
						final int[] screenPos = new int[2];
						view.getLocationOnScreen(screenPos);

						final float screenX = (float) (screenPos[0] + (x * view.getWidth()));
						final float screenY = (float) (screenPos[1] + (y * view.getHeight()));
						return new float[]{screenX, screenY};
					}
				},
				Press.FINGER);
	}
}
