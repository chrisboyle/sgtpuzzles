package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import android.view.KeyEvent.KEYCODE_1
import android.view.KeyEvent.KEYCODE_2
import android.view.KeyEvent.KEYCODE_3
import android.view.KeyEvent.KEYCODE_D
import android.view.KeyEvent.KEYCODE_DPAD_CENTER
import android.view.KeyEvent.KEYCODE_DPAD_DOWN
import android.view.KeyEvent.KEYCODE_DPAD_LEFT
import android.view.KeyEvent.KEYCODE_DPAD_RIGHT
import android.view.KeyEvent.KEYCODE_DPAD_UP
import android.view.KeyEvent.KEYCODE_G
import android.view.KeyEvent.KEYCODE_L
import android.view.KeyEvent.KEYCODE_SPACE
import android.view.KeyEvent.KEYCODE_V
import android.view.KeyEvent.KEYCODE_Z
import android.view.View
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.ViewAction
import androidx.test.espresso.action.CoordinatesProvider
import androidx.test.espresso.action.GeneralClickAction
import androidx.test.espresso.action.GeneralSwipeAction
import androidx.test.espresso.action.Press
import androidx.test.espresso.action.Swipe
import androidx.test.espresso.action.Tap
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.ViewAssertions
import androidx.test.espresso.assertion.ViewAssertions.doesNotExist
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import name.boyle.chris.sgtpuzzles.backend.BLACKBOX
import name.boyle.chris.sgtpuzzles.backend.BRIDGES
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.CUBE
import name.boyle.chris.sgtpuzzles.backend.DOMINOSA
import name.boyle.chris.sgtpuzzles.backend.FIFTEEN
import name.boyle.chris.sgtpuzzles.backend.FILLING
import name.boyle.chris.sgtpuzzles.backend.FLIP
import name.boyle.chris.sgtpuzzles.backend.FLOOD
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
import name.boyle.chris.sgtpuzzles.backend.UNRULY
import name.boyle.chris.sgtpuzzles.backend.UNTANGLE
import org.hamcrest.Matchers.containsString
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.Parameterized
import kotlin.math.min

