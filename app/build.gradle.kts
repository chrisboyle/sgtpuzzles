import java.io.ByteArrayOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.TimeZone

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    `generate-backendname-objects`
}

fun timestamp(time: Boolean): String {
    val dateFormat = SimpleDateFormat(if (time) "HHmm" else "yyyy-MM-dd")
    dateFormat.timeZone = TimeZone.getTimeZone("UTC")
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
    compileSdk = 34
    defaultConfig {
        applicationId = "name.boyle.chris.sgtpuzzles"
        minSdk = 21
        targetSdk = 34
        versionCode = 138
        versionName = timestamp(false)

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        resValue("string", "issues_url", issuesURL())
    }

    externalNativeBuild {
        cmake {
            path = File("src/main/jni/CMakeLists.txt")
        }
    }

    buildFeatures {
        viewBinding = true
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.4.2"
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
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        jniLibs {
            // see GamePlay.startGameGenProcess(...): this is the easiest way to get puzzlesgen installed and found per-architecture
            useLegacyPackaging = true
        }
    }
}

androidComponents {
    // These .sav files are needed by GamePlayScreenshotsTest.launchTestGame(...)
    onVariants(selector().withBuildType("debug")) { variant ->
        variant.sources.assets?.addStaticSourceDirectory("src/main/jni/icons")
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.gridlayout:gridlayout:1.0.0")
    implementation("androidx.annotation:annotation:1.7.0")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("androidx.webkit:webkit:1.8.0")
    implementation(platform("androidx.compose:compose-bom:2023.09.02"))
    implementation("androidx.activity:activity-compose:1.8.0")
    implementation("androidx.compose.material3:material3:1.1.2")
    implementation("androidx.compose.ui:ui-tooling:1.5.3")
    implementation("org.jetbrains.kotlin:kotlin-reflect:1.9.10")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.mockito:mockito-core:5.5.0")
    androidTestImplementation("androidx.annotation:annotation:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test:rules:1.5.0")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
    androidTestImplementation("tools.fastlane:screengrab:2.1.1")
}
