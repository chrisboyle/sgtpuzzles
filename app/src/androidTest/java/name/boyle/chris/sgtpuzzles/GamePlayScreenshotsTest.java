package name.boyle.chris.sgtpuzzles;

import static android.content.Context.MODE_PRIVATE;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static androidx.test.platform.app.InstrumentationRegistry.getInstrumentation;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.CONTROLS_REMINDERS_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.NIGHT_MODE_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.STATE_PREFS_NAME;

import android.content.Intent;
import android.content.SharedPreferences;

import androidx.preference.PreferenceManager;
import androidx.test.core.app.ActivityScenario;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.io.IOException;

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
    public void testTakeScreenshots() throws IOException {
        screenshotAllGames("01_day_");
        prefs.edit().putString(NIGHT_MODE_KEY, "on").apply();
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        screenshotAllGames("02_night_");
        prefs.edit().remove(NIGHT_MODE_KEY).apply();
    }

    private void screenshotAllGames(String prefix) throws IOException {
        for (final BackendName backend : BackendName.values()) {
            final String savedGame = Utils.readAllOf(getInstrumentation().getContext().getAssets().open(backend.toString() + ".sav"));
            final Intent intent = new Intent(getApplicationContext(), GamePlay.class).putExtra("game", savedGame);
            try(ActivityScenario<GamePlay> ignored = ActivityScenario.launch(intent)) {
                onView(withText(R.string.starting)).check(doesNotExist());
                Screengrab.screenshot(prefix + backend);
            }
        }
    }
}
