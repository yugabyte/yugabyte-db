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
lazy val generateCrdObjects = taskKey[Int]("Generating CRD classes..")
lazy val buildUI = taskKey[Int]("Build UI")
lazy val buildModules = taskKey[Int]("Build modules")
lazy val buildDependentArtifacts = taskKey[Int]("Build dependent artifacts")
lazy val releaseModulesLocally = taskKey[Int]("Release modules locally")
lazy val downloadThirdPartyDeps = taskKey[Int]("Downloading thirdparty dependencies")
lazy val devSpaceReload = taskKey[Int]("Do a build without UI for DevSpace and reload")

lazy val cleanUI = taskKey[Int]("Clean UI")
lazy val cleanVenv = taskKey[Int]("Clean venv")
lazy val cleanModules = taskKey[Int]("Clean modules")
lazy val cleanCrd = taskKey[Int]("Clean CRD")


lazy val compileJavaGenClient = taskKey[Int]("Compile generated Java code")

// ------------------------------------------------------------------------------------------------
// Main build.sbt script
// ------------------------------------------------------------------------------------------------

name := "yugaware"

def commonSettings = Seq(
  scalaVersion := "2.13.12"
)

lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging, JavaAgent)
  .disablePlugins(PlayLayoutPlugin)
  .settings(commonSettings)
  .settings(commands += Command.command("deflake") { state =>
    "test" :: "deflake" :: state
  })
  .settings(commands += Command.args("deflakeOne", "<arg>") { (state, args) =>
    "testOnly " + args.mkString(" ") :: "deflakeOne " + args.mkString(" "):: state
  })

javacOptions ++= Seq("-source", "17", "-target", "17")
Compile / managedClasspath += baseDirectory.value / "target/scala-2.13/"
version := sys.process.Process("cat version.txt").lineStream_!.head
Global / onChangedBuildSource := ReloadOnSourceChanges

