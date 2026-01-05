import better.files.File.OpenOptions
import jline.console.ConsoleReader
import play.sbt.PlayImport.PlayKeys.{playInteractionMode, playMonitoredFiles}
import play.sbt.PlayInteractionMode

import java.io.File
import java.nio.charset.StandardCharsets
import java.nio.file.{FileSystems, Files, Paths}
import sbt.complete.Parsers.spaceDelimited
import sbt.Tests.*
import sbt.nio.file.FileAttributes

import java.util.stream.Collectors
import scala.collection.JavaConverters.*
import scala.sys.process.Process
import scala.sys.process.*



// ------------------------------------------------------------------------------------------------
// Constants
// ------------------------------------------------------------------------------------------------

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
lazy val generateOssConfig = taskKey[Int]("Generating OSS class.")
lazy val buildDependentArtifacts = taskKey[Int]("Build dependent artifacts")
lazy val releaseModulesLocally = taskKey[Int]("Release modules locally")
lazy val testDependentArtifacts = taskKey[Int]("Test dependent artifacts")
lazy val downloadThirdPartyDeps = taskKey[Int]("Downloading thirdparty dependencies")
lazy val devSpaceReload = taskKey[Int]("Do a build without UI for DevSpace and reload")

lazy val cleanUI = taskKey[Int]("Clean UI")
lazy val cleanVenv = taskKey[Int]("Clean venv")
lazy val cleanModules = taskKey[Int]("Clean modules")
lazy val cleanCrd = taskKey[Int]("Clean CRD")
lazy val cleanOperatorConfig = taskKey[Unit]("Clean OperatorConfig")
lazy val cleanThirdParty = taskKey[Unit]("Clean ThirdParty Downloaded Marker")

// ------------------------------------------------------------------------------------------------
// Main build.sbt script
// ------------------------------------------------------------------------------------------------

name := "yugaware"
generateReverseRouter := false

def commonSettings = Seq(
  scalaVersion := "2.13.12",
  useCoursier := false
)

lazy val TestLocalProviderSuite = config("testLocalSuite") extend(Test)
lazy val TestQuickSuite = config("testQuickSuite") extend(Test)
lazy val TestRetrySuite = config("testRetrySuite") extend(Test)
lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging, JavaAgent)
  .configs(TestLocalProviderSuite, TestQuickSuite, TestRetrySuite)
  .disablePlugins(PlayLayoutPlugin)
  .settings(commonSettings)
  .settings(
    inConfig(TestLocalProviderSuite)(Defaults.testTasks),
    inConfig(TestQuickSuite)(Defaults.testTasks),
    inConfig(TestRetrySuite)(Defaults.testTasks)
  )
  .settings(commands += Command.command("deflake") { state =>
    "test" :: "deflake" :: state
  })
  .settings(commands += Command.args("deflakeOne", "<arg>") { (state, args) =>
    "testOnly " + args.mkString(" ") :: "deflakeOne " + args.mkString(" "):: state
  })

javacOptions ++= Seq("-source", "17", "-target", "17")

