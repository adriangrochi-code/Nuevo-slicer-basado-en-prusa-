import javax.inject.Inject

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

/**
 * The catalog of the printer picker (assets/printers.json), made from Tisma's vendor bundles by
 * android/tools/printer_catalog.py, so that the app always has the printers of the profiles it is built with.
 */
abstract class PrinterCatalogTask : DefaultTask() {
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val profiles: DirectoryProperty

    @get:InputFiles
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val scripts: ConfigurableFileCollection

    @get:Input
    abstract val python: Property<String>

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @get:Inject
    abstract val exec: ExecOperations

    @TaskAction
    fun generate() {
        // Only the catalog in the folder (it becomes the assets of the app).
        outputDir.get().asFile.listFiles()?.forEach { it.deleteRecursively() }
        exec.exec {
            commandLine(python.get(), scripts.files.first { it.name == "printer_catalog.py" }.absolutePath,
                profiles.get().asFile.absolutePath, outputDir.file("printers.json").get().asFile.absolutePath)
        }
    }
}

val printerCatalog = tasks.register<PrinterCatalogTask>("printerCatalog") {
    profiles.set(rootProject.layout.projectDirectory.dir("../resources/profiles"))
    scripts.from(rootProject.file("tools/printer_catalog.py"), rootProject.file("../tools/profiles/validate_bundles.py"))
    // Python 3: -PtismaPython=... or TISMA_PYTHON, else "python" on Windows and "python3" elsewhere.
    python.set((project.findProperty("tismaPython") as String?) ?: System.getenv("TISMA_PYTHON")
        ?: if (System.getProperty("os.name").startsWith("Windows")) "python" else "python3")
    outputDir.set(layout.buildDirectory.dir("generated/printerCatalog"))
}

/** The CA certificates of the Bambu Lab printers (resources/cert, as the desktop program) as an asset of the app. */
abstract class BambuCaTask : DefaultTask() {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val source: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        val dir = outputDir.get().asFile
        dir.listFiles()?.forEach { it.deleteRecursively() }
        source.get().asFile.copyTo(dir.resolve("bambu_printer_ca.pem"), overwrite = true)
    }
}

val bambuCa = tasks.register<BambuCaTask>("bambuCa") {
    source.set(rootProject.layout.projectDirectory.file("../resources/cert/bambu_printer_ca.pem"))
    outputDir.set(layout.buildDirectory.dir("generated/bambuCa"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(printerCatalog, PrinterCatalogTask::outputDir)
        variant.sources.assets?.addGeneratedSourceDirectory(bambuCa, BambuCaTask::outputDir)
    }
}

// The JVM tests also read the catalog of the real profiles (PrinterCatalogTest).
tasks.withType<Test>().configureEach {
    dependsOn(printerCatalog)
    // The output folder is set by the Android plugin when it adds it to the assets: read it when the tests run.
    doFirst { systemProperty("tisma.printerCatalog", printerCatalog.get().outputDir.file("printers.json").get().asFile.absolutePath) }
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
                    // Only the library of the app, not the desktop programs of Tisma's tree.
                    targets += "tisma"
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
    // The TLS of Android (BoringSSL) for the tests of the printer connections.
    testImplementation("org.conscrypt:conscrypt-openjdk-uber:2.5.2")
}
