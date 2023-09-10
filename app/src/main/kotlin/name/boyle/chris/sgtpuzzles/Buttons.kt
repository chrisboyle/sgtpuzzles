package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.content.res.Configuration
import android.util.AttributeSet
import android.util.Log
import androidx.annotation.DrawableRes
import androidx.compose.foundation.background
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
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.AbstractComposeView
import androidx.compose.ui.platform.LocalContext
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.max
import androidx.compose.ui.unit.sp
import androidx.compose.ui.unit.times
import name.boyle.chris.sgtpuzzles.GameView.CURSOR_DOWN
import name.boyle.chris.sgtpuzzles.GameView.CURSOR_LEFT
import name.boyle.chris.sgtpuzzles.GameView.CURSOR_RIGHT
import name.boyle.chris.sgtpuzzles.GameView.CURSOR_UP
import name.boyle.chris.sgtpuzzles.GameView.MOD_NUM_KEYPAD
import name.boyle.chris.sgtpuzzles.SmallKeyboard.ArrowMode
import name.boyle.chris.sgtpuzzles.SmallKeyboard.ArrowMode.ARROWS_DIAGONALS
import name.boyle.chris.sgtpuzzles.SmallKeyboard.ArrowMode.ARROWS_LEFT_CLICK
import name.boyle.chris.sgtpuzzles.SmallKeyboard.ArrowMode.ARROWS_LEFT_RIGHT_CLICK
import name.boyle.chris.sgtpuzzles.SmallKeyboard.ArrowMode.ARROWS_ONLY
import name.boyle.chris.sgtpuzzles.SmallKeyboard.ArrowMode.NO_ARROWS
import name.boyle.chris.sgtpuzzles.SmallKeyboard.SWAP_L_R_KEY
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
import name.boyle.chris.sgtpuzzles.backend.PATTERN
import name.boyle.chris.sgtpuzzles.backend.PEARL
import name.boyle.chris.sgtpuzzles.backend.RANGE
import name.boyle.chris.sgtpuzzles.backend.RECT
import name.boyle.chris.sgtpuzzles.backend.SAMEGAME
import name.boyle.chris.sgtpuzzles.backend.SINGLES
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.TENTS
import name.boyle.chris.sgtpuzzles.backend.TOWERS
import name.boyle.chris.sgtpuzzles.backend.TWIDDLE
import name.boyle.chris.sgtpuzzles.backend.UNDEAD
import name.boyle.chris.sgtpuzzles.backend.UNEQUAL
import name.boyle.chris.sgtpuzzles.backend.UNRULY
import name.boyle.chris.sgtpuzzles.backend.UNTANGLE
import kotlin.math.ceil
import kotlin.math.floor


