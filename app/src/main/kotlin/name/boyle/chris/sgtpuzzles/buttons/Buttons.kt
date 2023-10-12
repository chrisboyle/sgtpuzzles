package name.boyle.chris.sgtpuzzles.buttons

import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import android.util.AttributeSet
import android.util.Log
import androidx.annotation.DrawableRes
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.core.animateIntOffsetAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.absoluteOffset
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.ripple.LocalRippleTheme
import androidx.compose.material.ripple.RippleAlpha
import androidx.compose.material.ripple.RippleTheme
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.input.InputMode.Companion.Touch
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.AbstractComposeView
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.max
import androidx.compose.ui.unit.sp
import androidx.compose.ui.unit.times
import androidx.preference.PreferenceManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import name.boyle.chris.sgtpuzzles.GameView
import name.boyle.chris.sgtpuzzles.GameView.Companion.CURSOR_DOWN
import name.boyle.chris.sgtpuzzles.GameView.Companion.CURSOR_LEFT
import name.boyle.chris.sgtpuzzles.GameView.Companion.CURSOR_RIGHT
import name.boyle.chris.sgtpuzzles.GameView.Companion.CURSOR_UP
import name.boyle.chris.sgtpuzzles.GameView.Companion.MOD_NUM_KEYPAD
import name.boyle.chris.sgtpuzzles.R
import name.boyle.chris.sgtpuzzles.backend.BLACKBOX
import name.boyle.chris.sgtpuzzles.backend.BRIDGES
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.FILLING
import name.boyle.chris.sgtpuzzles.backend.GALAXIES
import name.boyle.chris.sgtpuzzles.backend.GUESS
import name.boyle.chris.sgtpuzzles.backend.INERTIA
import name.boyle.chris.sgtpuzzles.backend.KEEN
import name.boyle.chris.sgtpuzzles.backend.LIGHTUP
import name.boyle.chris.sgtpuzzles.backend.LOOPY
import name.boyle.chris.sgtpuzzles.backend.MINES
import name.boyle.chris.sgtpuzzles.backend.MOSAIC
import name.boyle.chris.sgtpuzzles.backend.NET
import name.boyle.chris.sgtpuzzles.backend.NETSLIDE
import name.boyle.chris.sgtpuzzles.backend.PATTERN
import name.boyle.chris.sgtpuzzles.backend.PEARL
import name.boyle.chris.sgtpuzzles.backend.RANGE
import name.boyle.chris.sgtpuzzles.backend.RECT
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME
import name.boyle.chris.sgtpuzzles.backend.SINGLES
import name.boyle.chris.sgtpuzzles.backend.SIXTEEN
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.TENTS
import name.boyle.chris.sgtpuzzles.backend.TOWERS
import name.boyle.chris.sgtpuzzles.backend.TWIDDLE
import name.boyle.chris.sgtpuzzles.backend.UNDEAD
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL
import name.boyle.chris.sgtpuzzles.backend.UNRULY
import name.boyle.chris.sgtpuzzles.backend.UNTANGLE
import name.boyle.chris.sgtpuzzles.backend.UsedByJNI
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode.ARROWS_DIAGONALS
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode.ARROWS_LEFT_CLICK
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode.ARROWS_LEFT_RIGHT_CLICK
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode.ARROWS_ONLY
import name.boyle.chris.sgtpuzzles.buttons.ArrowMode.NO_ARROWS
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.KEYBOARD_BORDERS_KEY
import kotlin.math.ceil
import kotlin.math.floor

const val SEEN_SWAP_L_R_TOAST = "seenSwapLRToast"
const val SWAP_L_R_KEY = '*'

@UsedByJNI
enum class ArrowMode {
    NO_ARROWS,  // untangle
    ARROWS_ONLY,  // cube
    ARROWS_LEFT_CLICK,  // flip, filling, guess, keen, solo, towers, unequal
    ARROWS_LEFT_RIGHT_CLICK,  // unless phone has a d-pad (most games)
    ARROWS_DIAGONALS;   // Inertia