@RunWith(Parameterized::class)
class GamePlayParameterizedTest(
    private val backend: BackendName,
    private val gameID: String,
    private vararg val viewActions: ViewAction
) {

    @Test
    fun testGameCompletion() {
        assertFalse("Missing test for $backend", gameID.isEmpty())
        val uri = Uri.parse("sgtpuzzles:$backend:$gameID")
        val intent = Intent(
            Intent.ACTION_VIEW,
            uri,
            ApplicationProvider.getApplicationContext(),
            GamePlay::class.java
        )
        ActivityScenario.launch<GamePlay>(intent).use {
            if (viewActions[0] is GeneralSwipeAction) {
                SystemClock.sleep(500) // Untangle seems to need this (multiple layout passes?)
            }
            onView(withText(R.string.starting)).check(doesNotExist())
            assertCompleted(false)
            onView(withId(R.id.game_view)).perform(*viewActions)
            assertCompleted(true)
        }
    }

    private fun assertCompleted(isCompleted: Boolean) {
        // Note that flood and mines copy the status bar so there is score info before/after "COMPLETED".
        onView(withText(containsString(ApplicationProvider.getApplicationContext<Context>().getString(R.string.COMPLETED)))).apply {
            if (isCompleted) {
                inRoot(isDialog()).check(ViewAssertions.matches(isDisplayed()))
            } else {
                check(doesNotExist())
            }
        }
    }

    companion object {
        data class Example(
            val backend: BackendName,
            val gameID: String,
            val actions: List<ViewAction>
        ) {
            constructor(
                backend: BackendName,
                gameID: String,
                vararg keystrokes: Int
            ) : this(backend, gameID, keystrokes.map { ViewActions.pressKey(it) })

            constructor(backend: BackendName, gameID: String, vararg actions: ViewAction) : this(
                backend,
                gameID,
                actions.toList()
            )
        }

        private val examples = setOf(
            Example(
                NET, "1x2:42", GeneralClickAction(
                    Tap.SINGLE,
                    squareProportions(0.0, -0.25), Press.FINGER, 0, 0
                )
            ),
            Example(BLACKBOX, "w3h3m1M1:38727296",
                KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER),
            Example(BRIDGES, "3x3m2:3a2c1b",
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT),
            Example(CUBE, "c4x4:0C56,0", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_LEFT),
            Example(DOMINOSA, "1:011100",
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER),
            Example(FIFTEEN, "2x2:1,2,0,3", KEYCODE_DPAD_LEFT),
            Example(FILLING, "2x1:02", KEYCODE_DPAD_UP, KEYCODE_2),
            Example(FLIP, "2x2:edb7,d", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(FLOOD, "2x2:1212,6", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(GALAXIES, "3x3:co",
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(GUESS, "c2p2g2Bm:c2ab",
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(INERTIA, "3x3:wbbSbbgms", KEYCODE_DPAD_DOWN),
            Example(KEEN, "3:_baa_3a,m6s1m3a3",
                KEYCODE_DPAD_UP, KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3, KEYCODE_DPAD_RIGHT,
                KEYCODE_2, KEYCODE_DPAD_DOWN, KEYCODE_1, KEYCODE_DPAD_LEFT, KEYCODE_2,
                KEYCODE_DPAD_LEFT, KEYCODE_3, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
                KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3),
            Example(LIGHTUP, "2x2:a0b", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER),
            Example(LOOPY, "3x3t0:02a2a1c",
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER),
            Example(MAGNETS, "3x2:111,21,111,12,TTTBBB",
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(MAP, "3x2n5:afa,3120a", KEYCODE_DPAD_UP,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(MINES, "4x3:2,0,m5d9",
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER),
            Example(MOSAIC, "3x3:a4a4a6c", KEYCODE_DPAD_UP,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(NET, "1x2:12", KEYCODE_DPAD_UP, KEYCODE_D),
            Example(NETSLIDE, "2x2:ch116", KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER),
            Example(PALISADE, "2x3n3:d33", KEYCODE_L, KEYCODE_L,
                KEYCODE_DPAD_DOWN, KEYCODE_L, KEYCODE_L,
                KEYCODE_DPAD_DOWN, KEYCODE_L, KEYCODE_L),
            Example(PATTERN, "1x2:2/1/1", KEYCODE_DPAD_UP,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER),
            Example(PEARL, "5x5:dBaWaBgWaBeB", KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER),
            Example(PEGS, "4x4:PHPPHPPOPOPOPOPO", KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_DOWN),
            Example(RANGE, "3x2:b2_4b",
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(RECT, "2x2:2a2a", KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(SAMEGAME, "2x2c3s2:1,1,3,3", KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER),
            Example(SIGNPOST, "3x2:1ccfcg6a",
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER),
            Example(SINGLES, "2x2:1121", KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER),
            Example(SIXTEEN, "2x2:1,4,3,2", KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER),
            Example(SLANT, "2x2:1c1d", KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_LEFT, KEYCODE_DPAD_CENTER),
            Example(SOLO, "2j:1c,b__", KEYCODE_DPAD_RIGHT, KEYCODE_2,
                KEYCODE_DPAD_DOWN, KEYCODE_1, KEYCODE_DPAD_LEFT, KEYCODE_2),
            Example(TENTS, "4x4:baj_,1,1,0,1,1,0,2,0", KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_LEFT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER),
            Example(TOWERS, "3:1/2/3/2/2/1/1/2/2/3/2/1",
                KEYCODE_DPAD_UP, KEYCODE_3, KEYCODE_DPAD_RIGHT, KEYCODE_2, KEYCODE_DPAD_RIGHT,
                KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_LEFT, KEYCODE_3,
                KEYCODE_DPAD_LEFT, KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
                KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3),
            Example(TRACKS, "4x4:Cm9a,3,2,S3,4,S3,4,3,2", KEYCODE_DPAD_UP, KEYCODE_DPAD_UP,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER),
            Example(TWIDDLE, "2x2n2:4,3,2,1",
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_CENTER),
            Example(UNDEAD, "3x3:1,2,2,dLaRLL,2,0,2,2,2,0,1,0,0,2,1,2",
                KEYCODE_DPAD_UP, KEYCODE_DPAD_DOWN, KEYCODE_Z, KEYCODE_DPAD_UP, KEYCODE_Z,
                KEYCODE_DPAD_RIGHT, KEYCODE_G, KEYCODE_DPAD_RIGHT, KEYCODE_V, KEYCODE_DPAD_DOWN,
                KEYCODE_V),
            Example(UNEQUAL, "3:0D,0,0,0,0,0,0R,0,0U,",
                KEYCODE_DPAD_UP, KEYCODE_3, KEYCODE_DPAD_RIGHT, KEYCODE_2, KEYCODE_DPAD_RIGHT,
                KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_LEFT, KEYCODE_3,
                KEYCODE_DPAD_LEFT, KEYCODE_1, KEYCODE_DPAD_DOWN, KEYCODE_2, KEYCODE_DPAD_RIGHT,
                KEYCODE_1, KEYCODE_DPAD_RIGHT, KEYCODE_3),
            Example(UNRULY, "6x6:BCCAHgBCga", KEYCODE_DPAD_UP,
                KEYCODE_SPACE, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN,
                KEYCODE_SPACE, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT,
                KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_SPACE, KEYCODE_DPAD_UP,
                KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_DPAD_RIGHT, KEYCODE_SPACE,
                KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
                KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP,
                KEYCODE_DPAD_CENTER, KEYCODE_DPAD_UP, KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_SPACE,
                KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN,
                KEYCODE_DPAD_DOWN, KEYCODE_SPACE, KEYCODE_DPAD_DOWN, KEYCODE_DPAD_DOWN,
                KEYCODE_SPACE, KEYCODE_DPAD_RIGHT, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_UP, KEYCODE_SPACE, KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER,
                KEYCODE_DPAD_UP, KEYCODE_DPAD_CENTER),
            Example(
                UNTANGLE, "4:0-1,0-2,0-3,1-2,1-3,2-3", GeneralSwipeAction(
                    Swipe.FAST,
                    squareProportions(0.0, -0.42), squareProportions(0.0, 0.25), Press.FINGER
                )
            )
        )

        @Parameterized.Parameters(name = "{0}:{1}")
        @JvmStatic
        fun data(): Iterable<Array<Any?>> =
            (examples + BackendName.all.minus(examples.map { it.backend }.toSet()).map {
                // testGameCompletion will fail appropriately
                Example(it, "", listOf())
            }).map { arrayOf(it.backend, it.gameID, it.actions.toTypedArray()) }

        @Suppress("SameParameterValue")
        private fun squareProportions(xProp: Double, yProp: Double): CoordinatesProvider {
            return CoordinatesProvider { view: View ->
                val screenPos = IntArray(2)
                view.getLocationOnScreen(screenPos)
                val squareSz = min(view.width, view.height)
                val screenX = (screenPos[0] + 0.5 * view.width + xProp * squareSz).toFloat()
                val screenY = (screenPos[1] + 0.5 * view.height + yProp * squareSz).toFloat()
                floatArrayOf(screenX, screenY)
            }
        }
    }
}