// This is for dev-mode server. In dev-mode, the play server is started before the files are compiled.
// Hence, the application files are not available in the path. For prod, It is in reference.conf file.
PlayKeys.devSettings += "play.pekko.dev-mode.pekko.coordinated-shutdown.phases.service-requests-done.timeout" -> "150s"

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
  "org.apache.httpcomponents.core5" % "httpcore5" % "5.2.4",
  "org.apache.httpcomponents.core5" % "httpcore5-h2" % "5.2.4",
  "org.apache.httpcomponents.client5" % "httpclient5" % "5.2.3",
  "org.apache.mina" % "mina-core" % "2.2.4",
  "org.flywaydb" %% "flyway-play" % "9.0.0",
  // https://github.com/YugaByte/cassandra-java-driver/releases
  "com.yugabyte" % "java-driver-core" % "4.15.0-yb-3",
  "org.yaml" % "snakeyaml" % "2.1",
  "org.bouncycastle" % "bc-fips" % "2.1.0",
  "org.bouncycastle" % "bcpkix-fips" % "2.1.9",
  "org.bouncycastle" % "bctls-fips" % "2.1.20",
  "org.mindrot" % "jbcrypt" % "0.4",
  "org.springframework.security" % "spring-security-core" % "5.8.16",
  // AWS SDK 2.x dependencies
  "software.amazon.awssdk" % "bom" % "2.33.10" pomOnly(),
  "software.amazon.awssdk" % "core" % "2.33.10",
  "software.amazon.awssdk" % "ec2" % "2.33.10",
  "software.amazon.awssdk" % "kms" % "2.33.10",
  "software.amazon.awssdk" % "iam" % "2.33.10",
  "software.amazon.awssdk" % "sts" % "2.33.10",
  "software.amazon.awssdk" % "s3" % "2.33.10",
  "software.amazon.awssdk" % "elasticloadbalancingv2" % "2.33.10",
  "software.amazon.awssdk" % "route53" % "2.33.10",
  "software.amazon.awssdk" % "cloudtrail" % "2.33.10",
  "net.minidev" % "json-smart" % "2.5.2",
  "com.cronutils" % "cron-utils" % "9.1.6",
  // Be careful when changing azure library versions.
  // Make sure all itests and existing functionality works as expected.
  // Used below azure versions from azure-sdk-bom:1.2.6
  "com.azure" % "azure-core-http-netty" % "1.16.2",
  "com.azure" % "azure-core" % "1.57.0",
  "com.azure" % "azure-identity" % "1.18.1",
  "com.azure" % "azure-security-keyvault-keys" % "4.10.3",
  "com.azure" % "azure-storage-blob" % "12.31.3",
  "com.azure" % "azure-storage-blob-batch" % "12.27.3",
  "com.azure.resourcemanager" % "azure-resourcemanager" % "2.55.0",
  "com.azure.resourcemanager" % "azure-resourcemanager-marketplaceordering" % "1.0.0",
  "com.github.seancfoley" % "ipaddress" % "2.0.1",
  "jakarta.mail" % "jakarta.mail-api" % "2.1.2",
  "org.eclipse.angus" % "jakarta.mail" % "1.0.0",
  "javax.validation" % "validation-api" % "2.0.1.Final",
  "io.prometheus" % "prometheus-metrics-core" % "1.4.3",
  "io.prometheus" % "prometheus-metrics-exporter-httpserver" % "1.4.3",
  "org.glassfish.jaxb" % "jaxb-runtime" % "2.3.2",
  // pac4j and nimbusds libraries need to be upgraded together.
  "org.pac4j" %% "play-pac4j" % "11.0.0-PLAY2.8",
  "org.pac4j" % "pac4j-oauth" % "5.7.7" exclude("commons-io" , "commons-io"),
  "org.pac4j" % "pac4j-oidc" % "5.7.7"  exclude("commons-io" , "commons-io"),
  "com.nimbusds" % "nimbus-jose-jwt" % "9.37.2",
  "com.nimbusds" % "oauth2-oidc-sdk" % "10.1",
  "org.playframework" %% "play-json" % "3.0.4",
  "commons-validator" % "commons-validator" % "1.10.0",
  "org.apache.velocity" % "velocity-engine-core" % "2.4.1",
  "com.fasterxml.woodstox" % "woodstox-core" % "6.4.0",
  "com.jayway.jsonpath" % "json-path" % "2.9.0",
  "commons-io" % "commons-io" % "2.15.1",
  "commons-codec" % "commons-codec" % "1.16.0",
  "com.google.apis" % "google-api-services-compute" % "v1-rev20241008-2.0.0",
  "com.google.apis" % "google-api-services-iam" % "v1-rev20240918-2.0.0",
  "com.google.cloud" % "google-cloud-compute" % "1.88.0",
  "com.google.cloud" % "google-cloud-storage" % "2.60.0",
  "com.google.cloud" % "google-cloud-kms" % "2.81.0",
  "com.google.cloud" % "google-cloud-resourcemanager" % "1.80.0",
  "com.google.cloud" % "google-cloud-logging" % "3.23.7",
  "com.google.oauth-client" % "google-oauth-client" % "1.35.0",
  "org.projectlombok" % "lombok" % "1.18.26",
  "com.squareup.okhttp3" % "okhttp" % "4.12.0",
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-xml" % "2.17.2",
  "com.google.protobuf" % "protobuf-java-util" % "3.20.3",
  "io.kamon" %% "kamon-bundle" % "2.7.5",
  "io.kamon" %% "kamon-prometheus" % "2.7.5",
  "org.unix4j" % "unix4j-command" % "0.6",
  "com.bettercloud" % "vault-java-driver" % "5.1.0",
  "org.apache.directory.api" % "api-all" % "2.1.7",
  "io.fabric8" % "crd-generator-apt" % "6.8.0",
  "io.fabric8" % "kubernetes-client" % "6.8.0",
  "io.fabric8" % "kubernetes-client-api" % "6.8.0",
  "io.fabric8" % "kubernetes-model" % "6.8.0",
  "io.fabric8" % "kubernetes-server-mock" % "6.8.0",
  "org.modelmapper" % "modelmapper" % "2.4.4",
  "com.datadoghq" % "datadog-api-client" % "2.25.0" classifier "shaded-jar",
  "javax.xml.bind" % "jaxb-api" % "2.3.1",
  "io.jsonwebtoken" % "jjwt-api" % "0.11.5",
  "io.jsonwebtoken" % "jjwt-impl" % "0.11.5",
  "io.jsonwebtoken" % "jjwt-jackson" % "0.11.5",
  "io.swagger" % "swagger-annotations" % "1.6.1", // needed for annotations in prod code
  "de.dentrassi.crypto" % "pem-keystore" % "3.0.0",
  // Prod dependency temporary as we use HSQLDB as a dummy perf_advisor DB for YBM scenario
  // Remove once YBM starts using real PG DB.
  "org.hsqldb" % "hsqldb" % "2.7.1",
  "org.mapstruct" %"mapstruct" % "1.5.5.Final",
  "org.mapstruct" %"mapstruct-processor" % "1.5.5.Final",
  "org.projectlombok" %"lombok-mapstruct-binding" % "0.2.0",
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
  "com.yugabyte" % "yba-client-v2" % "0.1.0-SNAPSHOT" % Test,
)

// Clear default resolvers.
appResolvers := None
bootResolvers := None
otherResolvers := Seq()


lazy val ybMvnLocalRepoEnvVarName = "YB_MVN_LOCAL_REPO"
lazy val ybLocalResolverDescription =
    "Local resolver (path can be customized with " +
    ybMvnLocalRepoEnvVarName + ")"
lazy val ybLocalResolver = {
  val localMavenRepo = getEnvVar(ybMvnLocalRepoEnvVarName)
  if (isDefined(localMavenRepo)) {
    val desc = "Custom local Maven repo at " + localMavenRepo
    Seq(desc at "file://" + localMavenRepo)
  } else {
    Seq(Resolver.mavenLocal)
  }
}

// Whether to use local maven repo to retrieve artifacts (used for yb-client).
lazy val ybUseMavenLocalEnvVarName = "USE_MAVEN_LOCAL"
lazy val mavenLocal = getBoolEnvVar(ybUseMavenLocalEnvVarName)
lazy val ybMvnSnapshotUrlEnvVarName = "YB_MVN_SNAPSHOT_URL"

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