class ButtonsView(context: Context, attrs: AttributeSet? = null) :
    AbstractComposeView(context, attrs) {

    val keys = mutableStateOf("")
    val arrowMode = mutableStateOf(NO_ARROWS)
    val swapLR = mutableStateOf(false)
    val hidePrimary = mutableStateOf(false)
    val disableCharacterIcons = mutableStateOf("")
    val backend: MutableState<BackendName> = mutableStateOf(UNTANGLE)
    val undoEnabled = mutableStateOf(false)
    val redoEnabled = mutableStateOf(false)
    val onKeyListener: MutableState<((Int) -> Unit)> = mutableStateOf({})

    @Composable
    override fun Content() {
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
            onKeyListener)
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
    onKey: MutableState<((Int) -> Unit)>
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
                AddCharacters(
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
                    onKey
                )
            }

            if (hasArrows) {
                val arrowsRightEdge: Dp = if (isLandscape) largerWidth else maxDp
                val arrowsBottomEdge: Dp = if (isLandscape) maxDp else largerHeight
                AddArrows(
                    backend,
                    keySize,
                    arrowMode,
                    swapLR,
                    hidePrimary,
                    arrowRows,
                    arrowsRightEdge,
                    arrowsBottomEdge,
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
private fun AddCharacters(
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
    onKey: MutableState<(Int) -> Unit>
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
        AddCharacterKey(backend, undoEnabled, redoEnabled, c, x, y, disableCharacterIcons, onKey)
        minor++
        minorDp += keySize
    }
}

@Composable
private fun AddCharacterKey(
    backend: MutableState<BackendName>,
    undoEnabled: MutableState<Boolean>,
    redoEnabled: MutableState<Boolean>,
    c: Char,
    x: Dp,
    y: Dp,
    disableCharacterIcons: MutableState<String>,
    onKey: MutableState<(Int) -> Unit>
) {
    when (c.code) {
        GameView.UI_UNDO, 'U'.code -> IconKeyButton(
            c.code, R.drawable.ic_action_undo, "Undo", onKey, Pair(x, y),
            repeatable = true, enabled = undoEnabled.value
        )

        GameView.UI_REDO, 'R'.code -> IconKeyButton(
            c.code, R.drawable.ic_action_redo, "Redo", onKey, Pair(x, y),
            repeatable = true, enabled = redoEnabled.value
        )

        '\b'.code -> IconKeyButton(
            c.code, R.drawable.sym_key_backspace, "Backspace",
            onKey, Pair(x, y), repeatable = true
        )

        SWAP_L_R_KEY.code -> IconKeyButton(
            c.code, R.drawable.ic_action_swap_l_r, "Swap press and long press", onKey, Pair(x, y)
            // TODO swapLR modifies state from here?
        )

        else -> {
            CharacterButton(backend, c, Pair(x, y), disableCharacterIcons, onKey)
        }
    }
}

@Composable
private fun AddArrows(
    backend: MutableState<BackendName>,
    keySize: Dp,
    arrowMode: MutableState<ArrowMode>,
    swapLR: MutableState<Boolean>,
    hidePrimary: MutableState<Boolean>,
    arrowRows: Int,
    arrowsRightEdge: Dp,
    arrowsBottomEdge: Dp,
    onKey: MutableState<((Int) -> Unit)>
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
    val primaryOffset = Pair(arrowsRightEdge - (if (isDiagonal) 2 else 3) * keySize, arrowsBottomEdge - 2 * keySize)
    val secondaryOffset = Pair(arrowsRightEdge - keySize, arrowsBottomEdge - arrowRows * keySize)

    for (arrow in arrows) {
        when (arrow) {
            CURSOR_UP -> IconKeyButton(
                arrow, R.drawable.sym_key_north, "Up", onKey,
                Pair(arrowsRightEdge - 2 * keySize, arrowsBottomEdge - arrowRows * keySize),
                repeatable = true
            )
            CURSOR_DOWN -> IconKeyButton(
                arrow, R.drawable.sym_key_south, "Down", onKey,
                Pair(arrowsRightEdge - 2 * keySize, arrowsBottomEdge - keySize),
                repeatable = true
            )
            CURSOR_LEFT -> IconKeyButton(
                arrow, R.drawable.sym_key_west, "Left", onKey,
                Pair(arrowsRightEdge - 3 * keySize, arrowsBottomEdge - leftRightRow * keySize),
                repeatable = true
            )
            CURSOR_RIGHT -> IconKeyButton(
                arrow, R.drawable.sym_key_east, "Right", onKey,
                Pair(arrowsRightEdge - keySize, arrowsBottomEdge - leftRightRow * keySize),
                repeatable = true
            )
            '\n'.code -> if (!hidePrimary.value) IconKeyButton(
                arrow, mouseIcon(backend, false), "", onKey,  // TODO primary desc
                if (swapLR.value) secondaryOffset else primaryOffset,
                repeatable = true
            )
            ' '.code -> IconKeyButton(
                arrow, mouseIcon(backend, true),
                "", // TODO secondary desc
                onKey,
                if (swapLR.value) primaryOffset else secondaryOffset,
            )
            MOD_NUM_KEYPAD or '7'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_north_west, "Up and left", onKey,
                Pair(arrowsRightEdge - 3 * keySize, arrowsBottomEdge - 3 * keySize),
                repeatable = true
            )
            MOD_NUM_KEYPAD or '1'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_south_west, "Down and left", onKey,
                Pair(arrowsRightEdge - 3 * keySize, arrowsBottomEdge - keySize),
                repeatable = true
            )
            MOD_NUM_KEYPAD or '9'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_north_east, "Up and right", onKey,
                Pair(arrowsRightEdge - keySize, arrowsBottomEdge - 3 * keySize),
                repeatable = true
            )
            MOD_NUM_KEYPAD or '3'.code -> IconKeyButton(
                arrow, R.drawable.sym_key_south_east, "Down and right", onKey,
                Pair(arrowsRightEdge - keySize, arrowsBottomEdge - keySize),
                repeatable = true
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
    offset: Pair<Dp, Dp>,
    disableCharacterIcons: MutableState<String>,
    onKey: MutableState<(Int) -> Unit>
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
            c.code, icon, "",  // TODO desc
            onKey, offset
        )
    } else {
        // Not proud of this, but: I'm using uppercase letters to mean it's a command as
        // opposed to data entry (Mark all squares versus enter 'm'). But I still want the
        // keys for data entry to be uppercase in unequal because that matches the board.
        TextKeyButton(
            if (backend.value == UNEQUAL) c.uppercaseChar().code else c.code,
            "", // TODO desc
            onKey, offset)  // TODO onKey for unequal!
    }
}

