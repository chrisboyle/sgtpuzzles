package name.boyle.chris.sgtpuzzles;

import static android.content.Context.MODE_PRIVATE;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withSubstring;

import android.content.SharedPreferences;

import androidx.test.ext.junit.rules.ActivityScenarioRule;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import tools.fastlane.screengrab.Screengrab;
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar;

@RunWith(JUnit4.class)
public class ScreenshotsTest {

    @BeforeClass
    public static void beforeAll() {
        CleanStatusBar.enableWithDefaults();
        SharedPreferences state = getApplicationContext().getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE);
        state.edit().clear().putString(PrefsConstants.CHOOSER_STYLE_KEY, "grid").apply();
    }

    @AfterClass
    public static void afterAll() {
        CleanStatusBar.disable();
    }

    @Rule
    public ActivityScenarioRule<GameChooser> activityRule = new ActivityScenarioRule<>(GameChooser.class);

    @Test
    public void testTakeScreenshot() {
        onView(withSubstring("Starred")).check(matches(isDisplayed()));
        Screengrab.screenshot("chooser");
//        onView(withSubstring("Cube")).perform(scrollTo());
    }
}
