package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.graphics.Point;
import android.graphics.RectF;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import java.io.ByteArrayOutputStream;

public class GameEngine implements CustomDialogBuilder.EngineCallbacks {

    public interface ActivityCallbacks {
        boolean allowFlash();
        void changedState(final boolean canUndo, final boolean canRedo);
        void completed();
        //String gettext(String s);
        void inertiaFollow(final boolean isSolved);
        void purgingStates();
        void requestTimer(boolean on);
        void setStatus(final String status);
    }

    public interface ViewCallbacks {
        int blitterAlloc(int w, int h);
        void blitterFree(int i);
        void blitterLoad(int i, int x, int y);
        void blitterSave(int i, int x, int y);
        void clipRect(int x, int y, int w, int h);
        void drawCircle(float thickness, float x, float y, float r, int lineColour, int fillColour);
        void drawLine(float thickness, float x1, float y1, float x2, float y2, int colour);
        void drawPoly(float thickness, int[] points, int ox, int oy, int line, int fill);
        void drawText(int x, int y, int flags, int size, int colour, String text);
        void fillRect(final int x, final int y, final int w, final int h, final int colour);
        int getDefaultBackgroundColour();
        void postInvalidateOnAnimation();
        void unClip(int marginX, int marginY);
    }

    @UsedByJNI
    private final long _nativeFrontend;

    @UsedByJNI
    private GameEngine(final long nativeFrontend) {
        _nativeFrontend = nativeFrontend;
    }

    public native void onDestroy();

    public static native GameEngine fromSavedGame(final String savedGame, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks);
    public static native GameEngine fromGameID(final String gameID, final BackendName backendName, final ActivityCallbacks activityCallbacks, final ViewCallbacks viewCallbacks);
    @NonNull static native BackendName identifyBackend(String savedGame);

    public native void configEvent(CustomDialogBuilder.ActivityCallbacks activityCallbacks, int whichEvent, Context context, BackendName backendName);
    public native String configOK();
    public native String getFullGameIDFromDialog();
    public native String getFullSeedFromDialog();
    public native void configCancel();
    public native void configSetString(String item_ptr, String s);
    public native void configSetBool(String item_ptr, int selected);
    public native void configSetChoice(String item_ptr, int selected);

    public static class KeysResult {
        private final String _keys;
        private final String _keysIfArrows;
        private final SmallKeyboard.ArrowMode _arrowMode;

        public KeysResult(final String keys, final String keysIfArrows, SmallKeyboard.ArrowMode arrowMode) {
            _keys = keys;
            _keysIfArrows = keysIfArrows;
            _arrowMode = arrowMode;
        }
        public String getKeys() { return _keys; }
        public String getKeysIfArrows() { return _keysIfArrows; }
        public SmallKeyboard.ArrowMode getArrowMode() { return _arrowMode; }
    }

    @Nullable native KeysResult requestKeys(@NonNull BackendName backend, @Nullable String params);

    native void timerTick();
    native String htmlHelpTopic();
    native void keyEvent(int x, int y, int k);
    native void restartEvent();
    native void solveEvent();
    native void resizeEvent(int x, int y);
    native void serialise(ByteArrayOutputStream baos);
    native String getCurrentParams();
    native void setCursorVisibility(boolean visible);
    native MenuEntry[] getPresets();
    native int getUIVisibility();
    native void resetTimerBaseline();
    native void purgeStates();
    native boolean isCompletedNow();
    native float[] getColours();
    native float suggestDensity(int x, int y);
    native RectF getCursorLocation();
    @VisibleForTesting native Point getGameSizeInGameCoords();
    @VisibleForTesting native void freezePartialRedo();
}
