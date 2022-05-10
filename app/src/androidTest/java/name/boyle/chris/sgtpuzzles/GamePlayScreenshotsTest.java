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
import static org.junit.Assert.assertEquals;
import static name.boyle.chris.sgtpuzzles.BackendName.BLACKBOX;
import static name.boyle.chris.sgtpuzzles.BackendName.BRIDGES;
import static name.boyle.chris.sgtpuzzles.BackendName.DOMINOSA;
import static name.boyle.chris.sgtpuzzles.BackendName.FIFTEEN;
import static name.boyle.chris.sgtpuzzles.BackendName.FILLING;
import static name.boyle.chris.sgtpuzzles.BackendName.FLIP;
import static name.boyle.chris.sgtpuzzles.BackendName.GALAXIES;
import static name.boyle.chris.sgtpuzzles.BackendName.GUESS;
import static name.boyle.chris.sgtpuzzles.BackendName.INERTIA;
import static name.boyle.chris.sgtpuzzles.BackendName.KEEN;
import static name.boyle.chris.sgtpuzzles.BackendName.LIGHTUP;
import static name.boyle.chris.sgtpuzzles.BackendName.LOOPY;
import static name.boyle.chris.sgtpuzzles.BackendName.MAGNETS;
import static name.boyle.chris.sgtpuzzles.BackendName.MAP;
import static name.boyle.chris.sgtpuzzles.BackendName.MINES;
import static name.boyle.chris.sgtpuzzles.BackendName.MOSAIC;
import static name.boyle.chris.sgtpuzzles.BackendName.NET;
import static name.boyle.chris.sgtpuzzles.BackendName.NETSLIDE;
import static name.boyle.chris.sgtpuzzles.BackendName.PALISADE;
import static name.boyle.chris.sgtpuzzles.BackendName.PATTERN;
import static name.boyle.chris.sgtpuzzles.BackendName.PEARL;
import static name.boyle.chris.sgtpuzzles.BackendName.PEGS;
import static name.boyle.chris.sgtpuzzles.BackendName.RANGE;
import static name.boyle.chris.sgtpuzzles.BackendName.RECT;
import static name.boyle.chris.sgtpuzzles.BackendName.SAMEGAME;
import static name.boyle.chris.sgtpuzzles.BackendName.SIGNPOST;
import static name.boyle.chris.sgtpuzzles.BackendName.SINGLES;
import static name.boyle.chris.sgtpuzzles.BackendName.SIXTEEN;
import static name.boyle.chris.sgtpuzzles.BackendName.SLANT;
import static name.boyle.chris.sgtpuzzles.BackendName.SOLO;
import static name.boyle.chris.sgtpuzzles.BackendName.TENTS;
import static name.boyle.chris.sgtpuzzles.BackendName.TOWERS;
import static name.boyle.chris.sgtpuzzles.BackendName.TRACKS;
import static name.boyle.chris.sgtpuzzles.BackendName.TWIDDLE;
import static name.boyle.chris.sgtpuzzles.BackendName.UNDEAD;
import static name.boyle.chris.sgtpuzzles.BackendName.UNEQUAL;
import static name.boyle.chris.sgtpuzzles.BackendName.UNTANGLE;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.CONTROLS_REMINDERS_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.LIMIT_DPI_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.NIGHT_MODE_KEY;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.STATE_PREFS_NAME;

import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Point;
import android.graphics.Rect;
import android.util.Pair;

import androidx.preference.PreferenceManager;
import androidx.test.core.app.ActivityScenario;
import androidx.test.espresso.core.internal.deps.guava.collect.Maps;

import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.io.IOException;
import java.util.Map;

import tools.fastlane.screengrab.FileWritingScreenshotCallback;
import tools.fastlane.screengrab.Screengrab;
import tools.fastlane.screengrab.ScreenshotCallback;
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar;

@RunWith(JUnit4.class)
public class GamePlayScreenshotsTest {

    private enum SpecialMode { NONE, NIGHT, CUSTOM }

