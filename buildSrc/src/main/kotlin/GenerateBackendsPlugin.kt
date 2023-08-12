import org.gradle.api.Plugin
import org.gradle.api.Project
import com.android.build.api.variant.AndroidComponentsExtension

class GenerateBackendsPlugin: Plugin<Project> {

    override fun apply(project: Project) {
        val androidComponents = project.extensions.getByType(AndroidComponentsExtension::class.java)
        androidComponents.onVariants { variant ->
            val taskProvider = project.tasks.register(
                "${variant.name}GenerateBackendsEnum",
                GenerateBackendsTask::class.java
            ) {
                val appDir = project.layout.projectDirectory
                jniDir.set(appDir.dir("src/main/jni"))
                colourResources.set(appDir.file("src/main/res/values/game_props.xml"))
            }
            variant.sources.java?.addGeneratedSourceDirectory(
                taskProvider,
                GenerateBackendsTask::outputDir
            )
        }
    }
}