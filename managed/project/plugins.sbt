useCoursier := false

// The Play plugin
addSbtPlugin("com.typesafe.play" % "sbt-plugin" % "2.6.25")

// Play enhancer - this automatically generates getters/setters for public fields
// and rewrites accessors of these fields to use the getters/setters. Remove this
// plugin if you prefer not to have this feature, or disable on a per project
// basis using disablePlugins(PlayEnhancer) in your build.sbt
addSbtPlugin("com.typesafe.sbt" % "sbt-play-enhancer" % "1.2.2")

// ORM
addSbtPlugin("com.typesafe.sbt" % "sbt-play-ebean" % "4.1.4")

addSbtPlugin("com.typesafe.sbt" % "sbt-native-packager" % "1.3.4")

addSbtPlugin("com.github.sbt" % "sbt-jacoco" % "3.0.3")

addSbtPlugin("com.typesafe.sbteclipse" % "sbteclipse-plugin" % "5.2.4")

addSbtPlugin("org.openapitools" % "sbt-openapi-generator" % "5.0.1")

// Plugin for formatting code.
addSbtPlugin("com.swoval" % "sbt-jvm-format" % "0.3.1")

//addSbtPlugin("com.hootsuite" %% "sbt-swagger" % "1.0.0")

addSbtPlugin("io.kamon" % "sbt-kanela-runner-play-2.6" % "2.0.14")

addSbtPlugin("net.virtual-void" % "sbt-dependency-graph" % "0.10.0-RC1")