(Compile / compile) := (Compile / compile).dependsOn(buildDependentArtifacts).value
(Test / test) := (Test / test).dependsOn(testDependentArtifacts).value

(Compile / compilePlatform) := {
  Def.sequential(
    buildVenv,
    (Compile / compile)
  ).value
  uIInstallDependency.value
  versionGenerate.value
  compileYbaCliBinary.value
  downloadThirdPartyDeps.value
}

clean := (clean dependsOn cleanV2ServerStubs).value

cleanPlatform := {
  clean.value
  (swagger / clean).value
  cleanOperatorConfig.value
  cleanThirdParty.value
  cleanCrd.value
  cleanVenv.value
  cleanUI.value
  cleanModules.value
  cleanV2ServerStubs.value
  cleanYbaCliBinary.value
  cleanClients.value
}

lazy val moveYbcPackageEnvName = "MOVE_YBC_PKG"
lazy val moveYbcPackage = getBoolEnvVar(moveYbcPackageEnvName)

versionGenerate := {
  val buildType = sys.env.getOrElse("BUILD_TYPE", "release")
  val status = Process("../python/yugabyte/gen_version_info.py --build-type=" + buildType + " " +
    (Compile / resourceDirectory).value / "version_metadata.json").!
  ybLog("version_metadata.json Generated")
  Process("rm -f " + (Compile / resourceDirectory).value / "gen_version_info.log").!
  var downloadYbcCmd = "./download_ybc.sh -c " + (Compile / resourceDirectory).value / "ybc_version.conf" + " -i"
  if (moveYbcPackage) {
    downloadYbcCmd = downloadYbcCmd + " -s"
  }
  val status2 = Process(downloadYbcCmd, baseDirectory.value).!
  status | status2
}

lazy val ybLogTask = inputKey[Unit]("Task to log a message")
ybLogTask := {
  val msg: String = spaceDelimited("<arg>").parsed.mkString(" ")
  ybLog(msg)
}

buildVenv / fileInputs += baseDirectory.value.toGlob /
    "devops/python3_requirements*.txt"
buildVenv := {
  val venvDir: String = get_venv_dir()
  if (buildVenv.inputFileChanges.hasChanges ||
    !(baseDirectory.value / "devops" / venvDir).exists) {
    ybLog("Building virtual env...")
    Process("./bin/install_python_requirements.sh", baseDirectory.value / "devops") #&&
      Process("./bin/install_ansible_requirements.sh --force", baseDirectory.value / "devops") !
  } else {
    ybLog("buildVenv already done. Call 'cleanVenv' to force build again.")
    0
  }
}

testDependentArtifacts := {
  ybLog("Testing modules...")
  val status = Process("mvn test", baseDirectory.value / "parent-module").!
  status
}

releaseModulesLocally := {
  ybLog("Releasing modules...")
  val status = Process("mvn install -DskipTests -P releaseLocally", baseDirectory.value / "parent-module").!
  status
}

buildDependentArtifacts / fileInputs += baseDirectory.value.toGlob /
  "node-agent/**"
buildDependentArtifacts / fileInputExcludeFilter :=
  ((path: java.nio.file.Path, attributes: FileAttributes) => {
    ".*(generated|target|third-party|pywheels|build|version_metadata).*".r.pattern.matcher(path.toString).matches
   })
buildDependentArtifacts := {
  (Compile / openApiProcessServer).value
  openApiProcessClients.value
  generateCrdObjects.value
  generateOssConfig.value
  if (buildDependentArtifacts.inputFileChanges.hasChanges ||
    !(baseDirectory.value / "node-agent" / "target").exists) {
    ybLog("Building dependencies...")
    val status = Process("mvn -DskipTests install", baseDirectory.value / "parent-module").!
    status
  } else {
    ybLog("buildDependentArtifacts already done. Call 'cleanModules' to force build again.")
    0
  }
}

generateOssConfig := {
  ybLog("Generating oss config class.")
  val srcTemplatePath = (baseDirectory.value / "src/main/resources/templates/OperatorConfig.template").toPath
  val generatedFilePath = (baseDirectory.value / "src/main/java/com/yugabyte/operator/OperatorConfig.java").toPath
  val directoryPath =  (baseDirectory.value / "src/main/java/com/yugabyte/operator/").toPath

  Files.createDirectories(directoryPath)

  val regex = "###OSSMODE###".r
  val replacement = if (sys.props.getOrElse("communityOperator.enabled", false) == "true") "true" else "false"
  val source = scala.io.Source.fromFile(srcTemplatePath.toFile)
  try {
    val content = regex.replaceAllIn(source.mkString, replacement)
    Files.write(generatedFilePath, content.getBytes(StandardCharsets.UTF_8))
  } finally {
    source.close()
  }
  0 // Assuming success
}

generateCrdObjects / fileInputs += baseDirectory.value.toGlob /
    "src/main/java/com/yugabyte/yw/common/operator/resources/" / ** / "*.yaml"
// Process and compile open api files
generateCrdObjects := {
  val generatedSourcesDirectory = baseDirectory.value / "target/operatorCRD"
  if (generateCrdObjects.inputFileChanges.hasChanges ||
    !generatedSourcesDirectory.exists) {
    ybLog("Generating crd classes...")
    val generatedSourcesDirectory = baseDirectory.value / "target/operatorCRD/"
    val command = s"mvn generate-sources -DoutputDirectory=$generatedSourcesDirectory"
    val status = Process(command, baseDirectory.value / "src/main/java/com/yugabyte/yw/common/operator/").!
    status
  } else {
    ybLog("Generated crd classes are up to date. Run 'cleanCrd' to force generate.")
    0
  }
}
Compile / unmanagedSourceDirectories += baseDirectory.value / "target/operatorCRD/"

downloadThirdPartyDeps / fileInputs += baseDirectory.value.toGlob /
  "support/thirdparty-dependencies.txt"
