package name.boyle.chris.sgtpuzzles

import android.content.Context
import android.util.AttributeSet
import androidx.annotation.DrawableRes
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateListOf
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

    private val keyList = mutableStateListOf<Char>()

    var keys: String
        get() = keyList.joinToString("")
        set(value) {
            keyList.clear()
            keyList.addAll(value.toList())
        }

    @Composable
    override fun Content() {
        Buttons(keyList)
    }
}

@Composable
fun Buttons(keyList: List<Char>) {
    Row {
        keyList.map {
            GameButton(it)
        }
    }
}

@Composable
private fun GameButton(c: Char) {
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
        modifier = Modifier
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

@Preview
@Composable
fun ButtonsPreview() {
    Buttons("123456\bur".toList())
}
