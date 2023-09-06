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

class ButtonsView(
    context: Context, attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : AbstractComposeView(context, attrs, defStyleAttr) {

    val keys = mutableStateOf("")

    @Composable
    override fun Content() {
        Buttons(keys.value.toList())
    }
}

@Composable
fun Buttons(keyList: List<Char>) {
    BoxWithConstraints {
        var x = 0.dp
        var y = 0.dp
        keyList.map {
            GameButton(it, Modifier.offset(x = x, y = y))
            x += 48.dp
            if (x >= maxWidth - 48.dp) {
                x = 0.dp
                y += 48.dp
            }
        }
    }
}

@Composable
private fun GameButton(c: Char, modifier: Modifier = Modifier) {
    val buttonColors = ButtonDefaults.buttonColors(
        colorResource(R.color.keyboard_background),
        colorResource(R.color.keyboard_foreground),
        colorResource(R.color.keyboard_background),
        colorResource(R.color.keyboard_foreground)
    )
    Button(
        onClick = {
            // TODO
        },
        colors = buttonColors,
        shape = RectangleShape,
        contentPadding = PaddingValues(0.dp, 0.dp),
        modifier = modifier
            .width(dimensionResource(id = R.dimen.keySize))
            .height(dimensionResource(id = R.dimen.keySize))
    ) {
        when (c) {
            '\b' -> ResIcon(R.drawable.sym_key_backspace, "Backspace")
            'u' -> ResIcon(R.drawable.ic_action_undo, "Undo")
            'r' -> ResIcon(R.drawable.ic_action_redo, "Redo")
            else -> Text(c.toString())
        }
    }
}

@Composable
fun ResIcon(@DrawableRes icon: Int, contentDescription: String) {
    Icon(
        painter = painterResource(id = icon),
        contentDescription = contentDescription,
        modifier = Modifier.size(18.dp),
    )
}

@Preview(widthDp = 500, heightDp = 96)
@Composable
fun ButtonsPreview() {
    Buttons("123456789\bMur".toList())
}
