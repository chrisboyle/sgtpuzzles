package name.boyle.chris.sgtpuzzles;

import static android.content.Context.MODE_PRIVATE;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withSubstring;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;
import static name.boyle.chris.sgtpuzzles.config.PrefsConstants.CONTROLS_REMINDERS_KEY;
import static name.boyle.chris.sgtpuzzles.config.PrefsConstants.LIMIT_DPI_KEY;
import static name.boyle.chris.sgtpuzzles.config.PrefsConstants.NIGHT_MODE_KEY;
import static name.boyle.chris.sgtpuzzles.config.PrefsConstants.STATE_PREFS_NAME;

import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Point;
import android.graphics.Rect;
import android.util.Pair;

import androidx.preference.PreferenceManager;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

import name.boyle.chris.sgtpuzzles.backend.BLACKBOX;
import name.boyle.chris.sgtpuzzles.backend.BRIDGES;
import name.boyle.chris.sgtpuzzles.backend.BackendName;
import name.boyle.chris.sgtpuzzles.backend.DOMINOSA;
import name.boyle.chris.sgtpuzzles.backend.FIFTEEN;
import name.boyle.chris.sgtpuzzles.backend.FILLING;
import name.boyle.chris.sgtpuzzles.backend.FLIP;
import name.boyle.chris.sgtpuzzles.backend.GALAXIES;
import name.boyle.chris.sgtpuzzles.backend.GUESS;
import name.boyle.chris.sgtpuzzles.backend.GameEngine;
import name.boyle.chris.sgtpuzzles.backend.INERTIA;
import name.boyle.chris.sgtpuzzles.backend.KEEN;
import name.boyle.chris.sgtpuzzles.backend.LIGHTUP;
import name.boyle.chris.sgtpuzzles.backend.LOOPY;
import name.boyle.chris.sgtpuzzles.backend.MAGNETS;
import name.boyle.chris.sgtpuzzles.backend.MAP;
import name.boyle.chris.sgtpuzzles.backend.MINES;
import name.boyle.chris.sgtpuzzles.backend.MOSAIC;
import name.boyle.chris.sgtpuzzles.backend.NET;
import name.boyle.chris.sgtpuzzles.backend.NETSLIDE;
import name.boyle.chris.sgtpuzzles.backend.PALISADE;
import name.boyle.chris.sgtpuzzles.backend.PATTERN;
import name.boyle.chris.sgtpuzzles.backend.PEARL;
import name.boyle.chris.sgtpuzzles.backend.PEGS;
import name.boyle.chris.sgtpuzzles.backend.RANGE;
import name.boyle.chris.sgtpuzzles.backend.RECT;
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME;
import name.boyle.chris.sgtpuzzles.backend.SIGNPOST;
import name.boyle.chris.sgtpuzzles.backend.SINGLES;
import name.boyle.chris.sgtpuzzles.backend.SIXTEEN;
import name.boyle.chris.sgtpuzzles.backend.SLANT;
import name.boyle.chris.sgtpuzzles.backend.SOLO;
import name.boyle.chris.sgtpuzzles.backend.TENTS;
import name.boyle.chris.sgtpuzzles.backend.TOWERS;
import name.boyle.chris.sgtpuzzles.backend.TRACKS;
import name.boyle.chris.sgtpuzzles.backend.TWIDDLE;
import name.boyle.chris.sgtpuzzles.backend.UNDEAD;
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL;
import name.boyle.chris.sgtpuzzles.backend.UNTANGLE;
import tools.fastlane.screengrab.FileWritingScreenshotCallback;
import tools.fastlane.screengrab.Screengrab;
import tools.fastlane.screengrab.ScreenshotCallback;
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar;

@RunWith(JUnit4.class)
public class GamePlayScreenshotsTest {

    private enum SpecialMode { NONE, NIGHT, CUSTOM }