@Composable
private fun IconKeyButton(
    c: Int,
    @DrawableRes icon: Int,
    contentDescription: String,
    onKey: MutableState<((Int) -> Unit)>,
    offset: Pair<Dp, Dp>,
    modifier: Modifier = Modifier,
    repeatable: Boolean = false,
    enabled: Boolean = true,
) {
    KeyButton(c, contentDescription, onKey, offset, modifier, repeatable, enabled = enabled) {
        ResIcon(icon)
    }
}

@Composable
private fun TextKeyButton(
    c: Int,
    contentDescription: String,
    onKey: MutableState<((Int) -> Unit)>,
    offset: Pair<Dp, Dp>,
    modifier: Modifier = Modifier,
    repeatable: Boolean = false,
    enabled: Boolean = true,
) {
    KeyButton(c, contentDescription, onKey, offset, modifier, repeatable, enabled = enabled) {
        Text(c.toChar().toString(), fontSize = 24.sp, fontWeight = FontWeight.Normal)
    }
}

@Composable
private fun KeyButton(
    c: Int,
    contentDescription: String,
    onKey: MutableState<((Int) -> Unit)>,
    offset: Pair<Dp, Dp>,
    modifier: Modifier = Modifier,
    repeatable: Boolean = false,  // TODO! repeatable
    enabled: Boolean = true,
    content: @Composable (RowScope.() -> Unit),
) {
    val bordered = false
    val buttonColors = ButtonDefaults.buttonColors(
        colorResource(if (bordered) R.color.key_background_border else R.color.keyboard_background),
        colorResource(R.color.keyboard_foreground),
        colorResource(R.color.keyboard_background),
        colorResource(R.color.keyboard_foreground_disabled)
    )
    Button(
        onClick = { onKey.value(c) },
        enabled = enabled,
        colors = buttonColors,
        shape = RectangleShape,
        contentPadding = PaddingValues(0.dp, 0.dp),
        modifier = modifier
            .absoluteOffset(offset.first, offset.second)
            .width(dimensionResource(id = R.dimen.keySize))
            .height(dimensionResource(id = R.dimen.keySize))
            .semantics {
                role = Role.Button
                this.contentDescription = contentDescription
            },
        content = content
    )
}

@Composable
private fun ResIcon(@DrawableRes icon: Int) {
    Icon(
        painter = painterResource(id = icon),
        contentDescription = null,  // described on parent button
        modifier = Modifier.size(32.dp),
    )
}

@Preview(widthDp = 500, heightDp = 96)
@Composable
fun ButtonsPreview() {
    Buttons(
        remember { mutableStateOf("123456789\bMUR") },
        remember { mutableStateOf(SOLO) },
        remember { mutableStateOf(ARROWS_LEFT_CLICK) },
        swapLR = remember { mutableStateOf(false) },
        undoEnabled = remember { mutableStateOf(true) },
        redoEnabled = remember { mutableStateOf(false) },
        isLandscape = false,
        onKey = remember { mutableStateOf({}) },
    )
}
