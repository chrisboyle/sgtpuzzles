import java.io.ByteArrayOutputStream
import java.text.SimpleDateFormat
import java.util.TimeZone
import java.util.Date
import java.util.Locale

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

fun timestamp(time: Boolean): String {
    val dateFormat = SimpleDateFormat(if (time) "HHmm" else "yyyy-MM-dd")
    dateFormat.setTimeZone(TimeZone.getTimeZone("UTC"))
    return dateFormat.format(Date())
}

fun idForSimon(): String {
    try {
        val commit = ByteArrayOutputStream()
        exec {
            // Require remote called simon because someone downstream might call my branch "upstream"
            commandLine("git", "merge-base", "simon/main", "main")
            standardOutput = commit
        }
        val shortUnique = ByteArrayOutputStream()
        exec {
            commandLine("git", "rev-parse", "--short", commit.toString().trim())
            standardOutput = shortUnique
        }
        return shortUnique.toString().trim()
    } catch (ignored: Exception) {
        return "UNOFFICIAL"
    }
}

fun issuesURL(): String {
    val gitRemote = ByteArrayOutputStream()
    exec {
        commandLine("git", "ls-remote", "--get-url", "origin")
        standardOutput = gitRemote
    }
    return gitRemote.toString().trim().replaceFirst("\\.git\$", "") + "/issues"
}