    private static final Map<BackendName, Pair<Point, Rect>> iconCrops = new LinkedHashMap<>();
    static {
        iconCrops.put(BLACKBOX.INSTANCE, Pair.create(new Point(385, 385), new Rect(0,   228, 158, 385)));
        iconCrops.put(BRIDGES.INSTANCE,  Pair.create(new Point(384, 384), new Rect(228, 228, 384, 384)));
        iconCrops.put(DOMINOSA.INSTANCE, Pair.create(new Point(388, 347), new Rect(194, 0,   388, 194)));
        iconCrops.put(FIFTEEN.INSTANCE,  Pair.create(new Point(390, 390), new Rect(0,   194, 194, 390)));
        iconCrops.put(FILLING.INSTANCE,  Pair.create(new Point(392, 392), new Rect(21,  119, 225, 323)));
        iconCrops.put(FLIP.INSTANCE,     Pair.create(new Point(389, 389), new Rect(162, 97,  358, 293)));
        iconCrops.put(GALAXIES.INSTANCE, Pair.create(new Point(387, 387), new Rect(0,   0,   222, 222)));
        iconCrops.put(GUESS.INSTANCE,    Pair.create(new Point(386, 616), new Rect(110, 25,  371, 286)));
        iconCrops.put(INERTIA.INSTANCE,  Pair.create(new Point(391, 391), new Rect(235, 0,   391, 156)));
        iconCrops.put(KEEN.INSTANCE,     Pair.create(new Point(389, 389), new Rect(32,  162, 162, 292)));
        iconCrops.put(LIGHTUP.INSTANCE,  Pair.create(new Point(391, 391), new Rect(220, 0,   391, 171)));
        iconCrops.put(LOOPY.INSTANCE,    Pair.create(new Point(392, 392), new Rect(0,   0,   172, 172)));
        iconCrops.put(MAGNETS.INSTANCE,  Pair.create(new Point(386, 339), new Rect(53,  146, 193, 286)));
        iconCrops.put(MINES.INSTANCE,    Pair.create(new Point(385, 385), new Rect(209, 209, 385, 385)));
        iconCrops.put(MOSAIC.INSTANCE,   Pair.create(new Point(387, 387), new Rect(191, 105, 321, 235)));
        iconCrops.put(NET.INSTANCE,      Pair.create(new Point(390, 390), new Rect(0,   162, 228, 390)));
        iconCrops.put(NETSLIDE.INSTANCE, Pair.create(new Point(391, 391), new Rect(0,   0,   195, 195)));
        iconCrops.put(PALISADE.INSTANCE, Pair.create(new Point(390, 390), new Rect(0,   0,   260, 260)));
        iconCrops.put(PATTERN.INSTANCE,  Pair.create(new Point(384, 384), new Rect(0,   0,   223, 223)));
        iconCrops.put(PEARL.INSTANCE,    Pair.create(new Point(384, 384), new Rect(192, 27,  359, 194)));
        iconCrops.put(PEGS.INSTANCE,     Pair.create(new Point(391, 391), new Rect(172, 0,   391, 219)));
        iconCrops.put(RANGE.INSTANCE,    Pair.create(new Point(392, 392), new Rect(170, 23,  320, 173)));
        iconCrops.put(RECT.INSTANCE,     Pair.create(new Point(390, 390), new Rect(171, 0,   390, 219)));
        iconCrops.put(SIGNPOST.INSTANCE, Pair.create(new Point(390, 390), new Rect(37,  37,  197, 197)));
        iconCrops.put(SINGLES.INSTANCE,  Pair.create(new Point(392, 392), new Rect(26,  26,  198, 198)));
        iconCrops.put(SIXTEEN.INSTANCE,  Pair.create(new Point(390, 390), new Rect(195, 195, 390, 390)));
        iconCrops.put(SLANT.INSTANCE,    Pair.create(new Point(391, 391), new Rect(195, 195, 390, 390)));
        iconCrops.put(SOLO.INSTANCE,     Pair.create(new Point(390, 390), new Rect(19,  19,  137, 137)));
        iconCrops.put(TENTS.INSTANCE,    Pair.create(new Point(389, 389), new Rect(173, 0,   373, 201)));
        iconCrops.put(TOWERS.INSTANCE,   Pair.create(new Point(392, 392), new Rect(197, 8,   331, 141)));
        iconCrops.put(TRACKS.INSTANCE,   Pair.create(new Point(346, 346), new Rect(8,   8,   174, 174)));
        iconCrops.put(TWIDDLE.INSTANCE,  Pair.create(new Point(392, 392), new Rect(141, 43,  349, 251)));
        iconCrops.put(UNDEAD.INSTANCE,   Pair.create(new Point(390, 450), new Rect(15,  75,  195, 255)));
        iconCrops.put(UNEQUAL.INSTANCE,  Pair.create(new Point(390, 390), new Rect(195, 195, 390, 390)));
        iconCrops.put(UNTANGLE.INSTANCE, Pair.create(new Point(390, 390), new Rect(4,   141, 204, 341)));
    }

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
    public void testGooglePlayScreenshots() throws IOException {
        final String deviceType = TestUtils.getFastlaneDeviceTypeOrSkipTest();
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        switch (deviceType) {
            case "tenInch" -> {
                screenshotGame("01_", MAP.INSTANCE);
                screenshotGame("02_", SOLO.INSTANCE, "solo_4x4.sav", SpecialMode.NIGHT);
                screenshotGame("03_", MOSAIC.INSTANCE);
                screenshotGame("04_", TENTS.INSTANCE);
                screenshotGame("05_", TRACKS.INSTANCE, "tracks_15x10.sav");
                screenshotGame("06_", GALAXIES.INSTANCE);
            }
            case "sevenInch" -> {
                screenshotGame("01_", LIGHTUP.INSTANCE);
                screenshotGame("02_", SIGNPOST.INSTANCE);
                screenshotGame("03_", SAMEGAME.INSTANCE);
                screenshotGame("04_", INERTIA.INSTANCE, SpecialMode.NIGHT);
                screenshotGame("05_", PEGS.INSTANCE);
            }
            case "phone" -> {
                screenshotGame("01_", NET.INSTANCE);
                screenshotGame("02_", SOLO.INSTANCE);
                screenshotGame("03_", MOSAIC.INSTANCE);
                screenshotGame("04_", LOOPY.INSTANCE, "loopy_8x14.sav", SpecialMode.CUSTOM);
                screenshotGame("05_", MAGNETS.INSTANCE);
                screenshotGame("06_", TOWERS.INSTANCE, SpecialMode.NIGHT);
                screenshotGame("07_", UNTANGLE.INSTANCE);
            }
        }
    }