downloadThirdPartyDeps := {
  val downloadedMarkerPath = Paths.get("/opt/third-party/downloaded");
  if (downloadThirdPartyDeps.inputFileChanges.hasChanges || !Files.exists(downloadedMarkerPath)) {
    ybLog("Downloading third-party dependencies...")
    val status = Process("wget -Nqi thirdparty-dependencies.txt -P /opt/third-party -c", baseDirectory.value / "support").!
    Files.write(downloadedMarkerPath, "true".getBytes(StandardCharsets.UTF_8))
    status
  } else {
    ybLog("Thirdparty dependencies already downloaded. Run 'cleanThirdParty' to force download.")
    0
  }
}

devSpaceReload := {
  (Universal / packageBin).value
  Process("./devspace.sh", baseDirectory.value / "scripts") !
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
  "venv"
}

cleanVenv := {
  ybLog("Cleaning virtual env...")
  val venvDir: String = get_venv_dir()
  val status = Process("rm -rf " + venvDir, baseDirectory.value / "devops").!
  status
}

cleanOperatorConfig := {
  val filePath = baseDirectory.value / "src/main/java/com/yugabyte/operator/OperatorConfig.java"
  val file = sbt.file(filePath.toString)
  if (file.exists()) {
    sbt.IO.delete(file)
  }
}

cleanCrd := {
  ybLog("Cleaning CRD generated code...")
  val generatedSourcesDirectory = baseDirectory.value / "target/operatorCRD"
  val command = s"mvn clean -DoutputDirectory=$generatedSourcesDirectory"
  val status = Process(command, baseDirectory.value / "src/main/java/com/yugabyte/yw/common/operator/").!
  status
}

cleanThirdParty := {
  ybLog("Cleaning ThirdParty downloaded marker...")
  val filePath = "/opt/third-party/downloaded"
  val file = sbt.file(filePath)
  if (file.exists()) {
    sbt.IO.delete(file)
  }
}

lazy val cleanV2ServerStubs = taskKey[Int]("Clean v2 server stubs")
cleanV2ServerStubs := {
  ybLog("Cleaning Openapi v2 server stubs...")
  Process("rm -rf openapi", target.value) !
  val openapiDir = baseDirectory.value / "src/main/resources/openapi"
  Process("rm -f ../openapi.yaml ../openapi_public.yaml", openapiDir) !
}

lazy val cleanClients = taskKey[Int]("Clean generated clients")
cleanClients := {
  ybLog("Cleaning generated clients...")
  val javaDir = baseDirectory.value / "client/java"
  val pythonDir = baseDirectory.value / "client/python"
  val goDir = baseDirectory.value / "client/go"
  Process("find . -depth ! -path . ! -name pom.xml -exec rm -rf {} +", javaDir / "v1") #|
      Process("find . -depth ! -path . ! -name pom.xml -exec rm -rf {} +", javaDir / "v2") #|
      Process("rm -rf v1 v2 target", pythonDir) #|
      Process("rm -rf v1 v2 target", goDir) !
}

lazy val openApiBundle = taskKey[Unit]("Running bundle on openapi spec")
openApiBundle := {
  val rc = Process("./openapi_bundle.sh", baseDirectory.value / "scripts").!
  if (rc != 0) {
    throw new RuntimeException("openapi bundle failed!!!")
  }
}

lazy val openApiFormat = taskKey[Unit]("Format openapi files")
openApiFormat / fileInputs += baseDirectory.value.toGlob /
    "src/main/resources/openapi" / ** / "[!_]*.yaml"
openApiFormat := {
  import java.nio.file.Path
  def formatFile(file: Path): Unit = {
    ybLog(s"formatting api file $file")
    val rc = Process(s"./openapi_format.sh $file", baseDirectory.value / "scripts").!
    if (rc != 0) {
      throw new RuntimeException("openapi format failed!!!")
    }
  }
  val changes = openApiFormat.inputFileChanges
  val changedFiles = (changes.created ++ changes.modified).toSet
  changedFiles.par.foreach(formatFile)
}

lazy val openApiLint = taskKey[Unit]("Running lint on openapi spec")
openApiLint := {
  val rc = Process("./openapi_lint.sh", baseDirectory.value / "scripts").!
  if (rc != 0) {
    throw new RuntimeException("openapi lint failed!!!")
  }
}

lazy val jsOpenApiStubs = taskKey[Unit]("Generating JS Api Stubs")
jsOpenApiStubs := Def.taskDyn {
   Def.sequential(
    uIInstallDependency,
    Def.task {
      val rc = Process("npm run generateV2JSApiClient", baseDirectory.value / "ui").!
      if (rc != 0) {
        throw new RuntimeException("Generating JS Api Stubs failed")
      }
    }
   )
}.value

lazy val openApiProcessServer = taskKey[Seq[File]]("Process OpenApi files")
Compile / openApiProcessServer / fileInputs += baseDirectory.value.toGlob /
    "src/main/resources/openapi" / ** / "[!_]*.yaml"
Compile / openApiProcessServer / fileInputs += baseDirectory.value.toGlob /
    "src/main/resources/openapi_templates" / ** / "*.mustache"