    fun hasArrows(): Boolean = this != NO_ARROWS
}

class ButtonsView(context: Context, attrs: AttributeSet? = null) :
    AbstractComposeView(context, attrs) {
    private val prefs: SharedPreferences = PreferenceManager.getDefaultSharedPreferences(context)

    val keys = mutableStateOf("")
    val arrowMode = mutableStateOf(NO_ARROWS)
    val swapLR = mutableStateOf(false)
    val hidePrimary = mutableStateOf(false)
    val disableCharacterIcons = mutableStateOf("")
    val backend: MutableState<BackendName> = mutableStateOf(UNTANGLE)
    val undoEnabled = mutableStateOf(false)
    val redoEnabled = mutableStateOf(false)
    val borders = mutableStateOf(prefs.getBoolean(KEYBOARD_BORDERS_KEY, false))
    val onKeyListener: MutableState<((Int, Boolean) -> Unit)> = mutableStateOf({ _, _ -> })
    val onSwapLRListener: MutableState<((Boolean) -> Unit)> = mutableStateOf({})

    @Composable
    override fun Content() {
        DisposableEffect(Unit) {
            val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == KEYBOARD_BORDERS_KEY) {
                    borders.value = prefs.getBoolean(KEYBOARD_BORDERS_KEY, false)
                }
            }
            prefs.registerOnSharedPreferenceChangeListener(listener)
            onDispose {
                prefs.unregisterOnSharedPreferenceChangeListener(listener)
            }
        }

        Buttons(
            keys,
            backend,
            arrowMode,
            swapLR,
            hidePrimary,
            disableCharacterIcons,
            undoEnabled,
            redoEnabled,
            LocalContext.current.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE,
            borders,
            onKeyListener,
            onSwapLRListener)
    }
}

