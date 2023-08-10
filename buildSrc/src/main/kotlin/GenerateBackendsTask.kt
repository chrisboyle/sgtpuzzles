import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import java.util.Locale

/**
 * Generates BackendName objects for each game. Since that's a sealed class, this allows enum-like
 * behaviour without keeping all of BackendName's methods here in a big string.
 *
 * Really this should use some CMake output instead of the regex hack, but the Android Gradle Plugin
 * wants to compile all Kotlin and Java before running any of the CMake.
 */
abstract class GenerateBackendsTask: DefaultTask()  {

    @get:OutputDirectory
    abstract val outputFolder: DirectoryProperty

    @get:InputDirectory
    abstract val jniDir: DirectoryProperty

    @TaskAction
    fun taskAction() {
        val cmakeContent = jniDir.file("CMakeLists.txt").get().asFile.readText(Charsets.UTF_8)
        val puzzleDeclarations =
            Regex("""puzzle\(\s*(\w+)\s+DISPLAYNAME\s+"([^"]+)"""").findAll(
                cmakeContent
            )
        val backends = puzzleDeclarations.map { decl ->
            val (puz, display) = decl.destructured
            val text = jniDir.file("${puz}.c").get().asFile.readText(Charsets.UTF_8)
            val enumMatch = Regex("""enum\s+\{\s*COL_[^,]+,\s*(COL_[^}]+)}""").find(text)
            var colours = listOf<String>()
            enumMatch?.let { em ->
                val (colourStr) = em.destructured
                colours = colourStr.replace(Regex("""(?s)/\*.*?\*/"""), "")
                    .replace(Regex("""#[^\n]*\n"""), "")
                    .trim().split(",").map {
                        it.trim().removePrefix("COL_").lowercase(Locale.ROOT)
                    }
                    .filter { Regex("""^[^=]+$""").containsMatchIn(it) } - setOf("ncolours", "crossedline")
                if (colours.any { Regex("""[^a-z\d_]""").containsMatchIn(it) }) {
                    throw Exception("Couldn't parse colours for $puz: $colourStr -> $colours")
                }
            }
            val colourSet = "setOf(${colours.joinToString(", ") {"\"${it}\""}})"
            "object ${puz.uppercase(Locale.ROOT)}: BackendName(\"${puz}\", \"${display}\", R.drawable.${puz}, R.string.desc_${puz}, ${colourSet})\n"
        }
        val out = outputs.files.singleFile.resolve("name/boyle/chris/sgtpuzzles/BackendNames.kt")
        out.parentFile.mkdirs()
        out.delete()
        out.writeText(
            "package name.boyle.chris.sgtpuzzles\n\n${backends.joinToString("")}\n"
        )
    }
}