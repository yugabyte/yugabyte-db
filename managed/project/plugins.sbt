useCoursier := false

dependencyOverrides += "org.scala-lang.modules" %% "scala-xml" % "2.1.0"

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

libraryDependencies += "ch.qos.logback" % "logback-classic" % "1.2.11"

// The Play plugin
addSbtPlugin("com.typesafe.play" % "sbt-plugin" % "2.8.19")

// ORM
addSbtPlugin("com.typesafe.play" % "sbt-play-ebean" % "6.2.0-RC5")

addSbtPlugin("com.github.sbt" % "sbt-native-packager" % "1.9.16")

addSbtPlugin("com.github.sbt" % "sbt-jacoco" % "3.4.0")

addSbtPlugin("com.github.sbt" % "sbt-eclipse" % "6.0.0")

addSbtPlugin("org.openapitools" % "sbt-openapi-generator" % "5.0.1")

// Plugin for formatting code.
addSbtPlugin("com.lightbend.sbt" % "sbt-java-formatter" % "0.8.0")

addSbtPlugin("io.kamon" % "sbt-kanela-runner-play-2.8" % "2.0.14")

addSbtPlugin("net.virtual-void" % "sbt-dependency-graph" % "0.10.0-RC1")