// Process and compile open api files
Compile / openApiProcessServer := {
  if (openApiProcessServer.inputFileChanges.hasChanges ||
      !(baseDirectory.value / "src/main/resources/openapi.yaml").exists ||
      !(baseDirectory.value / "target/openapi/src").exists) {
    Def.taskDyn {
      val output = Def.sequential(
          ybLogTask.toTask(" Generating V2 server stubs..."),
          openApiFormat,
          openApiBundle,
          javaGenV2Server / openApiGenerate
        ).value
      Def.task{
        (javaGenV2Server / openApiStyleValidate).value
        openApiLint.value
        // return the list of generated java files to be managed
        output.filter(_.getName.endsWith(".java"))
      }}.value
  } else {
    ybLog("OpenApi server stubs already generated." +
        " Run 'cleanV2ServerStubs' to force regeneration.")
    Seq()
  }
}
Compile / openApiProcessServer / fileOutputs := Seq(
  target.value.toGlob / "openapi/src/main/java/" / ** / "*.java",
  baseDirectory.value.toGlob / "src/main/resources/openapi.yaml",
  baseDirectory.value.toGlob / "src/main/resources/openapi_public.yaml")
Compile / sourceGenerators += (Compile / openApiProcessServer)
Compile / unmanagedSourceDirectories += target.value / "openapi/src/main/java"

// Generate a Java v1 API client.
lazy val javagen = project.in(file("client/java"))
  .settings(
    openApiInputSpec := "src/main/resources/swagger.json",
    openApiGeneratorName := "java",
    openApiOutputDir := "client/java/v1",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/java/openapi-java-config.json",
    target := file("client/java/target/v1"),
  )

// Generate a Java v2 API client.
lazy val javaGenV2Client = project.in(file("client/java"))
  .settings(
    openApiInputSpec := "target/openapi/src/main/java/public/openapi.json",
    openApiGeneratorName := "java",
    openApiOutputDir := "client/java/v2",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/java/openapi-java-config-v2.json",
    openApiGlobalProperties += ("skipFormModel" -> "false"),
    target := file("client/java/target/v2"),
  )

// Compile generated java v1 and v2 clients
lazy val compileJavaGenV1Client = taskKey[Int]("Compile generated v1 Java client code")
compileJavaGenV1Client / fileInputs += baseDirectory.value.toGlob /
  "client/java/v1/" / ** / "*"
compileJavaGenV1Client := {
  if (compileJavaGenV1Client.inputFileChanges.hasChanges) {
    val localMavenRepo = getEnvVar(ybMvnLocalRepoEnvVarName)
    val cmdOpt = if (isDefined(localMavenRepo)) "-Dmaven.repo.local=" + localMavenRepo else ""
    val status = Process("mvn clean install -pl v1 -am " + cmdOpt, new File(baseDirectory.value + "/client/java")).!
    status
  } else {
    ybLog("OpenApi java client stubs for v1 are already generated." +
      " Run 'cleanClients' to force regeneration.")
    0
  }
}

lazy val compileJavaGenV2Client = taskKey[Int]("Compile generated v2 Java client code")
compileJavaGenV2Client / fileInputs += baseDirectory.value.toGlob /
  "client/java/v2/" / ** / "*"
compileJavaGenV2Client := {
  if (compileJavaGenV2Client.inputFileChanges.hasChanges) {
    val localMavenRepo = getEnvVar(ybMvnLocalRepoEnvVarName)
    val cmdOpt = if (isDefined(localMavenRepo)) "-Dmaven.repo.local=" + localMavenRepo else ""
    val status = Process("mvn clean install -pl v2 -am " + cmdOpt, new File(baseDirectory.value + "/client/java")).!
    status
  } else {
    ybLog("OpenApi java client stubs for v2 are already generated." +
      " Run 'cleanClients' to force regeneration.")
    0
  }
}

// Generate a Python API client.
lazy val pythongen = project.in(file("client/python"))
  .settings(
    openApiInputSpec := "src/main/resources/swagger.json",
    openApiGeneratorName := "python",
    openApiOutputDir := "client/python/v1",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/python/openapi-python-config.json",
    target := file("client/python/target/v1"),
  )

// Generate a Python V2 API client.
lazy val pythonGenV2Client = project.in(file("client/python"))
  .settings(
    openApiInputSpec := "target/openapi/src/main/java/public/openapi.json",
    openApiGeneratorName := "python",
    openApiOutputDir := "client/python/v2",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/python/openapi-python-config-v2.json",
    target := file("client/python/target/v2"),
  )

// Generate a Go API client.
lazy val gogen = project.in(file("client/go"))
  .settings(
    openApiInputSpec := "src/main/resources/swagger-all.json",
    openApiGeneratorName := "go",
    openApiOutputDir := "client/go/v1",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/go/openapi-go-config.json",
    target := file("client/go/target/v1"),
  )

// Generate a Go V2 API client.
lazy val goGenV2Client = project.in(file("client/go"))
  .settings(
    openApiInputSpec := "target/openapi/src/main/java/public/openapi.json",
    openApiGeneratorName := "go",
    openApiOutputDir := "client/go/v2",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/go/openapi-go-config-v2.json",
    target := file("client/go/target/v2"),
    openApiGlobalProperties += ("skipFormModel" -> "false"),
  )

// Compile generated go v1 and v2 clients
lazy val compileGoGenV1Client = taskKey[Int]("Compile generated v1 Go clients")
compileGoGenV1Client := {
  val status = Process("make testv1", new File(baseDirectory.value + "/client/go/")).!
  status
}
lazy val compileGoGenV2Client = taskKey[Int]("Compile generated v2 Go clients")
compileGoGenV2Client := {
  val status = Process("make testv2", new File(baseDirectory.value + "/client/go/")).!
  status
}

// Compile the YBA CLI binary
lazy val compileYbaCliBinary = taskKey[(Int, Seq[String])]("Compile YBA CLI Binary")
compileYbaCliBinary := {
  var status = 0
  var completeFileList = Seq.empty[String]
  var fileList = Seq.empty[String]

  ybLog("Generating YBA CLI go binary.")

  val (status1, fileList1) = makeYbaCliPackage("linux", baseDirectory.value)
  completeFileList = fileList1
  status = status1

  val (status2, fileList2) = makeYbaCliPackage("darwin", baseDirectory.value)
  completeFileList = completeFileList ++ fileList2
  status = status max status2


  (status, completeFileList)
}

