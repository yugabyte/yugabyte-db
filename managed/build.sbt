import jline.console.ConsoleReader
import play.sbt.PlayImport.PlayKeys.{playInteractionMode, playMonitoredFiles}
import play.sbt.PlayInteractionMode
import sbt.Tests._

import scala.sys.process.Process

historyPath := Some(file(System.getenv("HOME") + "/.sbt/.yugaware-history"))

useCoursier := false

// ------------------------------------------------------------------------------------------------
// Constants
// ------------------------------------------------------------------------------------------------

// This is used to decide whether to clean/build the py2 or py3 venvs.
lazy val USE_PYTHON3 = strToBool(System.getenv("YB_MANAGED_DEVOPS_USE_PYTHON3"), default = true)

// Use this to enable debug logging in this script.
lazy val YB_DEBUG_ENABLED = strToBool(System.getenv("YB_BUILD_SBT_DEBUG"))

// ------------------------------------------------------------------------------------------------
// Functions
// ------------------------------------------------------------------------------------------------

def normalizeEnvVarValue(value: String): String = {
  if (value == null) null else value.trim()
}

def strToBool(s: String, default: Boolean = false): Boolean = {
  if (s == null) default
  else {
    val normalizedStr = normalizeEnvVarValue(s)
    normalizedStr != null && (normalizedStr.toLowerCase() == "true" || normalizedStr == "1")
  }
}

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
// Task Keys
// ------------------------------------------------------------------------------------------------

lazy val cleanPlatform = taskKey[Int]("Clean Yugabyte Platform")

lazy val compilePlatform = taskKey[Int]("Compile Yugabyte Platform")

lazy val runPlatformTask = taskKey[Unit]("Run Yugabyte Platform helper task")

lazy val runPlatform = inputKey[Unit]("Run Yugabyte Platform with UI")

lazy val consoleSetting = settingKey[PlayInteractionMode]("custom console setting")

lazy val versionGenerate = taskKey[Int]("Add version_metadata.json file")

lazy val buildVenv = taskKey[Int]("Build venv")
lazy val buildUI = taskKey[Int]("Build UI")

lazy val cleanUI = taskKey[Int]("Clean UI")
lazy val cleanVenv = taskKey[Int]("Clean venv")


lazy val compileJavaGenClient = taskKey[Int]("Compile generated Java code")

// ------------------------------------------------------------------------------------------------
// Main build.sbt script
// ------------------------------------------------------------------------------------------------

name := "yugaware"

lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging, JavaAgent)
  .disablePlugins(PlayLayoutPlugin)
  .settings(commands += Command.command("deflake") { state =>
    "test" :: "deflake" :: state
  })
  .settings(commands += Command.args("deflakeOne", "<arg>") { (state, args) =>
    "testOnly " + args.mkString(" ") :: "deflakeOne " + args.mkString(" "):: state
  })

scalaVersion := "2.12.10"
version := sys.process.Process("cat version.txt").lineStream_!.head
Global / onChangedBuildSource := ReloadOnSourceChanges

