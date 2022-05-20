-dontobfuscate
-keep @name.boyle.chris.sgtpuzzles.UsedByJNI class * {*;}
-keepclassmembers class * {
    @name.boyle.chris.sgtpuzzles.UsedByJNI <methods>;
}
-keepclasseswithmembers class * {
    @name.boyle.chris.sgtpuzzles.UsedByJNI <methods>;
}
-keepclasseswithmembers class * {
    @name.boyle.chris.sgtpuzzles.UsedByJNI <fields>;
}
-keepclasseswithmembers class * {
    @name.boyle.chris.sgtpuzzles.UsedByJNI <init>(...);
}
-printusage /tmp/usage.txt