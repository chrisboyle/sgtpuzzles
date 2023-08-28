package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.content.Intent
import android.graphics.Point
import android.graphics.Rect
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.espresso.Espresso
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.ViewAssertions
import androidx.test.espresso.matcher.ViewMatchers
import name.boyle.chris.sgtpuzzles.Utils.readAllOf
import name.boyle.chris.sgtpuzzles.backend.BLACKBOX
import name.boyle.chris.sgtpuzzles.backend.BRIDGES
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.BackendName.Companion.all
import name.boyle.chris.sgtpuzzles.backend.DOMINOSA
import name.boyle.chris.sgtpuzzles.backend.FIFTEEN
import name.boyle.chris.sgtpuzzles.backend.FILLING
import name.boyle.chris.sgtpuzzles.backend.FLIP
import name.boyle.chris.sgtpuzzles.backend.GALAXIES
import name.boyle.chris.sgtpuzzles.backend.GUESS
import name.boyle.chris.sgtpuzzles.backend.INERTIA
import name.boyle.chris.sgtpuzzles.backend.KEEN
import name.boyle.chris.sgtpuzzles.backend.LIGHTUP
import name.boyle.chris.sgtpuzzles.backend.LOOPY
import name.boyle.chris.sgtpuzzles.backend.MAGNETS
import name.boyle.chris.sgtpuzzles.backend.MAP
import name.boyle.chris.sgtpuzzles.backend.MINES
import name.boyle.chris.sgtpuzzles.backend.MOSAIC
import name.boyle.chris.sgtpuzzles.backend.NET
import name.boyle.chris.sgtpuzzles.backend.NETSLIDE
import name.boyle.chris.sgtpuzzles.backend.PALISADE
import name.boyle.chris.sgtpuzzles.backend.PATTERN
import name.boyle.chris.sgtpuzzles.backend.PEARL
import name.boyle.chris.sgtpuzzles.backend.PEGS
import name.boyle.chris.sgtpuzzles.backend.RANGE
import name.boyle.chris.sgtpuzzles.backend.RECT
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME
import name.boyle.chris.sgtpuzzles.backend.SIGNPOST
import name.boyle.chris.sgtpuzzles.backend.SINGLES
import name.boyle.chris.sgtpuzzles.backend.SIXTEEN
import name.boyle.chris.sgtpuzzles.backend.SLANT
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.TENTS
import name.boyle.chris.sgtpuzzles.backend.TOWERS
import name.boyle.chris.sgtpuzzles.backend.TRACKS
import name.boyle.chris.sgtpuzzles.backend.TWIDDLE
import name.boyle.chris.sgtpuzzles.backend.UNDEAD
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL
import name.boyle.chris.sgtpuzzles.backend.UNTANGLE
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CONTROLS_REMINDERS_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.LIMIT_DPI_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.NIGHT_MODE_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.STATE_PREFS_NAME
import org.junit.AfterClass
import org.junit.Assert.assertEquals
import org.junit.BeforeClass
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import tools.fastlane.screengrab.FileWritingScreenshotCallback
import tools.fastlane.screengrab.Screengrab
import tools.fastlane.screengrab.ScreenshotCallback
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar
import java.io.IOException

@RunWith(JUnit4::class)
class GamePlayScreenshotsTest {
    private enum class SpecialMode {
        NONE, NIGHT, CUSTOM
    }

    private data class Crop(val expectSize: Point, val crop: Rect)

