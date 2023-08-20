package name.boyle.chris.sgtpuzzles

import android.net.Uri
import androidx.core.content.FileProvider

class FixedTypeFileProvider : FileProvider() {
    override fun getType(uri: Uri): String = GamePlay.MIME_TYPE
}