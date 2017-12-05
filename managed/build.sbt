name := """yugaware"""
import com.typesafe.sbt.packager.MappingsHelper._

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
  "mysql" % "mysql-connector-java" % "5.1.27",
  "org.postgresql" % "postgresql" % "9.4.1208",
  "commons-io" % "commons-io" % "2.4",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.2",
  "org.flywaydb" %% "flyway-play" % "3.0.1",
  "com.datastax.cassandra" % "cassandra-driver-core" % "3.1.2",
  "org.yaml" % "snakeyaml" % "1.17",
  "com.google.protobuf" % "protobuf-java" % "2.6.1"
)

lazy val mavenLocal = System.getenv("USE_MAVEN_LOCAL")
resolvers += {
  if (mavenLocal != null && mavenLocal.equals("true")) {
    Resolver.mavenLocal
  } else {
    "Yugabyte S3 Snapshots" at "s3://no-such-url/"
  }
}

libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT"
publishTo := Some("yugabyteS3" at "s3://no-such-url/")

javaOptions in Test += "-Dconfig.file=src/main/resources/application.test.conf"
testOptions += Tests.Argument(TestFrameworks.JUnit, "-v", "-q", "-a")

// Skip packaging javadoc for now
sources in (Compile, doc) := Seq()
publishArtifact in (Compile, packageDoc) := false

// Add react ui files to public folder of universal package
mappings in Universal ++= contentOf(baseDirectory.value / "ui/build").map {
  case (file, dest) => file -> s"public/$dest"
}

topLevelDirectory := None

