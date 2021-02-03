import jline.console.ConsoleReader
import play.sbt.PlayImport.PlayKeys.{playInteractionMode, playMonitoredFiles}
import play.sbt.PlayInteractionMode

// ------------------------------------------------------------------------------------------------
// Functions
// ------------------------------------------------------------------------------------------------

def normalizeEnvVarValue(value: String): String = {
  if (value == null) null else value.trim()
}

def strToBool(s: String): Boolean = {
  val normalizedStr = normalizeEnvVarValue(s)
  normalizedStr != null && (normalizedStr.toLowerCase() == "true" || normalizedStr == "1")
}

// Use this to enable debug logging in this script.
val YB_DEBUG_ENABLED = strToBool(System.getenv("YB_BUILD_SBT_DEBUG"))

def ybLog(s: String): Unit = {
  println("[Yugabyte sbt log] " + s)
}

def getEnvVar(envVarName: String): String = {
  val envVarValue = System.getenv(envVarName)
  val strValue = normalizeEnvVarValue(envVarValue)
  if (YB_DEBUG_ENABLED) {
    ybLog("getEnvVar: envVarName=" + envVarName + ", strValue=" + strValue)
  }
  strValue
}

def getBoolEnvVar(envVarName: String): Boolean = {
  val strValue = getEnvVar(envVarName)
  val boolValue = strToBool(strValue)
  if (YB_DEBUG_ENABLED) {
    ybLog("getBoolEnvVar: envVarName=" + envVarName + ", boolValue=" + boolValue)
  }
  boolValue
}

def isDefined(s: String): Boolean = {
  s != null && normalizeEnvVarValue(s).nonEmpty
}

def validateResolver(
    resolver: Seq[sbt.Resolver],
    description: String): Seq[sbt.Resolver] = {
  if (resolver == null) {
    throw new AssertionError("Undefined resolver: " + description)
  }
  // We are logging this even in non-debug mode, because these log messages are very useful and
  // only one message is logged per resolver.
  ybLog("[Resolver] " + description + ": " + resolver)
  resolver
}

// ------------------------------------------------------------------------------------------------
// Main build.sbt script
// ------------------------------------------------------------------------------------------------

name := "yugaware"

lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging)
  .disablePlugins(PlayLayoutPlugin)

scalaVersion := "2.11.7"
version := (sys.process.Process("cat version.txt").lines_!.head)

libraryDependencies ++= Seq(
  javaJdbc,
  ehcache,
  javaWs,
  filters,
  guice,
  "com.google.inject.extensions" % "guice-multibindings" % "4.2.3",
  "org.mockito" % "mockito-core" % "2.13.0",
  "org.mindrot" % "jbcrypt" % "0.3m",
  "org.postgresql" % "postgresql" % "9.4.1208",
  "commons-io" % "commons-io" % "2.4",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.2",
  "org.flywaydb" %% "flyway-play" % "4.0.0",
  // https://github.com/YugaByte/cassandra-java-driver/releases
  "com.yugabyte" % "cassandra-driver-core" % "3.2.0-yb-19",
  "org.yaml" % "snakeyaml" % "1.17",
  "org.bouncycastle" % "bcpkix-jdk15on" % "1.61",
  "org.springframework.security" % "spring-security-core" % "5.1.6.RELEASE",
  "com.amazonaws" % "aws-java-sdk-ec2" % "1.11.907",
  "com.amazonaws" % "aws-java-sdk-kms" % "1.11.638",
  "com.amazonaws" % "aws-java-sdk-iam" % "1.11.670",
  "com.amazonaws" % "aws-java-sdk-sts" % "1.11.678",
  "com.cronutils" % "cron-utils" % "9.0.1",
  "io.prometheus" % "simpleclient" % "0.8.0",
  "io.prometheus" % "simpleclient_hotspot" % "0.8.0",
  "io.prometheus" % "simpleclient_servlet" % "0.8.0",
  "org.glassfish.jaxb" % "jaxb-runtime" % "2.3.2",
  "org.pac4j" %% "play-pac4j" % "7.0.1",
  "org.pac4j" % "pac4j-oauth" % "3.7.0" exclude("commons-io" , "commons-io"),
  "org.pac4j" % "pac4j-oidc" % "3.7.0" exclude("commons-io" , "commons-io"),
  "com.typesafe.play" %% "play-json" % "2.6.14",
  "org.asynchttpclient" % "async-http-client" % "2.2.1",
  "commons-validator" % "commons-validator" % "1.7",
  "com.h2database" % "h2" % "1.4.200" % Test,
  "org.hamcrest" % "hamcrest-core" % "2.2" % Test,
  "pl.pragmatists" % "JUnitParams" % "1.1.1" % Test,
  "com.icegreen" % "greenmail" % "1.6.1" % Test,
  "com.icegreen" % "greenmail-junit4" % "1.6.1" % Test
)
// Clear default resolvers.
appResolvers := None
bootResolvers := None
otherResolvers := Seq()

// Whether to use local maven repo to retrieve artifacts (used for yb-client).
lazy val ybUseMavenLocalEnvVarName = "USE_MAVEN_LOCAL"
lazy val mavenLocal = getBoolEnvVar(ybUseMavenLocalEnvVarName)