compileYbaCliBinary := ((compileYbaCliBinary) dependsOn versionGenerate).value

def makeYbaCliPackage(goos: String, directory: java.io.File): (Int, Seq[String]) = {

  var status = 0
  var output = Seq.empty[String]
  var fileList = Seq.empty[String]

  val processLogger = ProcessLogger(
    line => output :+= line,
    line => println(s"Error: $line")
  )
  val env = Seq("GOOS" -> goos)
  val process = Process("make package", new File(directory + "/yba-cli/"), env: _*)
  status = process.!(processLogger)
  if (status == 0) {
    val fileListIndex = output.indexWhere(_.startsWith("Folder path for"))
    fileList = if (fileListIndex != -1) output.drop(fileListIndex + 1) else Seq.empty[String]
  } else {
    fileList = Seq.empty[String]
  }

  (status, fileList)
}

// Clean the YBA CLI binary
lazy val cleanYbaCliBinary = taskKey[Int]("Clean YBA CLI Binary")
cleanYbaCliBinary := {
  ybLog("Cleaning YBA CLI go binary.")

  var status = cleanYbaCliPackage("linux", baseDirectory.value)
  status = cleanYbaCliPackage("darwin", baseDirectory.value)

  status
}

def cleanYbaCliPackage(goos: String, directory: java.io.File): Int = {
  val env = Seq("GOOS" -> goos)
  val status = Process("make clean", new File(directory + "/yba-cli/"), env: _*).!

  status
}

lazy val openApiProcessClients = taskKey[Unit]("Generate and compile openapi clients")
openApiProcessClients / fileInputs += baseDirectory.value.toGlob / "src/main/resources/openapi.yaml"
openApiProcessClients := {
  if (openApiProcessClients.inputFileChanges.hasChanges |
      !(baseDirectory.value / "client/java/v2/build.sbt").exists() ||
      !(baseDirectory.value / "ui/src/v2/api").exists)
    Def.sequential(
      ybLogTask.toTask(" openapi.yaml file has changed, so regenerating clients..."),
      cleanClients,
      openApiGenClients,
      openApiCompileClients,
      jsOpenApiStubs
    ).value
  else
    ybLog("Generated Openapi clients are up to date. Run 'cleanClients' to force generation.")
}

lazy val openApiGenClients = taskKey[Unit]("Generating openapi v2 clients")
openApiGenClients := {
  (javaGenV2Client / openApiGenerate).value
  (pythonGenV2Client / openApiGenerate).value
  (goGenV2Client / openApiGenerate).value
}
openApiGenClients := openApiGenClients.dependsOn(Compile/openApiProcessServer).value

lazy val openApiCompileClients = taskKey[Unit]("Compiling openapi v2 clients")
openApiCompileClients := {
  compileJavaGenV2Client.value
  compileGoGenV2Client.value
  // no compilation or running tests for python client
}

lazy val swaggerGenClients = taskKey[Unit]("Generating swagger v1 clients")
swaggerGenClients := {
  (javagen / openApiGenerate).value
  (pythongen / openApiGenerate).value
  (gogen / openApiGenerate).value
}

lazy val swaggerCompileClients = taskKey[Unit]("Compiling swagger v1 clients")
swaggerCompileClients := {
  compileJavaGenV1Client.value
  compileGoGenV1Client.value
  // no compilation or running tests for python client
}

// Generate Java V2 API server stubs.
val resDir = "../../src/main/resources"
lazy val javaGenV2Server = project.in(file("target/openapi"))
  .enablePlugins(OpenApiGeneratorPlugin, OpenApiStylePlugin)
  .dependsOn(root % "compile->compile;test->test")
  .settings(commonSettings)
  .settings(
    openApiGeneratorName := "java-play-framework",
    openApiInputSpec := (baseDirectory.value / resDir / "openapi.yaml").absolutePath,
    openApiOutputDir := (baseDirectory.value / "src/main/java/").absolutePath,
    openApiConfigFile := (baseDirectory.value / resDir / "openapi-java-server-config.json").absolutePath,
    openApiTemplateDir := (baseDirectory.value / resDir / "openapi_templates/").absolutePath,
    openApiValidateSpec := SettingDisabled,
    openApiGenerate := (openApiGenerate dependsOn openApiCopyIgnoreFile).value,
    // style plugin configurations
    openApiStyleSpec := baseDirectory.value / resDir / "openapi.yaml",
    openApiStyleConfig := Some(baseDirectory.value / resDir / "openapi_style_validator.conf"),
    openApiGlobalProperties += ("skipFormModel" -> "false"),

    excludeDependencies += "org.yb" % "yb-client",
    excludeDependencies += "com.yugabyte" % "yba-client-v2"
  )

// copy over the ignore file manually since openApiIgnoreFileOverride does not work
lazy val openApiCopyIgnoreFile = taskKey[Unit]("Copy the openapi ignore file to target")
javaGenV2Server / openApiCopyIgnoreFile := {
  val src = (baseDirectory.value / "src/main/resources/.openapi-generator-ignore").toPath
  var tgt = (baseDirectory.value / "target/openapi/src/main/java/.openapi-generator-ignore").toPath
  ybLog("Copying " + src + " to " + tgt)
  Files.createDirectories((baseDirectory.value / "target/openapi/src/main/java/").toPath)
  Files.copy(src, tgt, java.nio.file.StandardCopyOption.REPLACE_EXISTING)
}

Universal / packageZipTarball := (Universal / packageZipTarball).dependsOn(versionGenerate, buildDependentArtifacts).value

// Being used by DevSpace tool to build an archive without building the UI
Universal / packageBin := (Universal / packageBin).dependsOn(versionGenerate, buildDependentArtifacts).value

