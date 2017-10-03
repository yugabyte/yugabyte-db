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
package org.kududb.client;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Lists;
import com.sun.security.auth.module.UnixSystem;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.lang.reflect.Field;
import java.net.ServerSocket;
import java.net.URL;
import java.net.URLDecoder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.List;
import java.util.Set;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A grouping of methods that help unit testing.
 */
public class TestUtils {
  private static final Logger LOG = LoggerFactory.getLogger(TestUtils.class);

  // Used by pidOfProcess()
  private static String UNIX_PROCESS_CLS_NAME =  "java.lang.UNIXProcess";
  private static Set<String> VALID_SIGNALS =  ImmutableSet.of("STOP", "CONT", "TERM", "KILL");

  private static final String BIN_DIR_PROP = "binDir";

  /**
   * @return the path of the flags file to pass to daemon processes
   * started by the tests
   */
  public static String getFlagsPath() {
    URL u = BaseKuduTest.class.getResource("/flags");
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
          Paths.get(getBaseDir()), "kudu-flags", ".flags");
      Files.copy(BaseKuduTest.class.getResourceAsStream("/flags"), tmpFile,
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

  private static String findBuildDir() {
    URL myUrl = BaseKuduTest.class.getProtectionDomain().getCodeSource().getLocation();
    File myPath = new File(urlToPath(myUrl));
    while (myPath != null) {
      if (new File(myPath, ".git").isDirectory()) {
        return new File(myPath, "build/latest/bin").getAbsolutePath();
      }
      myPath = myPath.getParentFile();
    }
    LOG.warn("Unable to find build dir! myUrl={}", myUrl);
    return null;
  }

  /**
   * @param binName the binary to look for (eg 'kudu-tserver')
   * @return the absolute path of that binary
   * @throws FileNotFoundException if no such binary is found
   */
  public static String findBinary(String binName) throws FileNotFoundException {
    String binDir = System.getProperty(BIN_DIR_PROP);
    if (binDir != null) {
      LOG.info("Using binary directory specified by property: {}",
          binDir);
    } else {
      binDir = findBuildDir();
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
    String s = System.getenv("TEST_TMPDIR");
    if (s == null) {
      s = String.format("/tmp/kudutest-%d", new UnixSystem().getUid());
    }
    File f = new File(s);
    f.mkdirs();
    return f.getAbsolutePath();
  }

  /**
   * Finds the next free port, starting with the one passed. Keep in mind the
   * time-of-check-time-of-use nature of this method, the returned port might become occupied
   * after it was checked for availability.
   * @param startPort First port to be probed.
   * @return A currently usable port.
   * @throws IOException IOE is thrown if we can't close a socket we tried to open or if we run
   * out of ports to try.
   */
  public static int findFreePort(int startPort) throws IOException {
    ServerSocket ss;
    for(int i = startPort; i < 65536; i++) {
      try {
        ss = new ServerSocket(i);
      } catch (IOException e) {
        continue;
      }
      ss.close();
      return i;
    }
    throw new IOException("Ran out of ports.");
  }

  /**
   * Finds a specified number of parts, starting with one passed. Keep in mind the
   * time-of-check-time-of-use nature of this method.
   * @see {@link #findFreePort(int)}
   * @param startPort First port to be probed.
   * @param numPorts Number of ports to reserve.
   * @return A list of currently usable ports.
   * @throws IOException IOE Is thrown if we can't close a socket we tried to open or if run
   * out of ports to try.
   */
  public static List<Integer> findFreePorts(int startPort, int numPorts) throws IOException {
    List<Integer> ports = Lists.newArrayListWithCapacity(numPorts);
    for (int i = 0; i < numPorts; i++) {
      startPort = findFreePort(startPort);
      ports.add(startPort++);
    }
    return ports;
  }

  /**
   * Gets the pid of a specified process. Relies on reflection and only works on
   * UNIX process, not guaranteed to work on JDKs other than Oracle and OpenJDK.
   * @param proc The specified process.
   * @return The process UNIX pid.
   * @throws IllegalArgumentException If the process is not a UNIXProcess.
   * @throws Exception If there are other getting the pid via reflection.
   */
  static int pidOfProcess(Process proc) throws Exception {
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
}