lazy val ybMvnSnapshotUrlEnvVarName = "YB_MVN_SNAPSHOT_URL"
lazy val ybMvnLocalRepoEnvVarName = "YB_MVN_LOCAL_REPO"

lazy val ybLocalResolverDescription =
    "Local resolver (enabled by " + ybUseMavenLocalEnvVarName + ", path can be customized with " +
    ybMvnLocalRepoEnvVarName + ")"
lazy val ybLocalResolver = {
  if (mavenLocal) {
    val localMavenRepo = getEnvVar(ybMvnLocalRepoEnvVarName)
    if (isDefined(localMavenRepo)) {
      Seq("Local Maven Repository" at "file://" + localMavenRepo)
    } else {
      Seq(Resolver.mavenLocal)
    }
  } else {
    Seq()
  }
}

lazy val ybClientSnapshotResolverDescription =
    "Snapshot resolver for yb-client jar (used when " + ybUseMavenLocalEnvVarName + " is not " +
    "set, mostly during local development, configured with " + ybMvnSnapshotUrlEnvVarName + ")"

lazy val ybClientSnapshotResolver = {
  if (mavenLocal) {
    Seq()
  } else {
    val ybMavenSnapshotUrl = getEnvVar(ybMvnSnapshotUrlEnvVarName)
    if (isDefined(ybMavenSnapshotUrl)) {
      Seq("Yugabyte Maven Snapshots" at ybMavenSnapshotUrl)
    } else {
      Seq()
    }
  }
}

// Custom remote maven repository to retrieve library dependencies from.
lazy val ybMvnCacheUrlEnvVarName = "YB_MVN_CACHE_URL"
lazy val ybMvnCacheUrl = getEnvVar(ybMvnCacheUrlEnvVarName)
lazy val mavenCacheServerResolverDescription =
    "Maven cache server (such as Nexus or Artifactory), specified by " + ybMvnCacheUrlEnvVarName
lazy val mavenCacheServerResolver = {
  if (isDefined(ybMvnCacheUrl)) {
    Seq("Yugabyte Maven Cache" at ybMvnCacheUrl)
  } else {
    Seq()
  }
}

// Override default resolver order.

// We put the local resolver because of a weird issue that happens on Jenkins. We somehow end up
// with only the .pom file but not the .jar file downloaded to the local Maven repo for the
// com.fasterxml.jackson.core#jackson-core;2.9.9 artifact, and then sbt 0.13.15 fails with the
// error below. When this issue is resolved, we can put the local resolver first to use cached
// jars as much as possible.
// https://gist.githubusercontent.com/mbautin/61a505cc9d35833d37557c9762130fd0/raw

externalResolvers := {
  validateResolver(mavenCacheServerResolver, mavenCacheServerResolverDescription) ++
  validateResolver(ybLocalResolver, ybLocalResolverDescription) ++
  validateResolver(externalResolvers.value, "Default resolver") ++
  validateResolver(ybClientSnapshotResolver, ybClientSnapshotResolverDescription)
}

libraryDependencies += "org.yb" % "yb-client" % "0.8.2-SNAPSHOT"

dependencyOverrides += "io.netty" % "netty-handler" % "4.0.36.Final"
dependencyOverrides += "com.google.protobuf" % "protobuf-java" % "latest.integration"
dependencyOverrides += "com.google.guava" % "guava" % "23.0"

javaOptions in Test += "-Dconfig.file=src/main/resources/application.test.conf"
testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")

// Skip packaging javadoc for now
sources in (Compile, doc) := Seq()
publishArtifact in (Compile, packageDoc) := false

topLevelDirectory := None

// Skip auto-recompile of code in dev mode if AUTO_RELOAD=false
lazy val autoReload = getBoolEnvVar("AUTO_RELOAD")
playMonitoredFiles := { if (autoReload) playMonitoredFiles.value else Seq() }

lazy val consoleSetting = settingKey[PlayInteractionMode]("custom console setting")

consoleSetting := {
  object PlayConsoleInteractionModeNew extends PlayInteractionMode {
    private def withConsoleReader[T](f: ConsoleReader => T): T = {
      val consoleReader = new ConsoleReader
      try f(consoleReader)
      finally consoleReader.close()
    }
    private def waitForKey(): Unit = {
      withConsoleReader { consoleReader =>
        def waitEOF(): Unit = {
          consoleReader.readCharacter() match {
            case 4 | -1 =>
            // Note: we have to listen to -1 for jline2, for some reason...
            // STOP on Ctrl-D, EOF.
            case 11 =>
              consoleReader.clearScreen(); waitEOF()
            case 10 | 13 =>
              println(); waitEOF()
            case x => waitEOF()
          }
        }
        doWithoutEcho(waitEOF())
      }
    }
    def doWithoutEcho(f: => Unit): Unit = {
      withConsoleReader { consoleReader =>
        val terminal = consoleReader.getTerminal
        terminal.setEchoEnabled(false)
        try f
        finally terminal.restore()
      }
    }
    override def waitForCancel(): Unit = waitForKey()

    override def toString = "Console Interaction Mode"
  }

  PlayConsoleInteractionModeNew
}

playInteractionMode := consoleSetting.value
