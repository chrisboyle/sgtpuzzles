-dontobfuscate
-keep @name.boyle.chris.sgtpuzzles.backend.UsedByJNI class * {*;}
-keepclassmembers class * {
    @name.boyle.chris.sgtpuzzles.backend.UsedByJNI <methods>;
}
-keepclasseswithmembers class * {
    @name.boyle.chris.sgtpuzzles.backend.UsedByJNI <methods>;
}
-keepclasseswithmembers class * {
    @name.boyle.chris.sgtpuzzles.backend.UsedByJNI <fields>;
}
-keepclasseswithmembers class * {
    @name.boyle.chris.sgtpuzzles.backend.UsedByJNI <init>(...);
}
-printusage /tmp/usage.txt