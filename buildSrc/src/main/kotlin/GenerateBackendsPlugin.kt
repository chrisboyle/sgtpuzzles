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
                jniDir.set(project.layout.projectDirectory.dir("src/main/jni"))
                outputFolder.set(project.layout.buildDirectory)
            }
            variant.sources.java?.addGeneratedSourceDirectory(
                taskProvider,
                GenerateBackendsTask::outputFolder
            )
        }
    }
}