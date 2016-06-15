name := """yugaware"""

version := "1.0-SNAPSHOT"

lazy val root = (project in file(".")).enablePlugins(PlayJava, PlayEbean)

scalaVersion := "2.11.7"

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs,
  "org.mockito" % "mockito-core" % "1.10.19",
  "org.mindrot" % "jbcrypt" % "0.3m",
  "mysql" % "mysql-connector-java" % "5.1.27",

  // WebJars (i.e. client-side) dependencies
  "org.webjars" %  "jquery" % "2.1.1",
  "org.webjars" %  "bootstrap" % "3.3.1",
  "org.webjars" %  "font-awesome" % "4.6.3"
)

resolvers += "Yugabyte S3 Snapshots" at "s3://no-such-url/"
libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT"
publishTo := Some("yugabyteS3" at "s3://no-such-url/")
