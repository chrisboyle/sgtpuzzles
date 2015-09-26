package name.boyle.chris.sgtpuzzles;

import android.net.Uri;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class FixedTypeFileProviderTest {

	@Test
	public void testGetType() throws Exception {
		assertEquals("text/plain", new FixedTypeFileProvider().getType(mockURI("file:///foo/bluetooth/foo.sav")));
		assertEquals(GamePlay.MIME_TYPE, new FixedTypeFileProvider().getType(mockURI("file:///foo/elsewhere/foo.sav")));
	}

	private Uri mockURI(String s) {
		final Uri uri = mock(Uri.class);
		when(uri.getPath()).thenReturn(s);
		return uri;
	}
}