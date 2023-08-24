package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.graphics.Point;
import android.graphics.RectF;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import java.io.ByteArrayOutputStream;

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

    public static GameEngine fromLaunch(final GameLaunch launch, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks) {
        if (launch.getSaved() != null) {
            return fromSavedGame(launch.getSaved(), activityCallbacks, viewCallbacks);
        } else if (launch.getGameID() != null) {
            return fromGameID(launch.getGameID(), launch.getWhichBackend(), activityCallbacks, viewCallbacks);
        } else {
            throw new IllegalArgumentException("GameEngine.fromLaunch without saved game or id");
        }
    }

    private static native GameEngine fromSavedGame(final String savedGame, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks);
    private static native GameEngine fromGameID(final String gameID, final BackendName backendName, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks);
    @NonNull static native BackendName identifyBackend(String savedGame);
    @NonNull static native String getDefaultParams(final BackendName backend);
    @NonNull static native GameEngine forPreferencesOnly(final BackendName backendName);

    public native void configEvent(CustomDialogBuilder.ActivityCallbacks activityCallbacks, int whichEvent, Context context, BackendName backendName);
    @NonNull public native String configOK();
    @NonNull public native String getFullGameIDFromDialog();
    @NonNull public native String getFullSeedFromDialog();
    public native void configCancel();
    public native void configSetString(String itemPtr, String s);
    public native void configSetBool(String itemPtr, int selected);
    public native void configSetChoice(String itemPtr, int selected);

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
