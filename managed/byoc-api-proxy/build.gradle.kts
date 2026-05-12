import org.gradle.api.tasks.testing.logging.TestLogEvent

plugins {
  java
  id("org.springframework.boot") version "4.0.2"
  id("io.spring.dependency-management") version "1.1.7"
  id("com.diffplug.spotless") version "6.19.0"
  id("com.google.cloud.artifactregistry.gradle-plugin") version "2.2.5"
}

group = "com.yugabyte"
version = "0.0.1-SNAPSHOT"
description = "API proxy for BYOC"

configurations {
  compileOnly {
    extendsFrom(configurations.annotationProcessor.get())
  }
}

java {
  toolchain {
    languageVersion = JavaLanguageVersion.of(17)
  }
}

repositories {
  mavenCentral()
  mavenLocal()
  maven {
    url = uri("artifactregistry://us-west1-maven.pkg.dev/yugabyte-cloud-dev/aeon-java-clients")
  }
}

extra["lombokVersion"] = "1.18.30"

dependencies {
  implementation("org.springframework.boot:spring-boot-starter")
  implementation("org.springframework.boot:spring-boot-starter-validation")
  implementation("com.yugabyte:aeon-openapi-client:1.0.0:shaded")
  implementation("net.logstash.logback:logstash-logback-encoder:7.3")
  implementation("org.codehaus.janino:janino:3.1.9")
  compileOnly("org.projectlombok:lombok:${property("lombokVersion")}")
  annotationProcessor("org.projectlombok:lombok:${property("lombokVersion")}")
  annotationProcessor("org.springframework.boot:spring-boot-configuration-processor")
  testImplementation("org.springframework.boot:spring-boot-starter-test")
  testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

tasks.withType<Test> {
  outputs.upToDateWhen { false }
  useJUnitPlatform()
  testLogging {
    events(TestLogEvent.PASSED, TestLogEvent.SKIPPED, TestLogEvent.FAILED)
    showStandardStreams = false
  }
}

spotless {
  java {
    removeUnusedImports()
    googleJavaFormat()
    target("src/**/*.java")
    importOrder("\\#","")
  }
}
