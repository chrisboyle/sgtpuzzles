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
import static android.view.KeyEvent.KEYCODE_DPAD_CENTER;
import static android.view.KeyEvent.KEYCODE_DPAD_DOWN;
import static android.view.KeyEvent.KEYCODE_DPAD_LEFT;
import static android.view.KeyEvent.KEYCODE_DPAD_RIGHT;
import static android.view.KeyEvent.KEYCODE_DPAD_UP;

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
		assertCompletesGame(backends, "blackbox", "w5h5m3M3:608e239b62f44365",
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
				KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
				KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT,
				KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER);
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
		backends.remove("filling");  // TODO
		backends.remove("flip");  // TODO
		backends.remove("flood");  // TODO
		backends.remove("galaxies");  // TODO
		backends.remove("guess");  // TODO
		backends.remove("inertia");  // TODO
		backends.remove("keen");  // TODO
		backends.remove("lightup");  // TODO
		backends.remove("loopy");  // TODO
		backends.remove("magnets");  // TODO
		backends.remove("map");  // TODO
		backends.remove("mines");  // TODO
		backends.remove("net");  // TODO
		backends.remove("netslide");  // TODO
		backends.remove("pattern");  // TODO
		backends.remove("pearl");  // TODO
		backends.remove("pegs");  // TODO
		backends.remove("range");  // TODO
		backends.remove("rect");  // TODO
		backends.remove("samegame");  // TODO
		backends.remove("signpost");  // TODO
		backends.remove("singles");  // TODO
		backends.remove("sixteen");  // TODO
		backends.remove("slant");  // TODO
		backends.remove("solo");  // TODO
		backends.remove("tents");  // TODO
		backends.remove("towers");  // TODO
		backends.remove("tracks");  // TODO
		backends.remove("twiddle");  // TODO
		backends.remove("undead");  // TODO
		backends.remove("unequal");  // TODO
		backends.remove("unruly");  // TODO
		backends.remove("untangle");  // TODO
		assertEmpty("Backends untested", backends);
	}

	@Test
	public void testPlayMinimalNetByTouch() throws InterruptedException {
		assertCompletesGame(null, "net", "1x2:42", tapXYProportion(0.5, 0.25));
	}

	private void assertCompletesGame(final Set<String> backends, final String backend,final  String gameID, final int... keystrokes) {
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
		if (isCompleted) {
			onView(withText(R.string.COMPLETED)).inRoot(isDialog()).check(matches(isDisplayed()));
		} else {
			onView(withText(R.string.COMPLETED)).check(doesNotExist());
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
