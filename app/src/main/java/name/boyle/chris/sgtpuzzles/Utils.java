package name.boyle.chris.sgtpuzzles;

import android.annotation.SuppressLint;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import android.net.Uri;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.text.MessageFormat;

abstract class Utils {

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

	static int waitForProcess(Process process) {
		if (process == null) return -1;
		try {
			while (true) {
				try {
					return process.waitFor();
				} catch (InterruptedException ignored) {}
			}
		} finally {
			process.destroy();
		}
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
		new AlertDialog.Builder(context)
				.setMessage(R.string.feedback_dialog)
				.setPositiveButton(R.string.review, (dialog, which) -> openURL(context, R.string.review_url))
				.setNegativeButton(R.string.issues, (dialog, which) -> openURL(context, R.string.issues_url))
				.show();
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

}
