import scala.sys.process.Process

/**
  * UI Build Tasks like clean node modules, npm install and npm run build
  */

// Delete node_modules directory in the given path. Return 0 if success.
def cleanNodeModules(implicit dir: File): Int = Process("rm -rf node_modules", dir)!

// Execute `npm ci` command to install all node module dependencies. Return 0 if success.
def runNpmInstall(implicit dir: File): Int =
  if (cleanNodeModules != 0) throw new Exception("node_modules not cleaned up")
  else {
    println("node version: " + Process("node" :: "--version" :: Nil).lineStream_!.head)
    println("npm version: " + Process("npm" :: "--version" :: Nil).lineStream_!.head)
    println("npm config get: " + Process("npm" :: "config" :: "get" :: Nil).lineStream_!.head)
    println("npm cache verify: " + Process("npm" :: "cache" :: "verify" :: Nil).lineStream_!.head)
    Process("npm" :: "ci" :: Nil, dir).!
  }

// Execute `npm run build` command to build the production build of the UI code. Return 0 if success.
def runNpmBuild(implicit dir: File): Int =
  if (runNpmInstall != 0) throw new Exception("npm install failed")
  else Process("npm run build-and-copy", dir)!

lazy val uIBuild = taskKey[Unit]("Build production version of UI code.")

uIBuild := {
  implicit val uiSource = baseDirectory.value / "ui"
  if (runNpmBuild != 0) throw new Exception("UI Build crashed.")
}

/**
 *  Make SBT packaging depend on the UI build hook.
 */
Universal / packageZipTarball := (Universal / packageZipTarball).dependsOn(uIBuild).value