    private val iconCrops = mapOf(
        BLACKBOX to Crop(Point(385, 385), Rect(0,   228, 158, 385)),
        BRIDGES  to Crop(Point(384, 384), Rect(228, 228, 384, 384)),
        DOMINOSA to Crop(Point(388, 347), Rect(194, 0,   388, 194)),
        FIFTEEN  to Crop(Point(390, 390), Rect(0,   194, 194, 390)),
        FILLING  to Crop(Point(392, 392), Rect(21,  119, 225, 323)),
        FLIP     to Crop(Point(389, 389), Rect(162, 97,  358, 293)),
        GALAXIES to Crop(Point(387, 387), Rect(0,   0,   222, 222)),
        GUESS    to Crop(Point(386, 616), Rect(110, 25,  371, 286)),
        INERTIA  to Crop(Point(391, 391), Rect(235, 0,   391, 156)),
        KEEN     to Crop(Point(389, 389), Rect(32,  162, 162, 292)),
        LIGHTUP  to Crop(Point(391, 391), Rect(220, 0,   391, 171)),
        LOOPY    to Crop(Point(392, 392), Rect(0,   0,   172, 172)),
        MAGNETS  to Crop(Point(386, 339), Rect(53,  146, 193, 286)),
        MINES    to Crop(Point(385, 385), Rect(209, 209, 385, 385)),
        MOSAIC   to Crop(Point(387, 387), Rect(191, 105, 321, 235)),
        NET      to Crop(Point(390, 390), Rect(0,   162, 228, 390)),
        NETSLIDE to Crop(Point(391, 391), Rect(0,   0,   195, 195)),
        PALISADE to Crop(Point(390, 390), Rect(0,   0,   260, 260)),
        PATTERN  to Crop(Point(384, 384), Rect(0,   0,   223, 223)),
        PEARL    to Crop(Point(384, 384), Rect(192, 27,  359, 194)),
        PEGS     to Crop(Point(391, 391), Rect(172, 0,   391, 219)),
        RANGE    to Crop(Point(392, 392), Rect(170, 23,  320, 173)),
        RECT     to Crop(Point(390, 390), Rect(171, 0,   390, 219)),
        SIGNPOST to Crop(Point(390, 390), Rect(37,  37,  197, 197)),
        SINGLES  to Crop(Point(392, 392), Rect(26,  26,  198, 198)),
        SIXTEEN  to Crop(Point(390, 390), Rect(195, 195, 390, 390)),
        SLANT    to Crop(Point(391, 391), Rect(195, 195, 390, 390)),
        SOLO     to Crop(Point(390, 390), Rect(19,  19,  137, 137)),
        TENTS    to Crop(Point(389, 389), Rect(173, 0,   373, 201)),
        TOWERS   to Crop(Point(392, 392), Rect(197, 8,   331, 141)),
        TRACKS   to Crop(Point(346, 346), Rect(8,   8,   174, 174)),
        TWIDDLE  to Crop(Point(392, 392), Rect(141, 43,  349, 251)),
        UNDEAD   to Crop(Point(390, 450), Rect(15,  75,  195, 255)),
        UNEQUAL  to Crop(Point(390, 390), Rect(195, 195, 390, 390)),
        UNTANGLE to Crop(Point(390, 390), Rect(4,   141, 204, 341)),
    )

    @Test
    @Throws(IOException::class)
    fun testGooglePlayScreenshots() {
        val deviceType = TestUtils.fastlaneDeviceTypeOrSkipTest
        state.edit().clear().apply() // Prevent "You have an unfinished game" dialog
        when (deviceType) {
            "tenInch" -> {
                screenshotGame("01_", MAP)
                screenshotGame("02_", SOLO, "solo_4x4.sav", SpecialMode.NIGHT)
                screenshotGame("03_", MOSAIC)
                screenshotGame("04_", TENTS)
                screenshotGame("05_", TRACKS, "tracks_15x10.sav")
                screenshotGame("06_", GALAXIES)
            }

            "sevenInch" -> {
                screenshotGame("01_", LIGHTUP)
                screenshotGame("02_", SIGNPOST)
                screenshotGame("03_", SAMEGAME)
                screenshotGame("04_", INERTIA, mode=SpecialMode.NIGHT)
                screenshotGame("05_", PEGS)
            }

            "phone" -> {
                screenshotGame("01_", NET)
                screenshotGame("02_", SOLO)
                screenshotGame("03_", MOSAIC)
                screenshotGame("04_", LOOPY, "loopy_8x14.sav", SpecialMode.CUSTOM)
                screenshotGame("05_", MAGNETS)
                screenshotGame("06_", TOWERS, mode=SpecialMode.NIGHT)
                screenshotGame("07_", UNTANGLE)
            }
        }
    }