Universal / javaOptions += "-J-XX:G1PeriodicGCInterval=120000"

// Enable viewing Java call stacks in "perf" tool
Universal / javaOptions += "-J-XX:+PreserveFramePointer"

// Disable shutdown hook of ebean to let play manage its lifecycle.
Universal / javaOptions += "-Debean.registerShutdownHook=false"

Universal / mappings ++= {
  val (status, cliFolders) = compileYbaCliBinary.value
  if (status == 0) {
    cliFolders.flatMap { folderPath =>
      val folder = file(folderPath)
      if (folder.isDirectory) {
        val targetPath = s"yba-cli/${folder.getName}"
        val folderMappings = (folder ** "*") pair Path.rebase(folder, targetPath)
        folderMappings
      } else {
        println(s"Warning: $folderPath is not a directory and will not be included in the package.")
        Nil
      }
    }
  } else {
    ybLog("Error generating YBA CLI binary.")
    Seq.empty
  }
}

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

libraryDependencies += "org.yb" % "yb-client" % "0.8.108-SNAPSHOT"
libraryDependencies += "org.yb" % "ybc-client" % "2.2.0.3-b17"
libraryDependencies += "org.yb" % "yb-perf-advisor" % "1.0.0-b35"

libraryDependencies ++= Seq(
  "io.netty" % "netty-tcnative-boringssl-static" % "2.0.54.Final",
  "io.netty" % "netty-codec-haproxy" % "4.1.89.Final",
  "io.projectreactor.netty" % "reactor-netty-http" % "1.0.39",
  "org.slf4j" % "slf4j-ext" % "1.7.26",
)


dependencyOverrides += "org.reflections" % "reflections" % "0.10.2"

// Following library versions for jersey, jakarta glassfish, jakarta ws.rs and
// jackson-module-jaxb-annotations are needed by the openapi java client. The
// datadog-api-client library also needs them, but the newer versions
// pulled by datadog-api-client are not compatible with the openapi java client. So
// fixing these to older versions.
val jerseyVersion = "2.41"
dependencyOverrides += "org.glassfish.jersey.connectors" % "jersey-apache-connector" % jerseyVersion % Test
dependencyOverrides += "org.glassfish.jersey.core" % "jersey-client" % jerseyVersion % Test
dependencyOverrides += "org.glassfish.jersey.core" % "jersey-common" % jerseyVersion % Test
dependencyOverrides += "org.glassfish.jersey.ext" % "jersey-entity-filtering" % jerseyVersion % Test
dependencyOverrides += "org.glassfish.jersey.inject" % "jersey-hk2" % jerseyVersion % Test
dependencyOverrides += "org.glassfish.jersey.media" % "jersey-media-json-jackson" % jerseyVersion % Test
dependencyOverrides += "org.glassfish.jersey.media" % "jersey-media-multipart" % jerseyVersion % Test

val hk2Version = "2.6.1"
dependencyOverrides += "org.glassfish.hk2.external" % "aopalliance-repackaged" % hk2Version % Test
dependencyOverrides += "org.glassfish.hk2.external" % "javax.inject" % hk2Version % Test
dependencyOverrides += "org.glassfish.hk2" % "hk2-api" % hk2Version % Test
dependencyOverrides += "org.glassfish.hk2" % "hk2-locator" % hk2Version % Test
dependencyOverrides += "org.glassfish.hk2" % "hk2-utils" % hk2Version % Test

dependencyOverrides += "jakarta.annotation" % "jakarta.annotation-api" % "1.3.5" % Test
dependencyOverrides += "jakarta.ws.rs" % "jakarta.ws.rs-api" % "2.1.6" % Test
dependencyOverrides += "com.fasterxml.jackson.module" % "jackson-module-jaxb-annotations" % "2.10.1" % Test

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
  "com.fasterxml.jackson.dataformat" % "jackson-dataformat-toml",
  "com.fasterxml.jackson.module"     % "jackson-module-parameter-names",
  "com.fasterxml.jackson.module"     %% "jackson-module-scala",
)

val jacksonOverrides = jacksonLibs.map(_ % jacksonVersion)

dependencyOverrides ++= jacksonOverrides

excludeDependencies += "org.eclipse.jetty" % "jetty-io"
excludeDependencies += "org.eclipse.jetty" % "jetty-server"
excludeDependencies += "commons-collections" % "commons-collections"
excludeDependencies += "org.bouncycastle" % "bcpkix-jdk15on"
excludeDependencies += "org.bouncycastle" % "bcprov-jdk15on"
excludeDependencies += "org.bouncycastle" % "bcpkix-jdk18on"
excludeDependencies += "org.bouncycastle" % "bcprov-jdk18on"

Global / concurrentRestrictions := Seq(Tags.limitAll(16))

val testParallelForks = SettingKey[Int]("testParallelForks",
  "Number of parallel forked JVMs, running tests")
testParallelForks := 4
val testShardSize = SettingKey[Int]("testShardSize",
  "Number of test classes, executed by each forked JVM")
testShardSize := 30

Global / concurrentRestrictions += Tags.limit(Tags.ForkedTestGroup, testParallelForks.value)

