package name.boyle.chris.sgtpuzzles

import android.content.Intent
import android.net.Uri
import androidx.preference.PreferenceManager
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.espresso.Espresso
import androidx.test.espresso.assertion.ViewAssertions
import androidx.test.espresso.matcher.ViewMatchers
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.NIGHT_MODE_KEY
import org.junit.Test

/** Tests of non-parameterized things in GamePlay, that don't need repeating for each backend.  */
class GamePlaySpecificTest {
    /** Check we don't crash when applying night mode to an activity with state DESTROYED.  */
    @Test
    fun testNightModeCrash() {
        val prefs =
            PreferenceManager.getDefaultSharedPreferences(ApplicationProvider.getApplicationContext())
        val uri = Uri.parse("sgtpuzzles:lightup:4x4:a12jBBa") // probably any game
        val intent = Intent(
            Intent.ACTION_VIEW,
            uri,
            ApplicationProvider.getApplicationContext(),
            GamePlay::class.java
        )
        prefs.edit().remove(NIGHT_MODE_KEY).apply()
        checkGameStarts(intent)
        prefs.edit().putString(NIGHT_MODE_KEY, "on").apply()
        checkGameStarts(intent)
        prefs.edit().remove(NIGHT_MODE_KEY).apply()
    }

    private fun checkGameStarts(intent: Intent) {
        ActivityScenario.launch<GamePlay>(intent).use {
            Espresso.onView(ViewMatchers.withText(R.string.starting))
                .check(ViewAssertions.doesNotExist())
        }
    }
}