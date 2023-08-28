package name.boyle.chris.sgtpuzzles

import android.app.Activity
import android.os.Bundle
import androidx.core.app.TaskStackBuilder
import name.boyle.chris.sgtpuzzles.launch.GameGenerator

class SGTPuzzles : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (GameGenerator.executableIsMissing(this)) {
            finish()
            return
        }
        TaskStackBuilder.create(this)
            .addNextIntentWithParentStack(intent.setClass(this, GamePlay::class.java))
            .startActivities()
        finish()
    }
}