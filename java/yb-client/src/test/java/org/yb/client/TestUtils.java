// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableSet;
import com.sun.security.auth.module.UnixSystem;
import org.junit.runner.Description;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.lang.reflect.Field;

import java.net.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.*;

/**
 * A grouping of methods that help unit testing.
 */
public class TestUtils {
  private static final Logger LOG = LoggerFactory.getLogger(TestUtils.class);

  // Used by pidOfProcess()
  private static String UNIX_PROCESS_CLASS_NAME =  "java.lang.UNIXProcess";
  private static String JDK9_PROCESS_IMPL_CLASS_NAME = "java.lang.ProcessImpl";

  private static Set<String> VALID_SIGNALS =  ImmutableSet.of("STOP", "CONT", "TERM", "KILL");

  private static final String BIN_DIR_PROP = "binDir";

  private static String ybRootDir = null;

  public static final boolean IS_LINUX =
      System.getProperty("os.name").toLowerCase().equals("linux");

  private static final long unixUserId = new UnixSystem().getUid();
  private static final long startTimeMillis = System.currentTimeMillis();
  private static Random randomGenerator;

  // The amount of time to wait for in addition to the ttl specified.
  private static final long WAIT_FOR_TTL_EXTENSION_MS = 100;

  private static String NONBLOCKING_RANDOM_DEVICE = "/dev/urandom";
  static {
    long seed = System.nanoTime();
    if (new File(NONBLOCKING_RANDOM_DEVICE).exists()) {
      try {
        InputStream in = new FileInputStream(NONBLOCKING_RANDOM_DEVICE);
        for (int i = 0; i < 64; ++i) {
          seed = seed * 37 + in.read();
        }
        in.close();
      } catch (IOException ex) {
        LOG.warn("Failed to read from " + NONBLOCKING_RANDOM_DEVICE + " to seed random generator");
      }
    }
    randomGenerator = new Random(seed);
  }

  public static final int MIN_PORT_TO_USE = 10000;
  public static final int MAX_PORT_TO_USE = 32768;

  // Set of ports for the network addresses being reserved.
  private static final Map<InetAddress, Set<Integer>> reservedPorts = new HashMap<>();

  /** Time to sleep in milliseconds waiting for conditions to be met. */
  private static final int SLEEP_TIME_MS = 1000;

  private static Path flagFileTmpPath = null;

  private static final Object flagFilePathLock = new Object();

  private static volatile String cppBinariesDir = null;
  private static volatile String buildType = null;

  /**
   * @return the path of the flags file to pass to daemon processes
   * started by the tests
   */
  public static String getFlagsPath() {
    // If the flags are inside a JAR, extract them into our temporary
    // test directory.
    try {
      // Somewhat unintuitively, createTempFile() actually creates the file,
      // not just the path, so we have to use REPLACE_EXISTING below.
      synchronized (flagFilePathLock) {
        if (flagFileTmpPath == null || !Files.exists(flagFileTmpPath)) {
          flagFileTmpPath = Files.createTempFile(
            Paths.get(getBaseDir()), "yb-flags", ".flags");
          Files.copy(BaseYBClientTest.class.getResourceAsStream("/flags"), flagFileTmpPath,
            StandardCopyOption.REPLACE_EXISTING);
        }
      }
      return flagFileTmpPath.toAbsolutePath().toString();
    } catch (IOException e) {
      throw new RuntimeException("Unable to extract flags file into tmp", e);
    }
  }

  /**
   * Return the path portion of a file URL, after decoding the escaped
   * components. This fixes issues when trying to build within a
   * working directory with special characters.
   */
  private static String urlToPath(URL u) {
    try {
      return URLDecoder.decode(u.getPath(), "UTF-8");
    } catch (UnsupportedEncodingException e) {
      throw new RuntimeException(e);
    }
  }

  public static synchronized String findYbRootDir() {
    if (ybRootDir != null) {
      return ybRootDir;
    }
    final URL myUrl = BaseYBClientTest.class.getProtectionDomain().getCodeSource().getLocation();
    final String pathToCode = urlToPath(myUrl);
    final String currentDir = System.getProperty("user.dir");

    // Try to find the YB directory root by navigating upward from either the source code location,
    // or, if that does not work, from the current directory.
    for (String initialPath : new String[] { pathToCode, currentDir }) {
      ybRootDir = findGitRepoContaining(initialPath);
      if (ybRootDir != null) {
        return ybRootDir;
      }
    }
    throw new RuntimeException(
        "Unable to find build dir! myUrl=" + myUrl + ", currentDir=" + currentDir);
  }

