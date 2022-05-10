package name.boyle.chris.sgtpuzzles;

import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Assume;

public class TestUtils {
    private TestUtils() {}

    public static String getFastlaneDeviceTypeOrSkipTest() {
        final String deviceType = InstrumentationRegistry.getArguments().getString("device_type");
        Assume.assumeNotNull(deviceType);  // only available under fastlane
        return deviceType;
    }
}
