package name.boyle.chris.sgtpuzzles;

import android.content.ActivityNotFoundException;
import android.content.Intent;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

public abstract class ActivityWithLoadButton extends AppCompatActivity {

    private final ActivityResultLauncher<String[]> loadLauncher = registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
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
