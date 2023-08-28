package name.boyle.chris.sgtpuzzles

import android.net.Uri
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mockito.Mockito

class FixedTypeFileProviderTest {
    @Test
    fun testGetType() {
        // We used to vary this to accommodate limitations of direct Bluetooth share, but not any more
        assertEquals(
            GamePlay.MIME_TYPE,
            FixedTypeFileProvider().getType(mockURI("file:///foo/bluetooth/foo.sav"))
        )
        assertEquals(
            GamePlay.MIME_TYPE,
            FixedTypeFileProvider().getType(mockURI("file:///foo/elsewhere/foo.sav"))
        )
    }

    private fun mockURI(s: String): Uri {
        val uri = Mockito.mock(Uri::class.java)
        Mockito.`when`(uri.path).thenReturn(s)
        return uri
    }
}