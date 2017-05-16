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
package org.yb.client;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableSet;
import com.sun.security.auth.module.UnixSystem;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.Field;

import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.URL;
import java.net.URLDecoder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.Random;
import java.util.Set;
import java.util.function.Supplier;

/**
 * A grouping of methods that help unit testing.
 */
public class TestUtils {
  private static final Logger LOG = LoggerFactory.getLogger(TestUtils.class);

  // Used by pidOfProcess()
  private static String UNIX_PROCESS_CLS_NAME =  "java.lang.UNIXProcess";
  private static Set<String> VALID_SIGNALS =  ImmutableSet.of("STOP", "CONT", "TERM", "KILL");

  private static final String BIN_DIR_PROP = "binDir";

  private static String ybRootDir = null;

  public static final boolean IS_LINUX = System.getProperty("os.name").equals("Linux");

  private static final long unixUserId = new UnixSystem().getUid();
  private static final long startTimeMillis = System.currentTimeMillis();
  private static final Random randomGenerator = new Random(startTimeMillis);

  public static final int MIN_PORT_TO_USE = 10000;
  public static final int MAX_PORT_TO_USE = 32768;

  /** Time to sleep in milliseconds waiting for conditions to be met. */
  private static final int SLEEP_TIME_MS = 1000;

  /**
   * @return the path of the flags file to pass to daemon processes
   * started by the tests
   */
  public static String getFlagsPath() {
    URL u = BaseYBClientTest.class.getResource("/flags");
    if (u == null) {
      throw new RuntimeException("Unable to find 'flags' file");
    }
    if (u.getProtocol() == "file") {
      return urlToPath(u);
    }
    // If the flags are inside a JAR, extract them into our temporary
    // test directory.
    try {
      // Somewhat unintuitively, createTempFile() actually creates the file,
      // not just the path, so we have to use REPLACE_EXISTING below.
      Path tmpFile = Files.createTempFile(
          Paths.get(getBaseDir()), "yb-flags", ".flags");
      Files.copy(BaseYBClientTest.class.getResourceAsStream("/flags"), tmpFile,
          StandardCopyOption.REPLACE_EXISTING);
      return tmpFile.toAbsolutePath().toString();
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
    URL myUrl = BaseYBClientTest.class.getProtectionDomain().getCodeSource().getLocation();
    File myPath = new File(urlToPath(myUrl));
    while (myPath != null) {
      if (new File(myPath, ".git").isDirectory()) {
        // Cache the root dir so that we don't have to find it every time.
        ybRootDir = myPath.getAbsolutePath();
        return ybRootDir;
      }
      myPath = myPath.getParentFile();
    }
    throw new RuntimeException("Unable to find build dir! myUrl=" + myUrl);
  }

  /**
   * @return the directory with YB daemons' web UI assets
   */
  public static String getWebserverDocRoot() {
    return TestUtils.findYbRootDir() + "/www";
  }

  /**
   * @param binName the binary to look for (eg 'yb-tserver')
   * @return the absolute path of that binary
   * @throws FileNotFoundException if no such binary is found
   */
  public static String findBinary(String binName) throws FileNotFoundException {
    String binDir = System.getProperty(BIN_DIR_PROP);
    if (binDir != null) {
      LOG.info("Using binary directory specified by property: {}",
          binDir);
    } else {
      binDir = findYbRootDir() + "/build/latest/bin";
    }

    File candidate = new File(binDir, binName);
    if (candidate.canExecute()) {
      return candidate.getAbsolutePath();
    }
    throw new FileNotFoundException("Cannot find binary " + binName +
        " in binary directory " + binDir);
  }

  /**
   * @return the base directory within which we will store server data
   */
  public static String getBaseDir() {
    String testTmpDir = System.getenv("TEST_TMPDIR");
    if (testTmpDir == null) {
      testTmpDir = "/tmp/ybtest-" + unixUserId + "-" + startTimeMillis;
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
    if (!procCls.getName().equals(UNIX_PROCESS_CLS_NAME)) {
      throw new IllegalArgumentException("stopProcess() expects objects of class " +
          UNIX_PROCESS_CLS_NAME + ", but " + procCls.getName() + " was passed in instead!");
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
      if (isPortFree(bindIp, port, attempt == MAX_ATTEMPTS - 1)) {
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
  }

}
