// Copyright (c) YugabyteDB, Inc.
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

package org.yb.minicluster;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.nio.file.Files;
import java.nio.file.Path;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.yb.BaseYBTest;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestExternalDaemonLogErrorListener extends BaseYBTest {
  @Rule
  public TemporaryFolder tmp = new TemporaryFolder();

  private static final String SERVER_STARTED =
      "I0929 17:34:23.000779 rpc_server.cc:179] RPC server started. Bound to: 127.0.0.1:9100";

  private static final String ASAN_REPORT =
      "==3669640==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x7bb278177500";

  private static void assertReportsError(ExternalDaemonLogErrorListener listener, String line) {
    try {
      listener.reportErrorsAtEnd();
    } catch (AssertionError e) {
      assertTrue(e.getMessage(), e.getMessage().contains(line));
      return;
    }
    fail("Expected the listener to report: " + line);
  }

  @Test
  public void testReportBeforeServerStart() throws Exception {
    ExternalDaemonLogErrorListener listener = new ExternalDaemonLogErrorListener("ts1");
    listener.handleLine(ASAN_REPORT);
    listener.handleLine(SERVER_STARTED);
    assertReportsError(listener, ASAN_REPORT);
  }

  // Postgres, and so every sanitizer report from a PG backend, starts after this line.
  @Test
  public void testReportAfterServerStart() throws Exception {
    ExternalDaemonLogErrorListener listener = new ExternalDaemonLogErrorListener("ts1");
    listener.handleLine(SERVER_STARTED);
    listener.handleLine(ASAN_REPORT);
    assertReportsError(listener, ASAN_REPORT);
  }

  // One line for each kind of MEMORY_CONTEXT_CHECKING report.  A PG backend logs it with a
  // "WARNING:" prefix; PG code in the tserver (ybgate) logs it at INFO severity without one.
  private static final String[] MEMORY_CONTEXT_REPORTS = {
      "2026-09-30 17:40:02.421 UTC [594514] WARNING:  detected write past chunk end in " +
          "ExprContext 0x7dbe9d830210",
      "2026-09-30 17:40:02.421 UTC [594514] WARNING:  problem in alloc set ExprContext: " +
          "detected write past chunk end in block 0x7dbe9d830000, chunk 0x7dbe9d830210",
      "2026-09-30 17:40:02.421 UTC [594514] WARNING:  problem in slab Change: number of free " +
          "chunks 3 in block 0x7dbe9d830000 does not match freelist 2",
      "2026-09-30 17:40:02.421 UTC [594514] WARNING:  problem in Generation Tuples: bogus " +
          "chunk size in block 0x7dbe9d830000, chunk 0x7dbe9d830210",
      "I0930 17:40:02.421000 594514 aset.c:1153] detected write past chunk end in " +
          "YbgMemoryContext 0x7dbe9d830210",
  };

  @Test
  public void testMemoryContextReports() throws Exception {
    for (String report : MEMORY_CONTEXT_REPORTS) {
      ExternalDaemonLogErrorListener listener = new ExternalDaemonLogErrorListener("ts1");
      listener.handleLine(SERVER_STARTED);
      listener.handleLine(report);
      assertReportsError(listener, report);
    }
  }

  // Lays out a log directory as yugabyted does: process stdout/stderr files at the top, and each
  // process's own log directory linked in.
  private Path makeLogDir(String postgresLogLine) throws Exception {
    Path logs = tmp.newFolder("logs").toPath();
    Path tserverLogs = tmp.newFolder("data", "yb-data", "tserver", "logs").toPath();
    Files.write(logs.resolve("tserver.out"), "Starting tserver\n".getBytes());
    Files.write(logs.resolve("tserver.err"), new byte[0]);
    Files.createSymbolicLink(logs.resolve("tserver"), tserverLogs);
    Files.write(tserverLogs.resolve("postgresql-2026-10-02_000000.log"),
                ("LOG:  database system is ready to accept connections\n" + postgresLogLine + "\n")
                    .getBytes());
    return logs;
  }

  @Test
  public void testLogFilesWithReport() throws Exception {
    Path logs = makeLogDir(ASAN_REPORT);
    try {
      ExternalDaemonLogErrorListener.checkLogFiles(logs, "node1");
    } catch (AssertionError e) {
      assertTrue(e.getMessage(), e.getMessage().contains(ASAN_REPORT));
      assertTrue(e.getMessage(), e.getMessage().contains("node1 tserver/postgresql-"));
      return;
    }
    fail("Expected a report in a file under a linked directory to be found");
  }

  @Test
  public void testLogFilesWithoutReport() throws Exception {
    Path logs = makeLogDir("LOG:  received fast shutdown request");
    ExternalDaemonLogErrorListener.checkLogFiles(logs, "node1");
  }

  @Test
  public void testNoReport() throws Exception {
    ExternalDaemonLogErrorListener listener = new ExternalDaemonLogErrorListener("ts1");
    listener.handleLine(SERVER_STARTED);
    listener.handleLine("I0929 17:36:16.447434 3642425 bgworker.c:868] Started yb_ash collector");
    listener.reportErrorsAtEnd();
  }
}
