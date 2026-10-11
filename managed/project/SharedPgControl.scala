import java.io.{File, FileInputStream}
import java.util.Properties

/**
 * Spawns and stops the dedicated JVM that owns the one shared embedded postgres for a whole test
 * run (see `com.yugabyte.yw.common.SharedEmbeddedPostgres`).
 *
 * <p>The server is NOT started inside the sbt JVM: that would need an isolated classloader to see
 * the test classpath (fragile) and mis-detects the CPU architecture (falling back to slow
 * x86_64/Rosetta binaries). Instead we launch a plain JVM - identical runtime to the test forks,
 * where embedded postgres is known to start reliably - and terminate it afterwards.
 *
 * <p>This object lives in the sbt meta-build, so it is a stable per-session singleton that holds
 * the owning `Process` across the Setup/Cleanup pair.
 *
 * <p><b>Reference counting.</b> sbt runs the {@code Tests.Setup}/{@code Tests.Cleanup} hooks once
 * per forked test <i>group</i>, not once per test task: with N groups we get N `start()` calls up
 * front (all but the first are no-ops) and N `stop()` calls as groups finish. Terminating the
 * shared server on the first `stop()` would kill it out from under every group still running
 * (they would then fail with "connection refused"). So we keep a reference count and only actually
 * tear the server down when the last group has cleaned up (count back to zero).
 */
object SharedPgControl {

  private var proc: Process = null
  // Number of test groups that have called start() but not yet stop(). The server is torn down only
  // when this returns to zero so a finishing group never kills it for the groups still running.
  private var active: Int = 0

  private val ReadyTimeoutMs = 180000L
  private val PollIntervalMs = 500L

  def start(classpath: Seq[File], confPath: String): Unit = synchronized {
    active += 1
    if (proc != null && proc.isAlive) {
      return
    }
    val confFile = new File(confPath)
    confFile.delete()
    val javaBin = System.getProperty("java.home") + File.separator + "bin" + File.separator + "java"
    val cp = classpath.map(_.getAbsolutePath).mkString(File.pathSeparator)
    val logFile = new File(confFile.getParentFile, "shared-embedded-pg.log")
    val pb = new ProcessBuilder(
      javaBin,
      "-cp",
      cp,
      "com.yugabyte.yw.common.SharedEmbeddedPostgres",
      confPath)
    pb.redirectErrorStream(true)
    pb.redirectOutput(ProcessBuilder.Redirect.to(logFile))
    proc = pb.start()

    val deadline = System.currentTimeMillis() + ReadyTimeoutMs
    while (!isReady(confFile)) {
      if (!proc.isAlive) {
        throw new IllegalStateException(
          s"Shared embedded postgres JVM exited before becoming ready; see $logFile")
      }
      if (System.currentTimeMillis() > deadline) {
        proc.destroyForcibly()
        proc = null
        throw new IllegalStateException(
          s"Shared embedded postgres did not become ready within ${ReadyTimeoutMs}ms; see $logFile")
      }
      Thread.sleep(PollIntervalMs)
    }
  }

  def stop(confPath: String): Unit = synchronized {
    if (active > 0) {
      active -= 1
    }
    // Other test groups are still using the server - leave it running.
    if (active > 0) {
      return
    }
    if (proc != null) {
      // SIGTERM so the owner's shutdown hook stops the server cleanly (and cleans its data dir);
      // force-kill if it does not exit promptly.
      proc.destroy()
      if (!proc.waitFor(30, java.util.concurrent.TimeUnit.SECONDS)) {
        proc.destroyForcibly()
      }
      proc = null
    }
    new File(confPath).delete()
  }

  private def isReady(confFile: File): Boolean = {
    if (!confFile.isFile) {
      return false
    }
    val props = new Properties()
    try {
      val in = new FileInputStream(confFile)
      try props.load(in)
      finally in.close()
      props.getProperty("port") != null
    } catch {
      case _: Throwable => false
    }
  }
}