  private static String findGitRepoContaining(String initialPath) {
    File currentPath = new File(initialPath);
    while (currentPath != null) {
      if (new File(currentPath, ".git").exists()) {
        // Cache the root dir so that we don't have to find it every time.
        return currentPath.getAbsolutePath();
      }
      currentPath = currentPath.getParentFile();
    }
    return null;
  }

  /**
   * @return the directory with YB daemons' web UI assets
   */
  public static String getWebserverDocRoot() {
    return TestUtils.findYbRootDir() + "/www";
  }

  public static String getBinDir() {
    if (cppBinariesDir != null)
      return cppBinariesDir;

    String binDir = System.getProperty(BIN_DIR_PROP);
    if (binDir != null) {
      LOG.info("Using binary directory specified by property: {}",
          binDir);
    } else {
      binDir = findYbRootDir() + "/build/latest/bin";
    }

    if (!new File(binDir).isDirectory()) {
      throw new RuntimeException(
          "Directory that is supposed to contain YB C++ binaries not found: " + binDir);
    }

    cppBinariesDir = binDir;
    return binDir;
  }

  /**
   * @param binName the binary to look for (eg 'yb-tserver')
   * @return the absolute path of that binary
   * @throws FileNotFoundException if no such binary is found
   */
  public static String findBinary(String binName) throws FileNotFoundException {
    String binDir = getBinDir();

    File candidate = new File(binDir, binName);
    if (candidate.canExecute()) {
      return candidate.getAbsolutePath();
    }
    throw new FileNotFoundException("Cannot find binary " + binName +
        " in binary directory " + binDir);
  }

  public static String getBuildType() {
    if (buildType != null)
      return buildType;

    try {
      final File canonicalBuildDir = new File(getBinDir()).getParentFile().getCanonicalFile();
      final String buildDirBasename = canonicalBuildDir.getName();
      final String[] buildDirNameComponents = buildDirBasename.split("-");
      assert buildDirNameComponents.length >= 3 :
          "buildDirNameComponents is expected to have at least 3 components: " +
              Arrays.asList(buildDirNameComponents) + ", canonicalBuildDir=" +
              canonicalBuildDir.getPath();
      buildType = buildDirNameComponents[0].toLowerCase();
      LOG.info("Identified build type as '" + buildType + "' based on canonical build " +
          "directory '" + canonicalBuildDir + "' and base name '" + buildDirBasename + "'");
      return buildType;
    } catch (IOException ex) {
      throw new RuntimeException("Failed trying to get the build type", ex);
    }
  }

  /**
   * @return the base directory within which we will store server data
   */
  public static String getBaseDir() {
    String testTmpDir = System.getenv("TEST_TMPDIR");
    if (testTmpDir == null) {
      testTmpDir = "/tmp/ybtest-" + unixUserId + "-" + startTimeMillis + "-" +
              randomGenerator.nextInt();
    }
    File f = new File(testTmpDir);
    f.mkdirs();
    return f.getAbsolutePath();
  }

  /**
   * Gets the pid of a specified process. Relies on reflection and only works on
   * UNIX process, not guaranteed to work on JDKs other than Oracle and OpenJDK.
   * @param proc The specified process.
   * @return The process UNIX pid.
   * @throws IllegalArgumentException If the process is not a UNIXProcess.
   * @throws Exception If there are other getting the pid via reflection.
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

  /**
   * Send a code specified by its string representation to the specified process.
   * TODO: Use a JNR/JNR-Posix instead of forking the JVM to exec "kill".
   * @param proc The specified process.
   * @param sig The string representation of the process (e.g., STOP for SIGSTOP).
   * @throws IllegalArgumentException If the signal type is not supported.
   * @throws IllegalStateException If we are unable to send the specified signal.
   */
  static void signalProcess(Process proc, String sig) throws Exception {
    if (!VALID_SIGNALS.contains(sig)) {
      throw new IllegalArgumentException(sig + " is not a supported signal, only " +
              Joiner.on(",").join(VALID_SIGNALS) + " are supported");
    }
    int pid = pidOfProcess(proc);
    int rv = Runtime.getRuntime()
            .exec(String.format("kill -%s %d", sig, pid))
            .waitFor();
    if (rv != 0) {
      throw new IllegalStateException(String.format("unable to send SIG%s to process %s(pid=%d): " +
              "expected return code from kill, but got %d instead", sig, proc, pid, rv));
    }
  }

