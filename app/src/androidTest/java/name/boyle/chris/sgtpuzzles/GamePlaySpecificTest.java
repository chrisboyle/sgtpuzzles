package name.boyle.chris.sgtpuzzles;

import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static name.boyle.chris.sgtpuzzles.PrefsConstants.NIGHT_MODE_KEY;

import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;

import androidx.preference.PreferenceManager;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;

import org.junit.Test;

/** Tests of non-parameterized things in GamePlay, that don't need repeating for each backend. */
public class GamePlaySpecificTest {

    /** Check we don't crash when applying night mode to an activity with state DESTROYED. */
    @Test
    public void testNightModeCrash() {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final Uri uri = Uri.parse("sgtpuzzles:lightup:4x4:a12jBBa");  // probably any game
        final Intent intent = new Intent(Intent.ACTION_VIEW, uri, ApplicationProvider.getApplicationContext(), GamePlay.class);
        prefs.edit().remove(NIGHT_MODE_KEY).apply();
        checkGameStarts(intent);
        prefs.edit().putString(NIGHT_MODE_KEY, "on").apply();
        checkGameStarts(intent);
        prefs.edit().remove(NIGHT_MODE_KEY).apply();
    }

    private void checkGameStarts(Intent intent) {
        try(ActivityScenario<GamePlay> ignored = ActivityScenario.launch(intent)) {
            onView(withText(R.string.starting)).check(doesNotExist());
        }
    }
}