android {
    namespace = "name.boyle.chris.sgtpuzzles"
    compileSdk = 33
    defaultConfig {
        applicationId = "name.boyle.chris.sgtpuzzles"
        minSdk = 19
        targetSdk = 33
        versionCode = 138
        versionName = timestamp(false)

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        resValue("string", "issues_url", issuesURL())
    }

    sourceSets {
        getByName("androidTest").assets.srcDirs(File(project.buildDir, "testGames"))
    }

    externalNativeBuild {
        cmake {
            path = File("src/main/jni/CMakeLists.txt")
        }
    }

    buildFeatures {
        viewBinding = true
    }

    buildTypes {
        debug {
            versionNameSuffix = "-DEBUG-${idForSimon()}"
            ndk {
                isDebuggable = true
                debugSymbolLevel = "FULL"
            }
        }
        release {
            versionNameSuffix = "-${timestamp(true)}-${idForSimon()}"
            isMinifyEnabled = true
            ndk {
                isDebuggable = false
                debugSymbolLevel = "FULL"
            }
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    packaging {
        jniLibs {
            // see GamePlay.startGameGenProcess(...): this is the easiest way to get puzzlesgen installed and found per-architecture
            useLegacyPackaging = true
        }
    }
}

/**
 * Generates a Java enum of games.
 *
 * Really this should use some CMake output instead of the regex hack, but the Android Gradle Plugin
 * wants to compile the Java before running any of the CMake.
 */
abstract class GenerateBackendsEnum: DefaultTask()  {

    @get:OutputDirectory
    abstract val outputFolder: DirectoryProperty

    @get:InputDirectory
    abstract val jniDir: DirectoryProperty

    @TaskAction
    fun taskAction() {
        val cmakeContent = File(
            jniDir.asFile.get(), "CMakeLists.txt"
        ).readText(Charsets.UTF_8)
        val puzzleDeclarations =
            """puzzle\(\s*(\w+)\s+DISPLAYNAME\s+"([^"]+)"""".toRegex().findAll(
                cmakeContent
            )
        val backends: List<List<String>> = puzzleDeclarations.map { m ->
            val puz = m.groups[1]!!.value
            val display = m.groups[2]!!.value
            val text = File(jniDir.asFile.get(), "${puz}.c").readText(Charsets.UTF_8)
            val match = """enum\s+\{\s*COL_[^,]+,\s*(COL_[^}]+)}""".toRegex().find(text)
            var colours = listOf<String>()
            match?.let {
                val colourStr: String = it.groups[1]!!.value
                colours = colourStr.replace("""(?s)\/\*.*?\*\/""".toRegex(), "")
                    .replace("""#[^\n]*\n""".toRegex(), "")
                    .trim().split(",").map {
                        it.trim().replaceFirst("""^COL_""".toRegex(), "").lowercase(Locale.ROOT)
                    }
                    .filter { member: String ->
                        """^[^=]+$""".toRegex().containsMatchIn(member)
                    } - setOf("ncolours", "crossedline")
                if (colours.any { """[^a-z\d_]""".toRegex().containsMatchIn(it) }) {
                    throw Exception("Couldn't parse colours for " + puz + ": " + it.groups[1] + " -> " + colours)
                }
            }
            listOf(puz, display, ("new String[]{\"" + colours.joinToString("\", \"") + "\"}"))
        }.toList()
        val out =
            File("${outputs.files.singleFile}/name/boyle/chris/sgtpuzzles/BackendName.java")
        out.parentFile.mkdirs()
        out.delete()
        out.writeText(
            """package name.boyle.chris.sgtpuzzles;
                |import java.util.Collections;
                |import java.util.LinkedHashMap;
                |import java.util.Locale;
                |import java.util.Map;
                |import java.util.Objects;
                |import android.content.Context;
                |import android.graphics.drawable.Drawable;
                |import androidx.annotation.DrawableRes;
                |import androidx.annotation.StringRes;
                |import androidx.annotation.NonNull;
                |import androidx.annotation.Nullable;
                |import androidx.core.content.ContextCompat;
                |
                |/** Names of all the backends. Automatically generated file, do not modify. */
                |public enum BackendName {
                |    ${
                backends.map { "${it[0].uppercase(Locale.ROOT)}(\"${it[1]}\", R.drawable.${it[0]}, R.string.desc_${it[0]}, ${it[2]})" }
                    .joinToString(",\n    ")
            };
                |    private final String _displayName;
                |    private final String[] _colours;
                |    @DrawableRes private final int _icon;
                |    @StringRes private final int _description;
                |    private BackendName(@NonNull final String displayName, @DrawableRes final int icon, @StringRes final int description, @NonNull final String[] colours) { _displayName = displayName; _icon = icon; _description = description; _colours = colours; }
                |    @NonNull public String getDisplayName() { return _displayName; }
                |    @NonNull public Drawable getIcon(final Context context) { return Objects.requireNonNull(ContextCompat.getDrawable(context, _icon)); }
                |    @StringRes public int getDescription() { return _description; }
                |    @NonNull public String[] getColours() { return _colours; }
                |    @NonNull public String toString() { return name().toLowerCase(Locale.ROOT); }
                |    public boolean isLatin() { return this == KEEN || this == SOLO || this == TOWERS || this == UNEQUAL; }
                |    private static final Map<String, BackendName> BY_DISPLAY_NAME, BY_LOWERCASE;
                |    static {
                |        final Map<String, BackendName> byDisp = new LinkedHashMap<String, BackendName>(), byLower = new LinkedHashMap<String, BackendName>();
                |        for (final BackendName bn : values()) { byDisp.put(bn.getDisplayName(), bn); byLower.put(bn.toString(), bn); }  // no streams until API 24
                |        BY_DISPLAY_NAME = Collections.unmodifiableMap(byDisp);
                |        BY_LOWERCASE = Collections.unmodifiableMap(byLower);
                |    }
                |    @UsedByJNI @Nullable public static BackendName byDisplayName(final String displayName) {
                |        return BY_DISPLAY_NAME.get(displayName);
                |    }
                |    @UsedByJNI @Nullable public static BackendName byLowerCase(final String lowerCase) {
                |        return BY_LOWERCASE.get(lowerCase);
                |    }
                |};
                |""".trimMargin()
        )
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.java?.addGeneratedSourceDirectory(
            project.tasks.register<GenerateBackendsEnum>("${variant.name}GenerateBackendsEnum") {
                jniDir.set(File("src/main/jni"))
                outputFolder.set(project.layout.buildDirectory.dir("generated/backendsEnum/${variant.name}"))
            },
            GenerateBackendsEnum::outputFolder
        )
    }
    // FIXME: these .sav files get as far as app/build/intermediates/assets/debug but not into the APK??
    // They are needed by GamePlayScreenshotsTest.launchTestGame(...)
    onVariants(selector().withBuildType("debug")) { variant ->
        variant.sources.assets?.addStaticSourceDirectory("src/main/jni/icons")
    }
}

dependencies {
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.gridlayout:gridlayout:1.0.0")
    implementation("androidx.annotation:annotation:1.6.0")
    implementation("androidx.preference:preference:1.2.1")
    implementation("androidx.webkit:webkit:1.7.0")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.mockito:mockito-core:4.5.1")
    androidTestImplementation("androidx.annotation:annotation:1.6.0")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test:rules:1.5.0")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
    androidTestImplementation("tools.fastlane:screengrab:2.1.1")
}