    @Test
    public void testIconScreenshots() throws IOException {
        if (!"phone".equals(TestUtils.getFastlaneDeviceTypeOrSkipTest())) return;
        final FileWritingScreenshotCallback screenshotWriter = new FileWritingScreenshotCallback(getApplicationContext(), Screengrab.getLocale());
        prefs.edit().putString(LIMIT_DPI_KEY, "icon").apply();
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        for (final BackendName backend : BackendName.getAll()) {
            screenshotIcon("icon_day_", backend, screenshotWriter);
        }
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        prefs.edit().putString(NIGHT_MODE_KEY, "on").apply();
        for (final BackendName backend : BackendName.getAll()) {

            screenshotIcon("icon_night_", backend, screenshotWriter);
        }
        prefs.edit().remove(NIGHT_MODE_KEY).remove(LIMIT_DPI_KEY).apply();
        state.edit().clear().apply();
    }

    private void screenshotGame(final String prefix, final BackendName backend) throws IOException {
        screenshotGame(prefix, backend, backend + ".sav", SpecialMode.NONE);
    }

    @SuppressWarnings("SameParameterValue")
    private void screenshotGame(final String prefix, final BackendName backend, final SpecialMode mode) throws IOException {
        screenshotGame(prefix, backend, backend + ".sav", mode);
    }

    @SuppressWarnings("SameParameterValue")
    private void screenshotGame(final String prefix, final BackendName backend, final String filename) throws IOException {
        screenshotGame(prefix, backend, filename, SpecialMode.NONE);
    }

    private void screenshotGame(final String prefix, final BackendName backend, final String filename, final SpecialMode mode) throws IOException {
        if (mode == SpecialMode.NIGHT) prefs.edit().putString(NIGHT_MODE_KEY, "on").apply();
        try(ActivityScenario<GamePlay> ignored = ActivityScenario.launch(launchTestGame(filename))) {
            onView(withText(R.string.starting)).check(doesNotExist());
            if (mode == SpecialMode.CUSTOM) {
                onView(withId(R.id.type_menu)).perform(click());
                onView(withSubstring("Custom")).perform(click());
            }
            Screengrab.screenshot(prefix + backend);
        }
        if (mode == SpecialMode.NIGHT) prefs.edit().remove(NIGHT_MODE_KEY).apply();
    }

    private void screenshotIcon(final String prefix, final BackendName backend, final ScreenshotCallback callback) throws IOException {
        try(ActivityScenario<GamePlay> scenario = ActivityScenario.launch(launchTestGame(backend + ".sav"))) {
            onView(withText(R.string.starting)).check(doesNotExist());
            scenario.onActivity(a -> {
                final GameEngine gameEngine = a.getGameEngine();
                gameEngine.setCursorVisibility(false);
                a.gameViewResized();  // redraw
                if (backend == FIFTEEN.INSTANCE || backend == FLIP.INSTANCE
                        || backend == NETSLIDE.INSTANCE || backend == SIXTEEN.INSTANCE
                        || backend == TWIDDLE.INSTANCE) {
                    gameEngine.freezePartialRedo();
                }
                final Pair<Point, Rect> pair = iconCrops.get(backend);
                final Point size = gameEngine.getGameSizeInGameCoords();
                if (pair != null) {
                    assertEquals("Game size for " + backend + " has changed", pair.first, size);
                }
                final Rect crop = pair == null ? new Rect(0, 0, size.x, size.y) : pair.second;
                callback.screenshotCaptured(prefix + backend, a.getGameView().screenshot(crop, gameEngine.getGameSizeInGameCoords()));
            });
        }
    }

    private Intent launchTestGame(String filename) throws IOException {
        final String savedGame = Utils.readAllOf(ApplicationProvider.getApplicationContext().getAssets().open(filename));
        return new Intent(getApplicationContext(), GamePlay.class).putExtra("game", savedGame);
    }
}
