package name.boyle.chris.sgtpuzzles.launch

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.ApplicationInfo
import android.net.Uri
import android.os.Build
import android.util.Log
import android.widget.Toast
import name.boyle.chris.sgtpuzzles.R
import name.boyle.chris.sgtpuzzles.Utils.readAllOf
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.OLD_PUZZLESGEN_LAST_UPDATE
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.PUZZLESGEN_CLEANUP_DONE
import java.io.File
import java.io.IOException
import java.util.Objects
import java.util.concurrent.Executors
import java.util.concurrent.Future

class GameGenerator {
    interface Callback {
        fun gameGeneratorSuccess(launch: GameLaunch, previousGame: String?)
        fun gameGeneratorFailure(e: Exception, launch: GameLaunch)
    }

    private val executor = Executors.newCachedThreadPool()
    fun onDestroy() {
        executor.shutdownNow()
    }

    fun generate(
        appInfo: ApplicationInfo,
        input: GameLaunch,
        args: List<String>,
        previousGame: String?,
        callback: Callback
    ): Future<*> {
        // CompletableFuture would be cleaner when we reach minSdkVersion 24
        lateinit var future: Future<*>
        future = executor.submit {
            val generated: String
            try {
                val process = startGameGenProcess(appInfo, args)
                process.outputStream.close()
                val exitStatus: Int
                val stderr: String
                try {
                    exitStatus = process.waitFor()
                    if (future.isCancelled) return@submit
                    generated = readAllOf(process.inputStream)
                    stderr = readAllOf(process.errorStream)
                } catch (e: InterruptedException) {
                    // cancelled
                    return@submit
                } finally {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        process.destroyForcibly()
                    } else {
                        process.destroy()
                    }
                }
                if (exitStatus != 0) {
                    require(stderr.isEmpty()) { stderr }   // probably bogus params
                    val exitError = "Game generation exited with status $exitStatus"
                    Log.e(TAG, exitError)
                    throw IOException(exitError)
                }
                if (generated.isEmpty()) {
                    throw IOException("Internal error generating game: result is blank")
                }
            } catch (e: IOException) {
                callback.gameGeneratorFailure(e, input)
                return@submit
            } catch (e: IllegalArgumentException) {
                callback.gameGeneratorFailure(e, input)
                return@submit
            }
            callback.gameGeneratorSuccess(input.finishedGenerating(generated), previousGame)
        }
        return future
    }

    companion object {
        private const val TAG = "GameGenerator"
        private const val PUZZLESGEN_EXECUTABLE = "libpuzzlesgen.so"
        private const val PUZZLES_LIBRARY = "libpuzzles.so"
        private val OBSOLETE_EXECUTABLES_IN_DATA_DIR =
            setOf("puzzlesgen", "puzzlesgen-with-pie", "puzzlesgen-no-pie")

        @Throws(IOException::class)
        private fun startGameGenProcess(appInfo: ApplicationInfo, args: List<String>): Process {
            val nativeLibraryDir = File(appInfo.nativeLibraryDir)
            val executablePath = fromInstallationOrSystem(nativeLibraryDir, PUZZLESGEN_EXECUTABLE)
            val libPuzDir = fromInstallationOrSystem(nativeLibraryDir, PUZZLES_LIBRARY).parentFile
            return ProcessBuilder().apply {
                command(listOf(executablePath.absolutePath) + args)
                Log.d(TAG, "exec: " + command())
                directory(libPuzDir)
                environment()["LD_LIBRARY_PATH"] = Objects.requireNonNull(libPuzDir).absolutePath
            }.start()
        }

        private fun fromInstallationOrSystem(nativeLibDir: File, basename: String): File {
            // Allow installing the app to /system (but prefer standard path)
            // https://github.com/chrisboyle/sgtpuzzles/issues/226
            val standardPath = File(nativeLibDir, basename)
            val sysPath = File("/system/lib", basename)
            return if (!standardPath.exists() && sysPath.exists()) sysPath else standardPath
        }

        @JvmStatic
        fun executableIsMissing(context: Context): Boolean {
            val nativeLibraryDir = File(context.applicationInfo.nativeLibraryDir)
            val executablePath = fromInstallationOrSystem(nativeLibraryDir, PUZZLESGEN_EXECUTABLE)
            if (executablePath.canExecute()) {
                return false
            }
            Toast.makeText(context, R.string.missing_game_generator, Toast.LENGTH_LONG).show()
            try {
                context.startActivity(
                    Intent(
                        Intent.ACTION_VIEW,
                        Uri.parse("https://play.google.com/store/apps/details?id=" + context.packageName)
                    )
                )
            } catch (ignored: ActivityNotFoundException) {
            }
            return true
        }

        @JvmStatic
        fun cleanUpOldExecutables(
            prefs: SharedPreferences,
            state: SharedPreferences,
            dataDir: File?
        ) {
            if (state.getBoolean(PUZZLESGEN_CLEANUP_DONE, false)) {
                return
            }
            // We used to copy the executable to our dataDir and execute it. I don't remember why
            // executing directly from nativeLibraryDir didn't work, but it definitely does now.
            // Clean up any previously stashed executable in dataDir to save the user some space.
            for (toDelete in OBSOLETE_EXECUTABLES_IN_DATA_DIR) {
                try {
                    Log.d(TAG, "deleting obsolete file: $toDelete")
                    File(dataDir, toDelete).delete() // ok to fail
                } catch (ignored: SecurityException) {
                }
            }
            prefs.edit().remove(OLD_PUZZLESGEN_LAST_UPDATE).apply()
            state.edit().putBoolean(PUZZLESGEN_CLEANUP_DONE, true).apply()
        }
    }
}