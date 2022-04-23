package name.boyle.chris.sgtpuzzles;

import android.annotation.SuppressLint;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.text.Html;
import android.text.method.LinkMovementMethod;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import java.io.BufferedReader;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.text.MessageFormat;
import java.util.Objects;

abstract class Utils {
	private Utils() {}

	static String readAllOf(InputStream s) throws IOException
	{
		BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(s), 8096);
		String line;
		StringBuilder log = new StringBuilder();
		while ((line = bufferedReader.readLine()) != null) {
			log.append(line);
			log.append("\n");
		}
		return log.toString();
	}

	static void closeQuietly(@Nullable Closeable c)
	{
		if (c == null) return;
		try {
			c.close();
		} catch (IOException ignored) {}
	}

	@SuppressLint("CommitPrefEdits")
	static void toastFirstFewTimes(Context context, SharedPreferences state, String prefID, int showCount, int messageID) {
		long seen = state.getLong(prefID, 0);
		if (seen < showCount) {
			Toast.makeText(context, messageID, Toast.LENGTH_SHORT).show();
		}
		seen++;
		SharedPreferences.Editor ed = state.edit();
		ed.putLong(prefID, seen);
		ed.apply();
	}

	static void sendFeedbackDialog(final Context context) {
		final AlertDialog dialog = new AlertDialog.Builder(context).setView(R.layout.feedback_dialog).show();
		final TextView textView = Objects.requireNonNull(dialog.findViewById(R.id.body));
		textView.setText(Html.fromHtml(MessageFormat.format(
				context.getString(R.string.feedback_dialog),
				context.getPackageName(),
				context.getString(R.string.issues_url))));
		textView.setMovementMethod(LinkMovementMethod.getInstance());
		final TextView version = Objects.requireNonNull(dialog.findViewById(R.id.version));
		version.setText(MessageFormat.format(context.getString(R.string.feedback_version), BuildConfig.VERSION_NAME));
		final View versionRow = Objects.requireNonNull(dialog.findViewById(R.id.versionRow));
		versionRow.setOnClickListener(view -> copyVersionToClipboard(context));
	}

	static void unlikelyBug(final Context context, final int reasonId) {
		new AlertDialog.Builder(context)
				.setMessage(MessageFormat.format(context.getString(R.string.bug_dialog), context.getString(reasonId)))
				.setPositiveButton(R.string.report, (dialog, which) -> openURL(context, R.string.issues_url))
				.setNegativeButton(R.string.cancel, null)
				.show();
	}

	static void openURL(final Context context, final int urlId) {
		final String url = context.getString(urlId);
		try {
			context.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
		} catch (ActivityNotFoundException e) {
			new AlertDialog.Builder(context)
					.setTitle(R.string.no_browser_title)
					.setMessage(context.getString(R.string.no_browser_body) + "\n\n" + url)
					.show();
		}
	}

	static void copyVersionToClipboard(final Context context) {
		final ClipData data = ClipData.newPlainText(context.getString(R.string.version_copied_label), MessageFormat.format(context.getString(R.string.version_for_clipboard), BuildConfig.VERSION_NAME));
		((ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE)).setPrimaryClip(data);
		Toast.makeText(context, R.string.version_copied, Toast.LENGTH_SHORT).show();
	}
}
