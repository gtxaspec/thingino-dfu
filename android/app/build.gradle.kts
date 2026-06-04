plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Derive versionName from the repo's latest git tag - the same source the web
// build uses (CMake `git describe --tags`) - so the app and web flasher match.
// Falls back to a default when no tags are present (e.g. shallow CI checkout).
val computedVersionName: String = run {
    try {
        val p = ProcessBuilder("git", "describe", "--tags", "--abbrev=0")
            .directory(rootProject.projectDir)
            .start()
        val out = p.inputStream.bufferedReader().readText().trim()
        val code = p.waitFor()
        // Only trust a clean exit AND a version-shaped string. Do NOT capture
        // stderr - otherwise git's "fatal: no names found" (tagless/shallow
        // checkout) would become the version. Mirrors the web build's fallback.
        if (code == 0 && out.matches(Regex("^v?[0-9].*"))) out.removePrefix("v") else "0.0.0"
    } catch (e: Exception) {
        "0.0.0"
    }
}

android {
    namespace = "com.thingino.dfu"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.thingino.dfu"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = computedVersionName

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        externalNativeBuild {
            cmake {
                arguments("-DANDROID_STL=none")
                cFlags("-DANDROID")
            }
        }
    }

    signingConfigs {
        create("release") {
            storeFile = file("../thingino-release.jks")
            storePassword = System.getenv("KEYSTORE_PASSWORD") ?: "thingino-dfu"
            keyAlias = "thingino"
            keyPassword = System.getenv("KEY_PASSWORD") ?: "thingino-dfu"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
            keepDebugSymbols += listOf("**/*.so")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // Follow symlinks when packaging assets
    androidResources {
        noCompress += listOf("bin")
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("src/main/assets")
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}
