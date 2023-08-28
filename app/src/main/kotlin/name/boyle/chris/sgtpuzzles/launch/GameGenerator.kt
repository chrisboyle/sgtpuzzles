package name.boyle.chris.sgtpuzzles.launch;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.net.Uri;
import android.os.Build;
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

import name.boyle.chris.sgtpuzzles.R;
import name.boyle.chris.sgtpuzzles.Utils;
import name.boyle.chris.sgtpuzzles.config.PrefsConstants;

public class GameGenerator {

    public interface Callback {
        void gameGeneratorSuccess(GameLaunch launch, String previousGame);
        void gameGeneratorFailure(Exception e, GameLaunch launch);
    }

    private static final String TAG = "GameGenerator";
    public static final String PUZZLESGEN_EXECUTABLE = "libpuzzlesgen.so";
    private static final String PUZZLES_LIBRARY = "libpuzzles.so";
    private static final String[] OBSOLETE_EXECUTABLES_IN_DATA_DIR = {"puzzlesgen", "puzzlesgen-with-pie", "puzzlesgen-no-pie"};

    private final ExecutorService executor = Executors.newCachedThreadPool();

    public void onDestroy() {
        executor.shutdownNow();
    }

    @NonNull
    public Future<?> generate(final ApplicationInfo appInfo, final GameLaunch input, final List<String> args, final String previousGame, final Callback callback) {
        // CompletableFuture would be cleaner when we reach minSdkVersion 24
        final Future<?>[] future = new Future[] {null};
        future[0] = executor.submit(() -> {
            final String generated;
            try {
                final Process process = startGameGenProcess(appInfo, args);
                process.getOutputStream().close();
                final int exitStatus;
                final String stderr;
                try {
                    exitStatus = process.waitFor();
                    if (future[0] != null && future[0].isCancelled()) return;
                    generated = Utils.readAllOf(process.getInputStream());
                    stderr = Utils.readAllOf(process.getErrorStream());
                } catch (InterruptedException e) {
                    // cancelled
                    return;
                } finally {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        process.destroyForcibly();
                    } else {
                        process.destroy();
                    }
                }
                if (exitStatus != 0) {
                    if (!stderr.isEmpty()) {  // probably bogus params
                        throw new IllegalArgumentException(stderr);
                    } else {
                        final String exitError = "Game generation exited with status " + exitStatus;
                        Log.e(TAG, exitError);
                        throw new IOException(exitError);
                    }
                }
                if (generated.isEmpty()) {
                    throw new IOException("Internal error generating game: result is blank");
                }
            } catch (IOException | IllegalArgumentException e) {
                callback.gameGeneratorFailure(e, input);
                return;
            }
            callback.gameGeneratorSuccess(input.finishedGenerating(generated), previousGame);
        });
        return future[0];
    }

    @NonNull
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

    static void cleanUpOldExecutables(final SharedPreferences prefs, final SharedPreferences state, final File dataDir) {
        if (state.getBoolean(PrefsConstants.PUZZLESGEN_CLEANUP_DONE, false)) {
            return;
        }
        // We used to copy the executable to our dataDir and execute it. I don't remember why
        // executing directly from nativeLibraryDir didn't work, but it definitely does now.
        // Clean up any previously stashed executable in dataDir to save the user some space.
        for (final String toDelete : OBSOLETE_EXECUTABLES_IN_DATA_DIR) {
            try {
                Log.d(TAG, "deleting obsolete file: " + toDelete);
                //noinspection ResultOfMethodCallIgnored
                new File(dataDir, toDelete).delete();  // ok to fail
            }
            catch (SecurityException ignored) {}
        }
        prefs.edit().remove(PrefsConstants.OLD_PUZZLESGEN_LAST_UPDATE).apply();
        state.edit().putBoolean(PrefsConstants.PUZZLESGEN_CLEANUP_DONE, true).apply();
    }
}