libraryDependencies ++= Seq(
  javaJdbc,
  caffeine,
  javaWs,
  filters,
  guice,
  "org.postgresql" % "postgresql" % "42.5.6",
  "net.logstash.logback" % "logstash-logback-encoder" % "6.2",
  "ch.qos.logback" % "logback-classic" % "1.4.14",
  "org.codehaus.janino" % "janino" % "3.1.9",
  "org.apache.commons" % "commons-lang3" % "3.17.0",
  "org.apache.commons" % "commons-collections4" % "4.4",
  "org.apache.commons" % "commons-compress" % "1.27.1",
  "org.apache.commons" % "commons-csv" % "1.13.0",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.13",
  "org.apache.mina" % "mina-core" % "2.2.4",
  "org.flywaydb" %% "flyway-play" % "9.0.0",
  // https://github.com/YugaByte/cassandra-java-driver/releases
  "com.yugabyte" % "cassandra-driver-core" % "3.8.0-yb-7",
  "org.yaml" % "snakeyaml" % "2.1",
  "org.bouncycastle" % "bcpkix-jdk18on" % "1.80",
  "org.springframework.security" % "spring-security-core" % "5.8.16",
  "com.amazonaws" % "aws-java-sdk-ec2" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-kms" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-iam" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-sts" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-s3" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-elasticloadbalancingv2" % "1.12.327",
  "com.amazonaws" % "aws-java-sdk-route53" % "1.12.400",
  "com.amazonaws" % "aws-java-sdk-cloudtrail" % "1.12.498",
  "net.minidev" % "json-smart" % "2.5.2",
  "com.cronutils" % "cron-utils" % "9.1.6",
  // Be careful when changing azure library versions.
  // Make sure all itests and existing functionality works as expected.
  // Used below azure versions from azure-sdk-bom:1.2.6
  "com.azure" % "azure-core" % "1.32.0",
  "com.azure" % "azure-identity" % "1.6.0",
  "com.azure" % "azure-security-keyvault-keys" % "4.5.0",
  "com.azure" % "azure-storage-blob" % "12.19.1",
  "com.azure.resourcemanager" % "azure-resourcemanager" % "2.43.0",
  "jakarta.mail" % "jakarta.mail-api" % "2.1.2",
  "org.eclipse.angus" % "jakarta.mail" % "1.0.0",
  "javax.validation" % "validation-api" % "2.0.1.Final",
  "io.prometheus" % "simpleclient" % "0.11.0",
  "io.prometheus" % "simpleclient_hotspot" % "0.11.0",
  "io.prometheus" % "simpleclient_servlet" % "0.11.0",
  "org.glassfish.jaxb" % "jaxb-runtime" % "2.3.2",
  // pac4j and nimbusds libraries need to be upgraded together.
  "org.pac4j" %% "play-pac4j" % "11.0.0-PLAY2.8",
  "org.pac4j" % "pac4j-oauth" % "5.7.7" exclude("commons-io" , "commons-io"),
  "org.pac4j" % "pac4j-oidc" % "5.7.7"  exclude("commons-io" , "commons-io"),
  "com.nimbusds" % "nimbus-jose-jwt" % "9.37.2",
  "com.nimbusds" % "oauth2-oidc-sdk" % "10.1",
  "org.playframework" %% "play-json" % "3.0.4",
  "commons-validator" % "commons-validator" % "1.8.0",
  "org.apache.velocity" % "velocity-engine-core" % "2.4.1",
  "com.fasterxml.woodstox" % "woodstox-core" % "6.4.0",
  "com.jayway.jsonpath" % "json-path" % "2.6.0",
  "commons-io" % "commons-io" % "2.15.1",
  "commons-codec" % "commons-codec" % "1.16.0",
  "com.google.apis" % "google-api-services-compute" % "v1-rev20241008-2.0.0",
  "com.google.apis" % "google-api-services-iam" % "v1-rev20240918-2.0.0",
  "com.google.cloud" % "google-cloud-compute" % "1.62.0",
  "com.google.cloud" % "google-cloud-storage" % "2.43.2",
  "com.google.cloud" % "google-cloud-kms" % "2.55.0",
  "com.google.cloud" % "google-cloud-resourcemanager" % "1.54.0",
  "com.google.cloud" % "google-cloud-logging" % "3.17.2",
  "com.google.oauth-client" % "google-oauth-client" % "1.35.0",
  "org.projectlombok" % "lombok" % "1.18.26",
  "com.squareup.okhttp3" % "okhttp" % "4.12.0",
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-xml" % "2.17.2",
  "io.kamon" %% "kamon-bundle" % "2.7.5",
  "io.kamon" %% "kamon-prometheus" % "2.7.5",
  "org.unix4j" % "unix4j-command" % "0.6",
  "com.bettercloud" % "vault-java-driver" % "5.1.0",
  "org.apache.directory.api" % "api-all" % "2.1.7",
  "io.fabric8" % "crd-generator-apt" % "6.8.0",
  "io.fabric8" % "kubernetes-client" % "6.8.0",
  "io.fabric8" % "kubernetes-client-api" % "6.8.0",
  "io.fabric8" % "kubernetes-model" % "6.8.0",
  "org.modelmapper" % "modelmapper" % "2.4.4",

  "io.jsonwebtoken" % "jjwt-api" % "0.11.5",
  "io.jsonwebtoken" % "jjwt-impl" % "0.11.5",
  "io.jsonwebtoken" % "jjwt-jackson" % "0.11.5",
  "io.swagger" % "swagger-annotations" % "1.6.1", // needed for annotations in prod code
  "de.dentrassi.crypto" % "pem-keystore" % "2.2.1",
  // Prod dependency temporary as we use HSQLDB as a dummy perf_advisor DB for YBM scenario
  // Remove once YBM starts using real PG DB.
  "org.hsqldb" % "hsqldb" % "2.7.1",
  // ---------------------------------------------------------------------------------------------//
  //                                   TEST DEPENDENCIES                                          //
  // ---------------------------------------------------------------------------------------------//
  "org.mockito" % "mockito-core" % "5.3.1" % Test,
  "org.mockito" % "mockito-inline" % "5.2.0" % Test,
  "org.mindrot" % "jbcrypt" % "0.4" % Test,
  "com.h2database" % "h2" % "2.1.212" % Test,
  "org.hamcrest" % "hamcrest-core" % "2.2" % Test,
  "pl.pragmatists" % "JUnitParams" % "1.1.1" % Test,
  "com.icegreen" % "greenmail" % "2.0.1" % Test,
  "com.icegreen" % "greenmail-junit4" % "2.0.1" % Test,
  "com.squareup.okhttp3" % "mockwebserver" % "4.9.2" % Test,
  "io.grpc" % "grpc-testing" % "1.67.1" % Test,
  "io.grpc" % "grpc-inprocess" % "1.67.1" % Test,
  "io.zonky.test" % "embedded-postgres" % "2.0.1" % Test,
  "org.springframework" % "spring-test" % "5.3.9" % Test,
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
      Seq(("Yugabyte Maven Snapshots" at ybMavenSnapshotUrl).withAllowInsecureProtocol(true))
    } else {
      Seq()
    }
  }
}

lazy val ybPublicSnapshotResolverDescription =
    "Public snapshot resolver for yb-client jar"

lazy val ybPublicSnapshotResolver = {
  val ybPublicSnapshotUrl = "https://repository.yugabyte.com/maven/"
  Seq("Yugabyte Public Maven Snapshots" at ybPublicSnapshotUrl)
}

