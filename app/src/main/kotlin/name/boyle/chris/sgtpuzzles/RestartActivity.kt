package name.boyle.chris.sgtpuzzles

import android.app.Activity
import android.content.Intent
import android.os.Bundle

class RestartActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startActivity(Intent(this, GamePlay::class.java))
        finish()
    }
}