def partitionTests(tests: Seq[TestDefinition], shardSize: Int) =
  tests.sortWith(_.name.hashCode() < _.name.hashCode()).grouped(shardSize).zipWithIndex map {
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
testOptions += Tests.Filter(s =>
  !s.contains("com.yugabyte.yw.commissioner.tasks.local")
)

lazy val testLocal = inputKey[Unit]("Runs local provider tests")
lazy val testFast = inputKey[Unit]("Runs quick tests")
lazy val testUpgradeRetry = inputKey[Unit]("Runs retry tests")

def localTestSuiteFilter(name: String): Boolean = (name startsWith "com.yugabyte.yw.commissioner.tasks.local")
def quickTestSuiteFilter(name: String): Boolean =
  !(name.startsWith("com.yugabyte.yw.commissioner.tasks.local") ||
    name.startsWith("com.yugabyte.yw.commissioner.tasks.upgrade"))
def upgradeRetryTestSuiteFilter(name: String): Boolean = (name startsWith "com.yugabyte.yw.commissioner.tasks.upgrade")

TestLocalProviderSuite / javaOptions += "-Dconfig.resource=application.test.conf"
TestLocalProviderSuite / testOptions := Seq(Tests.Filter(localTestSuiteFilter))
TestLocalProviderSuite / testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")
testLocal := (TestLocalProviderSuite / test).value

TestQuickSuite / javaOptions += "-Dconfig.resource=application.test.conf"
TestQuickSuite / testOptions := Seq(Tests.Filter(quickTestSuiteFilter))
TestQuickSuite / testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")
testFast := (TestQuickSuite / test).value

TestRetrySuite / javaOptions += "-Dconfig.resource=application.test.conf"
TestRetrySuite / testOptions := Seq(Tests.Filter(upgradeRetryTestSuiteFilter))
TestRetrySuite / testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")
testUpgradeRetry := (TestRetrySuite / test).value

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

val swaggerGenTest: TaskKey[Unit] = taskKey[Unit](
  "test generate swagger.json"
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

    excludeDependencies += "org.bouncycastle" % "bcpkix-jdk15on",
    excludeDependencies += "org.bouncycastle" % "bcprov-jdk15on",
    excludeDependencies += "org.bouncycastle" % "bcpkix-jdk18on",
    excludeDependencies += "org.bouncycastle" % "bcprov-jdk18on",

    // This is for Idea import - because otherwise it's not using any custom resolvers
    // For regular SBT build externalResolvers list is used.
    resolvers ++= validateResolver(mavenCacheServerResolver, mavenCacheServerResolverDescription),
    resolvers ++= validateResolver(ybLocalResolver, ybLocalResolverDescription),
    resolvers ++= validateResolver(ybClientSnapshotResolver, ybClientSnapshotResolverDescription),
    resolvers ++= validateResolver(ybPublicSnapshotResolver, ybPublicSnapshotResolverDescription),

    swaggerGen := Def.taskDyn {
      // Consider generating this only in managedResources
      val swaggerJson = (root / Compile / resourceDirectory).value / "swagger.json"
      val swaggerStrictJson = (root / Compile / resourceDirectory).value / "swagger-strict.json"
      val swaggerAllJson = (root / Compile / resourceDirectory).value / "swagger-all.json"
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
        (Test / runMain )
          .toTask(s" com.yugabyte.yw.controllers.SwaggerGenTest $swaggerAllJson --exclude_internal none")
      )
    }.value,

    swaggerGenTest := {
        (root / Test / testOnly).toTask(s" com.yugabyte.yw.controllers.YbaApiTest").value
        (Test / testOnly).toTask(s" com.yugabyte.yw.controllers.SwaggerGenTest").value
    }
  )

Test / test := (Test / test).dependsOn(swagger / Test / test).value

commands += Command.command("swaggerGen") { state =>
  "swagger/swaggerGen" ::
  "swagger/swaggerGenTest" ::
  "swaggerGenClients" ::
  "swaggerCompileClients" ::
  "openApiGenClients" ::
  "openApiCompileClients" ::
  state
}

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

/**
  * UI Build Tasks like clean node modules, npm install and npm run build
  */

// Execute `npm ci` command to install all node module dependencies. Return 0 if success.
def runNpmInstall(implicit dir: File): Int =
{
  println("node version: " + Process("node" :: "--version" :: Nil).lineStream_!.head)
  println("npm version: " + Process("npm" :: "--version" :: Nil).lineStream_!.head)
  println("npm config get: " + Process("npm" :: "config" :: "get" :: Nil).lineStream_!.head)
  println("npm cache verify: " + Process("npm" :: "cache" :: "verify" :: Nil).lineStream_!.head)
  Process("npm" :: "ci" :: "--legacy-peer-deps" :: Nil, dir).!
}

// Execute `npm run build` command to build the production build of the UI code. Return 0 if success.
def runNpmBuild(implicit dir: File): Int =
  Process("npm run build-and-copy", dir)!

lazy val uIInstallDependency = taskKey[Unit]("Install NPM dependencies")
lazy val uIBuild = taskKey[Unit]("Build production version of UI code.")

uIInstallDependency / fileInputs += baseDirectory.value.toGlob /
  "ui/**"
uIInstallDependency / fileInputExcludeFilter :=
  ((path: java.nio.file.Path, attributes: FileAttributes) => {
    ".*(node_modules).*".r.pattern.matcher(path.toString).matches
  })
uIInstallDependency := {
  if (uIInstallDependency.inputFileChanges.hasChanges ||
    !(baseDirectory.value / "ui/node_modules").exists) {
    ybLog("Installing UI dependencies.")
    implicit val uiSource = baseDirectory.value / "ui"
    if (runNpmInstall != 0) throw new Exception("npm install failed")
  } else {
    ybLog("UI dependencies are up to date. Run 'cleanUI' to force rebuild.")
  }
}
uIBuild := {
  implicit val uiSource = baseDirectory.value / "ui"
  if (runNpmBuild != 0) throw new Exception("UI Build crashed.")
}

uIBuild := (uIBuild dependsOn (buildDependentArtifacts)).value

/**
 *  Make SBT packaging depend on the UI build hook.
 */
Universal / packageZipTarball := (Universal / packageZipTarball).dependsOn(uIBuild).value

addCommandAlias("api", "cleanV2ServerStubs;openApiProcessServer;compile")