  /**
   * Pause the specified process by sending a SIGSTOP using the kill command.
   * @param proc The specified process.
   * @throws Exception If error prevents us from pausing the process.
   */
  static void pauseProcess(Process proc) throws Exception {
    signalProcess(proc, "STOP");
  }

  /**
   * Resumes the specified process by sending a SIGCONT using the kill command.
   * @param proc The specified process.
   * @throws Exception If error prevents us from resuming the process.
   */
  static void resumeProcess(Process proc) throws Exception {
    signalProcess(proc, "CONT");
  }

  /**
   * Check if the given port is free on the given network interface.
   *
   * @param bindInterface the network interface to bind to
   * @param port port to bind to
   * @param logException whether to log an exception in case of failure to bind to the port
   *                     (but not in case of a failure to close the server socket).
   * @return true if the given port is free on the given interface
   * @throws IOException
   */
  public static boolean isPortFree(InetAddress bindInterface, int port, boolean logException)
      throws IOException {
    final int DEFAULT_BACKLOG = 50;
    ServerSocket serverSocket;
    try {
      serverSocket = new ServerSocket(port, DEFAULT_BACKLOG, bindInterface);
    } catch (IOException e) {
      if (logException) {
        LOG.error("Error trying to bind to " + bindInterface + ":" + port, e);
      }
      return false;
    }
    serverSocket.close();
    return true;
  }

  /**
   * Check if the port for the bind interface has already been reserved.
   */
  private static boolean isReservedPort(InetAddress bindInterface, int port) {
    return (reservedPorts.containsKey(bindInterface) &&
            reservedPorts.get(bindInterface).contains(port));
  }

  /**
   * Reserve the port for the bind interface.
   */
  private static void reservePort(InetAddress bindInterface, int port) {
    if (!reservedPorts.containsKey(bindInterface)) {
      reservedPorts.put(bindInterface, new HashSet<>());
    }
    reservedPorts.get(bindInterface).add(port);
  }

  /**
   * Clear all reserved ports.
   */
  public static void clearReservedPorts() {
    reservedPorts.clear();
  }

