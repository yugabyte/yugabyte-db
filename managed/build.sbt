name := """yugaware"""

lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging)
  .disablePlugins(PlayLayoutPlugin)

scalaVersion := "2.11.7"
version := (sys.process.Process("cat version.txt").lines_!.head)

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs,
  filters,
  "org.mockito" % "mockito-core" % "1.10.19",
  "org.mindrot" % "jbcrypt" % "0.3m",
  "org.postgresql" % "postgresql" % "9.4.1208",
  "commons-io" % "commons-io" % "2.4",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.2",
  "org.flywaydb" %% "flyway-play" % "3.0.1",
  // https://github.com/YugaByte/cassandra-java-driver/releases
  "com.yugabyte" % "cassandra-driver-core" % "3.2.0-yb-19",
  "org.yaml" % "snakeyaml" % "1.17",
  "org.bouncycastle" % "bcpkix-jdk15on" % "1.61",
  "org.springframework.security" % "spring-security-core" % "5.1.6.RELEASE",
  "com.amazonaws" % "aws-java-sdk-kms" % "1.11.638",
  "com.amazonaws" % "aws-java-sdk-iam" % "1.11.670",
  "com.cronutils" % "cron-utils" % "9.0.1"
)
// Default to true if nothing passed on the env, so we can pick up YB jars from local java itest.
lazy val mavenLocal = Option(System.getenv("USE_MAVEN_LOCAL")).getOrElse("false")
resolvers += {
  if (mavenLocal != null && mavenLocal.equals("true")) {
    val localMavenRepo = System.getenv("YB_MVN_LOCAL_REPO")
    if (localMavenRepo != null) {
      "Local Maven Repository" at "file://" + localMavenRepo
    } else {
      Resolver.mavenLocal
    }
  } else {
    "Yugabyte Nexus Snapshots" at System.getenv("YB_NEXUS_SNAPSHOT_URL")
  }
}

lazy val groupId = Option(System.getenv("YB_TMP_GROUP_ID")).getOrElse("org.yb")
libraryDependencies += groupId % "yb-client" % "0.8.0-SNAPSHOT"


dependencyOverrides += "io.netty" % "netty-handler" % "4.0.36.Final"
dependencyOverrides += "com.google.protobuf" % "protobuf-java" % "latest.integration"

javaOptions in Test += "-Dconfig.file=src/main/resources/application.test.conf"
testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")

// Skip packaging javadoc for now
sources in (Compile, doc) := Seq()
publishArtifact in (Compile, packageDoc) := false

topLevelDirectory := None
