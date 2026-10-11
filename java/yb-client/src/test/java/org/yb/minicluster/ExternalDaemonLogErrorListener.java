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

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileVisitOption;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ExternalDaemonLogErrorListener implements LogErrorListener {

  private static final Logger LOG = LoggerFactory.getLogger(ExternalDaemonLogErrorListener.class);

  private final Object serverStartEventMonitor = new Object();
  private boolean sawServerStarting = false;

  // These messages in the log will cause a test failure in the end.
  private static final String[] ERROR_LOG_PATTERNS = {
      "LeakSanitizer: ",
      "AddressSanitizer: ",
      "ThreadSanitizer: ",
      "Segmentation fault: ",
      "UndefinedBehaviorSanitizer: ",
      // PG's MEMORY_CONTEXT_CHECKING reports memory corruption only as a WARNING like these.
      "detected write past chunk end in ",
      "problem in alloc set ",
      "problem in slab ",
      "problem in Generation "
  };

  // TODO: consider collecting all matching lines here, up to a certain number.
  private String errorLogLine ;
  private String processDescription;

  public ExternalDaemonLogErrorListener(String processDescription) {
    this.processDescription = processDescription;
  }

  @Override
  public void handleLine(String line) {
    if (line.contains("RPC server started.")) {
      synchronized (serverStartEventMonitor) {
        sawServerStarting = true;
        serverStartEventMonitor.notifyAll();
      }
    }
    if (errorLogLine == null) {
      for (String pattern : ERROR_LOG_PATTERNS) {
        if (line.contains(pattern)) {
          errorLogLine = line;
          break;
        }
      }
    }
  }

  @Override
  public void reportErrorsAtEnd() {
    if (errorLogLine != null) {
      LOG.error("Error log line causing the test to fail: ", errorLogLine);
      throw new AssertionError(
          "An error found in the log: " + processDescription + ": " + errorLogLine);
    }
  }

  /**
   * Matches the error patterns against every file under {@code dir}, following symlinks, for
   * daemons whose output goes to log files instead of a stream that a listener sees.  Throws an
   * AssertionError for the first file with a matching line.
   */
  public static void checkLogFiles(Path dir, String processDescription) throws IOException {
    List<Path> files;
    try (Stream<Path> paths = Files.walk(dir, FileVisitOption.FOLLOW_LINKS)) {
      files = paths.filter(Files::isRegularFile).sorted().collect(Collectors.toList());
    }
    Set<Path> seen = new HashSet<>();
    for (Path file : files) {
      // glog's yb-tserver.INFO and the like are symlinks to files also in the walk.
      if (!seen.add(file.toRealPath())) {
        continue;
      }
      ExternalDaemonLogErrorListener listener = new ExternalDaemonLogErrorListener(
          processDescription + " " + dir.relativize(file));
      try (BufferedReader reader = new BufferedReader(
               new InputStreamReader(Files.newInputStream(file), StandardCharsets.UTF_8))) {
        reader.lines().forEach(listener::handleLine);
      } catch (UncheckedIOException e) {
        throw e.getCause();
      }
      listener.reportErrorsAtEnd();
    }
  }

  public void waitForServerStartingLogLine(long deadlineMs) throws InterruptedException {
    long timeoutMs = deadlineMs - System.currentTimeMillis();
    synchronized (serverStartEventMonitor) {
      long timeLeftMs = deadlineMs - System.currentTimeMillis();
      while (timeLeftMs > 0 && !sawServerStarting) {
        serverStartEventMonitor.wait(timeLeftMs);
        timeLeftMs = deadlineMs - System.currentTimeMillis();
      }
      if (!sawServerStarting) {
        throw new RuntimeException(
            "Timed out waiting for a 'server starting' message to appear. " +
            "Waited for " + timeoutMs + ". Log: " + processDescription);
      }
    }
  }
}
