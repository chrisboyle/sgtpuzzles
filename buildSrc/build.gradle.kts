plugins {
    kotlin("jvm") version "1.9.0"
}

repositories {
    google()
    mavenCentral()
    gradlePluginPortal()
}

dependencies {
    implementation("com.android.tools.build:gradle-api:8.1.0")
    implementation(kotlin("stdlib"))
    gradleApi()
}