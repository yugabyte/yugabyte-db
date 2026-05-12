// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;

/**
 * Captures Odyssey (ysql-conn-mgr) log output from the tserver's stdout stream,
 * allowing tests to wait for log lines matching a regex pattern.
 *
 * In the minicluster, --logtostderr causes Odyssey to write logs to stdout
 * (log_to_stdout yes). Since Odyssey is a child process of the tserver, its
 * stdout flows through the tserver's LogPrinter. This class registers as a
 * LogErrorListener on that LogPrinter to receive lines in real time.
 */
public class ConnMgrLogTailer implements LogErrorListener {
  private static final Logger LOG = LoggerFactory.getLogger(ConnMgrLogTailer.class);

  private final List<String> lines = new ArrayList<>();
  private final Object lock = new Object();

  private ConnMgrLogTailer() {}

  /**
   * Creates a tailer for the connection manager logs of the given tserver.
   * Registers as a listener on the tserver daemon's LogPrinter so that
   * Odyssey stdout lines are captured in real time.
   */
  public static ConnMgrLogTailer create(MiniYBCluster cluster, int tserverIndex)
      throws IOException {
    MiniYBDaemon daemon = findDaemonForIndex(cluster, tserverIndex);
    ConnMgrLogTailer tailer = new ConnMgrLogTailer();
    daemon.getLogPrinter().addErrorListener(tailer);
    LOG.info("Created ConnMgrLogTailer for tserver index {}", tserverIndex);
    return tailer;
  }

  /**
   * Called by LogPrinter for every line from the tserver process's stdout.
   * Only buffers Odyssey lines, identified by a leading digit.
   */
  @Override
  public void handleLine(String line) {
    // Odyssey log lines start with the PID (a digit) per log_format "%p %t %l ...", e.g.:
    //   338530 2026-03-04 12:21:46.597 UTC error [...] (context) message
    // Tserver glog lines start with a level letter (I/W/E/F), e.g.:
    //   I0304 12:21:46.597123 12345 file.cc:42] message
    // Only buffer Odyssey lines so we don't match against tserver output.
    if (line.isEmpty() || !Character.isDigit(line.charAt(0))) {
      return;
    }
    synchronized (lock) {
      lines.add(line);
      lock.notifyAll();
    }
  }

  @Override
  public void reportErrorsAtEnd() {}

  /**
   * Clears buffered lines and resets the scan position.
   * Call before each test action to ignore previously written log lines.
   */
  public void skipToEnd() {
    synchronized (lock) {
      lines.clear();
    }
    LOG.info("skipToEnd: cleared buffer");
  }

  /**
   * Scans buffered lines from the mark position for a regex match
   * (partial match via {@code Matcher.find()}). Blocks until a match
   * is found or the timeout expires.
   *
   * @return the first matching line, or {@code null} on timeout
   */
  public String waitForLogRegex(String regex, long timeout, TimeUnit unit)
      throws InterruptedException {
    Pattern pattern = Pattern.compile(regex);
    long deadline = System.nanoTime() + unit.toNanos(timeout);

    LOG.info("waitForLogRegex: pattern='{}', timeout={} {}", regex, timeout, unit);

    int scanFrom = 0;

    while (true) {
      synchronized (lock) {
        for (int i = scanFrom; i < lines.size(); i++) {
          if (pattern.matcher(lines.get(i)).find()) {
            LOG.info("Matched line: {}", lines.get(i));
            return lines.get(i);
          }
        }
        scanFrom = lines.size();

        long waitMs = TimeUnit.NANOSECONDS.toMillis(deadline - System.nanoTime());
        if (waitMs <= 0) {
          LOG.warn("waitForLogRegex timed out without matching '{}'", regex);
          return null;
        }
        lock.wait(waitMs);
      }
    }
  }

  private static MiniYBDaemon findDaemonForIndex(MiniYBCluster cluster, int tserverIndex)
      throws IOException {
    String targetHost = cluster.getPostgresContactPoints().get(tserverIndex).getHostName();
    for (MiniYBDaemon daemon : cluster.getTabletServers().values()) {
      if (targetHost.equals(daemon.getLocalhostIP())) {
        return daemon;
      }
    }
    throw new IOException("No tserver daemon found for index " + tserverIndex
        + " (host " + targetHost + ")");
  }
}
