package name.boyle.chris.sgtpuzzles;

import static android.content.Context.MODE_PRIVATE;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.CONTROLS_REMINDERS_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.STATE_PREFS_NAME;

import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;

import androidx.preference.PreferenceManager;
import androidx.test.core.app.ActivityScenario;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import tools.fastlane.screengrab.Screengrab;
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar;

@RunWith(JUnit4.class)
public class GamePlayScreenshotsTest {

    private final static SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
    private final static SharedPreferences state = getApplicationContext().getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE);

    @BeforeClass
    public static void beforeAll() {
        CleanStatusBar.enableWithDefaults();
        prefs.edit().clear().putBoolean(CONTROLS_REMINDERS_KEY, false).apply();
        state.edit().clear().apply();
    }

    @AfterClass
    public static void afterAll() {
        CleanStatusBar.disable();
    }

    @Test
    public void testTakeScreenshots() {
        for (final BackendName backend : BackendName.values()) {
            final Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse("sgtpuzzles:" + backend), getApplicationContext(), GamePlay.class);
            try(ActivityScenario<GamePlay> ignored = ActivityScenario.launch(intent)) {
                onView(withText(R.string.starting)).check(doesNotExist());
                Screengrab.screenshot(backend.toString());
            }
        }
    }
}
