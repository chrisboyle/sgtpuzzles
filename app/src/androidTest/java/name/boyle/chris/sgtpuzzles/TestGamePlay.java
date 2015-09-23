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

import static android.support.test.espresso.Espresso.onView;
import static android.support.test.espresso.assertion.ViewAssertions.matches;
import static android.support.test.espresso.matcher.RootMatchers.isDialog;
import static android.support.test.espresso.matcher.ViewMatchers.isDisplayed;
import static android.support.test.espresso.matcher.ViewMatchers.withId;
import static android.support.test.espresso.matcher.ViewMatchers.withText;

@RunWith(AndroidJUnit4.class)
public class TestGamePlay {

	@Rule
	public ActivityTestRule<GamePlay> mActivityRule =
			new ActivityTestRule<>(GamePlay.class, false, false);

	@Test
	public void testPlayMinimalNet() throws InterruptedException {
		launchGameID("net:1x2:42");
		onView(withId(R.id.game)).perform(clickXYProportion(0.5, 0.25));
		onView(withText(R.string.COMPLETED)).inRoot(isDialog()).check(matches(isDisplayed()));
	}

	private GamePlay launchGameID(String gameID) {
		return mActivityRule.launchActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("sgtpuzzles:" + gameID)));
	}

	public static ViewAction clickXYProportion(final double x, final double y){
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