    private static final Map<BackendName, Pair<Point, Rect>> iconCrops = Maps.newHashMap();
    static {
        iconCrops.put(BLACKBOX, Pair.create(new Point(385, 385), new Rect(0,   228, 158, 385)));
        iconCrops.put(BRIDGES,  Pair.create(new Point(384, 384), new Rect(228, 228, 384, 384)));
        iconCrops.put(DOMINOSA, Pair.create(new Point(388, 347), new Rect(194, 0,   388, 194)));
        iconCrops.put(FIFTEEN,  Pair.create(new Point(390, 390), new Rect(0,   194, 194, 390)));
        iconCrops.put(FILLING,  Pair.create(new Point(392, 392), new Rect(21,  119, 225, 323)));
        iconCrops.put(FLIP,     Pair.create(new Point(389, 389), new Rect(162, 97,  358, 293)));
        iconCrops.put(GALAXIES, Pair.create(new Point(387, 387), new Rect(0,   0,   222, 222)));
        iconCrops.put(GUESS,    Pair.create(new Point(386, 616), new Rect(110, 25,  371, 286)));
        iconCrops.put(INERTIA,  Pair.create(new Point(391, 391), new Rect(235, 0,   391, 156)));
        iconCrops.put(KEEN,     Pair.create(new Point(389, 389), new Rect(32,  162, 162, 292)));
        iconCrops.put(LIGHTUP,  Pair.create(new Point(391, 391), new Rect(220, 0,   391, 171)));
        iconCrops.put(LOOPY,    Pair.create(new Point(392, 392), new Rect(0,   0,   172, 172)));
        iconCrops.put(MAGNETS,  Pair.create(new Point(386, 339), new Rect(53,  146, 193, 286)));
        iconCrops.put(MINES,    Pair.create(new Point(385, 385), new Rect(209, 209, 385, 385)));
        iconCrops.put(MOSAIC,   Pair.create(new Point(387, 387), new Rect(191, 105, 321, 235)));
        iconCrops.put(NET,      Pair.create(new Point(390, 390), new Rect(0,   162, 228, 390)));
        iconCrops.put(NETSLIDE, Pair.create(new Point(391, 391), new Rect(0,   0,   195, 195)));
        iconCrops.put(PALISADE, Pair.create(new Point(390, 390), new Rect(0,   0,   260, 260)));
        iconCrops.put(PATTERN,  Pair.create(new Point(384, 384), new Rect(0,   0,   223, 223)));
        iconCrops.put(PEARL,    Pair.create(new Point(384, 384), new Rect(192, 27,  359, 194)));
        iconCrops.put(PEGS,     Pair.create(new Point(391, 391), new Rect(172, 0,   391, 219)));
        iconCrops.put(RANGE,    Pair.create(new Point(392, 392), new Rect(170, 23,  320, 173)));
        iconCrops.put(RECT,     Pair.create(new Point(390, 390), new Rect(171, 0,   390, 219)));
        iconCrops.put(SIGNPOST, Pair.create(new Point(390, 390), new Rect(37,  37,  197, 197)));
        iconCrops.put(SINGLES,  Pair.create(new Point(392, 392), new Rect(26,  26,  198, 198)));
        iconCrops.put(SIXTEEN,  Pair.create(new Point(390, 390), new Rect(195, 195, 390, 390)));
        iconCrops.put(SLANT,    Pair.create(new Point(391, 391), new Rect(195, 195, 390, 390)));
        iconCrops.put(SOLO,     Pair.create(new Point(390, 390), new Rect(19,  19,  137, 137)));
        iconCrops.put(TENTS,    Pair.create(new Point(389, 389), new Rect(173, 0,   373, 201)));
        iconCrops.put(TOWERS,   Pair.create(new Point(392, 392), new Rect(197, 8,   331, 141)));
        iconCrops.put(TRACKS,   Pair.create(new Point(346, 346), new Rect(8,   8,   174, 174)));
        iconCrops.put(TWIDDLE,  Pair.create(new Point(392, 392), new Rect(141, 43,  349, 251)));
        iconCrops.put(UNDEAD,   Pair.create(new Point(390, 450), new Rect(15,  75,  195, 255)));
        iconCrops.put(UNEQUAL,  Pair.create(new Point(390, 390), new Rect(195, 195, 390, 390)));
        iconCrops.put(UNTANGLE, Pair.create(new Point(390, 390), new Rect(4,   141, 204, 341)));
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
            case "tenInch":
                screenshotGame("01_", MAP);
                screenshotGame("02_", SOLO, "solo_4x4.sav", SpecialMode.NIGHT);
                screenshotGame("03_", MOSAIC);
                screenshotGame("04_", TENTS);
                screenshotGame("05_", TRACKS, "tracks_15x10.sav");
                screenshotGame("06_", GALAXIES);
                break;
            case "sevenInch":
                screenshotGame("01_", LIGHTUP);
                screenshotGame("02_", SIGNPOST);
                screenshotGame("03_", SAMEGAME);
                screenshotGame("04_", INERTIA, SpecialMode.NIGHT);
                screenshotGame("05_", PEGS);
                break;
            case "phone":
                screenshotGame("01_", NET);
                screenshotGame("02_", SOLO);
                screenshotGame("03_", MOSAIC);
                screenshotGame("04_", LOOPY, "loopy_8x14.sav", SpecialMode.CUSTOM);
                screenshotGame("05_", MAGNETS);
                screenshotGame("06_", TOWERS, SpecialMode.NIGHT);
                screenshotGame("07_", UNTANGLE);
                break;
        }
    }

    @Test
    public void testIconScreenshots() throws IOException {
        if (!"phone".equals(TestUtils.getFastlaneDeviceTypeOrSkipTest())) return;
        final FileWritingScreenshotCallback screenshotWriter = new FileWritingScreenshotCallback(getApplicationContext(), Screengrab.getLocale());
        prefs.edit().putString(LIMIT_DPI_KEY, "icon").apply();
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        for (final BackendName backend : BackendName.values()) {
            screenshotIcon("icon_day_", backend, screenshotWriter);
        }
        state.edit().clear().apply();  // Prevent "You have an unfinished game" dialog
        prefs.edit().putString(NIGHT_MODE_KEY, "on").apply();
        for (final BackendName backend : BackendName.values()) {

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
                switch(backend) {
                    case FIFTEEN:
                    case FLIP:
                    case NETSLIDE:
                    case SIXTEEN:
                    case TWIDDLE:
                        gameEngine.freezePartialRedo();
                }
                final GameView gameView = a.findViewById(R.id.game);
                final Pair<Point, Rect> pair = iconCrops.get(backend);
                final Point size = gameEngine.getGameSizeInGameCoords();
                if (pair != null) {
                    assertEquals("Game size for " + backend + " has changed", pair.first, size);
                }
                final Rect crop = pair == null ? new Rect(0, 0, size.x, size.y) : pair.second;
                callback.screenshotCaptured(prefix + backend, gameView.screenshot(crop, gameEngine.getGameSizeInGameCoords()));
            });
        }
    }

    private Intent launchTestGame(String filename) throws IOException {
        final String savedGame = Utils.readAllOf(getInstrumentation().getContext().getAssets().open(filename));
        return new Intent(getApplicationContext(), GamePlay.class).putExtra("game", savedGame);
    }
}
