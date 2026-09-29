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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.BaseYBTest;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestExternalDaemonLogErrorListener extends BaseYBTest {
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

  @Test
  public void testNoReport() throws Exception {
    ExternalDaemonLogErrorListener listener = new ExternalDaemonLogErrorListener("ts1");
    listener.handleLine(SERVER_STARTED);
    listener.handleLine("I0929 17:36:16.447434 3642425 bgworker.c:868] Started yb_ash collector");
    listener.reportErrorsAtEnd();
  }
}
