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

import java.sql.Connection;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.RequiresLinux;
import org.yb.minicluster.MiniYBClusterBuilder;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCopyCmd extends BaseYsqlConnMgr {

  /** Timeout for operations that must not deadlock. */
  private static final int TEST_TIMEOUT_MS = 10_000;

  private static final String INSERT_TWO_ROWS =
      "INSERT INTO copytest VALUES ('foo', 1, 1.23), ('bar', 2, 4.56)";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_wait_for_rfq_on_sync", "true");
        put("ysql_conn_mgr_log_settings", "log_debug,log_query");
      }
    };
    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  private void createCopyTable() throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement st = conn.createStatement()) {
      st.execute("DROP TABLE IF EXISTS copytest");
      st.execute("CREATE TABLE copytest (a text, b int, c numeric(5,2))");
    }
  }

  private WireConn connect() throws Exception {
    return rawConnBuilder().socketTimeoutMs(TEST_TIMEOUT_MS).connect();
  }

  // It's been tested using raw packets since JDBC explicitly uses
  // simple query protocol for COPY operations and it's easy to validate
  // the response received from the backend.

  /**
   * Verifies that executing COPY via the extended-query protocol
   * does not hangup client operations when
   * ysql_conn_mgr_wait_for_rfq_on_sync is enabled.
   *
   * Packet sequence:
   *   Client P("COPY copytest FROM STDIN") + B + E + S
   *   Server ParseComplete + BindComplete + CopyInResponse
   *   Client CopyFail("test-cancel") + S
   *   Server ErrorResponse + ReadyForQuery
   */
  @Test
  public void testCopyFromViaExtendedQuery() throws Exception {
    createCopyTable();

    try (WireConn c = connect()) {
      // It's important to send SYNC to receive any packet from backend.
      // Postgres wouldn't send RFQ for this sync packet as it goes into
      // COPY mode.
      c.createPipeline()
       .parse("COPY copytest FROM STDIN").bind().execute().expectCopyIn()
       .sync().ignoredInCopyMode()
       .run();

      c.createPipeline()
       .copyFail("test-cancel")
       .sync()
       .run();

      // Conn mgr must got synchronized after RFQ and by ignoring the SYNC
      // sent in CopyMode.
      c.createPipeline()
       .query("SELECT 1").row("1")
       .run();
    }
  }

  /*
   * Test verifies when in Copy From mode, connection manager ignores mulitple
   * SYNC packets sent by client and after copy done, it starts processing
   * SYNC packets sent by client.
   */
  @Test
  public void testCopyFromMultipleSyncsViaExtendedQuery() throws Exception {
    createCopyTable();

    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("COPY copytest FROM STDIN").bind().execute().expectCopyIn()
       .sync().ignoredInCopyMode()
       .sync().ignoredInCopyMode()
       .sync().ignoredInCopyMode()
       .run();

      c.createPipeline()
       .copyDone().tag("COPY 0")
       .parse("S1", "INSERT INTO copytest VALUES (3, 2, 3)").bind("S1").execute().rowCount(0)
       .sync()
       .bind("S1").execute().rowCount(0)
       .sync()
       .run();

      // Conn mgr must got synchronized after RFQ and by ignoring the SYNC
      // sent in CopyMode.
      c.createPipeline()
       .query("SELECT 1").row("1")
       .run();
    }
  }

  /*
   * Some driver (go) can send CopyData, CopyDone immediately after
   * sending COPY FROM command without waiting for COPYINRESPONSE packet
   * from backend. This test verifies connection manager can handle this
   * scenario. Since JDBC doesn't do this, so sending raw packets.
   *
   */
  @Test
  public void testCopyFromCopyDoneSentImmediatelyExtendedQuery() throws Exception {
    createCopyTable();

    try (WireConn c = connect()) {
      // In this case, first SYNC shouldn't be ignored by conn mgr, as CopyDone
      // message has already been forwarded to the backend. If ignored, parse queue
      // would be corrupted and will throw error while dequeueing parse complete packet
      // for S2 prep stmt name.
      c.createPipeline()
       .parse("S1", "INSERT INTO copytest VALUES (3, 2, 3)").bind("S1").execute().rowCount(0)
       .parse("COPY copytest FROM STDIN").bind().execute().expectCopyIn()
       .copyData("3\t2\t3\n")
       // Conn mgr must pause the relay over here and don't resume until synchronised, as CopyDone
       // message has already been forwarded to the backend.
       .copyDone().tag("COPY 1")
       .sync()
       .parse("S2", "SELECT 2").bind("S2").execute().row("2")
       .sync()
       .run();

      // Since above transaction will get committed with no error,
      // we should see 2 rows (from the INSERT and COPY) in the table.
      c.createPipeline()
       .query("SELECT * FROM copytest").rowCount(2)
       .run();
    }
  }

  /*
   * This is an extension of testCopyFromCopyDoneSentImmediatelyExtendedQuery
   * test, where it validates the client relay for conn mgr is paused and
   * resumed correctly for COPY operations.
   * It validates by sending COPY Data and CopyDone packets immediately after
   * sending COPY FROM command without waiting for COPYINRESPONSE packet
   * from backend. And in the same sync boundary, sends:
   * P(Error) + P(S1) + B + E + SYNC
   * Followed by:
   * P(S1) + B + E SYNC.
   * COPYINRESPONSE must resume the client relay, if COPY_DATA, COPY_DONE/
   * COPY_FAIL is not forwarded.
   * So first SYNC would continue waiting to get reconcile server hashmap
   * before processing packets from next sync boundary (second S1) in
   * connection manager.
   */
  @Test
  public void testCopyFromCopyDoneSentImmedErrorInStream() throws Exception {
    createCopyTable();

    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("S1", "INSERT INTO copytest VALUES (3, 2, 3)").bind("S1").execute().rowCount(0)
       .parse("COPY copytest FROM STDIN").bind().execute().expectCopyIn()
       .copyData("3\t2\t3\n")
       .copyDone().tag("COPY 1")
       .parse("S_Error", "INSERT INTO copytest VALUES (4, 5, 6, 7)")
           .expectError("INSERT has more expressions than target columns")
       .bind("S_Error").execute()
       .parse("S2", "SELECT 2").bind("S2").execute()
       .sync()
       .parse("S2", "SELECT 2").bind("S2").execute().row("2")
       .sync()
       .run();
    }
  }

  /**
   * Verifies COPY TO STDOUT via extended query protocol works successfully
   * with connection manager by sending extra packets before and after
   * COPY TO STDOUT command.
   *
   * Client Packet sequence:
   *   Client P("SELECT * FROM copytest") + B + E
   *   Client P("COPY copytest TO STDOUT") + B + E
   *   Client P("SELECT 2") + B + E + SYNC.
   */
  @Test
  public void testCopyToViaExtendedQuery() throws Exception {
    createCopyTable();

    try (WireConn c = connect()) {
      // Insert rows so COPY TO returns actual data.
      c.createPipeline()
       .query(INSERT_TWO_ROWS)
       .run();

      c.createPipeline()
       .parse("S1", "SELECT * FROM copytest").bind("S1").execute().rowCount(2)
       .parse("COPY copytest TO STDOUT").bind().execute().expectCopyOut(2)
       .parse("S2", "SELECT 2").bind("S2").execute().row("2")
       .sync()
       .run();
    }
  }

  /**
   * Verifies COPY TO STDOUT and COPY FROM STDIN via extended query protocol
   * works successfully when they are sent in the same sync boundary.
   *
   * Client Packet sequence:
   *   Client P("SELECT * FROM copytest") + B + E
   *   Client P("COPY copytest TO STDOUT") + B + E
   *   Client P("COPY copytest FROM STDIN") + B + E + SYNC
   *   Client P("SELECT 2") + B + E + SYNC.
   *   Client P("SELECT * FROM copytest") + B + E
   *   Client P("SELECT 2") + B + E + SYNC.
   */

  @Test
  public void testCopyToCopyFromSyncViaExtendedQuery() throws Exception {
    createCopyTable();

    try (WireConn c = connect()) {
      // Insert rows so COPY TO returns actual data.
      c.createPipeline()
       .query(INSERT_TWO_ROWS)
       .run();

      c.createPipeline()
       .parse("S1", "SELECT * FROM copytest").bind("S1").execute().rowCount(2)
       .parse("COPY copytest TO STDOUT").bind().execute().expectCopyOut(2)
       .parse("COPY copytest FROM STDIN").bind().execute().expectCopyIn()
       .sync().ignoredInCopyMode()
       .copyData("3\t2\t3\n")
       .copyDone().tag("COPY 1")
       .parse("S2", "SELECT 2").bind("S2").execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .query("SELECT * FROM copytest").rowCount(3)
       .run();
    }
  }
}
