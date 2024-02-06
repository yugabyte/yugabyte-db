import jline.console.ConsoleReader
import play.sbt.PlayImport.PlayKeys.{playInteractionMode, playMonitoredFiles}
import play.sbt.PlayInteractionMode
import java.nio.charset.StandardCharsets
import java.nio.file.{FileSystems, Files}
import sbt.Tests._

import scala.collection.JavaConverters._
import scala.sys.process.Process
import scala.sys.process._

historyPath := Some(file(System.getenv("HOME") + "/.sbt/.yugaware-history"))

useCoursier := false


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
lazy val cleanOperatorConfig = taskKey[Unit]("Clean OperatorConfig")

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
  "com.google.inject"            % "guice"                % "5.1.0",
  "com.google.inject.extensions" % "guice-assistedinject" % "5.1.0",
  "org.postgresql" % "postgresql" % "42.3.3",
  "net.logstash.logback" % "logstash-logback-encoder" % "6.2",
  "ch.qos.logback" % "logback-classic" % "1.4.14",
  "org.codehaus.janino" % "janino" % "3.1.9",
  "org.apache.commons" % "commons-lang3" % "3.14.0",
  "org.apache.commons" % "commons-collections4" % "4.4",
  "org.apache.commons" % "commons-compress" % "1.25.0",
  "org.apache.commons" % "commons-csv" % "1.10.0",
  "org.apache.httpcomponents.core5" % "httpcore5" % "5.2.4",
  "org.apache.httpcomponents.core5" % "httpcore5-h2" % "5.2.4",
  "org.apache.httpcomponents.client5" % "httpclient5" % "5.2.3",
  "org.flywaydb" %% "flyway-play" % "9.0.0",
  // https://github.com/YugaByte/cassandra-java-driver/releases
  "com.yugabyte" % "cassandra-driver-core" % "3.8.0-yb-7",
  "org.yaml" % "snakeyaml" % "2.1",
  "org.bouncycastle" % "bcpkix-jdk15on" % "1.61",
  "org.springframework.security" % "spring-security-core" % "5.8.3",
  "com.amazonaws" % "aws-java-sdk-ec2" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-kms" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-iam" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-sts" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-s3" % "1.12.599",
  "com.amazonaws" % "aws-java-sdk-elasticloadbalancingv2" % "1.12.327",
  "com.amazonaws" % "aws-java-sdk-route53" % "1.12.400",
  "com.amazonaws" % "aws-java-sdk-cloudtrail" % "1.12.498",
  "net.minidev" % "json-smart" % "2.5.0",
  "com.cronutils" % "cron-utils" % "9.1.6",
  // Be careful when changing azure library versions.
  // Make sure all itests and existing functionality works as expected.
  // Used below azure versions from azure-sdk-bom:1.2.6
  "com.azure" % "azure-core" % "1.32.0",
  "com.azure" % "azure-identity" % "1.6.0",
  "com.azure" % "azure-security-keyvault-keys" % "4.5.0",
  "com.azure" % "azure-storage-blob" % "12.19.1",
  "com.azure.resourcemanager" % "azure-resourcemanager" % "2.28.0",
  "jakarta.mail" % "jakarta.mail-api" % "2.1.2",
  "org.eclipse.angus" % "jakarta.mail" % "1.0.0",
  "javax.validation" % "validation-api" % "2.0.1.Final",
  "io.prometheus" % "simpleclient" % "0.11.0",
  "io.prometheus" % "simpleclient_hotspot" % "0.11.0",
  "io.prometheus" % "simpleclient_servlet" % "0.11.0",
  "org.glassfish.jaxb" % "jaxb-runtime" % "2.3.2",
  "org.pac4j" %% "play-pac4j" % "9.0.2",
  "org.pac4j" % "pac4j-oauth" % "4.5.7" exclude("commons-io" , "commons-io"),
  "org.pac4j" % "pac4j-oidc" % "4.5.7" exclude("commons-io" , "commons-io"),
  "org.playframework" %% "play-json" % "3.0.1",
  "commons-validator" % "commons-validator" % "1.8.0",
  "org.apache.velocity" % "velocity-engine-core" % "2.3",
  "com.fasterxml.woodstox" % "woodstox-core" % "6.4.0",
  "com.jayway.jsonpath" % "json-path" % "2.6.0",
  "commons-io" % "commons-io" % "2.15.1",
  "commons-codec" % "commons-codec" % "1.16.0",
  "com.google.apis" % "google-api-services-compute" % "v1-rev20220506-1.32.1",
  "com.google.apis" % "google-api-services-iam" % "v1-rev20211104-1.32.1",
  "com.google.cloud" % "google-cloud-compute" % "1.9.1",
  "com.google.cloud" % "google-cloud-storage" % "2.2.1",
  "com.google.cloud" % "google-cloud-kms" % "2.4.4",
  "com.google.cloud" % "google-cloud-resourcemanager" % "1.4.0",
  "com.google.cloud" % "google-cloud-logging" % "3.14.5",
  "com.google.oauth-client" % "google-oauth-client" % "1.34.1",
  "org.projectlombok" % "lombok" % "1.18.26",
  "com.squareup.okhttp3" % "okhttp" % "4.12.0",
  "io.kamon" %% "kamon-bundle" % "2.5.9",
  "io.kamon" %% "kamon-prometheus" % "2.5.9",
  "org.unix4j" % "unix4j-command" % "0.6",
  "com.bettercloud" % "vault-java-driver" % "5.1.0",
  "org.apache.directory.api" % "api-all" % "2.1.0",
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
  "io.grpc" % "grpc-testing" % "1.48.0" % Test,
  "io.zonky.test" % "embedded-postgres" % "2.0.1" % Test,
  "org.springframework" % "spring-test" % "5.3.9" % Test,
  "com.yugabyte" % "yba-client-v2" % "0.1.0-SNAPSHOT" % "test",
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