@Composable
private fun Buttons(
    keys: MutableState<String>,
    backend: MutableState<BackendName>,
    arrowMode: MutableState<ArrowMode>,
    swapLR: MutableState<Boolean>,
    hidePrimary: MutableState<Boolean> = rememberSaveable { mutableStateOf(false) },
    disableCharacterIcons: MutableState<String> = rememberSaveable { mutableStateOf("") },
    undoEnabled: MutableState<Boolean> = rememberSaveable { mutableStateOf(true) },
    redoEnabled: MutableState<Boolean> = rememberSaveable { mutableStateOf(true) },
    isLandscape: Boolean,
    borders: MutableState<Boolean>,
    onKey: MutableState<(Int, Boolean) -> Unit>,
    onSwapLR: MutableState<(Boolean) -> Unit>
) {
    BoxWithConstraints {
        val keyList = keys.value.toList()
        val maxDp = if (isLandscape) maxHeight else maxWidth
        val keySize = dimensionResource(id = R.dimen.keySize)
        val hasArrows = arrowMode.value.hasArrows()
        val isDiagonal = arrowMode.value == ARROWS_DIAGONALS

        val arrowRows = if (hasArrows) if (isDiagonal) 3 else 2 else 0
        val arrowCols = if (hasArrows) 3 else 0
        val arrowMajors = if (isLandscape) arrowCols else arrowRows
        val arrowMinors = if (isLandscape) arrowRows else arrowCols

        // How many keys can we fit on a row?
        val maxDpMinusArrows: Dp =
            if (hasArrows) maxDp - arrowMinors * keySize else maxDp
        val minorsPerMajor =
            fudgeAvoidLonelyDigit(maxDpMinusArrows, keySize, keyList)
        val minorsPerMajorWithoutArrows = fudgeAvoidLonelyDigit(maxDp, keySize, keyList)

        // How many rows do we need?
        val needed = majorsNeeded(keyList, minorsPerMajor)
        val overlappingMajors = if (needed > arrowMajors) {
            arrowMajors + ceil((keyList.size.toDouble() - arrowMajors * minorsPerMajor) / minorsPerMajorWithoutArrows).toInt()
        } else {
            needed
        }
        val majors: Int
        val majorWhereArrowsStart: Int
        if (overlappingMajors < needed) {  // i.e. extending over arrows saves us anything
            majors = overlappingMajors
            majorWhereArrowsStart = (majors - arrowMajors).coerceAtLeast(0)
        } else {
            majors = needed
            majorWhereArrowsStart = -1
        }

        val widthOrHeight = keySize * if (majors > 1) minorsPerMajor else keyList.size
        val heightOrWidth = (majors * keySize)
        val charsWidth = if (majors == 0) 0.dp else if (isLandscape) heightOrWidth else widthOrHeight
        val charsHeight = if (majors == 0) 0.dp else if (isLandscape) widthOrHeight else heightOrWidth

        val spaceAfterKeys = keySize / 12f
        val largerWidth = max(charsWidth, arrowCols * keySize)
        val largerHeight = max(charsHeight, arrowRows * keySize)
        val totalWidth = if (isLandscape) largerWidth + spaceAfterKeys else maxWidth
        val totalHeight = if (isLandscape) maxHeight else largerHeight + spaceAfterKeys

        Box(modifier = Modifier
            .background(colorResource(id = R.color.keyboard_background))
            .width(totalWidth)
            .height(totalHeight)) {

            if (majors > 0) {
                val minorStartDp: Dp =
                    ((maxDpMinusArrows - (minorsPerMajor * keySize)) / 2)
                val majorStartDp: Dp =
                    if (majors < 3 && hasArrows) (arrowMajors - majors) * keySize else 0.dp
                Characters(
                    backend,
                    keyList,
                    isLandscape,
                    undoEnabled,
                    redoEnabled,
                    keySize,
                    minorsPerMajor,
                    minorsPerMajorWithoutArrows,
                    majorWhereArrowsStart,
                    majors,
                    minorStartDp,
                    majorStartDp,
                    disableCharacterIcons,
                    swapLR,
                    borders,
                    onKey,
                    onSwapLR
                )
            }

            if (hasArrows) {
                val arrowsRightEdge: Dp = if (isLandscape) largerWidth else maxDp
                val arrowsBottomEdge: Dp = if (isLandscape) maxDp else largerHeight
                Arrows(
                    backend,
                    keySize,
                    arrowMode,
                    swapLR,
                    hidePrimary,
                    arrowRows,
                    arrowsRightEdge,
                    arrowsBottomEdge,
                    borders,
                    onKey
                )
            }

            if (majors > 0 || hasArrows) {
                if (isLandscape)
                    Spacer(
                        Modifier
                            .offset(largerWidth, 0.dp)
                            .width(spaceAfterKeys)
                    )
                else
                    Spacer(
                        Modifier
                            .offset(0.dp, largerHeight)
                            .height(spaceAfterKeys)
                    )
            }
        }
    }
}

private fun majorsNeeded(characters: List<Char>, minorsPerMajor: Int): Int =
    ceil(characters.size.toDouble() / minorsPerMajor).toInt()

private fun fudgeAvoidLonelyDigit(
    maxDpMinusArrows: Dp,
    keyPlusPad: Dp,
    characters: List<Char>
): Int {
    val highestDigit = highestDigit(characters)
    var minorsPerMajor = floor(maxDpMinusArrows / keyPlusPad).toInt()
    if (highestDigit > 0 && minorsPerMajor == highestDigit - 1 &&
        majorsNeeded(characters, minorsPerMajor - 1) ==
        majorsNeeded(characters, minorsPerMajor)
    ) {
        minorsPerMajor--
    }
    return minorsPerMajor
}

val INITIAL_DIGITS = Regex("^[0-9][1-9]+")

private fun highestDigit(characters: List<Char>): Int = INITIAL_DIGITS.find(characters.joinToString(""))?.value?.length ?: 0

