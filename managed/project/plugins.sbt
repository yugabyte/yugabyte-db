useCoursier := false

val jacksonVersion         = "2.15.3"

val jacksonOverrides = Seq(
  "com.fasterxml.jackson.core"       % "jackson-core",
  "com.fasterxml.jackson.core"       % "jackson-annotations",
  "com.fasterxml.jackson.core"       % "jackson-databind",
  "com.fasterxml.jackson.datatype"   % "jackson-datatype-jdk8",
  "com.fasterxml.jackson.datatype"   % "jackson-datatype-jsr310",
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-cbor",
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-xml",
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-yaml",
  "com.fasterxml.jackson.module"     % "jackson-module-parameter-names",
  "com.fasterxml.jackson.module"     %% "jackson-module-scala",
).map(_ % jacksonVersion)

dependencyOverrides ++= jacksonOverrides

dependencyOverrides += "com.google.googlejavaformat" % "google-java-format" % "1.17.0"

libraryDependencies += "ch.qos.logback" % "logback-classic" % "1.4.14"

// The Play plugin
addSbtPlugin("org.playframework" % "sbt-plugin" % "3.0.0")

// ORM
addSbtPlugin("org.playframework" % "sbt-play-ebean" % "8.0.0")

addSbtPlugin("com.github.sbt" % "sbt-native-packager" % "1.9.16")

addSbtPlugin("com.github.sbt" % "sbt-javaagent" % "0.1.8")

addSbtPlugin("com.github.sbt" % "sbt-jacoco" % "3.4.0")

addSbtPlugin("com.github.sbt" % "sbt-eclipse" % "6.0.0")

addSbtPlugin("org.openapitools" % "sbt-openapi-generator" % "5.0.1")
addSbtPlugin("net.rouly" % "sbt-openapi-style-validator" % "0.0.4")

// Use a newer version of open api generator than the latest available
// SBT plugin version (defined above) comes packaged with.
libraryDependencies += "org.openapitools" % "openapi-generator" % "5.1.1"

// Plugin for formatting code.
addSbtPlugin("com.lightbend.sbt" % "sbt-java-formatter" % "0.8.0")

addSbtPlugin("net.virtual-void" % "sbt-dependency-graph" % "0.10.0-RC1")
