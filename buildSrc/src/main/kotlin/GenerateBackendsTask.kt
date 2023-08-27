import org.gradle.api.DefaultTask
import org.gradle.api.file.DirectoryProperty
import org.gradle.api.file.RegularFile
import org.gradle.api.provider.Provider
import org.gradle.api.tasks.InputDirectory
import org.gradle.api.tasks.OutputDirectory
import org.gradle.api.tasks.TaskAction
import org.w3c.dom.NodeList
import java.util.Locale
import javax.xml.parsers.DocumentBuilderFactory

/**
 * Generates BackendName objects for each game. Since that's a sealed class, this allows enum-like
 * behaviour without keeping all of BackendName's methods here in a big string.
 *
 * Really this should use some CMake output instead of the regex hack, but the Android Gradle Plugin
 * wants to compile all Kotlin and Java before running any of the CMake.
 */
abstract class GenerateBackendsTask: DefaultTask()  {

    @get:InputDirectory
    abstract val jniDir: DirectoryProperty

    @get:InputDirectory
    abstract val resDir: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    // The title on the Action Bar in play is usually the display name provided from CMake but we
    // override this because "Train Tracks" is too wide for some screens
    private val titleOverrides = mapOf("tracks" to "Tracks")

    @TaskAction
    fun taskAction() {
        val definedNightColours = resourceNames(resDir.file("values/game_props.xml"), "color")
        val definedStrings = resourceNames(resDir.file("values/strings.xml"), "string")
        val definedKeyIcons = resDir.asFileTree.matching { include("drawable*/*sym_key*") }.map { it.nameWithoutExtension }.toSet()
        val cmakeText = jniDir.file("CMakeLists.txt").get().asFile.readText()
        val objectLines =
            Regex("""puzzle\(\s*(\w+)\s+DISPLAYNAME\s+"([^"]+)"""").findAll(cmakeText).map {
                generateObjectLine(
                    it.groupValues[1],
                    it.groupValues[2],
                    definedNightColours,
                    definedStrings,
                    definedKeyIcons
                )
            }
        with(outputs.files.singleFile.resolve("name/boyle/chris/sgtpuzzles/backend/BackendNames.kt")) {
            parentFile.mkdirs()
            delete()
            writeText(
                """package name.boyle.chris.sgtpuzzles.backend
                |
                |import name.boyle.chris.sgtpuzzles.R
                |
                |${objectLines.joinToString("")}
                |""".trimMargin()
            )
        }
    }

    private fun resourceNames(xml: Provider<RegularFile>, tagName: String): Set<String> {
        fun NodeList.toSequence() = (0 until length).asSequence().map { item(it) }
        return DocumentBuilderFactory.newInstance().newDocumentBuilder()
            .parse(xml.get().asFile).getElementsByTagName(tagName).toSequence()
            .mapNotNull { it.attributes?.getNamedItem("name")?.nodeValue }.toSet()
    }

    private fun generateObjectLine(
        puz: String,
        display: String,
        definedNightColours: Set<String>,
        definedStrings: Set<String>,
        definedKeyIcons: Set<String>
    ): String {
        val sourceText = jniDir.file("${puz}.c").get().asFile.readText()
        val colourNames = (Regex("""enum\s+\{\s*COL_[^,]+,\s*(COL_[^}]+)}""").find(sourceText)
            ?.run { parseEnumMembers(puz, groupValues[1]) } ?: listOf())
            .map { "${puz}_night_colour_${it}" }
            .map { if (it in definedNightColours) "R.color.${it}" else "0" }
        val colours = "arrayOf(${colourNames.joinToString()})"
        val objName = puz.uppercase(Locale.ROOT)
        val title = titleOverrides[puz] ?: display
        val (toast, toastNoArrows) = listOf("toast_", "toast_no_arrows_").map {
            if (it + puz in definedStrings) "R.string.${it}${puz}" else "0"
        }
        val keyPrefix = "${puz}_sym_key_"
        val keyIcons = definedKeyIcons.filter { it.startsWith(keyPrefix) }
            .map { "\"${it.removePrefix(keyPrefix)}\" to R.drawable.$it" }
        return """object $objName : BackendName(
            |    "$puz", "$display", "$title",
            |    R.drawable.$puz,
            |    R.string.desc_$puz, $toast, $toastNoArrows,
            |    mapOf(${keyIcons.joinToString()}),
            |    $colours
            |)
            |""".trimMargin()
    }

    private fun parseEnumMembers(puz: String, enumSource: String) = (enumSource
        .replace(Regex("""(?s)/\*.*?\*/"""), "")
        .replace(Regex("""#[^\n]*\n"""), "")
        .trim().split(",").map { it.trim().removePrefix("COL_").lowercase(Locale.ROOT) }
        .filter { Regex("""^[^=]+$""").containsMatchIn(it) }
        .minus(setOf("ncolours", "crossedline"))
        .plus(if (puz == "signpost") ("bmdx".flatMap { c -> (0..15).map { "$c$it" } }
            .drop(1)) else setOf()))
        .map { it.apply { if (contains(Regex("""[^a-z\d_]"""))) {
            throw Exception("Couldn't parse colours for $puz: $enumSource")
        }}}
}