@Composable
private fun Characters(
    backend: MutableState<BackendName>,
    characters: List<Char>,
    columnMajor: Boolean,
    undoEnabled: MutableState<Boolean>,
    redoEnabled: MutableState<Boolean>,
    keySize: Dp,
    minorsPerMajor: Int,
    minorsPerMajorWithoutArrows: Int,
    majorWhereArrowsStart: Int,
    majors: Int,
    minorStartDp: Dp,
    majorStartDp: Dp,
    disableCharacterIcons: MutableState<String>,
    swapLR: MutableState<Boolean>,
    borders: MutableState<Boolean>,
    onKey: MutableState<(Int, Boolean) -> Unit>,
    onSwapLR: MutableState<(Boolean) -> Unit>
) {
    val length = characters.size
    var minorDp = minorStartDp
    var majorDp = majorStartDp
    var minor = 0
    var major = -1
    // avoid having a last major with a single character
    for (i in 0 until length) {
        val charsThisMajor =
            if (major < majorWhereArrowsStart) minorsPerMajorWithoutArrows else minorsPerMajor
        val c = characters[i]
        val preventingSingleton = major == majors - 2 && i == length - 2
        if (i == 0 || minor >= charsThisMajor || preventingSingleton) {
            major++
            minor = 0
            val willPreventSingleton = minorsPerMajor > 0 && (length - i) % minorsPerMajor == 1
            val charsNextMajor =
                if (major == majors - 1) length - i else if (major == majors - 2 && willPreventSingleton) minorsPerMajor - 1 else minorsPerMajor
            minorDp =
                minorStartDp + ((minorsPerMajor.toDouble() - charsNextMajor) / 2) * keySize
            if (i > 0) {
                majorDp += keySize
            }
        }
        val x = if (columnMajor) majorDp else minorDp
        val y = if (columnMajor) minorDp else majorDp
        Character(
            backend,
            undoEnabled,
            redoEnabled,
            c,
            DpOffset(x, y).toIntOffset(),
            disableCharacterIcons,
            swapLR,
            borders,
            onKey,
            onSwapLR
        )
        minor++
        minorDp += keySize
    }
}

@Composable
private fun Character(
    backend: MutableState<BackendName>,
    undoEnabled: MutableState<Boolean>,
    redoEnabled: MutableState<Boolean>,
    c: Char,
    offset: IntOffset,
    disableCharacterIcons: MutableState<String>,
    swapLR: MutableState<Boolean>,
    borders: MutableState<Boolean>,
    onKey: MutableState<(Int, Boolean) -> Unit>,
    onSwapLR: MutableState<(Boolean) -> Unit>
) {
    when (c.code) {
        GameView.UI_UNDO, 'U'.code -> IconKeyButton(
            GameView.UI_UNDO, R.drawable.ic_action_undo, "Undo", onKey, offset,
            repeatable = true, enabled = undoEnabled.value, borders = borders
        )

        GameView.UI_REDO, 'R'.code -> IconKeyButton(
            GameView.UI_REDO, R.drawable.ic_action_redo, "Redo", onKey, offset,
            repeatable = true, enabled = redoEnabled.value, borders = borders
        )

        '\b'.code -> IconKeyButton(
            c.code, R.drawable.sym_key_backspace, "Backspace",
            onKey, offset, repeatable = true, borders = borders
        )

        SWAP_L_R_KEY.code -> IconKeyButton(
            c.code, R.drawable.ic_action_swap_l_r, "Swap press and long press",
            remember { mutableStateOf({ _, _ ->
                swapLR.value = !swapLR.value
                onSwapLR.value(swapLR.value)
            })},
            offset,
            on = swapLR.value,
            borders = borders
        )

        else -> {
            CharacterButton(backend, c, offset, disableCharacterIcons, borders, onKey)
        }
    }
}

@Composable
fun DpOffset.toIntOffset(): IntOffset = with(LocalDensity.current) {
    IntOffset(x.roundToPx(), y.roundToPx())
}

