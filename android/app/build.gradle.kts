plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Tisma's deps/ built for Android (see android/README.md): -PtismaDepsDir=... or TISMA_ANDROID_DEPS. Without them the
// app is built without the slicing core (the screens say so), which is enough to work on the interface.
val tismaDepsDir: String = (project.findProperty("tismaDepsDir") as String?) ?: System.getenv("TISMA_ANDROID_DEPS") ?: ""
val withCore = tismaDepsDir.isNotEmpty() && file(tismaDepsDir).isDirectory

android {
    namespace = "uy.com.tisma.app"
    compileSdk = 35
    ndkVersion = "27.3.13750724"

    defaultConfig {
        applicationId = "uy.com.tisma.app"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            // The deps are built for 64-bit ARM (almost every phone since 2017).
            abiFilters += "arm64-v8a"
        }
        if (withCore) {
            externalNativeBuild {
                cmake {
                    arguments += listOf(
                        "-DANDROID_STL=c++_shared",
                        "-DTISMA_DEPS_DIR=$tismaDepsDir",
                    )
                }
            }
        }
    }

    if (withCore) {
        externalNativeBuild {
            cmake {
                path = file("src/main/cpp/CMakeLists.txt")
                version = "3.31.6"
            }
        }
    }

    buildTypes {
        // The core is slow without optimizations: the native part is always built optimized.
        debug {
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")
    debugImplementation("androidx.compose.ui:ui-tooling")
    testImplementation("junit:junit:4.13.2")
    // The JVM tests run without Android: org.json of android.jar is only a stub there.
    testImplementation("org.json:json:20240303")
}
