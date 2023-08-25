package name.boyle.chris.sgtpuzzles;

import android.content.Context;
import android.graphics.Point;
import android.graphics.RectF;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import java.io.ByteArrayOutputStream;

import name.boyle.chris.sgtpuzzles.config.ConfigBuilder;
import name.boyle.chris.sgtpuzzles.config.CustomDialogBuilder;

public interface GameEngine {
    @UsedByJNI
    interface ActivityCallbacks {
        boolean allowFlash();
        void changedState(final boolean canUndo, final boolean canRedo);
        void completed();
        void inertiaFollow(final boolean isSolved);
        void purgingStates();
        void requestTimer(boolean on);
        void setStatus(final String status);
    }

    @UsedByJNI
    interface ViewCallbacks {
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

    void onDestroy();

    void configEvent(CustomDialogBuilder.ActivityCallbacks activityCallbacks, int whichEvent, Context context, BackendName backendName);
    void configEvent(int whichEvent, @NonNull ConfigBuilder builder);

    class KeysResult {
        private final String _keys;
        private final String _keysIfArrows;
        private final SmallKeyboard.ArrowMode _arrowMode;

        @UsedByJNI
        public KeysResult(final String keys, final String keysIfArrows, SmallKeyboard.ArrowMode arrowMode) {
            _keys = keys;
            _keysIfArrows = keysIfArrows;
            _arrowMode = arrowMode;
        }
        public String getKeys() { return _keys; }
        public String getKeysIfArrows() { return _keysIfArrows; }
        public SmallKeyboard.ArrowMode getArrowMode() { return _arrowMode; }
    }

    @Nullable KeysResult requestKeys(@NonNull BackendName backend, @Nullable String params);

    void timerTick();
    String htmlHelpTopic();
    void keyEvent(int x, int y, int k);
    void restartEvent();
    void solveEvent();
    void resizeEvent(int x, int y);
    void serialise(ByteArrayOutputStream baos);
    String getCurrentParams();
    void setCursorVisibility(boolean visible);
    MenuEntry[] getPresets();
    int getUIVisibility();
    void resetTimerBaseline();
    void purgeStates();
    boolean isCompletedNow();
    float[] getColours();
    float suggestDensity(int x, int y);
    RectF getCursorLocation();
    @VisibleForTesting Point getGameSizeInGameCoords();
    @VisibleForTesting void freezePartialRedo();

    GameEngine NOT_LOADED_YET = new GameEngine() {
        @Override public void onDestroy() {}
        @Override public void configEvent(CustomDialogBuilder.ActivityCallbacks activityCallbacks, int whichEvent, Context context, BackendName backendName) {}
        @Override public void configEvent(int whichEvent, @NonNull ConfigBuilder builder) {}
        @Nullable @Override public KeysResult requestKeys(@NonNull BackendName backend, @Nullable String params) { return null; }
        @Override public void timerTick() {}
        @Override public String htmlHelpTopic() { return null; }
        @Override public void keyEvent(int x, int y, int k) {}
        @Override public void restartEvent() {}
        @Override public void solveEvent() {}
        @Override public void resizeEvent(int x, int y) {}
        @Override public void serialise(ByteArrayOutputStream baos) {}
        @Override public String getCurrentParams() { return null; }
        @Override public void setCursorVisibility(boolean visible) {}
        @Override public MenuEntry[] getPresets() { return new MenuEntry[0]; }
        @Override public int getUIVisibility() { return 0; }
        @Override public void resetTimerBaseline() {}
        @Override public void purgeStates() {}
        @Override public boolean isCompletedNow() { return false; }
        @Override public float[] getColours() { return new float[0]; }
        @Override public float suggestDensity(int x, int y) { return 1.f; }
        @Override public RectF getCursorLocation() { return new RectF(0, 0, 1, 1); }
        @Override public Point getGameSizeInGameCoords() { return new Point(1, 1); }
        @Override public void freezePartialRedo() {}
    };
}
