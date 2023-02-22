useCoursier := false

dependencyOverrides += "org.scala-lang.modules" %% "scala-xml" % "2.1.0"

val jacksonVersion         = "2.13.4"
val jacksonDatabindVersion = "2.13.4.2"

val jacksonOverrides = Seq(
  "com.fasterxml.jackson.core"     % "jackson-core",
  "com.fasterxml.jackson.core"     % "jackson-annotations",
  "com.fasterxml.jackson.datatype" % "jackson-datatype-jdk8",
  "com.fasterxml.jackson.datatype" % "jackson-datatype-jsr310"
).map(_ % jacksonVersion)

val jacksonDatabindOverrides = Seq(
  "com.fasterxml.jackson.core" % "jackson-databind" % jacksonDatabindVersion
)

val akkaSerializationJacksonOverrides = Seq(
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-cbor",
  "com.fasterxml.jackson.module"     % "jackson-module-parameter-names",
  "com.fasterxml.jackson.module"     %% "jackson-module-scala",
).map(_ % jacksonVersion)

libraryDependencies ++= jacksonDatabindOverrides ++ jacksonOverrides ++ akkaSerializationJacksonOverrides

// The Play plugin
addSbtPlugin("com.typesafe.play" % "sbt-plugin" % "2.8.19")

// ORM
addSbtPlugin("com.typesafe.play" % "sbt-play-ebean" % "6.2.0-RC4")

addSbtPlugin("com.github.sbt" % "sbt-native-packager" % "1.9.16")

addSbtPlugin("com.github.sbt" % "sbt-jacoco" % "3.4.0")

addSbtPlugin("com.github.sbt" % "sbt-eclipse" % "6.0.0")

addSbtPlugin("org.openapitools" % "sbt-openapi-generator" % "5.0.1")

// Plugin for formatting code.
addSbtPlugin("com.swoval" % "sbt-jvm-format" % "0.3.1")

//addSbtPlugin("com.hootsuite" %% "sbt-swagger" % "1.0.0")

addSbtPlugin("io.kamon" % "sbt-kanela-runner-play-2.8" % "2.0.14")

addDependencyTreePlugin