  /**
   * Find a free port for the given bind interface, starting with the one passed. Keep in mind the
   * time-of-check-time-of-use nature of this method, the returned port might become occupied
   * after it was checked for availability.
   * @return A currently usable port.
   * @throws IOException if we can't close a socket we tried to open or if we run out of ports to
   *                     try.
   */
  public static int findFreePort(String bindInterface) throws IOException {
    final InetAddress bindIp = InetAddress.getByName(bindInterface);
    final int MAX_ATTEMPTS = 1000;
    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
      final int port = MIN_PORT_TO_USE +
          randomGenerator.nextInt(MAX_PORT_TO_USE - MIN_PORT_TO_USE);
      if (!isReservedPort(bindIp, port) && isPortFree(bindIp, port, attempt == MAX_ATTEMPTS - 1)) {
        reservePort(bindIp, port);
        return port;
      }
    }
    throw new IOException("Could not find a free port on interface " + bindInterface + " in " +
        MAX_ATTEMPTS + " attempts");
  }

  public static boolean isJenkins() {
    return System.getenv("BUILD_ID") != null && System.getenv("JOB_NAME") != null;
  }

  public interface Condition {
    boolean get() throws Exception;
  }

  public static void waitFor(Condition condition, long timeoutMs) throws Exception {
    final long startTimeMs = System.currentTimeMillis();
    while (System.currentTimeMillis() - startTimeMs < timeoutMs && !condition.get()) {
      Thread.sleep(SLEEP_TIME_MS);
    }

    if (!condition.get()) {
      throw new Exception(String.format("Operation timed out after %dms", timeoutMs));
    }
  }

  public static void printHorizontalLine(PrintStream out) {
    final StringBuilder horizontalLine = new StringBuilder();
    for (int i = 0; i < 100; ++i) {
      horizontalLine.append("-");
    }
    out.println(horizontalLine);
  }

  public static void printHeading(PrintStream out, String msg) {
    out.println();
    printHorizontalLine(out);
    out.println(msg + "\n");
  }

  public static String getClassAndMethodStr(Description description) {
    return "class=\"" + description.getClassName() +
        "\", method=\"" + description.getMethodName() + "\"";
  }

  /**
   * Tries to connect to the given host and port until the provided timeout has expired.
   * @param host host to connect to.
   * @param port port to connect to.
   * @param timeoutMs timeout in milliseconds to wait for a successful connection.
   * @throws Exception
   */
  public static void waitForServer(String host, int port, long timeoutMs) throws Exception {
    TestUtils.waitFor(() -> {
      try {
        new Socket(host, port);
      } catch (IOException ie) {
        return false;
      }
      return true;
    }, timeoutMs);
  }

  public static boolean isTSAN() {
    return getBuildType().equals("tsan");
  }

  public static long nonTsanVsTsan(long nonTsanValue, long tsanValue) {
    return isTSAN() ? tsanValue : nonTsanValue;
  }

  /** @return a timeout multiplier to apply in tests based on the build type */
  public static double getTimeoutMultiplier() {
    return isTSAN() ? 3.0 : 1.0;
  }

  /**
   * Adjusts a timeout based on the build type.
   *
   * @param timeout timeout in any time units
   * @return adjusted timeout
   */
  public static long adjustTimeoutForBuildType(long timeout) {
    return (long) (timeout * TestUtils.getTimeoutMultiplier());
  }

  public static Random getRandomGenerator() {
    return randomGenerator;
  }

  public static int randomNonNegNumber() {
    return randomGenerator.nextInt(Integer.MAX_VALUE);
  }

  /**
   * Waits for the given ttl (in msec) to expire.
   * @param ttl the ttl (in msec) to wait for expiry.
   * @throws Exception
   */
  public static void waitForTTL(long ttl) throws Exception {
    Thread.sleep(ttl + WAIT_FOR_TTL_EXTENSION_MS);
  }

  public static String joinLinesForLogging(List<String> lines) {
    if (lines.isEmpty()) {
      return "";
    }
    StringBuilder sb = new StringBuilder();
    boolean firstLine = true;
    for (String line : lines) {
      if (firstLine) {
        firstLine = false;
      } else {
        sb.append("\n");
      }
      sb.append("    " + line);
    }
    return sb.toString();
  }

  public static class CommandResult {
    public final String cmd;
    public final int exitCode;
    public final List<String> stdoutLines;
    public final List<String> stderrLines;

    public CommandResult(
        String cmd, int exitCode, List<String> stdoutLines, List<String> stderrLines) {
      this.cmd = cmd;
      this.exitCode = exitCode;
      this.stdoutLines = stdoutLines;
      this.stderrLines = stderrLines;
    }

    public boolean isSuccess() {
      return exitCode == 0;
    }

    public void logErrorOutput() {
      if (!stderrLines.isEmpty()) {
        LOG.warn("Standard error output from command {{ " + cmd + " }}" +
            (exitCode == 0 ? "" : " (exit code: " + exitCode + "):\n" +
                joinLinesForLogging(stderrLines)));
      }
    }
  }

  public static List<String> readLinesFrom(File f) throws IOException {
    if (!f.exists()) {
      return new ArrayList<>();
    }
    BufferedReader reader = new BufferedReader(new InputStreamReader(
        new FileInputStream(f)));
    List<String> lines = new ArrayList<>();
    String line;
    while ((line = reader.readLine()) != null) {
      lines.add(line);
    }
    return lines;
  }

  public static CommandResult runShellCommand(String cmd) throws IOException {
    File outputFile = new File(TestUtils.getBaseDir() + "/tmp_stdout_"  +
        randomNonNegNumber() + ".txt");
    File errorFile = new File(TestUtils.getBaseDir() + "/tmp_stderr_"  +
        randomNonNegNumber() + ".txt");
    try {

      Process process = new ProcessBuilder().command(Arrays.asList(new String[]{
          "bash", "-c", cmd
      })).redirectOutput(outputFile).redirectError(errorFile).start();
      int exitCode;
      try {
        exitCode = process.waitFor();
      } catch (InterruptedException ex) {
        throw new IOException("Interrupted while trying to run command: " + cmd, ex);
      }
      CommandResult result;
      return new CommandResult(
          cmd,
          exitCode,
          readLinesFrom(outputFile),
          readLinesFrom(errorFile));
    } catch (IOException ex) {
      LOG.error("Exception while running command: " + cmd, ex);
      throw ex;
    } finally {
      if (outputFile.exists()) {
        outputFile.delete();
      }
      if (errorFile.exists()) {
        errorFile.delete();
      }
    }
  }
}
