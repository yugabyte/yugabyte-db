name := """yugaware"""

version := "1.0-SNAPSHOT"

lazy val root = (project in file(".")).enablePlugins(PlayJava)

scalaVersion := "2.11.7"

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs
)

resolvers += "Yugabyte S3 Snapshots" at "s3://no-such-url/"

libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT"
libraryDependencies += "org.mockito" % "mockito-core" % "1.10.19"

publishTo := Some("yugabyteS3" at "s3://no-such-url/")
