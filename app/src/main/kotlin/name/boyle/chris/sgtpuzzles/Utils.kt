package name.boyle.chris.sgtpuzzles

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.content.SharedPreferences
import android.text.method.LinkMovementMethod
import android.view.LayoutInflater
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.core.text.HtmlCompat
import name.boyle.chris.sgtpuzzles.databinding.FeedbackDialogBinding
import java.io.IOException
import java.io.InputStream
import java.text.MessageFormat
import java.util.Locale
import kotlin.math.roundToInt

var TextView.intValue: Int?
    get() = try {
        text.toString().toDouble().roundToInt()
    } catch (_: NumberFormatException) {
        null
    }
    set(value) { text = value?.let { String.format(Locale.ROOT, "%d", it) } ?: "" }

object Utils {
    @JvmStatic
    @Throws(IOException::class)
    fun readAllOf(s: InputStream?): String {
        requireNotNull(s)
        return s.bufferedReader().use { it.readText() }
    }

    @JvmStatic
    fun toastFirstFewTimes(
        context: Context?,
        state: SharedPreferences,
        prefID: String?,
        showCount: Int,
        messageID: Int
    ) {
        var seen = state.getLong(prefID, 0)
        if (seen < showCount) {
            Toast.makeText(context, messageID, Toast.LENGTH_SHORT).show()
        }
        seen++
        state.edit { putLong(prefID, seen) }
    }

    @JvmStatic
    fun sendFeedbackDialog(context: Context) {
        val binding = FeedbackDialogBinding.inflate(LayoutInflater.from(context))
        binding.body.text = HtmlCompat.fromHtml(
            MessageFormat.format(
                context.getString(R.string.feedback_dialog),
                context.packageName,
                context.getString(R.string.issues_url)
            ), HtmlCompat.FROM_HTML_MODE_LEGACY
        )
        binding.body.movementMethod = LinkMovementMethod.getInstance()
        binding.version.text = MessageFormat.format(
            context.getString(R.string.feedback_version),
            BuildConfig.VERSION_NAME
        )
        binding.versionRow.setOnClickListener { copyVersionToClipboard(context) }
        AlertDialog.Builder(context).setView(binding.root).show()
    }

    @JvmStatic
    fun unlikelyBug(context: Context, reasonId: Int) {
        AlertDialog.Builder(context)
            .setMessage(
                MessageFormat.format(
                    context.getString(R.string.bug_dialog),
                    context.getString(reasonId)
                )
            )
            .setPositiveButton(R.string.report) { _: DialogInterface?, _: Int ->
                openURL(
                    context,
                    R.string.issues_url
                )
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    @JvmStatic
    fun openURL(context: Context, urlId: Int) {
        val url = context.getString(urlId)
        try {
            context.startActivity(Intent(Intent.ACTION_VIEW, url.toUri()))
        } catch (_: ActivityNotFoundException) {
            AlertDialog.Builder(context)
                .setTitle(R.string.no_browser_title)
                .setMessage(
                    """
                    ${context.getString(R.string.no_browser_body)}
                    
                    $url
                    """.trimIndent()
                )
                .show()
        }
    }

    @JvmStatic
    fun copyVersionToClipboard(context: Context) {
        (context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(
            ClipData.newPlainText(
                context.getString(R.string.version_copied_label),
                MessageFormat.format(
                    context.getString(R.string.version_for_clipboard),
                    BuildConfig.VERSION_NAME
                )
            )
        )
        Toast.makeText(context, R.string.version_copied, Toast.LENGTH_SHORT).show()
    }

    @JvmStatic
    fun listFromSeparated(s: String) = s.drop(1).split(s.take(1))
}