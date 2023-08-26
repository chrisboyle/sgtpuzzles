package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.graphics.Point;
import android.graphics.RectF;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.preference.PreferenceManager;

import java.io.ByteArrayOutputStream;

import name.boyle.chris.sgtpuzzles.config.ConfigBuilder;
import name.boyle.chris.sgtpuzzles.config.CustomDialogBuilder;

public class GameEngineImpl implements CustomDialogBuilder.EngineCallbacks, GameEngine {

    @UsedByJNI
    private final long _nativeFrontend;

    @NonNull
    private final BackendName _backend;

    @UsedByJNI
    private GameEngineImpl(final long nativeFrontend, @NonNull final BackendName backend) {
        _nativeFrontend = nativeFrontend;
        _backend = backend;
    }

    @NonNull
    public BackendName getBackend() {
        return _backend;
    }

    public native void onDestroy();

    public static GameEngine fromLaunch(final GameLaunch launch, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks, final Context contextForPrefs) {
        if (launch.getSaved() != null) {
            final String initialPrefs = getPrefs(contextForPrefs, identifyBackend(launch.getSaved()));
            return fromSavedGame(launch.getSaved(), activityCallbacks, viewCallbacks, initialPrefs);
        } else if (launch.getGameID() != null) {
            final String initialPrefs = getPrefs(contextForPrefs, launch.getWhichBackend());
            return fromGameID(launch.getGameID(), launch.getWhichBackend(), activityCallbacks, viewCallbacks, initialPrefs);
        } else {
            throw new IllegalArgumentException("GameEngine.fromLaunch without saved game or id");
        }
    }

    @NonNull private static native GameEngine fromSavedGame(final String savedGame, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks, @Nullable final String initialPrefs);
    @NonNull private static native GameEngine fromGameID(final String gameID, final BackendName backendName, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks, @Nullable final String initialPrefs);
    @NonNull static native BackendName identifyBackend(String savedGame);
    @NonNull static native String getDefaultParams(final BackendName backend);
    @NonNull static GameEngine forPreferencesOnly(@NonNull final BackendName backendName, @NonNull final Context context) {
        return forPreferencesOnly(backendName, getPrefs(context, backendName));
    }
    @NonNull static native GameEngine forPreferencesOnly(final BackendName backendName, final String initialPrefs);

    public void configEvent(@NonNull CustomDialogBuilder.ActivityCallbacks activityCallbacks, int whichEvent, @NonNull Context context, @NonNull BackendName backendName) {
        configEvent(whichEvent, new CustomDialogBuilder(context, this, activityCallbacks, whichEvent, backendName));
    }

    public native void configEvent(int whichEvent, @NonNull ConfigBuilder builder);

    @Override
    public void savePrefs(@NonNull final Context context) {
        final ByteArrayOutputStream baos = new ByteArrayOutputStream();
        serialisePrefs(baos);
        final String serialised = baos.toString();
        Log.d("Prefs", "Saving " + _backend.getPreferencesName() + ": \"" + serialised + "\"");
        PreferenceManager.getDefaultSharedPreferences(context).edit()
                .putString(_backend.getPreferencesName(), serialised)
                .apply();
    }

    @Override
    public void loadPrefs(@NonNull final Context context) {
        final String toLoad = getPrefs(context, _backend);
        if (toLoad != null)
            deserialisePrefs(toLoad);
    }

    @Nullable
    private static String getPrefs(@NonNull Context context, @NonNull final BackendName backend) {
        String toLoad = PreferenceManager.getDefaultSharedPreferences(context)
                .getString(backend.getPreferencesName(), null);
        if (toLoad != null) {
            // Work around Android bug https://issuetracker.google.com/issues/37032278
            // (incorrectly marked as obsolete even though it still applies on Android 13)
            // in which "foo\n" is read back as "foo\n    ". Remove just the spaces.
            toLoad = toLoad.replaceFirst(" +$", "");
        }
        Log.d("Prefs", "Loading " + backend.getPreferencesName() + ": \"" + toLoad + "\"");
        return toLoad;
    }

    private native void serialisePrefs(@NonNull ByteArrayOutputStream baos);
    private native void deserialisePrefs(@NonNull String prefs);
    @NonNull public native String configOK();
    @NonNull public native String getFullGameIDFromDialog();
    @NonNull public native String getFullSeedFromDialog();
    public native void configCancel();
    public native void configSetString(@NonNull String itemPtr, @NonNull String s, boolean isPrefs);
    public native void configSetBool(@NonNull String itemPtr, boolean selected, boolean isPrefs);
    public native void configSetChoice(@NonNull String itemPtr, int selected, boolean isPrefs);

    @Nullable public native GameEngine.KeysResult requestKeys(@NonNull BackendName backend, @Nullable String params);
    public native void timerTick();
    public native String htmlHelpTopic();
    public native void keyEvent(int x, int y, int k);
    public native void restartEvent();
    public native void solveEvent();
    public native void resizeEvent(int x, int y);
    public native void serialise(ByteArrayOutputStream baos);
    public native String getCurrentParams();
    public native void setCursorVisibility(boolean visible);
    public native MenuEntry[] getPresets();
    public native int getUIVisibility();
    public native void resetTimerBaseline();
    public native void purgeStates();
    public native boolean isCompletedNow();
    public native float[] getColours();
    public native float suggestDensity(int x, int y);
    public native RectF getCursorLocation();
    @VisibleForTesting public native Point getGameSizeInGameCoords();
    @VisibleForTesting public native void freezePartialRedo();
}
