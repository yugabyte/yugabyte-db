name := """yugaware"""
import com.typesafe.sbt.packager.MappingsHelper._
import com.typesafe.sbt.packager.docker._

version := "1.0-SNAPSHOT"

lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging)
  .disablePlugins(PlayLayoutPlugin)


scalaVersion := "2.11.7"

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs,
  filters,
  "org.mockito" % "mockito-core" % "1.10.19",
  "org.mindrot" % "jbcrypt" % "0.3m",
  "mysql" % "mysql-connector-java" % "5.1.27",
  "org.postgresql" % "postgresql" % "9.2-1003-jdbc4",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.2",
  "org.flywaydb" %% "flyway-play" % "3.0.1",
  "com.datastax.cassandra" % "cassandra-driver-core" % "3.1.2",
  "org.yaml" % "snakeyaml" % "1.17",
  "com.google.protobuf" % "protobuf-java" % "2.6.1"
)
resolvers += "Yugabyte S3 Snapshots" at "s3://no-such-url/"
// resolvers += Resolver.mavenLocal
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

dockerExposedPorts := Seq(9000)
defaultLinuxInstallLocation in Docker := "/opt/yugabyte/yugaware"
dockerEntrypoint := Seq("bin/yugaware", "-Dconfig.file=conf/application.docker.conf")
mappings in Docker ++= directory("docker")

dockerCommands ++= Seq(
  Cmd("USER", "root"),
  Cmd("RUN", "apt-get update && apt-get install -y python-dev libffi-dev " +
    "openssl libssl-dev python-pip git"),
  Cmd("ADD", "packages/devops/devops*.tar.gz /opt/yugabyte/devops"),
  Cmd("RUN", "export USER=root && /opt/yugabyte/devops/bin/install_python_requirements.sh"),
  Cmd("RUN", "export USER=root && /opt/yugabyte/devops/bin/install_ansible_requirements.sh"),
  Cmd("ADD", "packages/yugabyte /opt/yugabyte/releases/"),
  Cmd("ADD", "packages/java/yb-sample-apps.jar /opt/yugabyte/utils/"),
  Cmd("ADD", "packages/java/yb-sample-apps-sources.jar /opt/yugabyte/utils/"),
  Cmd("ADD", "docker /etc/yugaware")
)
