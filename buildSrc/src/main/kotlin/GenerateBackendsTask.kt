import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import java.io.File
import java.util.Locale

/**
 * Generates a Java enum of games.
 *
 * Really this should use some CMake output instead of the regex hack, but the Android Gradle Plugin
 * wants to compile the Java before running any of the CMake.
 */
abstract class GenerateBackendsTask: DefaultTask()  {

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
            Regex("""puzzle\(\s*(\w+)\s+DISPLAYNAME\s+"([^"]+)"""").findAll(
                cmakeContent
            )
        val backends: List<List<String>> = puzzleDeclarations.map { decl ->
            val (puz, display) = decl.destructured
            val text = File(jniDir.asFile.get(), "${puz}.c").readText(Charsets.UTF_8)
            val enumMatch = Regex("""enum\s+\{\s*COL_[^,]+,\s*(COL_[^}]+)}""").find(text)
            var colours = listOf<String>()
            enumMatch?.let { em ->
                val (colourStr) = em.destructured
                colours = colourStr.replace(Regex("""(?s)\/\*.*?\*\/"""), "")
                    .replace(Regex("""#[^\n]*\n"""), "")
                    .trim().split(",").map {
                        it.trim().replaceFirst(Regex("""^COL_"""), "").lowercase(Locale.ROOT)
                    }
                    .filter { member: String ->
                        Regex("""^[^=]+$""").containsMatchIn(member)
                    } - setOf("ncolours", "crossedline")
                if (colours.any { Regex("""[^a-z\d_]""").containsMatchIn(it) }) {
                    throw Exception("Couldn't parse colours for $puz: $colourStr -> $colours")
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
                |    ${backends.joinToString(",\n    ") { "${it[0].uppercase(Locale.ROOT)}(\"${it[1]}\", R.drawable.${it[0]}, R.string.desc_${it[0]}, ${it[2]})" }};
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