(Compile / compile) := ((Compile / compile) dependsOn buildDependentArtifacts).value

(Compile / compilePlatform) := {
  Def.sequential(
    (Compile / compile),
    releaseModulesLocally
  ).value
  buildUI.value
  versionGenerate.value
  downloadThirdPartyDeps.value
}

cleanPlatform := {
  clean.value
  (swagger / clean).value
  cleanOperatorConfig.value
  cleanCrd.value
  cleanVenv.value
  cleanUI.value
  cleanModules.value
  cleanV2ServerStubs.value
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
  Def.sequential(
      buildVenv,
      openApiProcess
    ).value
  generateCrdObjects.value
  generateOssConfig.value
  val status = Process("mvn install -P buildDependenciesOnly", baseDirectory.value / "parent-module").!
  status
}

generateOssConfig := {
  ybLog("Generating oss config class.")
  val srcTemplatePath = (baseDirectory.value / "src/main/resources/templates/OperatorConfig.template").toPath
  val generatedFilePath = (baseDirectory.value / "target/scala-2.13/com/yugabyte/operator/OperatorConfig.java").toPath
  val directoryPath =  (baseDirectory.value / "target/scala-2.13/com/yugabyte/operator/").toPath

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

generateCrdObjects := {
  ybLog("Generating crd classes...")
  val generatedSourcesDirectory = baseDirectory.value / "target/scala-2.13/"
  val command = s"mvn generate-sources -DoutputDirectory=$generatedSourcesDirectory"
  val status = Process(command, baseDirectory.value / "src/main/java/com/yugabyte/yw/common/operator/").!
  status
}

downloadThirdPartyDeps := {
  ybLog("Downloading third-party dependencies...")
  val status = Process("wget -qi thirdparty-dependencies.txt -P /opt/third-party -c", baseDirectory.value / "support").!
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
  "venv"
}

cleanVenv := {
  ybLog("Cleaning virtual env...")
  val venvDir: String = get_venv_dir()
  val status = Process("rm -rf " + venvDir, baseDirectory.value / "devops").!
  status
}

cleanOperatorConfig := {
  val filePath = baseDirectory.value / "target/scala-2.13/OperatorConfig.java"
  val file = sbt.file(filePath.toString)
  if (file.exists()) {
    sbt.IO.delete(file)
  }
}

cleanCrd := {
  ybLog("Cleaning CRD generated code...")
  val generatedSourcesDirectory = baseDirectory.value / "target/scala-2.13/"
  val command = s"mvn clean -DoutputDirectory=$generatedSourcesDirectory"
  val status = Process(command, baseDirectory.value / "src/main/java/com/yugabyte/yw/common/operator/").!
  status
}

lazy val cleanV2ServerStubs = taskKey[Int]("Clean v2 server stubs")
cleanV2ServerStubs := {
  ybLog("Cleaning Openapi v2 server stubs...")
  val apiDir = baseDirectory.value / "src/main/java/api/v2/"
  Process("find . -name *ApiController.java -o -name *ApiControllerImpInterface.java", apiDir) #|
      Process("xargs rm -f", apiDir) #|
      Process("rm -rf models", apiDir) !
  val openapiDir = baseDirectory.value / "src/main/resources/openapi"
  Process("rm -f paths/_index.yaml", openapiDir) #|
      Process("rm -f ../openapi.yaml ../openapi_public.yaml", openapiDir) !
}

lazy val cleanClients = taskKey[Int]("Clean generated clients")
cleanClients := {
  ybLog("Cleaning generated clients...")
  val javaDir = baseDirectory.value / "client/java"
  val pythonDir = baseDirectory.value / "client/python"
  val goDir = baseDirectory.value / "client/go"
  Process("rm -rf v1 v2", javaDir) #|
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
openApiFormat := {
  val rc = Process("./openapi_format.sh", baseDirectory.value / "scripts").!
  if (rc != 0) {
    throw new RuntimeException("openapi format failed!!!")
  }
}

lazy val openApiLint = taskKey[Unit]("Running lint on openapi spec")
openApiLint := {
  val rc = Process("./openapi_lint.sh", baseDirectory.value / "scripts").!
  if (rc != 0) {
    throw new RuntimeException("openapi lint failed!!!")
  }
}

lazy val openApiProcess = taskKey[Unit]("Process OpenApi files")
// Process and compile open api files
openApiProcess := Def.sequential(
  openApiFormat,
  openApiBundle,
  javaGenV2Server / openApiGenerate,
  javaGenV2Server / openApiStyleValidate,
  openApiLint
).value

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
    openApiInputSpec := "src/main/java/public/openapi.json",
    openApiGeneratorName := "java",
    openApiOutputDir := "client/java/v2",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/java/openapi-java-config-v2.json",
    target := file("client/java/target/v2"),
  )

// Compile generated java v1 and v2 clients
lazy val compileJavaGenClient = taskKey[Int]("Compile generated Java code")
compileJavaGenClient := {
  val localMavenRepo = getEnvVar(ybMvnLocalRepoEnvVarName)
  val cmdOpt = if (isDefined(localMavenRepo)) "-Dmaven.repo.local=" + localMavenRepo else ""
  val status = Process("mvn clean install " + cmdOpt, new File(baseDirectory.value + "/client/java/")).!
  status
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
    openApiInputSpec := "src/main/java/public/openapi.json",
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
    openApiInputSpec := "src/main/resources/swagger.json",
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
    openApiInputSpec := "src/main/java/public/openapi.json",
    openApiGeneratorName := "go",
    openApiOutputDir := "client/go/v2",
    openApiGenerateModelTests := SettingDisabled,
    openApiGenerateApiTests := SettingDisabled,
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "client/go/openapi-go-config-v2.json",
    target := file("client/go/target/v2"),
  )

// Compile generated go v1 and v2 clients
lazy val compileGoGenClient = taskKey[Int]("Compile generated Go clients")
compileGoGenClient := {
  val status = Process("make", new File(baseDirectory.value + "/client/go/")).!
  status
}

// Require sbt to first generate clients and install in local mvn repo
// so that unit tests libraryDependencies can depend on them
update := update.dependsOn(openApiClients).value

lazy val openApiClients = taskKey[Unit]("Generate and compile openapi clients")
openApiClients := Def.sequential(
  openApiGenClients,
  openApiCompileClients,
).value

lazy val openApiGenClients = taskKey[Unit]("Generating openapi clients")
openApiGenClients := {
  (javagen / openApiGenerate).value
  (pythongen / openApiGenerate).value
  (gogen / openApiGenerate).value
  (javaGenV2Client / openApiGenerate).value
  (pythonGenV2Client / openApiGenerate).value
  (goGenV2Client / openApiGenerate).value
}

lazy val openApiCompileClients = taskKey[Unit]("Compiling openapi clients")
openApiCompileClients := {
  compileJavaGenClient.value
  compileGoGenClient.value
  // no compilation or running tests for python client
}

// Generate Java V2 API server stubs.
lazy val javaGenV2Server = project.in(file("src"))
  .enablePlugins(OpenApiGeneratorPlugin, OpenApiStylePlugin)
  .settings(
    openApiInputSpec := "src/main/resources/openapi.yaml",
    openApiGeneratorName := "java-play-framework",
    openApiOutputDir := "src/main/java/",
    openApiValidateSpec := SettingDisabled,
    openApiConfigFile := "src/main/resources/openapi-java-server-config.json",
    // style plugin configurations
    openApiStyleSpec := file("src/main/resources/openapi.yaml"),
    openApiStyleConfig := Some(file("src/main/resources/openapi_style_validator.conf")),
  )

Universal / packageZipTarball := (Universal / packageZipTarball).dependsOn(versionGenerate, buildDependentArtifacts).value

// Being used by DevSpace tool to build an archive without building the UI
Universal / packageBin := (Universal / packageBin).dependsOn(versionGenerate, buildDependentArtifacts).value

Universal / javaOptions += "-J-XX:G1PeriodicGCInterval=120000"



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

libraryDependencies += "org.yb" % "yb-client" % "0.8.76-SNAPSHOT"
libraryDependencies += "org.yb" % "ybc-client" % "2.1.0.0-b2"
libraryDependencies += "org.yb" % "yb-perf-advisor" % "1.0.0-b33"

libraryDependencies ++= Seq(
  "io.netty" % "netty-tcnative-boringssl-static" % "2.0.54.Final",
  "io.netty" % "netty-codec-haproxy" % "4.1.89.Final",
  "org.slf4j" % "slf4j-ext" % "1.7.26",
  "com.nimbusds" % "nimbus-jose-jwt" % "7.9",
)

dependencyOverrides += "com.google.protobuf" % "protobuf-java" % "3.21.7"
dependencyOverrides += "com.google.guava" % "guava" % "32.1.1-jre"
// SSO functionality only works on the older version of nimbusds.
// Azure library upgrade tries to upgrade nimbusds to latest version.
dependencyOverrides += "com.nimbusds" % "oauth2-oidc-sdk" % "7.1.1"
dependencyOverrides += "org.reflections" % "reflections" % "0.10.2"

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

// Skip packaging javadoc for now
Compile / doc / sources := Seq()
Compile / doc / publishArtifact.withRank(KeyRanks.Invisible) := false

topLevelDirectory := None

// Skip auto-recompile of code in dev mode if AUTO_RELOAD=false
lazy val autoReload = getBoolEnvVar("AUTO_RELOAD")
playMonitoredFiles := {
  val dirs = playMonitoredFiles.value
  if (autoReload) {
    // remove java and resources dirs and instead add sub-dirs later
    val dirsFiltered = dirs.filterNot{
      f: File =>
        f.getPath().endsWith("src/main/java") ||
        f.getPath().endsWith("src/main/resources")
    }
    // Don't monitor dirs where openapi generates files. Since it leads to infinite auto reload.
    // add all dirs under src/main/java except api
    // TODO: Editing the imp files under src/main/java/api/v2/controllers should trigger auto reload
    val srcDir = FileSystems.getDefault.getPath(baseDirectory.value + "/src/main/java")
    val srcSubDirs = Files.list(srcDir).iterator().asScala
                     .filter(Files.isDirectory(_))
                     .filterNot(_.endsWith("api/v2"))
                     .map(_.toFile).toList

    // add all dirs under src/main/resources except openapi
    // TODO: Editing files directly under src/main/resources should trigger auto reload
    val resDir = FileSystems.getDefault.getPath(baseDirectory.value + "/src/main/resources")
    val resSubDirs = Files.list(resDir).iterator().asScala
                    .filter(Files.isDirectory(_))
                    .filterNot(_.endsWith("openapi"))
                    .map(_.toFile).toList

    ybLog("playMonitoredFiles for auto reload: " + (dirsFiltered.toList ::: resSubDirs ::: srcSubDirs))
    dirsFiltered.toList ::: resSubDirs ::: srcSubDirs
  }: @sbtUnchecked
  else Seq()
}

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

val swaggerJacksonVersion = "2.11.1"
val swaggerJacksonOverrides = jacksonLibs.map(_ % swaggerJacksonVersion)

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

    dependencyOverrides ++= swaggerJacksonOverrides,
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
    }.value,

    swaggerGenTest := Def.taskDyn {
      Def.sequential(
        (root / Test / testOnly).toTask(s" com.yugabyte.yw.controllers.YbaApiTest"),
        (Test / testOnly).toTask(s" com.yugabyte.yw.controllers.SwaggerGenTest"),
      )
    }.value
  )

Test / test := (Test / test).dependsOn(swagger / Test / test).value

swaggerGen := Def.taskDyn {
  Def.sequential(
    swagger /swaggerGen,
    swagger /swaggerGenTest,
    openApiGenClients,
    openApiCompileClients,
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
