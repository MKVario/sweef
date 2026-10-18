plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Versionsnummer kommt beim Bauen auf GitHub aus der Laufnummer
val laufNr = (System.getenv("GITHUB_RUN_NUMBER") ?: "1").toInt()

android {
    namespace = "de.sweef.vario"
    compileSdk = 34

    defaultConfig {
        applicationId = "de.sweef.vario"
        minSdk = 26
        targetSdk = 34
        versionCode = laufNr
        versionName = "1.0.$laufNr"
    }

    signingConfigs {
        create("sweef") {
            storeFile = file("../signatur/sweef.jks")
            storePassword = System.getenv("SWEEF_KEYSTORE_PW") ?: ""
            keyAlias = "sweef"
            keyPassword = System.getenv("SWEEF_KEYSTORE_PW") ?: ""
        }
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("sweef")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.webkit:webkit:1.11.0")
    implementation("no.nordicsemi.android:dfu:2.5.0")
}
