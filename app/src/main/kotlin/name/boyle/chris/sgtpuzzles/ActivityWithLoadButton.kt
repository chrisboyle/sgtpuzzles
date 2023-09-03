package name.boyle.chris.sgtpuzzles

import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.Intent.ACTION_VIEW
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContracts
import name.boyle.chris.sgtpuzzles.NightModeHelper.ActivityWithNightMode

abstract class ActivityWithLoadButton : ActivityWithNightMode() {

    private val loadLauncher =
        registerForActivityResult<Array<String>, Uri>(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri == null) return@registerForActivityResult
            startActivity(
                Intent(ACTION_VIEW, uri, this@ActivityWithLoadButton, GamePlay::class.java)
            )
            overridePendingTransition(0, 0)
        }

    protected fun loadGame() {
        try {
            loadLauncher.launch(arrayOf("text/*", "application/octet-stream"))
        } catch (e: ActivityNotFoundException) {
            Utils.unlikelyBug(this, R.string.saf_missing_short)
        }
    }
}