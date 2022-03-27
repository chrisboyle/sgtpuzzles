package name.boyle.chris.sgtpuzzles;

import android.net.Uri;
import androidx.annotation.NonNull;
import androidx.core.content.FileProvider;

public class FixedTypeFileProvider extends FileProvider
{
	@Override
	public String getType(@NonNull Uri uri) {
		return uri.getPath().contains("bluetooth") ? "text/plain" : GamePlay.MIME_TYPE;
	}
}
