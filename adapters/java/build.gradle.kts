plugins {
    java
    application
}

repositories {
    mavenCentral()
}

dependencies {
    // Distinct approach from the Kotlin adapter (which hand-rolls JSON
    // parsing via org.json's dynamic JSONObject/JSONArray, then hand-rolls
    // CBOR itself): this adapter uses Jackson's typed tree model
    // (JsonNode/ObjectMapper) for the stdin JSON layer, with
    // USE_BIG_INTEGER_FOR_INTS so large `tag` values parse straight to
    // BigInteger without a lossy 64-bit intermediate. See README.md for the
    // real CBOR-library investigation (com.upokecenter:cbor,
    // jackson-dataformat-cbor) and why the actual CBOR encode/decode engine
    // below is still hand-rolled.
    implementation("com.fasterxml.jackson.core:jackson-databind:2.17.2")
    testImplementation(platform("org.junit:junit-bom:5.10.2"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

application {
    mainClass.set("adapter.Main")
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(21))
    }
}

tasks.test {
    useJUnitPlatform()
}
