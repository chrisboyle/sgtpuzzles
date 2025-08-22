// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    id("com.android.application") version "8.12.1" apply false
    // Can't use 2.2.10 yet because:
    // Cause: loader constraint violation in interface itable initialization for class com.android.build.gradle.internal.api.VariantFilter: when selecting method 'com.android.builder.model.ProductFlavor com.android.build.api.variant.VariantFilter.getDefaultConfig()' the class loader org.gradle.internal.classloader.VisitableURLClassLoader$InstrumentingVisitableURLClassLoader @34f9e937 for super interface com.android.build.api.variant.VariantFilter, and the class loader org.gradle.internal.classloader.VisitableURLClassLoader$InstrumentingVisitableURLClassLoader @67cc5c15 of the selected method's class, com.android.build.gradle.internal.api.VariantFilter have different Class objects for the type com.android.builder.model.ProductFlavor used in the signature (com.android.build.api.variant.VariantFilter is in unnamed module of loader org.gradle.internal.classloader.VisitableURLClassLoader$InstrumentingVisitableURLClassLoader @34f9e937, parent loader org.gradle.internal.classloader.VisitableURLClassLoader$InstrumentingVisitableURLClassLoader @6f0d277a; com.android.build.gradle.internal.api.VariantFilter is in unnamed module of loader org.gradle.internal.classloader.VisitableURLClassLoader$InstrumentingVisitableURLClassLoader @67cc5c15, parent loader org.gradle.internal.classloader.VisitableURLClassLoader$InstrumentingVisitableURLClassLoader @34f9e937)
    id("org.jetbrains.kotlin.android") version "2.2.0" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.2.0" apply false
}

allprojects {
    gradle.projectsEvaluated {
        tasks.withType<JavaCompile> {
            options.compilerArgs.addAll(listOf("-Xlint:unchecked", "-Xlint:deprecation"))
        }
    }
}
