name := """yugaware"""

version := "1.0-SNAPSHOT"

lazy val root = (project in file(".")).enablePlugins(PlayJava)

scalaVersion := "2.11.7"

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs
)

resolvers += Resolver.mavenLocal
libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT"
libraryDependencies += "org.mockito" % "mockito-core" % "1.10.19"
