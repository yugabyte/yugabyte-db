plugins {
  java
  id("org.springframework.boot") version "4.0.2"
  id("io.spring.dependency-management") version "1.1.7"
  id("com.diffplug.spotless") version "6.19.0"
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
}

extra["lombokVersion"] = "1.18.30"

dependencies {
  implementation("org.springframework.boot:spring-boot-starter")
//  TODO pick this up from a repo as a published artifact
//  implementation("org.openapitools:openapi-java-client:0.1.0-SNAPSHOT")
  compileOnly("org.projectlombok:lombok:${property("lombokVersion")}")
  annotationProcessor("org.projectlombok:lombok:${property("lombokVersion")}")
  annotationProcessor("org.springframework.boot:spring-boot-configuration-processor")
  testImplementation("org.springframework.boot:spring-boot-starter-test")
  testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

tasks.withType<Test> {
  useJUnitPlatform()
}

spotless {
  java {
    removeUnusedImports()
    googleJavaFormat()
    target("src/**/*.java")
    importOrder("\\#","")
  }
}
