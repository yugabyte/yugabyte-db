/*
 * Copyright (c) YugabyteDB, Inc.
 */

import play.sbt.PlayRunHook
import sbt._
import java.net.InetSocketAddress
import scala.sys.process._

object UIRunHook {
  def apply(base: File): PlayRunHook = {

    object NpmProcess extends PlayRunHook {

      var watchProcess: Option[Process] = None
      var pid: Option[Int] = None

       override def afterStarted(): Unit = {
        // don't run "npm start" directly as it leaves zombie node.js child processes on termination
        // EXTEND_ESLINT is a CRA env variable. If true, it allows your project to extend the
        // default ESLint config with your own custom rules in .eslintrc.js or similar.
        val cracoCmd = "EXTEND_ESLINT=true exec node node_modules/@craco/craco/bin/craco.js start"
        val command = Seq("bash", "-c", cracoCmd)
        val pb = Process(command, base)

        val p = pb.run()
        watchProcess = Some(p)

        // Capture the PID (best-effort via ps)
        val getPidCmd = Seq("bash", "-c", s"pgrep -f 'node.*craco.js start'")
        pid = getPidCmd.lineStream_!.headOption.map(_.toInt)
      }

      override def afterStopped(): Unit = {
        println("[Yugabyte sbt log] Shutting down UI...")

        // Kill the entire process group using negative PID
        pid.foreach { p =>
          println(s"[Yugabyte sbt log] Killing process group: -$p")
          // Sends SIGTERM to the whole group
          s"kill -TERM -$p".!
        }

        // Also destroy the original process as fallback
        watchProcess.foreach(_.destroy())
        watchProcess = None
        pid = None
      }
    }

    NpmProcess
  }
}
