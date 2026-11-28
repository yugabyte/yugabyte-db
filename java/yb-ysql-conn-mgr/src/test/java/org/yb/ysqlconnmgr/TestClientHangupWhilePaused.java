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

import static org.yb.AssertionWrappers.assertLessThan;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;

/**
 * Tests YSQL Connection Manager noticing a client that goes away while it is paused after a Sync.
 *
 * Conn mgr stops reading a client after its Sync until the backend's ReadyForQuery, so the
 * client's FIN or RST can wait on the socket behind bytes conn mgr has not read. Conn mgr must
 * still notice the hangup, so the client's cleanup cancels the running statement and returns the
 * backend to the pool. Each test runs with a single backend and a 2 s wait before that cancel.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestClientHangupWhilePaused extends BaseYsqlConnMgr {

  public TestClientHangupWhilePaused() {
    // Certificates are issued for IP addresses, not hostnames.
    useIpWithCertificate = true;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
  }

  @Test
  public void testDisconnectWhilePausedAfterSync() throws Exception {
    restartWithFlags(Collections.emptyMap());
    assertDisconnectWhilePausedCancelsStatement(rawConnBuilder(), 0);
  }

  // More client bytes after the Sync than the readahead holds, so the FIN waits behind them.
  @Test
  public void testDisconnectWithFullReadaheadWhilePausedAfterSync() throws Exception {
    restartWithFlags(Collections.singletonMap("ysql_conn_mgr_readahead_buffer_size", "64"));
    assertDisconnectWhilePausedCancelsStatement(rawConnBuilder(), 4096);
  }

  // As above over TLS, with a 64 KB Query so that TLS records stay on the socket ahead of the FIN.
  @Test
  public void testDisconnectWithFullReadaheadWhilePausedAfterSyncWithSsl() throws Exception {
    Map<String, String> flags = new HashMap<>();
    flags.put("use_client_to_server_encryption", "true");
    flags.put("certs_for_client_dir", certsDir());
    flags.put("ysql_conn_mgr_readahead_buffer_size", "64");
    restartWithFlags(flags);
    assertDisconnectWhilePausedCancelsStatement(rawConnBuilder().ssl(), 65536);
  }

  // A client sends Parse/Bind/Execute/Sync for an INSERT and closes its socket right away,
  // without waiting for the reply. Conn mgr has read the batch before it sees the FIN, so it must
  // send the batch to the backend before acting on the hangup, as Postgres would run it. The
  // INSERT must commit, and the backend must go back to the pool for the next client.
  @Test
  public void testInsertThenDisconnectWhilePausedAfterSync() throws Exception {
    restartWithFlags(Collections.emptyMap());
    try (WireConn conn = rawConnBuilder().connect()) {
      conn.createPipeline().query("CREATE TABLE paused_hangup_t (k int)").run();
    }

    long startMs;
    try (WireConn conn = rawConnBuilder().connect()) {
      conn.createPipeline()
          .parse("INSERT INTO paused_hangup_t VALUES (1)").bind().execute()
          .sync()
          .sendWithoutReading();
      startMs = System.currentTimeMillis();
      conn.closeAbruptly();
    }

    try (WireConn conn = rawConnBuilder().socketTimeoutMs(30000).connect()) {
      conn.createPipeline().query("SELECT count(*) FROM paused_hangup_t").row("1").run();
    }
    assertNextClientGotBackendSoon(startMs, "right after an INSERT's Sync");
  }

  /**
   * A client sends Parse/Bind/Execute/Sync for a 60 s sleep, followed by a Query of tailBytes
   * characters when tailBytes is above 0, and goes away while conn mgr has it paused on the Sync:
   * once with a FIN and once with a RST. Each time, conn mgr must cancel the sleep and give the
   * backend to the next client within seconds, however much of the Query is still unread.
   */
  private void assertDisconnectWhilePausedCancelsStatement(
      WireConn.Builder builder, int tailBytes) throws Exception {
    for (boolean reset : new boolean[] {false, true}) {
      long startMs;
      try (WireConn conn = builder.connect()) {
        Pipeline pipeline = conn.createPipeline()
            .parse("SELECT pg_sleep(60)").bind().execute()
            .sync();
        if (tailBytes > 0) {
          pipeline.query("SELECT '" + String.join("", Collections.nCopies(tailBytes, "a")) + "'");
        }
        pipeline.sendWithoutReading();
        Thread.sleep(1000);
        // Over TLS, close() can block until conn mgr closes its side, so time from here.
        startMs = System.currentTimeMillis();
        if (reset) {
          conn.resetAbruptly();
        } else {
          conn.closeAbruptly();
        }
      }

      try (WireConn conn = builder.socketTimeoutMs(30000).connect()) {
        conn.createPipeline().query("SELECT 1").row("1").run();
      }
      assertNextClientGotBackendSoon(startMs, String.format(
          "with a %s while paused after its Sync, with a %d byte Query after the Sync",
          reset ? "RST" : "FIN", tailBytes));
    }
  }

  private void assertNextClientGotBackendSoon(long startMs, String how) {
    long elapsedMs = System.currentTimeMillis() - startMs;
    String msg = String.format(
        "The next client got the backend %d ms after a client closed its socket %s",
        elapsedMs, how);
    LOG.info(msg);
    assertLessThan(msg, elapsedMs, 20000L);
  }

  private void restartWithFlags(Map<String, String> extraTserverFlags) throws Exception {
    Map<String, String> flags = new HashMap<>(SINGLE_BACKEND_FLAGS);
    // How long the client's cleanup waits for the statement before cancelling it.
    flags.put("ysql_conn_mgr_wait_timeout_ms", "2000");
    flags.putAll(extraTserverFlags);
    restartClusterWithAdditionalFlags(Collections.emptyMap(), flags);
  }
}
