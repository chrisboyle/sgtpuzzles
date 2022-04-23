package name.boyle.chris.sgtpuzzles;

import static android.content.Context.MODE_PRIVATE;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withSubstring;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static androidx.test.platform.app.InstrumentationRegistry.getInstrumentation;
import static name.boyle.chris.sgtpuzzles.BackendName.GALAXIES;
import static name.boyle.chris.sgtpuzzles.BackendName.INERTIA;
import static name.boyle.chris.sgtpuzzles.BackendName.LIGHTUP;
import static name.boyle.chris.sgtpuzzles.BackendName.LOOPY;
import static name.boyle.chris.sgtpuzzles.BackendName.MAGNETS;
import static name.boyle.chris.sgtpuzzles.BackendName.MAP;
import static name.boyle.chris.sgtpuzzles.BackendName.MOSAIC;
import static name.boyle.chris.sgtpuzzles.BackendName.NET;
import static name.boyle.chris.sgtpuzzles.BackendName.PEGS;
import static name.boyle.chris.sgtpuzzles.BackendName.SAMEGAME;
import static name.boyle.chris.sgtpuzzles.BackendName.SIGNPOST;
import static name.boyle.chris.sgtpuzzles.BackendName.SOLO;
import static name.boyle.chris.sgtpuzzles.BackendName.TENTS;
import static name.boyle.chris.sgtpuzzles.BackendName.TOWERS;
import static name.boyle.chris.sgtpuzzles.BackendName.TRACKS;
import static name.boyle.chris.sgtpuzzles.BackendName.UNTANGLE;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.CONTROLS_REMINDERS_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.NIGHT_MODE_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.STATE_PREFS_NAME;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;

import androidx.preference.PreferenceManager;
import androidx.test.core.app.ActivityScenario;
import androidx.test.platform.app.InstrumentationRegistry;

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
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        final Bundle launchArguments = InstrumentationRegistry.getArguments();
        final String deviceType = launchArguments.getString("device_type", "phone");
        if (deviceType.equals("tenInch")) {
            screenshotGame("01_", MAP);
            screenshotGame("02_", SOLO, "solo_4x4.sav", true, false);
            screenshotGame("03_", MOSAIC);
            screenshotGame("04_", TENTS);
            screenshotGame("05_", TRACKS, "tracks_15x10.sav", false, false);
            screenshotGame("06_", GALAXIES);
        } else if (deviceType.equals("sevenInch")) {
            screenshotGame("01_", LIGHTUP);
            screenshotGame("02_", SIGNPOST);
            screenshotGame("03_", SAMEGAME);
            screenshotGame("04_", INERTIA, true, false);
            screenshotGame("05_", PEGS);
        } else {
            screenshotGame("01_", NET);
            screenshotGame("02_", SOLO);
            screenshotGame("03_", MOSAIC);
            screenshotGame("04_", LOOPY, "loopy_8x14.sav", false, true);
            screenshotGame("05_", MAGNETS);
            screenshotGame("06_", TOWERS, true, false);
            screenshotGame("07_", UNTANGLE);
        }
    }

    private void screenshotGame(final String prefix, final BackendName backend) throws IOException {
        screenshotGame(prefix, backend, backend + ".sav", false, false);
    }

    @SuppressWarnings("SameParameterValue")
    private void screenshotGame(final String prefix, final BackendName backend, final boolean night, final boolean custom) throws IOException {
        screenshotGame(prefix, backend, backend + ".sav", night, custom);
    }

    private void screenshotGame(final String prefix, final BackendName backend, final String filename, final boolean night, final boolean custom) throws IOException {
        final String savedGame = Utils.readAllOf(getInstrumentation().getContext().getAssets().open(filename));
        final Intent intent = new Intent(getApplicationContext(), GamePlay.class).putExtra("game", savedGame);
        if (night) prefs.edit().putString(NIGHT_MODE_KEY, "on").apply();
        try(ActivityScenario<GamePlay> ignored = ActivityScenario.launch(intent)) {
            onView(withText(R.string.starting)).check(doesNotExist());
            if (custom) {
                onView(withId(R.id.type_menu)).perform(click());
                onView(withSubstring("Custom")).perform(click());
            }
            Screengrab.screenshot(prefix + backend);
        }
        if (night) prefs.edit().remove(NIGHT_MODE_KEY).apply();
    }
}
