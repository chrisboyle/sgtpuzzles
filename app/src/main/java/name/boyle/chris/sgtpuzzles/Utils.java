package name.boyle.chris.sgtpuzzles;

import android.annotation.SuppressLint;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;

import android.net.Uri;
import android.text.Html;
import android.text.method.LinkMovementMethod;
import android.util.TypedValue;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.Closeable;
import java.io.File;
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
		final TextView textView = new TextView(context);
		textView.setText(Html.fromHtml(MessageFormat.format(
				context.getString(R.string.feedback_dialog),
				context.getPackageName(),
				context.getString(R.string.issues_url))));
		textView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
		final int pad = (int)(25 * context.getResources().getDisplayMetrics().density + 0.5f);
		textView.setPaddingRelative(pad, pad, pad, 0);
		textView.setMovementMethod(LinkMovementMethod.getInstance());
		new AlertDialog.Builder(context).setView(textView).show();
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

	static File fromInstallationOrSystem(final File nativeLibDir, final String basename) {
		// Allow installing the app to /system (but prefer standard path)
		// https://github.com/chrisboyle/sgtpuzzles/issues/226
		final File standardPath = new File(nativeLibDir, basename);
		final File sysPath = new File("/system/lib", basename);
		return (!standardPath.exists() && sysPath.exists()) ? sysPath : standardPath;
	}

	static boolean gameGeneratorExecutableIsMissing(final Context context) {
		final File nativeLibraryDir = new File(context.getApplicationInfo().nativeLibraryDir);
		final File executablePath = fromInstallationOrSystem(nativeLibraryDir, GamePlay.PUZZLESGEN_EXECUTABLE);
		if (executablePath.canExecute()) {
			return false;
		}
		Toast.makeText(context, R.string.missing_game_generator, Toast.LENGTH_LONG).show();
		return true;
	}
}
