package name.boyle.chris.sgtpuzzles

import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assume.assumeNotNull

object TestUtils {
    val fastlaneDeviceTypeOrSkipTest: String
        get() {
            val deviceType = InstrumentationRegistry.getArguments().getString("device_type")
            assumeNotNull(deviceType) // only available under fastlane
            return deviceType!!
        }
}