@Composable
private fun Arrows(
    backend: MutableState<BackendName>,
    keySize: Dp,
    arrowMode: MutableState<ArrowMode>,
    swapLR: MutableState<Boolean>,
    hidePrimary: MutableState<Boolean>,
    arrowRows: Int,
    arrowsRightEdge: Dp,
    arrowsBottomEdge: Dp,
    borders: MutableState<Boolean>,
    onKey: MutableState<((Int, Boolean) -> Unit)>
) {
    val arrows: IntArray = when (arrowMode.value) {
        NO_ARROWS -> intArrayOf()

        ARROWS_DIAGONALS -> intArrayOf(
            CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, '\n'.code,
            MOD_NUM_KEYPAD or '7'.code, MOD_NUM_KEYPAD or '1'.code,
            MOD_NUM_KEYPAD or '9'.code, MOD_NUM_KEYPAD or '3'.code
        )

        ARROWS_ONLY -> intArrayOf(
            CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT
        )

        ARROWS_LEFT_CLICK -> intArrayOf(
            CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, '\n'.code
        )

        ARROWS_LEFT_RIGHT_CLICK -> intArrayOf(
            CURSOR_UP, CURSOR_DOWN, CURSOR_LEFT, CURSOR_RIGHT, '\n'.code, ' '.code
        )
    }

    val isDiagonal = arrowMode.value == ARROWS_DIAGONALS
    val leftRightRow = if (isDiagonal) 2 else 1
    val leftClickOffset: IntOffset
    val rightClickOffset: IntOffset
    with(LocalDensity.current) {
        leftClickOffset = IntOffset(
            (arrowsRightEdge - (if (isDiagonal) 2 else 3) * keySize).roundToPx(),
            (arrowsBottomEdge - 2 * keySize).roundToPx()
        )
        rightClickOffset = IntOffset(
            (arrowsRightEdge - keySize).roundToPx(),
            (arrowsBottomEdge - arrowRows * keySize).roundToPx()
        )
    }
    val slowerSpring =
        spring(visibilityThreshold = IntOffset.VisibilityThreshold, stiffness = Spring.StiffnessMediumLow)
    val primaryOffset by animateIntOffsetAsState(
        targetValue = if (swapLR.value) rightClickOffset else leftClickOffset,
        animationSpec = slowerSpring,
        label = "primaryOffset"
    )
    val secondaryOffset by animateIntOffsetAsState(
        targetValue = if (swapLR.value) leftClickOffset else rightClickOffset,
        animationSpec = slowerSpring,
        label = "secondaryOffset"
    )
    for (arrow in arrows) {
        when (arrow) {
            '\n'.code -> AnimatedVisibility(
                !hidePrimary.value,
                enter = fadeIn(),
                exit = fadeOut(),
                // The offset must be here not on the key: https://stackoverflow.com/a/73975722/6540
                modifier = Modifier.absoluteOffset { primaryOffset }) {
                IconKeyButton(
                    arrow, mouseIcon(backend, false), "Enter", onKey,
                    IntOffset(0, 0),
                    repeatable = backend.value in setOf(INERTIA, NETSLIDE, SAMEGAME, SIXTEEN, TWIDDLE), borders = borders
                )
            }
            ' '.code -> IconKeyButton(
                arrow, mouseIcon(backend, true),
                "Space",
                onKey,
                secondaryOffset,
                repeatable = backend.value in setOf(NETSLIDE, SIXTEEN, TWIDDLE), borders = borders
            )
            CURSOR_UP -> IconKeyButton(
                arrow, R.drawable.sym_key_north, "Up", onKey,
                DpOffset(arrowsRightEdge - 2 * keySize, arrowsBottomEdge - arrowRows * keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            CURSOR_DOWN -> IconKeyButton(
                arrow, R.drawable.sym_key_south, "Down", onKey,
                DpOffset(arrowsRightEdge - 2 * keySize, arrowsBottomEdge - keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            CURSOR_LEFT -> IconKeyButton(
                arrow, R.drawable.sym_key_west, "Left", onKey,
                DpOffset(arrowsRightEdge - 3 * keySize, arrowsBottomEdge - leftRightRow * keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            CURSOR_RIGHT -> IconKeyButton(
                arrow, R.drawable.sym_key_east, "Right", onKey,
                DpOffset(arrowsRightEdge - keySize, arrowsBottomEdge - leftRightRow * keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            MOD_NUM_KEYPAD or '7'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_north_west, "Up and left", onKey,
                DpOffset(arrowsRightEdge - 3 * keySize, arrowsBottomEdge - 3 * keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            MOD_NUM_KEYPAD or '1'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_south_west, "Down and left", onKey,
                DpOffset(arrowsRightEdge - 3 * keySize, arrowsBottomEdge - keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            MOD_NUM_KEYPAD or '9'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_north_east, "Up and right", onKey,
                DpOffset(arrowsRightEdge - keySize, arrowsBottomEdge - 3 * keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            MOD_NUM_KEYPAD or '3'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_south_east, "Down and right", onKey,
                DpOffset(arrowsRightEdge - keySize, arrowsBottomEdge - keySize).toIntOffset(),
                repeatable = true, borders = borders
            )
            else -> Log.wtf("Buttons", "unknown key in keyboard: $arrow")
        }
    }
}

private val sharedMouseIcons = mapOf(
    BLACKBOX to Pair(null, R.drawable.square_empty),
    BRIDGES to Pair(R.drawable.line, null),
    FILLING to Pair(R.drawable.square_filled, null),
    GALAXIES to Pair(R.drawable.line, null),
    GUESS to Pair(null, R.drawable.lock),
    INERTIA to Pair(R.drawable.ic_action_solve, null),
    KEEN to Pair(R.drawable.square_corner, null),
    LIGHTUP to Pair(R.drawable.square_circle, R.drawable.square_dot),
    LOOPY to Pair(R.drawable.line, R.drawable.no_line),
    MINES to Pair(R.drawable.square_empty, null),
    MOSAIC to Pair(R.drawable.square_empty, R.drawable.square_filled),
    PATTERN to Pair(R.drawable.square_empty, R.drawable.square_filled),
    PEARL to Pair(R.drawable.line, R.drawable.no_line),
    RANGE to Pair(R.drawable.square_filled, R.drawable.square_dot),
    RECT to Pair(R.drawable.square_empty, R.drawable.no_line),
    SAMEGAME to Pair(R.drawable.square_dot, R.drawable.square_empty),
    SINGLES to Pair(R.drawable.square_filled, R.drawable.square_circle),
    SOLO to Pair(R.drawable.square_corner, null),
    TENTS to Pair(null, R.drawable.square_filled),
    TOWERS to Pair(R.drawable.square_corner, null),
    TWIDDLE to Pair(R.drawable.rotate_left_90, R.drawable.rotate_right_90),
    UNDEAD to Pair(R.drawable.square_corner, null),
    UNEQUAL to Pair(R.drawable.square_corner, null),
    UNRULY to Pair(R.drawable.square_empty, R.drawable.square_filled),
)

private val sharedCharIcons = mapOf(
    BRIDGES to mapOf('l' to R.drawable.lock),
    FILLING to mapOf('0' to R.drawable.square_empty),
    KEEN to mapOf('m' to R.drawable.square_corner_123),
    NET to mapOf(
        'a' to R.drawable.rotate_left_90,
        's' to R.drawable.lock,
        'd' to R.drawable.rotate_right_90,
        'f' to R.drawable.rotate_left_180
    ),
    SOLO to mapOf('m' to R.drawable.square_corner_123),
    TOWERS to mapOf('m' to R.drawable.square_corner_123),
    UNEQUAL to mapOf('m' to R.drawable.square_corner_123),
)

@Composable
private fun mouseIcon(backend: MutableState<BackendName>, isRight: Boolean): Int =
    sharedMouseIcons[backend.value]?.run { if (isRight) second else first }
        ?: backend.value.keyIcon(if (isRight) "sym_key_mouse_right" else "sym_key_mouse_left")
        ?: if (isRight) R.drawable.sym_key_mouse_right else R.drawable.sym_key_mouse_left

@Composable
private fun CharacterButton(
    backend: MutableState<BackendName>,
    c: Char,
    offset: IntOffset,
    disableCharacterIcons: MutableState<String>,
    borders: MutableState<Boolean>,
    onKey: MutableState<(Int, Boolean) -> Unit>
) {
    val lowerChar = c.lowercaseChar()
    val shouldTryIcon =
        (Character.isUpperCase(c) || Character.isDigit(c)) && !disableCharacterIcons.value.contains(c.toString())
    val icon: Int? = if (shouldTryIcon) {
        sharedCharIcons[backend.value]?.get(lowerChar) ?: backend.value.keyIcon(lowerChar.toString())
    } else {
        null
    }
    if (icon != null) {
        IconKeyButton(
            c.code, icon, contentDescription = c.toString(),
            onKey, offset, borders = borders
        )
    } else {
        // Not proud of this, but: I'm using uppercase letters to mean it's a command as
        // opposed to data entry (Mark all squares versus enter 'm'). But I still want the
        // keys for data entry to be uppercase in unequal because that matches the board.
        val label = (if (backend.value == UNEQUAL) c.uppercaseChar() else c).toString()
        TextKeyButton(
            c.code, contentDescription = c.toString(),
            onKey, offset, label = label, borders = borders)
    }
}

@Composable
private fun IconKeyButton(
    c: Int,
    @DrawableRes icon: Int,
    contentDescription: String,
    onKey: MutableState<((Int, Boolean) -> Unit)>,
    offset: IntOffset,
    modifier: Modifier = Modifier,
    repeatable: Boolean = false,
    enabled: Boolean = true,
    on: Boolean = false,
    borders: MutableState<Boolean>,
) {
    KeyButton(
        c,
        contentDescription,
        onKey,
        offset,
        modifier,
        repeatable,
        enabled = enabled,
        on = on,
        borders = borders
    ) {
        ResIcon(icon, enabled)
    }
}

@Composable
private fun TextKeyButton(
    c: Int,
    contentDescription: String,
    onKey: MutableState<((Int, Boolean) -> Unit)>,
    offset: IntOffset,
    modifier: Modifier = Modifier,
    repeatable: Boolean = false,
    enabled: Boolean = true,
    label: String = c.toChar().toString(),
    borders: MutableState<Boolean>,
) {
    KeyButton(
        c,
        contentDescription,
        onKey,
        offset,
        modifier,
        repeatable,
        enabled = enabled,
        borders = borders
    ) {
        Text(label, fontSize = 24.sp, fontWeight = FontWeight.Normal)
    }
}

fun Modifier.autoRepeat(
    interactionSource: InteractionSource,
    repeatable: Boolean,
    enabled: Boolean,
    initialDelayMillis: Long = 400,
    repeatDelayMillis: Long = 50,
    onKey: (Boolean) -> Unit
) = composed {
    val currentOnKey by rememberUpdatedState(onKey)
    val isEnabled by rememberUpdatedState(enabled)
    var isDisposed = false
    DisposableEffect(Unit) {
        onDispose { isDisposed = true }
    }

    pointerInput(interactionSource, enabled) {
        awaitEachGesture {
            val down = awaitFirstDown(requireUnconsumed = false)
            val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
            val heldButtonJob = scope.launch {
                var isRepeat = false
                while (!isDisposed && isEnabled && down.pressed && (repeatable || !isRepeat)) {
                    currentOnKey(isRepeat)
                    delay(if (isRepeat) repeatDelayMillis else initialDelayMillis)
                    isRepeat = true
                }
            }
            waitForUpOrCancellation()
            heldButtonJob.cancel()
        }
    }
}

private object BrighterRippleTheme : RippleTheme {
    @Composable
    override fun defaultColor(): Color = Color.White

    @Composable
    override fun rippleAlpha(): RippleAlpha = RippleTheme.defaultRippleAlpha(
        colorResource(id = R.color.keyboard_foreground),
        lightTheme = !isSystemInDarkTheme()
    )
}

@Composable
private fun KeyButton(
    c: Int,
    contentDescription: String,
    onKey: MutableState<((Int, Boolean) -> Unit)>,
    offset: IntOffset,
    modifier: Modifier = Modifier,
    repeatable: Boolean = false,
    enabled: Boolean = true,
    on: Boolean = false,
    borders: MutableState<Boolean>,
    interactionSource: MutableInteractionSource = remember { MutableInteractionSource() },
    content: @Composable (RowScope.() -> Unit),
) {
    val background =
        if (on) R.color.key_background_on
        else if (borders.value) R.color.key_background_border
        else R.color.keyboard_background
    val buttonColors = ButtonDefaults.buttonColors(
        colorResource(background),
        colorResource(R.color.keyboard_foreground),
        colorResource(background),
        colorResource(R.color.keyboard_foreground_disabled)
    )
    CompositionLocalProvider(LocalRippleTheme provides BrighterRippleTheme) {
        val inputModeManager = LocalInputModeManager.current
        Button(
            onClick = {
                // Touch inputs handled by modifier.autoRepeat; don't invoke again on release
                if (inputModeManager.inputMode != Touch) onKey.value(c, false)
            },
            enabled = enabled,
            colors = buttonColors,
            shape = RectangleShape,
            contentPadding = PaddingValues(0.dp, 0.dp),
            modifier = modifier
                .absoluteOffset { offset }
                .width(dimensionResource(id = R.dimen.keySize))
                .height(dimensionResource(id = R.dimen.keySize))
                .semantics {
                    role = Role.Button
                    this.contentDescription = contentDescription
                }
                .border(
                    if (borders.value) 1.dp else 0.dp,
                    colorResource(id = R.color.keyboard_background)
                )
                // applied even if not repeatable, for consistency of acting on press not release
                .autoRepeat(
                    interactionSource = interactionSource,
                    repeatable = repeatable,
                    enabled = enabled
                ) { r -> onKey.value(c, r) },
            content = content
        )
    }
}

@Composable
private fun ResIcon(@DrawableRes icon: Int, enabled: Boolean) {
    Icon(
        painter = painterResource(id = icon),
        contentDescription = null,  // described on parent button
        modifier = Modifier.size(32.dp),
        // our icons are the right colour; only tint if disabled
        tint = if (enabled) Color.Unspecified else LocalContentColor.current,
    )
}

@Preview(widthDp = 500, heightDp = 96)
@Composable
fun Solo() {
    Buttons(
        remember { mutableStateOf("123456789\bMUR") },
        remember { mutableStateOf(SOLO) },
        remember { mutableStateOf(ARROWS_LEFT_CLICK) },
        swapLR = remember { mutableStateOf(false) },
        undoEnabled = remember { mutableStateOf(true) },
        redoEnabled = remember { mutableStateOf(false) },
        isLandscape = false,
        borders = remember { mutableStateOf(false) },
        onKey = remember { mutableStateOf({ _, _ -> }) },
        onSwapLR = remember { mutableStateOf({}) },
    )
}

@Preview(widthDp = 500, heightDp = 96)
@Composable
fun SoloWithBorders() {
    Buttons(
        remember { mutableStateOf("123456789\bMUR") },
        remember { mutableStateOf(SOLO) },
        remember { mutableStateOf(ARROWS_LEFT_CLICK) },
        swapLR = remember { mutableStateOf(false) },
        undoEnabled = remember { mutableStateOf(true) },
        redoEnabled = remember { mutableStateOf(false) },
        isLandscape = false,
        borders = remember { mutableStateOf(true) },
        onKey = remember { mutableStateOf({ _, _ -> }) },
        onSwapLR = remember { mutableStateOf({}) },
    )
}
