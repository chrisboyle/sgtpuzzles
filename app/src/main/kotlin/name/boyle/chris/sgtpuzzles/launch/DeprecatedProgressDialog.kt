@file:Suppress("DEPRECATION")

package name.boyle.chris.sgtpuzzles.launch

import android.app.ProgressDialog
import android.content.Context
import android.content.DialogInterface
import android.os.CountDownTimer
import android.view.View
import name.boyle.chris.sgtpuzzles.R

/** Acknowledge the deprecation of ProgressDialog in one place.
 *
 *  Yes, it would be more modern-looking to put the spinner in the game area instead of a dialog,
 *  but the user experience wouldn't be that different as we do need to block everything. */
class DeprecatedProgressDialog(context: Context, launch: GameLaunch, onCancel: () -> Unit, onReset: () -> Unit) : ProgressDialog(context) {
    init {
        val msgId = if (launch.needsGenerating) R.string.starting else R.string.resuming
        setMessage(context.getString(msgId))
        isIndeterminate = true
        setCancelable(true)
        setCanceledOnTouchOutside(false)
        setOnCancelListener {
            onCancel()
        }
        setButton(
            DialogInterface.BUTTON_NEGATIVE,
            context.getString(android.R.string.cancel)
        ) { _: DialogInterface?, _: Int ->
            onCancel()
        }
        if (launch.needsGenerating) {
            val backend = launch.whichBackend
            setButton(
                DialogInterface.BUTTON_NEUTRAL,
                context.getString(R.string.reset_this_backend, backend.displayName)
            ) { _: DialogInterface?, _: Int ->
                onReset()
            }
        }
        show()
        if (launch.needsGenerating) {
            getButton(DialogInterface.BUTTON_NEUTRAL).visibility = View.GONE
            val progressResetRevealer = object : CountDownTimer(3000, 3000) {
                override fun onTick(millisUntilFinished: Long) {}
                override fun onFinish() {
                    if (isShowing) {
                        getButton(DialogInterface.BUTTON_NEUTRAL).visibility = View.VISIBLE
                    }
                }
            }.start()
            setOnDismissListener { progressResetRevealer.cancel() }
        }
    }
}