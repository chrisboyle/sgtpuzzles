package name.boyle.chris.sgtpuzzles;

import android.net.Uri;
import android.support.v4.content.FileProvider;

public class FixedTypeFileProvider extends FileProvider
{
	@Override
	public String getType(Uri uri) {
		return uri.getPath().contains("bluetooth") ? "text/plain" : GamePlay.MIME_TYPE;
	}
}