libraryDependencies ++= Seq(
  javaJdbc,
  ehcache,
  javaWs,
  filters,
  guice,
  "com.google.inject.extensions" % "guice-multibindings" % "4.2.3",
  "org.mindrot" % "jbcrypt" % "0.4",
  "org.postgresql" % "postgresql" % "42.2.25",
  "net.logstash.logback" % "logstash-logback-encoder" % "6.2",
  "org.codehaus.janino" % "janino" % "3.1.6",
  "org.apache.commons" % "commons-compress" % "1.21",
  "org.apache.commons" % "commons-csv" % "1.9.0",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.13",
  "org.flywaydb" %% "flyway-play" % "4.0.0",
  // https://github.com/YugaByte/cassandra-java-driver/releases
  "com.yugabyte" % "cassandra-driver-core" % "3.8.0-yb-7",
  "org.yaml" % "snakeyaml" % "2.1",
  "org.bouncycastle" % "bcpkix-jdk15on" % "1.61",
  "org.springframework.security" % "spring-security-core" % "5.8.1",
  "com.amazonaws" % "aws-java-sdk-ec2" % "1.12.129",
  "com.amazonaws" % "aws-java-sdk-kms" % "1.12.129",
  "com.amazonaws" % "aws-java-sdk-iam" % "1.12.129",
  "com.amazonaws" % "aws-java-sdk-sts" % "1.12.129",
  "com.amazonaws" % "aws-java-sdk-s3" % "1.12.129",
  "net.minidev" % "json-smart" % "2.5.0",
  "com.cronutils" % "cron-utils" % "9.1.6",
  "com.azure" % "azure-storage-blob" % "12.7.0",
  "com.azure" % "azure-core" % "1.1.0",
  "io.prometheus" % "simpleclient" % "0.11.0",
  "io.prometheus" % "simpleclient_hotspot" % "0.11.0",
  "io.prometheus" % "simpleclient_servlet" % "0.11.0",
  "org.glassfish.jaxb" % "jaxb-runtime" % "2.3.2",
  "org.pac4j" %% "play-pac4j" % "7.0.1",
  "org.pac4j" % "pac4j-oauth" % "3.7.0" exclude("commons-io" , "commons-io"),
  "org.pac4j" % "pac4j-oidc" % "3.7.0" exclude("commons-io" , "commons-io"),
  "com.typesafe.play" %% "play-json" % "2.6.14",
  "org.asynchttpclient" % "async-http-client" % "2.2.1",
  "commons-validator" % "commons-validator" % "1.7",
  "org.apache.velocity" % "velocity-engine-core" % "2.3",
  "com.fasterxml.woodstox" % "woodstox-core" % "6.4.0",
  "com.jayway.jsonpath" % "json-path" % "2.6.0",
  "commons-io" % "commons-io" % "2.8.0",
  "commons-codec" % "commons-codec" % "1.15",
  "com.google.cloud" % "google-cloud-storage" % "2.2.1",
  "com.google.oauth-client" % "google-oauth-client" % "1.34.1",
  "org.projectlombok" % "lombok" % "1.18.20",
  "com.squareup.okhttp3" % "okhttp" % "4.9.2",
  "io.kamon" %% "kamon-bundle" % "2.2.2",
  "io.kamon" %% "kamon-prometheus" % "2.2.2",
  "org.unix4j" % "unix4j-command" % "0.6",
  "com.github.dikhan" % "pagerduty-client" % "3.1.2",
  "com.bettercloud" % "vault-java-driver" % "5.1.0",
  "org.apache.directory.api" % "api-all" % "2.1.0",
  "io.fabric8" % "kubernetes-client" % "5.10.2",
  "io.jsonwebtoken" % "jjwt-api" % "0.11.5",
  "io.jsonwebtoken" % "jjwt-impl" % "0.11.5",
  "io.jsonwebtoken" % "jjwt-jackson" % "0.11.5",
  "io.swagger" % "swagger-annotations" % "1.5.22", // needed for annotations in prod code
  // ---------------------------------------------------------------------------------------------//
  //                                   TEST DEPENDENCIES                                          //
  // ---------------------------------------------------------------------------------------------//
  "org.mockito" % "mockito-core" % "2.13.0" % Test,
  "org.mockito" % "mockito-inline" % "3.8.0" % Test,
  "org.mindrot" % "jbcrypt" % "0.4" % Test,
  "com.h2database" % "h2" % "2.1.212" % Test,
  "org.hamcrest" % "hamcrest-core" % "2.2" % Test,
  "pl.pragmatists" % "JUnitParams" % "1.1.1" % Test,
  "com.icegreen" % "greenmail" % "1.6.1" % Test,
  "com.icegreen" % "greenmail-junit4" % "1.6.1" % Test,
  "com.squareup.okhttp3" % "mockwebserver" % "4.9.2" % Test,
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

lazy val ybPublicSnapshotResolverDescription =
    "Public snapshot resolver for yb-client jar"

lazy val ybPublicSnapshotResolver = {
  if (mavenLocal) {
    Seq()
  } else {
    val ybPublicSnapshotUrl = "https://repository.yugabyte.com/maven/"
    Seq("Yugabyte Public Maven Snapshots" at ybPublicSnapshotUrl)
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
  validateResolver(ybClientSnapshotResolver, ybClientSnapshotResolverDescription) ++
  validateResolver(ybPublicSnapshotResolver, ybPublicSnapshotResolverDescription)
}

(Compile / compilePlatform) := {
  (Compile / compile).value
  buildVenv.value
  buildUI.value
  versionGenerate.value
}

cleanPlatform := {
  clean.value
  cleanVenv.value
  cleanUI.value
}

versionGenerate := {
  val buildType = sys.env.getOrElse("BUILD_TYPE", "release")
  val status = Process("../build-support/gen_version_info.py --build-type=" + buildType + " " +
    (Compile / resourceDirectory).value / "version_metadata.json").!
  ybLog("version_metadata.json Generated")
  Process("rm -f " + (Compile / resourceDirectory).value / "gen_version_info.log").!
  status
}

buildVenv := {
  ybLog("Building virtual env...")
  val status = Process("./bin/install_python_requirements.sh", baseDirectory.value / "devops").!
  Process("./bin/install_ansible_requirements.sh --force", baseDirectory.value / "devops").!
  status
}

buildUI := {
  ybLog("Building UI...")
  val status = Process("npm ci", baseDirectory.value / "ui").!
  status
}

compileJavaGenClient := {
  val buildType = sys.env.getOrElse("BUILD_TYPE", "release")
  val status = Process("mvn install", new File(baseDirectory.value + "/client/java/generated")).!
  status
}

cleanUI := {
  ybLog("Cleaning UI...")
  val status = Process("rm -rf node_modules", baseDirectory.value / "ui").!
  status
}

def get_venv_dir(): String = {
  if (USE_PYTHON3) "venv" else "python_virtual_env"
}

cleanVenv := {
  ybLog("Cleaning virtual env...")
  val venvDir: String = get_venv_dir()
  val status = Process("rm -rf " + venvDir, baseDirectory.value / "devops").!
  status
}

// Generate a Java API client.
lazy val javagen = project.in(file("client/java"))
  .settings(
    openApiInputSpec := "src/main/resources/swagger.json",
    openApiGeneratorName := "java",
    openApiOutputDir := "client/java/generated",
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/java/openapi-java-config.json",

  )

// Generate a Python API client.
lazy val pythongen = project.in(file("client/python"))
  .settings(
    openApiInputSpec := "src/main/resources/swagger.json",
    openApiGeneratorName := "python",
    openApiOutputDir := "client/python/generated",
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/python/openapi-python-config.json"
  )

// Generate a Go API client.
lazy val gogen = project.in(file("client/go"))
  .settings(
    openApiInputSpec := "src/main/resources/swagger.json",
    openApiGeneratorName := "go",
    openApiOutputDir := "client/go/generated",
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/go/openapi-go-config.json"
  )

packageZipTarball.in(Universal) := packageZipTarball.in(Universal).dependsOn(versionGenerate).value

runPlatformTask := {
  (Compile / run).toTask("").value
}

/**
 * Add UI Run hook to run UI alongside with API.
 */
runPlatform := {
  val curState = state.value
  val newState = Project.extract(curState).appendWithoutSession(
    Vector(PlayKeys.playRunHooks += UIRunHook(baseDirectory.value / "ui")),
    curState
  )
  Project.extract(newState).runTask(runPlatformTask, newState)
}

libraryDependencies += "org.yb" % "yb-client" % "0.8.17.3-SNAPSHOT"

libraryDependencies ++= Seq(
  "io.netty" % "netty-tcnative-boringssl-static" % "2.0.54.Final",
  "org.slf4j" % "slf4j-ext" % "1.7.26",
  // TODO(Shashank): Remove this in Step 3:
  // Overrides to address vulnerability in swagger-play2
  "com.typesafe.akka" %% "akka-actor" % "2.5.16",
  "com.nimbusds" % "nimbus-jose-jwt" % "7.9"
)


dependencyOverrides += "com.google.protobuf" % "protobuf-java" % "latest.integration"
dependencyOverrides += "com.google.guava" % "guava" % "32.1.1-jre"
// TODO(Shashank): Remove these in Step 3:

val jacksonVersion         = "2.15.3"

val jacksonLibs = Seq(
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
)

val jacksonOverrides = jacksonLibs.map(_ % jacksonVersion)

dependencyOverrides ++= jacksonOverrides

excludeDependencies += "org.eclipse.jetty" % "jetty-io"
excludeDependencies += "org.eclipse.jetty" % "jetty-server"

concurrentRestrictions in Global := Seq(Tags.limitAll(16))

val testParallelForks = SettingKey[Int]("testParallelForks",
  "Number of parallel forked JVMs, running tests")
testParallelForks := 4
val testShardSize = SettingKey[Int]("testShardSize",
  "Number of test classes, executed by each forked JVM")
testShardSize := 30

concurrentRestrictions in Global += Tags.limit(Tags.ForkedTestGroup, testParallelForks.value)

def partitionTests(tests: Seq[TestDefinition], shardSize: Int) =
  tests.sortWith(_.name < _.name).grouped(shardSize).zipWithIndex map {
    case (tests, index) =>
      val options = ForkOptions().withRunJVMOptions(Vector(
        "-Xmx2g", "-XX:MaxMetaspaceSize=600m", "-XX:MetaspaceSize=200m",
        "-Dconfig.resource=application.test.conf"
      ))
      Group("testGroup" + index, tests, SubProcess(options))
  } toSeq

Test / parallelExecution := true
Test / fork := true
Test / testGrouping := partitionTests( (Test / definedTests).value, testShardSize.value )

javaOptions in Test += "-Dconfig.resource=application.test.conf"
testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")

// Skip packaging javadoc for now
sources in (Compile, doc) := Seq()
publishArtifact in (Compile, packageDoc) := false

topLevelDirectory := None

// Skip auto-recompile of code in dev mode if AUTO_RELOAD=false
lazy val autoReload = getBoolEnvVar("AUTO_RELOAD")
playMonitoredFiles := { if (autoReload) playMonitoredFiles.value: @sbtUnchecked else Seq() }

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
            case _ => waitEOF()
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

val swaggerGen: TaskKey[Unit] = taskKey[Unit](
  "generate swagger.json"
)

lazy val swagger = project
  .dependsOn(root % "compile->compile;test->test")
  .settings(
    Test / fork := true,
    javaOptions in Test += "-Dconfig.resource=application.test.conf",
    testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a"),
    libraryDependencies ++= Seq(
      "io.swagger" %% "swagger-play2" % "1.6.1" % Test,
      "io.swagger" %% "swagger-scala-module" % "1.0.5" % Test,
    ),
    dependencyOverrides += "com.fasterxml.jackson.module" %% "jackson-module-scala" % "2.9.10",
    dependencyOverrides += "com.fasterxml.jackson.dataformat" % "jackson-dataformat-cbor" % "2.9.10",
    dependencyOverrides += "com.fasterxml.jackson.datatype" % "jackson-datatype-jsr310" % "2.9.10",
    dependencyOverrides += "com.fasterxml.jackson.core" % "jackson-databind" % "2.9.10.8",
    dependencyOverrides += "com.google.guava" % "guava" % "23.0",

    swaggerGen := Def.taskDyn {
      // Consider generating this only in managedResources
      val file = (resourceDirectory in Compile in root).value / "swagger.json"
      Def.sequential(
        (Test / runMain )
          .toTask(s" com.yugabyte.yw.controllers.SwaggerGenTest $file"),
      )
    }.value
  )

test in Test := (test in Test).dependsOn(swagger / Test / test).value

swaggerGen := Def.taskDyn {
  Def.sequential(
    swagger /swaggerGen,
    javagen / openApiGenerate,
    compileJavaGenClient,
    pythongen / openApiGenerate,
    gogen / openApiGenerate
  )
}.value

val grafanaGen: TaskKey[Unit] = taskKey[Unit](
  "generate dashboard.json"
)

grafanaGen := Def.taskDyn {
  val file = (resourceDirectory in Compile).value / "metric" / "Dashboard.json"
  Def.sequential(
    (runMain in Test)
      .toTask(s" com.yugabyte.yw.controllers.GrafanaGenTest $file")
  )
}.value
