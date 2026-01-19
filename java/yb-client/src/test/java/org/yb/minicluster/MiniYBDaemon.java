/**
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 */
package org.yb.minicluster;

import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import static org.yb.AssertionWrappers.assertNotNull;
import org.yb.client.TestUtils;
import org.yb.util.*;

import java.io.*;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public class MiniYBDaemon {
  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  private static final String PID_PREFIX = "pid";
  private static final String LOG_PREFIX_SEPARATOR = "|";
  private static final String SYSLOG_PATH = "/var/log/messages";
  private static final int SYSLOG_CONTEXT_NUM_LINES = 15;
  private static final int NUM_LAST_SYSLOG_LINES_TO_USE = 10000;

  // This corresponds to regular termination of yb-master and yb-tserver with SIGTERM.
  private static final int EXPECTED_EXIT_CODE = 143;

  private static final String INVALID_PID_STR = "<error_getting_pid>";

  // This is a helper class that waits for the process to terminate and logs possible reasons for
  // the termination, e.g. looks at /var/log/messages.
  private class TerminationHandler {

    private int exitCode;

    private void analyzeSystemLog() throws IOException {
      File syslogFile = new File(SYSLOG_PATH);
      if (!syslogFile.exists())
        return;
      if (!syslogFile.canRead()) {
        LOG.warn("Cannot read syslog file at " + SYSLOG_PATH);
        return;
      }

      String pidStr = getPidStr();
      if (pidStr.equals(INVALID_PID_STR)) {
        LOG.warn("pid unknown, cannot look for it in " + SYSLOG_PATH + ": " + this);
        return;
      }
      String regexStr = "(\\b" + pidStr + "\\b|out-of-memory|killed process|oom_killer)";
      CommandResult cmdResult = CommandUtil.runShellCommand(
          String.format(
              "tail -%d '%s' | egrep -i -C %d '%s'", NUM_LAST_SYSLOG_LINES_TO_USE,
              SYSLOG_PATH, SYSLOG_CONTEXT_NUM_LINES, regexStr));
      cmdResult.logStderr();
      if (cmdResult.getStdoutLines().isEmpty()) {
        if (!terminatedNormally()) {
          LOG.warn("Could not find anything in " + SYSLOG_PATH + " relevant to the " +
              "disappearance of process: " + MiniYBDaemon.this);
        }
      } else {
        LOG.warn("Potentially relevant lines from " + SYSLOG_PATH +
            " for termination of " + this + ":\n" +
            StringUtil.joinLinesForLogging(cmdResult.getStdoutLines()));
      }
    }

    private void analyzeMemoryUsage() throws IOException {
      CommandResult cmdResult = CommandUtil.runShellCommand(
          "ps -e -orss=,pid=,args= | egrep 'yb-(master|tserver)' | sort -k2,2 -rn");
      cmdResult.logStderr();
      if (!cmdResult.isSuccess()) {
        return;
      }
      long totalMasterRssKB = 0;
      long totalTserverRssKB = 0;
      int numMasters = 0;
      int numTservers = 0;
      List<String> masterTserverPsLines = new ArrayList<String>();
      for (String line : cmdResult.getStdoutLines()) {
        // Four parts: RSS, pid, executable path, arguments.
        String[] items = line.trim().split("\\s+", 4);
        if (items.length < 4) {
          LOG.warn("Could not parse a ps output line: " + line + " (got " +
              items.length + " parts, expected 4)");
          continue;
        }
        long rssKB = 0;
        String rssKbStr = items[0].trim();
        try {
          rssKB = Long.valueOf(rssKbStr);
        } catch (NumberFormatException ex) {
          LOG.warn("Failed parsing number: '" + rssKbStr + "' in ps output line:" + line);
          continue;
        }
        String executablePath = items[2];
        String executableBasename = new File(executablePath).getName();

        if (executableBasename.equals("yb-master")) {
          totalMasterRssKB += rssKB;
          ++numMasters;
          masterTserverPsLines.add(line);
        } else if (executableBasename.equals("yb-tserver")) {
          totalTserverRssKB += rssKB;
          ++numTservers;
          masterTserverPsLines.add(line);
        }
      }
      if (numMasters + numTservers > 0) {
        LOG.info(
            "Num master processes: " + numMasters +
                ", total master memory usage (MB): " + (totalMasterRssKB / 1024) +
                ", num tserver processes: " + numTservers +
                ", total tserver memory usage (MB): " + (totalTserverRssKB / 1024) + "; " +
                "ps output:\n" +
                StringUtil.joinLinesForLogging(cmdResult.getStdoutLines()));
      } else {
        LOG.info("Did not find any yb-master/yb-tserver processes in 'ps' output");
      }
    }

    private String processDescription() {
      return MiniYBDaemon.this.toString();
    }

    private boolean terminatedNormally() {
      return exitCode == EXPECTED_EXIT_CODE || exitCode == 0;
    }

    private void handleTermination(int exitCode) throws IOException {
      LOG.info("Process " + processDescription() + " exited with code " + exitCode + " " +
               (terminatedNormally() ? "(normal termination)" : "(abnormal termination)"));
      if (exitCode == 0) {
        return;
      }

      analyzeSystemLog();
      if (!terminatedNormally()) {
        analyzeMemoryUsage();
      }
    }

    private void waitForAndHandleTermination() {
      try {
        exitCode = process.waitFor();
      } catch (InterruptedException ex) {
        LOG.info("Interrupted when waiting for process to complete: " + this, ex);
        return;
      }

      try {
        handleTermination(exitCode);
      } catch (IOException ex) {
        LOG.info("Error handling termination of " + processDescription(), ex);
      }
    }

    void startInBackground() {
      Thread thread = new Thread(() -> {
        try {
          waitForAndHandleTermination();
        } finally {
          shutdownLatch.countDown();
        }
      });
      thread.setName("Termination handler for " + MiniYBDaemon.this);
      thread.start();
    }
  }

  public static final int NO_DAEMON_INDEX = -1;
  public static final int NO_RPC_PORT = -1;
  public static final String NO_WEB_UI_URL = null;

  public static String makeLogPrefix(
      String shortDaemonTypeStr,
      int daemonIndex,
      String pidAsString,
      int rpcPort,
      String webUiUrl) {
    return shortDaemonTypeStr +
        (daemonIndex == NO_DAEMON_INDEX ? "" : String.valueOf(daemonIndex)) +
        LOG_PREFIX_SEPARATOR + PID_PREFIX + pidAsString + LOG_PREFIX_SEPARATOR +
        (rpcPort == NO_RPC_PORT ? "" : ":" + rpcPort) +
        (ConfForTesting.isCI() || webUiUrl == null || webUiUrl.isEmpty()
            ? "" // No need for a clickable web UI link on Jenkins, or if it is not defined.
            : LOG_PREFIX_SEPARATOR + webUiUrl) +
        " ";
  }

  private String getLogPrefix() {
    return makeLogPrefix(
        type.shortStr(), indexForLog, getPidStr(), rpcPort, "http://" + getWebHostAndPort());
  }

  /**
   * @param type daemon type (master / tablet / yb controller server)
   * @param commandLine command line used to run the daemon
   * @param process daemon process
   */
  public MiniYBDaemon(
      MiniYBDaemonType type, int indexForLog, String[] commandLine, Process process, String bindIp,
      int rpcPort, int webPort, int pgsqlWebPort, int cqlWebPort, int redisWebPort,
      String dataDirPath) {
    this.type = type;
    this.commandLine = commandLine;
    this.process = process;
    this.indexForLog = indexForLog;
    this.bindIp = bindIp;
    this.rpcPort = rpcPort;
    this.webPort = webPort;
    this.cqlWebPort = cqlWebPort;
    this.pgsqlWebPort = pgsqlWebPort;
    this.redisWebPort = redisWebPort;
    this.dataDirPath = dataDirPath;
    this.logListener = new ExternalDaemonLogErrorListener(getLogPrefix());
    this.logPrinter = new LogPrinter(process.getInputStream(), getLogPrefix(), logListener);
    LOG.info("Started stdout/stderr threads for mini YB daemon: " + this);
    new TerminationHandler().startInBackground();
  }

  public LogPrinter getLogPrinter() {
    return logPrinter;
  }

  public MiniYBDaemonType getType() {
    return type;
  }

  public String[] getCommandLine() {
    return commandLine;
  }

  public Process getProcess() {
    return process;
  }

  public int getPid() throws NoSuchFieldException, IllegalAccessException {
    return ProcessUtil.pidOfProcess(process);
  }

  String getPidStr() {
    try {
      return String.valueOf(getPid());
    } catch (NoSuchFieldException | IllegalAccessException ex) {
      return INVALID_PID_STR;
    }
  }

  /**
   * Restart the daemon.
   * @return the restarted daemon
   */
  MiniYBDaemon restart() throws Exception {
    return new MiniYBDaemon(type, indexForLog, commandLine,
                            new ProcessBuilder(commandLine).redirectErrorStream(true).start(),
                            bindIp, rpcPort, webPort, cqlWebPort, pgsqlWebPort, redisWebPort,
                            dataDirPath);
  }

  @Override
  public String toString() {
    return type.toString().toLowerCase() + " process on bind IP " + bindIp + ", rpc port " +
        rpcPort + ", web port " + webPort + ", pid " + getPidStr();
  }

  private final MiniYBDaemonType type;
  private final int indexForLog;
  private final String[] commandLine;
  private final Process process;
  private final String bindIp;
  private final int rpcPort;
  private final int webPort;
  private final int cqlWebPort;
  private final int pgsqlWebPort;
  private final int redisWebPort;
  private final String dataDirPath;
  private final CountDownLatch shutdownLatch = new CountDownLatch(1);
  private final LogPrinter logPrinter;
  private final ExternalDaemonLogErrorListener logListener;
  private boolean shutdownStarted = false;

  public HostAndPort getWebHostAndPort() {
    return HostAndPort.fromParts(bindIp, webPort);
  }

  public HostAndPort getHostAndPort() {
    return HostAndPort.fromParts(bindIp, rpcPort);
  }

  public int getWebPort() {
    return webPort;
  }

  public int getCqlWebPort() {
    return cqlWebPort;
  }

  public String getDataDirPath() {
    return dataDirPath;
  }

  // TODO: rename to getBindIp
  public String getLocalhostIP() {
    return bindIp;
  }

  public int getPgsqlWebPort() {
    return pgsqlWebPort;
  }

  public void waitForServerStartLogMessage(long deadlineMs) throws InterruptedException {
    logListener.waitForServerStartingLogLine(deadlineMs);
    LOG.info("Saw an 'RPC server started' message from " + this);
  }

  public void terminate() throws Exception {
    try {
      ProcessUtil.signalProcess(process, "TERM");
    } catch (IllegalStateException ex) {
      // Failed to send signal, if process is not alive - it is OK, otherwise rethrow the exception.
      if (process.isAlive()) {
        throw ex;
      }
    }

    // Manually cleanup left behind YB Controller subprocesses if any.
    if (this.type == MiniYBDaemonType.YBCONTROLLER) {
      String cmd = "ps -ef | grep -v grep | grep " + dataDirPath + "/tmp"
          + " | awk '{print $2}' | xargs kill -9";
      final List<String> cmdLine = Lists.newArrayList("bash", "-c", cmd);
      try {
        ProcessUtil.executeSimple(cmdLine, "Killing YB Controller subprocesses!");
      } catch (Exception e) {
        // OK to fail. It means there are no subprocesses left behind.
        LOG.info("No Yb controller subprocesses left behind to kill! " + e.getMessage());
      }
    }
  }

  public void startShutdown() throws Exception {
    try {
      ProcessUtil.signalProcess(process, "TERM");
    } catch (IllegalStateException ex) {
      // Failed to send signal, if process is not alive - it is OK, otherwise rethrow the exception.
      if (process.isAlive()) {
        throw ex;
      }
    }
    shutdownStarted = true;
  }

  // Returns whether the shutdown was graceful.
  public boolean completeShutdown(long deadlineNs, long killWaitTimeNs) throws Exception {
    if (!shutdownStarted) {
      throw new IllegalStateException("Must call startShutdown first");
    }
    boolean gracefulTermination = true;
    if (process.isAlive()) {
      long currentNs = System.nanoTime();
      if (deadlineNs > currentNs) {
        gracefulTermination = process.waitFor(deadlineNs - currentNs, TimeUnit.NANOSECONDS);
      } else {
        gracefulTermination = false;
      }
      if (!gracefulTermination) {
        LOG.warn("{} failed to terminate within the deadline", this);
      }
      killProcessIfNecessary(killWaitTimeNs);
    }
    cleanupResourcesPostShutdown();
    return gracefulTermination;
  }

  public void kill(long killWaitTimeNs) throws Exception {
    killProcessIfNecessary(killWaitTimeNs);
    cleanupResourcesPostShutdown();
  }

  private void killProcessIfNecessary(long killWaitTimeNs) throws Exception {
    if (process.isAlive()) {
      process.destroyForcibly();
      if (!process.waitFor(killWaitTimeNs, TimeUnit.NANOSECONDS)) {
        LOG.warn("Failed to kill {} within {} ms",
                 this,
                 TimeUnit.NANOSECONDS.toMillis(killWaitTimeNs));
      }
    }
  }

  private void cleanupResourcesPostShutdown() throws Exception {
    try {
      if (!shutdownLatch.await(10, TimeUnit.SECONDS)) {
        LOG.warn("Timed out waiting for logging of process shutdown to finish for {}", this);
      }
    } catch (InterruptedException ex) {
      LOG.warn("Interrupted when waiting for logging of process shutdown to finish {}", this);
      Thread.currentThread().interrupt();
    }
    try {
      process.getInputStream().close();
    } catch (IOException ex) {
      LOG.warn("Exception when trying to close process stdin for process {}", this, ex);
    }
  }

  /**
   * Ping the YB Controller server.
   * Throws an exception if ping fails.
   */
  public void ping() throws Exception {
    if (this.type != MiniYBDaemonType.YBCONTROLLER) {
      LOG.warn("This method is for YB Controller only.");
      return;
    }

    final List<String> cmdLine = Lists.newArrayList(
        TestUtils.findBinary("../../ybc/yb-controller-cli"),
        "ping",
        "--tserver_ip=" + this.bindIp,
        "--server_port=" + this.rpcPort);

    LOG.info("Pinging YB Controller server with host = {}, port = {}", this.bindIp, this.rpcPort);
    ProcessUtil.executeSimple(cmdLine, " ");
  }

  public String getFlag(String flagName) throws Exception{
    URL url = new URL("http", bindIp, webPort, "/varz?raw=true");
    LOG.info("Fetching flags from: {}", url);
    HttpURLConnection conn = (HttpURLConnection) url.openConnection();
    conn.setRequestMethod("GET");
    if (conn.getResponseCode() != 200) {
      throw new RuntimeException("HTTP varz request failed with code: " + conn.getResponseCode());
    }

    List<String> responseLines = new ArrayList<>();
    try (BufferedReader reader = new BufferedReader(
           new InputStreamReader(conn.getInputStream(), StandardCharsets.UTF_8))) {
      String line;
      while ((line = reader.readLine()) != null) {
        responseLines.add(line);
      }
    }
    String flagTag = "--" + flagName;
    String flagStr = null;
    for (String str : responseLines) {
      if (str.length() > flagTag.length() && str.substring(0, flagTag.length()).equals(flagTag)) {
        LOG.info("found flag " + str);
        flagStr = str;
        break;
      }
    }
    assertNotNull(flagStr);
    return flagStr.substring(flagStr.indexOf("=") + 1);
  }
}
