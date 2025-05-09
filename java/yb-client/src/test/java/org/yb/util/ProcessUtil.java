// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.util;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableSet;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.lang.reflect.Field;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import org.yb.minicluster.LogPrinter;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class ProcessUtil {
  private static final Logger LOG = LoggerFactory.getLogger(ProcessUtil.class);

  // Used by pidOfProcess()
  private static String UNIX_PROCESS_CLASS_NAME =  "java.lang.UNIXProcess";
  private static String JDK9_PROCESS_IMPL_CLASS_NAME = "java.lang.ProcessImpl";

  private static Set<String> VALID_SIGNALS =  ImmutableSet.of("STOP", "CONT", "TERM", "KILL",
                                                              "SIGSEGV");

  private ProcessUtil() {
  }

  /**
   * Gets the pid of a specified process. Relies on reflection and only works on
   * UNIX process, not guaranteed to work on JDKs other than Oracle and OpenJDK.
   *
   * @param proc The specified process.
   * @return The process UNIX pid.
   * @throws IllegalArgumentException If the process is not a UNIXProcess.
   * @throws Exception                If there are other getting the pid via reflection.
   */
  public static int pidOfProcess(Process proc) throws NoSuchFieldException, IllegalAccessException {
    Class<?> procCls = proc.getClass();
    final String actualClassName = procCls.getName();
    if (!actualClassName.equals(UNIX_PROCESS_CLASS_NAME) &&
        !actualClassName.equals(JDK9_PROCESS_IMPL_CLASS_NAME)) {
      throw new IllegalArgumentException("pidOfProcess() expects an object of class " +
          UNIX_PROCESS_CLASS_NAME + " or " + JDK9_PROCESS_IMPL_CLASS_NAME + ", but " +
          procCls.getName() + " was passed in instead!");
    }
    Field pidField = procCls.getDeclaredField("pid");
    pidField.setAccessible(true);
    return (Integer) pidField.get(proc);
  }

  public static String pidStrOfProcess(Process proc) {
    try {
      return String.valueOf(pidOfProcess(proc));
    } catch (NoSuchFieldException | IllegalAccessException ex) {
      return "<error_getting_pid>";
    }
  }

  /**
   * Send a code specified by its string representation to the specified process.
   *
   * @param pid The pid of the process to send a signal to.
   * @param sig  The string representation of the process (e.g., STOP for SIGSTOP).
   * @throws IllegalArgumentException If the signal type is not supported.
   * @throws IllegalStateException    If we are unable to send the specified signal.
   */
  public static void signalProcess(int pid, String sig) throws Exception {
    if (!VALID_SIGNALS.contains(sig)) {
      throw new IllegalArgumentException(sig + " is not a supported signal, only " +
          Joiner.on(",").join(VALID_SIGNALS) + " are supported");
    }
    int rv = Runtime.getRuntime()
        .exec(String.format("kill -%s %d", sig, pid))
        .waitFor();
    if (rv != 0) {
      throw new IllegalStateException(
          String.format("unable to send SIG%s to process with pid=%d: " +
                        "expected return code from kill, but got %d instead", sig, pid, rv));
    }
  }

  public static void signalProcess(Process process, String sig) throws Exception {
    signalProcess(pidOfProcess(process), sig);
  }

  /**
   * Pause the specified process by sending a SIGSTOP using the kill command.
   * @param proc The specified process.
   * @throws Exception If error prevents us from pausing the process.
   */
  public static void pauseProcess(Process proc) throws Exception {
    signalProcess(proc, "STOP");
  }

  /**
   * Resumes the specified process by sending a SIGCONT using the kill command.
   * @param proc The specified process.
   * @throws Exception If error prevents us from resuming the process.
   */
  public static void resumeProcess(Process proc) throws Exception {
    signalProcess(proc, "CONT");
  }

  /**
   * Execute a simple command as a process, awaiting for it to complete and redirecting its output
   * to a Java test log.
   * <p>
   * Waits for a process forever.
   * <p>
   * Throws an exception on non-zero exit code.
   */
  public static void executeSimple(List<String> args, String logPrefix) throws Exception {
    executeSimple(args, logPrefix, Integer.MAX_VALUE);
  }

  /**
   * Execute a simple command as a process, awaiting for it to complete and redirecting its output
   * to a Java test log.
   * <p>
   * Throws an exception on non-zero exit code.
   */
  public static void executeSimple(List<String> args, String logPrefix, int timeoutSec)
      throws Exception {
    String[] pathPieces  = args.get(0).split("/");
    String   programName = pathPieces[pathPieces.length - 1];

    ProcessBuilder pb = new ProcessBuilder(args);
    if (!logPrefix.isEmpty())
      LOG.info("About to run " + args);

    // Handle the logs output.
    Process process = pb.start();
    try (LogPrinter outLp = new LogPrinter(process.getInputStream(), logPrefix + "|stdout ");
         LogPrinter errLp = new LogPrinter(process.getErrorStream(), logPrefix + "|stderr ")) {

      // Wait for the process to complete.
      if (!process.waitFor(timeoutSec, TimeUnit.SECONDS)) {
        throw new TimeoutException("Timed out waiting for " + process);
      }
    }

    if (process.exitValue() != 0) {
      throw new IOException(programName + " exited with code " + process.exitValue());
    }
  }

  /*
   * TODO(jason): combine this with executeSimple.
   */
  public static String runProcess(List<String> args, int timeoutSeconds) throws Exception {
    return runProcess(args, timeoutSeconds, new HashMap<>());
  }

  /*
   * TODO(jason): combine this with executeSimple.
   */
  public static String runProcess(List<String> args,
      int timeoutSeconds, Map<String, String> env) throws Exception {
    String processStr = "";
    for (String arg : args) {
      processStr += (processStr.isEmpty() ? "" : " ") + arg;
    }
    LOG.info("RUN:" + processStr);

    ProcessBuilder processBuilder = new ProcessBuilder(args);
    processBuilder.environment().putAll(env);
    final Process process = processBuilder.start();
    String line = null;

    final BufferedReader stderrReader =
        new BufferedReader(new InputStreamReader(process.getErrorStream()));
    while ((line = stderrReader.readLine()) != null) {
      LOG.info("STDERR: " + line);
    }

    final BufferedReader stdoutReader =
        new BufferedReader(new InputStreamReader(process.getInputStream()));
    StringBuilder stdout = new StringBuilder();
    while ((line = stdoutReader.readLine()) != null) {
      stdout.append(line + "\n");
    }

    if (!process.waitFor(timeoutSeconds, TimeUnit.SECONDS)) {
      throw new TimeoutException(
          "Timeout of process run (" + timeoutSeconds + " seconds): [" + processStr + "]");
    }

    final int exitCode = process.exitValue();
    LOG.info("Process [" + processStr + "] exit code: " + exitCode);

    if (exitCode != 0) {
      LOG.info("STDOUT:\n" + stdout.toString());
      throw new IOException(
          "Failed process with exit code " + exitCode + ": [" + processStr + "]");
    }

    return stdout.toString();
  }
}
