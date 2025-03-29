plugins {
    `kotlin-dsl`
}

repositories {
    google()
    mavenCentral()
}

dependencies {
    implementation(kotlin("script-runtime"))
    implementation("com.android.tools.build:gradle-api:8.9.1")
}