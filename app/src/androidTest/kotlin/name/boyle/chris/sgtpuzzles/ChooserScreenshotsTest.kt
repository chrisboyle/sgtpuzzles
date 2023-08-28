package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.os.SystemClock
import androidx.preference.PreferenceManager
import androidx.test.core.app.ApplicationProvider
import androidx.test.espresso.Espresso
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.ViewAssertions
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.ext.junit.rules.ActivityScenarioRule
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.GUESS
import name.boyle.chris.sgtpuzzles.backend.LOOPY
import name.boyle.chris.sgtpuzzles.backend.MOSAIC
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME
import name.boyle.chris.sgtpuzzles.backend.TRACKS
import name.boyle.chris.sgtpuzzles.backend.UNTANGLE
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CHOOSER_STYLE_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.STATE_PREFS_NAME
import org.junit.AfterClass
import org.junit.BeforeClass
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import tools.fastlane.screengrab.Screengrab
import tools.fastlane.screengrab.cleanstatusbar.CleanStatusBar

@RunWith(JUnit4::class)
class ChooserScreenshotsTest {
    @JvmField
    @Rule
    val activityRule = ActivityScenarioRule(
        GameChooser::class.java
    )

    @Test
    fun testTakeScreenshots() {
        when (TestUtils.fastlaneDeviceTypeOrSkipTest) {
            "tenInch" -> {
                scrollToAndScreenshot(GUESS, "07_chooser")
                scrollToAndScreenshot(UNTANGLE, "08_chooser")
            }

            "sevenInch" -> {
                scrollToAndScreenshot(GUESS, "06_chooser")
                scrollToAndScreenshot(SAMEGAME, "07_chooser")
                scrollToAndScreenshot(UNTANGLE, "08_chooser")
            }

            "phone" -> {
                prefs.edit().putString(CHOOSER_STYLE_KEY, "grid")
                    // Star 3 arbitrarily chosen extra games in order that all 40 games fit on a 16x9 screen
                    .putBoolean("starred_$LOOPY", true)
                    .putBoolean("starred_$MOSAIC", true)
                    .putBoolean("starred_$TRACKS", true)
                    .apply()
                Espresso.onView(ViewMatchers.withSubstring(GUESS.displayName))
                    .check(ViewAssertions.matches(ViewMatchers.withEffectiveVisibility(ViewMatchers.Visibility.GONE)))
                SystemClock.sleep(100) // Espresso thinks we're idle before the animation has finished :-(
                assertIconVisible(GUESS)
                assertIconVisible(UNTANGLE)
                Screengrab.screenshot("08_chooser_grid")
                prefs.edit().clear().apply()
            }
        }
    }

    private fun assertIconVisible(backend: BackendName) {
        Espresso.onView(ViewMatchers.withChild(ViewMatchers.withSubstring(backend.displayName)))
            .check(ViewAssertions.matches(ViewMatchers.isCompletelyDisplayed()))
    }

    private fun scrollToAndScreenshot(backend: BackendName, screenshotName: String) {
        Espresso.onView(ViewMatchers.withChild(ViewMatchers.withSubstring(backend.displayName)))
            .perform(ViewActions.scrollTo())
            .check(ViewAssertions.matches(ViewMatchers.isCompletelyDisplayed()))
        SystemClock.sleep(100) // Espresso thinks we're idle before the scroll has finished :-(
        Screengrab.screenshot(screenshotName)
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
            prefs.edit().clear().apply()
            state.edit().clear().apply()
        }

        @JvmStatic
        @AfterClass
        fun afterAll() {
            CleanStatusBar.disable()
        }
    }
}