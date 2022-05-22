package name.boyle.chris.sgtpuzzles;

import android.content.ActivityNotFoundException;
import android.content.Intent;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;

public abstract class ActivityWithLoadButton extends NightModeHelper.ActivityWithNightMode {

    private final ActivityResultLauncher<String[]> loadLauncher = registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
        if (uri == null) return;
        final Intent intent = new Intent(Intent.ACTION_VIEW, uri, ActivityWithLoadButton.this, GamePlay.class);
        startActivity(intent);
        overridePendingTransition(0, 0);
    });

    protected void loadGame() {
        try {
            loadLauncher.launch(new String[]{"text/*", "application/octet-stream"});
        } catch (ActivityNotFoundException e) {
            Utils.unlikelyBug(this, R.string.saf_missing_short);
        }
    }
}
