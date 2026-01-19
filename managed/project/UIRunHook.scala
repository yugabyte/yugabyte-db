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
        val checkServerStarted = Seq("bash", "-c",
          "while ! curl -s -f http://localhost:9000/api/v1/prometheus_metrics </dev/null;" +
            " do sleep 10; done")
        checkServerStarted.!!

        println("[Yugabyte sbt log] Starting UI...")
        val command = Seq("bash", "-c", "exec node node_modules/.bin/vite")
        val pb = Process(command, base)

        // Redirect stderr to stdout so we can see errors
        val p = pb.run()
        watchProcess = Some(p)
        // Capture the PID (best-effort via ps)
        val getPidCmd = Seq("bash", "-c", s"pgrep -f 'vite'")
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
