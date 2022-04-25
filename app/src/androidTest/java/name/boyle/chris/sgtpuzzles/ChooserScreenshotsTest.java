package name.boyle.chris.sgtpuzzles;

import static android.content.Context.MODE_PRIVATE;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isCompletelyDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withChild;
import static androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static androidx.test.espresso.matcher.ViewMatchers.withSubstring;
import static name.boyle.chris.sgtpuzzles.BackendName.GUESS;
import static name.boyle.chris.sgtpuzzles.BackendName.SAMEGAME;
import static name.boyle.chris.sgtpuzzles.BackendName.UNTANGLE;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.CHOOSER_STYLE_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.STATE_PREFS_NAME;

import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.SystemClock;

import androidx.preference.PreferenceManager;
import androidx.test.espresso.matcher.ViewMatchers;
import androidx.test.ext.junit.rules.ActivityScenarioRule;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import tools.fastlane.screengrab.Screengrab;
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar;

@RunWith(JUnit4.class)
public class ChooserScreenshotsTest {

    private final static SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
    private final static SharedPreferences state = getApplicationContext().getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE);

    @BeforeClass
    public static void beforeAll() {
        CleanStatusBar.enableWithDefaults();
        prefs.edit().clear().apply();
        state.edit().clear().apply();
    }

    @AfterClass
    public static void afterAll() {
        CleanStatusBar.disable();
    }

    @Rule
    public final ActivityScenarioRule<GameChooser> activityRule = new ActivityScenarioRule<>(GameChooser.class);

    @Test
    public void testTakeScreenshots() {
        switch (InstrumentationRegistry.getArguments().getString("device_type")) {
            case "tenInch":
                scrollToAndScreenshot(GUESS, "07_chooser");
                scrollToAndScreenshot(UNTANGLE, "08_chooser");
                break;
            case "sevenInch":
                scrollToAndScreenshot(GUESS, "06_chooser");
                scrollToAndScreenshot(SAMEGAME, "07_chooser");
                scrollToAndScreenshot(UNTANGLE, "08_chooser");
                break;
            case "phone":
                prefs.edit().putString(CHOOSER_STYLE_KEY, "grid").apply();
                onView(withSubstring(GUESS.getDisplayName())).check(matches(withEffectiveVisibility(ViewMatchers.Visibility.GONE)));
                SystemClock.sleep(100);  // Espresso thinks we're idle before the animation has finished :-(
                Screengrab.screenshot("08_chooser_grid");
                prefs.edit().remove(CHOOSER_STYLE_KEY).apply();
                break;
        }
    }

    private void scrollToAndScreenshot(final BackendName backend, final String screenshotName) {
        onView(withChild(withSubstring(backend.getDisplayName())))
                .perform(scrollTo())
                .check(matches(isCompletelyDisplayed()));
        SystemClock.sleep(100);  // Espresso thinks we're idle before the scroll has finished :-(
        Screengrab.screenshot(screenshotName);
    }
}
