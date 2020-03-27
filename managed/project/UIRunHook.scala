/*
 * Copyright (c) YugaByte, Inc.
 */

import play.sbt.PlayRunHook
import sbt._
import java.net.InetSocketAddress
import scala.sys.process.Process

object UIRunHook {
  def apply(base: File): PlayRunHook = {

    object NpmProcess extends PlayRunHook {

      var watchProcess: Option[Process] = None

      override def beforeStarted(): Unit = {
        Process("npm install", base).run
      }

      override def afterStarted(addr: InetSocketAddress): Unit = {
        watchProcess = Some(Process("npm start", base).run)
      }

      override def afterStopped(): Unit = {
        watchProcess.map(p => p.destroy())
        watchProcess = None
      }
    }

    NpmProcess
  }
}