// Custom remote maven repository to retrieve library dependencies from.
lazy val ybMvnCacheUrlEnvVarName = "YB_MVN_CACHE_URL"
lazy val ybMvnCacheUrl = getEnvVar(ybMvnCacheUrlEnvVarName)
lazy val mavenCacheServerResolverDescription =
    "Maven cache server (such as Nexus or Artifactory), specified by " + ybMvnCacheUrlEnvVarName
lazy val mavenCacheServerResolver = {
  if (isDefined(ybMvnCacheUrl)) {
    Seq(("Yugabyte Maven Cache" at ybMvnCacheUrl).withAllowInsecureProtocol(true))
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

(Compile / compile) := ((Compile / compile) dependsOn buildDependentArtifacts).value

(Compile / compilePlatform) := {
  (Compile / compile).value
  Def.sequential(
      generateCrdObjects,
      buildVenv,
      releaseModulesLocally
    ).value
  buildUI.value
  versionGenerate.value
  downloadThirdPartyDeps.value
}

cleanPlatform := {
  clean.value
  (swagger / clean).value
  cleanCrd.value
  cleanVenv.value
  cleanUI.value
  cleanModules.value
}

lazy val moveYbcPackageEnvName = "MOVE_YBC_PKG"
lazy val moveYbcPackage = getBoolEnvVar(moveYbcPackageEnvName)

versionGenerate := {
  val buildType = sys.env.getOrElse("BUILD_TYPE", "release")
  val status = Process("../python/yugabyte/gen_version_info.py --build-type=" + buildType + " " +
    (Compile / resourceDirectory).value / "version_metadata.json").!
  ybLog("version_metadata.json Generated")
  Process("rm -f " + (Compile / resourceDirectory).value / "gen_version_info.log").!
  if (moveYbcPackage) {
    Process("./download_ybc.sh -c " + (Compile / resourceDirectory).value / "reference.conf" + " -s", baseDirectory.value).!
  } else {
    Process("./download_ybc.sh -c " + (Compile / resourceDirectory).value / "reference.conf", baseDirectory.value).!
  }
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

releaseModulesLocally := {
  ybLog("Building modules...")
  val status = Process("mvn install -DskipTests=true -P releaseLocally", baseDirectory.value / "parent-module").!
  status
}

buildDependentArtifacts := {
  ybLog("Building dependencies...")
  generateCrdObjects.value
  val status = Process("mvn install -P buildDependenciesOnly", baseDirectory.value / "parent-module").!
  status
}

generateCrdObjects := {
  ybLog("Generating crd classes...")
  val generatedSourcesDirectory = baseDirectory.value / "target/scala-2.13/"
  val command = s"mvn generate-sources -DoutputDirectory=$generatedSourcesDirectory"
  val status = Process(command, baseDirectory.value / "src/main/java/com/yugabyte/yw/common/operator/").!
  status
}

downloadThirdPartyDeps := {
  ybLog("Downloading third-party dependencies...")
  val status = Process("wget -Nqi thirdparty-dependencies.txt -P /opt/third-party -c", baseDirectory.value / "support").!
  status
}

compileJavaGenClient := {
  val buildType = sys.env.getOrElse("BUILD_TYPE", "release")
  val status = Process("mvn install", new File(baseDirectory.value + "/client/java/generated")).!
  status
}

devSpaceReload := {
  (Universal / packageBin).value
  val status = Process("devspace run extract-archive").!
  status
}

cleanUI := {
  ybLog("Cleaning UI...")
  val status = Process("rm -rf node_modules", baseDirectory.value / "ui").!
  status
}

cleanModules := {
  ybLog("Cleaning modules...")
  val status = Process("mvn clean", baseDirectory.value / "parent-module").!
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

cleanCrd := {
  ybLog("Cleaning CRD generated code...")
  val generatedSourcesDirectory = baseDirectory.value / "target/scala-2.13/"
  val command = s"mvn clean -DoutputDirectory=$generatedSourcesDirectory"
  val status = Process(command, baseDirectory.value / "src/main/java/com/yugabyte/yw/common/operator/").!
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

Universal / packageZipTarball := (Universal / packageZipTarball).dependsOn(versionGenerate, buildDependentArtifacts).value

// Being used by DevSpace tool to build an archive without building the UI
Universal / packageBin := (Universal / packageBin).dependsOn(versionGenerate, buildDependentArtifacts).value

// Copying 'support/thirdparty-dependencies.txt' into the YBA tarball at 'conf/thirdparty-dependencies.txt'.
Universal / mappings ++= {
  val tpdSourceFile = baseDirectory.value / "support" / "thirdparty-dependencies.txt"
  Seq((tpdSourceFile, "conf/thirdparty-dependencies.txt"))
}

javaAgents += "io.kamon" % "kanela-agent" % "1.0.18"

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

libraryDependencies += "org.yb" % "yb-client" % "0.8.64.8-SNAPSHOT"
libraryDependencies += "org.yb" % "ybc-client" % "2.2.0.1-b2"
libraryDependencies += "org.yb" % "yb-perf-advisor" % "1.0.0-b35"

libraryDependencies ++= Seq(
  "io.netty" % "netty-tcnative-boringssl-static" % "2.0.54.Final",
  "io.netty" % "netty-codec-haproxy" % "4.1.89.Final",
  "io.projectreactor.netty" % "reactor-netty-http" % "1.0.39",
  "org.slf4j" % "slf4j-ext" % "1.7.26",
)

dependencyOverrides += "org.reflections" % "reflections" % "0.10.2"

// This is a custom version, built based on 1.0.3 with the following commit added on top:
// https://github.com/apache/pekko/commit/1e41829bf7abeec268b9a409f35051ed7f4e0090.
// This is required to fix TLS infinite loop issue, which causes high CPU usage.
// We can't use 1.1.0-M1 version yet, as it has the following issue:
// https://github.com/playframework/playframework/pull/12662
// Once the issue is fixed we should migrate back on stable version.
val pekkoVersion         = "1.0.3-tls-loop-fix"

val pekkoLibs = Seq(
  "org.apache.pekko" %% "pekko-actor-typed",
  "org.apache.pekko" %% "pekko-actor",
  "org.apache.pekko" %% "pekko-protobuf-v3",
  "org.apache.pekko" %% "pekko-serialization-jackson",
  "org.apache.pekko" %% "pekko-slf4j",
  "org.apache.pekko" %% "pekko-stream",
)

val pekkoOverrides = pekkoLibs.map(_ % pekkoVersion)

dependencyOverrides ++= pekkoOverrides

val jacksonVersion         = "2.17.1"

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
excludeDependencies += "commons-collections" % "commons-collections"

Global / concurrentRestrictions := Seq(Tags.limitAll(16))

val testParallelForks = SettingKey[Int]("testParallelForks",
  "Number of parallel forked JVMs, running tests")
testParallelForks := 4
val testShardSize = SettingKey[Int]("testShardSize",
  "Number of test classes, executed by each forked JVM")
testShardSize := 30

Global / concurrentRestrictions += Tags.limit(Tags.ForkedTestGroup, testParallelForks.value)

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

Test / javaOptions += "-Dconfig.resource=application.test.conf"
testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")

// Skip packaging javadoc for now
Compile / doc / sources := Seq()
Compile / doc / publishArtifact.withRank(KeyRanks.Invisible) := false

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
  .settings(commonSettings)
  .settings(
    Test / fork := true,
    Test / javaOptions += "-Dconfig.resource=application.test.conf",
    testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a"),
    libraryDependencies ++= Seq(
      "com.github.dwickern" %% "swagger-play3.0" % "4.0.0"
    ),

    dependencyOverrides ++= pekkoOverrides,
    dependencyOverrides ++= jacksonOverrides,
    dependencyOverrides += "org.scala-lang.modules" %% "scala-xml" % "2.1.0",

    swaggerGen := Def.taskDyn {
      // Consider generating this only in managedResources
      val swaggerJson = (root / Compile / resourceDirectory).value / "swagger.json"
      val swaggerStrictJson = (root / Compile / resourceDirectory).value / "swagger-strict.json"
      Def.sequential(
        (Test / runMain )
          .toTask(s" com.yugabyte.yw.controllers.SwaggerGenTest $swaggerJson"),
        // swagger-strict.json excludes deprecated apis
        // For ex use '--exclude_deprecated 2.15.0.0' to drop APIs deprecated before a version
        // or use '--exclude_deprecated 24m' to drop APIs deprecated before 2 years
        // or use '--exclude_deprecated 2020-12-21' (YYYY-MM-DD format) to drop since date
        // or use '--exclude_deprecated all' to drop all deprecated APIs
        (Test / runMain )
          .toTask(s" com.yugabyte.yw.controllers.SwaggerGenTest $swaggerStrictJson --exclude_deprecated all"),
      )
    }.value
  )

Test / test := (Test / test).dependsOn(swagger / Test / test).value

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
  val file = (Compile / resourceDirectory).value / "metric" / "Dashboard.json"
  Def.sequential(
    (Test / runMain)
      .toTask(s" com.yugabyte.yw.controllers.GrafanaGenTest $file")
  )
}.value
