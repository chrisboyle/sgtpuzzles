package name.boyle.chris.sgtpuzzles;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.net.Uri;
import android.util.Log;
import android.widget.Toast;

import androidx.annotation.NonNull;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class GameGenerator {

    public interface Callback {
        void gameGeneratorSuccess(GameLaunch launch, String previousGame);
        void gameGeneratorFailure(Exception e, boolean isFromChooser);
    }

    private static final String TAG = "GameGenerator";
    public static final String PUZZLESGEN_EXECUTABLE = "libpuzzlesgen.so";
    private static final String PUZZLES_LIBRARY = "libpuzzles.so";

    private final ExecutorService executor = Executors.newCachedThreadPool();

    public Future<?> generate(final ApplicationInfo appInfo, final GameLaunch input, final List<String> args, final String previousGame, final Callback callback) {
        return executor.submit(() -> {
            final String generated;
            try {
                final Process process = startGameGenProcess(appInfo, args);
                process.getOutputStream().close();
                generated = Utils.readAllOf(process.getInputStream());
                final int exitStatus = waitForProcess(process);
                if (generated.isEmpty()) {
                    throw new IOException("Internal error generating game: result is blank");
                }
                if (Thread.interrupted()) {
                    // cancelled
                    process.destroy();
                    return;
                }
                if (exitStatus != 0) {
                    final String stderr = Utils.readAllOf(process.getErrorStream());
                    if (!stderr.isEmpty()) {  // probably bogus params
                        throw new IllegalArgumentException(stderr);
                    } else {
                        final String exitError = "Game generation exited with status " + exitStatus;
                        Log.e(TAG, exitError);
                        throw new IOException(exitError);
                    }
                }
            } catch (IOException | IllegalArgumentException e) {
                callback.gameGeneratorFailure(e, input.isFromChooser());
                return;
            }
            callback.gameGeneratorSuccess(input.finishedGenerating(generated), previousGame);
        });
    }

    private static int waitForProcess(Process process) {
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

    private static Process startGameGenProcess(final ApplicationInfo appInfo, final List<String> args) throws IOException {
        final File nativeLibraryDir = new File(appInfo.nativeLibraryDir);
        final File executablePath = fromInstallationOrSystem(nativeLibraryDir, PUZZLESGEN_EXECUTABLE);
        final File libPuzDir = fromInstallationOrSystem(nativeLibraryDir, PUZZLES_LIBRARY).getParentFile();
        final ProcessBuilder builder = new ProcessBuilder();
        final List<String> execAndArgs = new ArrayList<>();
        execAndArgs.add(executablePath.getAbsolutePath());
        execAndArgs.addAll(args);
        builder.command(execAndArgs);
        builder.directory(libPuzDir);
        builder.environment().put("LD_LIBRARY_PATH", Objects.requireNonNull(libPuzDir).getAbsolutePath());
        Log.d(TAG, "exec: " + builder.command());
        return builder.start();
    }

    @NonNull
    private static File fromInstallationOrSystem(final File nativeLibDir, final String basename) {
        // Allow installing the app to /system (but prefer standard path)
        // https://github.com/chrisboyle/sgtpuzzles/issues/226
        final File standardPath = new File(nativeLibDir, basename);
        final File sysPath = new File("/system/lib", basename);
        return (!standardPath.exists() && sysPath.exists()) ? sysPath : standardPath;
    }

    static boolean executableIsMissing(final Context context) {
        final File nativeLibraryDir = new File(context.getApplicationInfo().nativeLibraryDir);
        final File executablePath = fromInstallationOrSystem(nativeLibraryDir, PUZZLESGEN_EXECUTABLE);
        if (executablePath.canExecute()) {
            return false;
        }
        Toast.makeText(context, R.string.missing_game_generator, Toast.LENGTH_LONG).show();
        try {
            context.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://play.google.com/store/apps/details?id=" + context.getPackageName())));
        } catch (ActivityNotFoundException ignored) {}
        return true;
    }
}
