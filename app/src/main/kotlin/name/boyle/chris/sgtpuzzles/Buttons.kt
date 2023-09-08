package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.util.AttributeSet
import androidx.annotation.DrawableRes
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.AbstractComposeView
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

class ButtonsView(context: Context, attrs: AttributeSet? = null) :
    AbstractComposeView(context, attrs) {

    val keys = mutableStateOf("")

    var onKeyListener: ((Char) -> Unit)? = null

    @Composable
    override fun Content() {
        Buttons(keys.value.toList(), onKeyListener ?: {})
    }
}

@Composable
fun Buttons(keyList: List<Char>, onKey: (Char) -> Unit) {
    BoxWithConstraints {
        var x = 0.dp
        var y = 0.dp
        keyList.map {
            GameButton(it, Modifier.offset(x = x, y = y), onKey)
            x += 48.dp
            if (x >= maxWidth - 48.dp) {
                x = 0.dp
                y += 48.dp
            }
        }
    }
}

@Composable
private fun GameButton(c: Char, modifier: Modifier = Modifier, onKey: (Char) -> Unit) {
    val buttonColors = ButtonDefaults.buttonColors(
        colorResource(R.color.keyboard_background),
        colorResource(R.color.keyboard_foreground),
        colorResource(R.color.keyboard_background),
        colorResource(R.color.keyboard_foreground)
    )
    Button(
        onClick = { onKey(c) },
        colors = buttonColors,
        shape = RectangleShape,
        contentPadding = PaddingValues(0.dp, 0.dp),
        modifier = modifier
            .width(dimensionResource(id = R.dimen.keySize))
            .height(dimensionResource(id = R.dimen.keySize))
    ) {
        when (c) {
            '\b' -> ResIcon(R.drawable.sym_key_backspace, "Backspace")
            'u', 'U' -> ResIcon(R.drawable.ic_action_undo, "Undo")
            'r', 'R' -> ResIcon(R.drawable.ic_action_redo, "Redo")
            '*' -> ResIcon(R.drawable.ic_action_swap_l_r, "Swap L/R")
            else -> Text(c.toString())
        }
    }
}

@Composable
fun ResIcon(@DrawableRes icon: Int, contentDescription: String) {
    Icon(
        painter = painterResource(id = icon),
        contentDescription = contentDescription,
        modifier = Modifier.size(24.dp),
    )
}

@Preview(widthDp = 500, heightDp = 96)
@Composable
fun ButtonsPreview() {
    Buttons("123456789\bMur".toList()) {}
}