    @Test
    @Throws(IOException::class)
    fun testIconScreenshots() {
        if (TestUtils.fastlaneDeviceTypeOrSkipTest != "phone") return
        val screenshotWriter = FileWritingScreenshotCallback(
            ApplicationProvider.getApplicationContext(),
            Screengrab.getLocale()
        )
        prefs.edit().putString(LIMIT_DPI_KEY, "icon").apply()
        state.edit().clear().apply() // Prevent "You have an unfinished game" dialog
        for (backend in all) {
            screenshotIcon("icon_day_", backend, screenshotWriter)
        }
        state.edit().clear().apply() // Prevent "You have an unfinished game" dialog
        prefs.edit().putString(NIGHT_MODE_KEY, "on").apply()
        for (backend in all) {
            screenshotIcon("icon_night_", backend, screenshotWriter)
        }
        prefs.edit().remove(NIGHT_MODE_KEY).remove(LIMIT_DPI_KEY).apply()
        state.edit().clear().apply()
    }

    @Throws(IOException::class)
    private fun screenshotGame(
        prefix: String,
        backend: BackendName,
        filename: String = "$backend.sav",
        mode: SpecialMode = SpecialMode.NONE
    ) {
        if (mode == SpecialMode.NIGHT) prefs.edit().putString(NIGHT_MODE_KEY, "on").apply()
        ActivityScenario.launch<GamePlay>(launchTestGame(filename)).use {
            Espresso.onView(ViewMatchers.withText(R.string.starting))
                .check(ViewAssertions.doesNotExist())
            if (mode == SpecialMode.CUSTOM) {
                Espresso.onView(ViewMatchers.withId(R.id.type_menu)).perform(ViewActions.click())
                Espresso.onView(ViewMatchers.withSubstring("Custom")).perform(ViewActions.click())
            }
            Screengrab.screenshot(prefix + backend)
        }
        if (mode == SpecialMode.NIGHT) prefs.edit().remove(NIGHT_MODE_KEY).apply()
    }

    @Throws(IOException::class)
    private fun screenshotIcon(prefix: String, backend: BackendName, callback: ScreenshotCallback) {
        ActivityScenario.launch<GamePlay>(launchTestGame("$backend.sav")).use { scenario ->
            Espresso.onView(ViewMatchers.withText(R.string.starting))
                .check(ViewAssertions.doesNotExist())
            scenario.onActivity { a: GamePlay ->
                val gameEngine = a.gameEngine
                gameEngine.setCursorVisibility(false)
                a.gameViewResized() // redraw
                if (backend in setOf(FIFTEEN, FLIP, NETSLIDE, SIXTEEN, TWIDDLE)) {
                    gameEngine.freezePartialRedo()
                }
                val expectAndCrop = iconCrops[backend]
                val size = gameEngine.gameSizeInGameCoords
                if (expectAndCrop != null) {
                    assertEquals("Game size for $backend has changed", expectAndCrop.expectSize, size)
                }
                val crop = expectAndCrop?.crop ?: Rect(0, 0, size.x, size.y)
                callback.screenshotCaptured(
                    prefix + backend,
                    a.gameView.screenshot(crop, gameEngine.gameSizeInGameCoords)
                )
            }
        }
    }

    @Throws(IOException::class)
    private fun launchTestGame(filename: String): Intent {
        val savedGame =
            readAllOf(ApplicationProvider.getApplicationContext<Context>().assets.open(filename))
        return Intent(
            ApplicationProvider.getApplicationContext(),
            GamePlay::class.java
        ).putExtra("game", savedGame)
    }

    companion object {

        private val prefs =
            PreferenceManager.getDefaultSharedPreferences(ApplicationProvider.getApplicationContext())
        private val state = ApplicationProvider.getApplicationContext<Context>()
            .getSharedPreferences(STATE_PREFS_NAME, Context.MODE_PRIVATE)

        @JvmStatic
        @BeforeClass
        fun beforeAll() {
            CleanStatusBar.enableWithDefaults()
            prefs.edit().clear().putBoolean(CONTROLS_REMINDERS_KEY, false).apply()
            state.edit().clear().apply()
        }

        @JvmStatic
        @AfterClass
        fun afterAll() {
            CleanStatusBar.disable()
        }
    }
}