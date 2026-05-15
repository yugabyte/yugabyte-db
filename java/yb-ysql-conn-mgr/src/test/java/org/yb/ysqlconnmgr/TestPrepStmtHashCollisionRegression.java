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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;
import static org.yb.ysqlconnmgr.PgWireProtocol.*;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;

/**
 * Regression test for a 32-bit MurmurHash collision in Odyssey Connection
 * Manager's prepared-statement handling.
 *
 * Two TPC-C queries with statement names S_165461 and S_167793 produce the
 * same 32-bit MurmurHash (0xd95f1311). When the CM maps client statement
 * names to server-side names using this hash, the second Parse overwrites
 * the first on the backend. Re-executing the first statement then fails
 * because the parameter count no longer matches.
 *
 * This test fails on a 32-bit hash build and passes once the hash is
 * widened to 64 bits.
 */
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestPrepStmtHashCollisionRegression extends BaseYsqlConnMgr {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPrepStmtHashCollisionRegression.class);

  private static final int SOCKET_TIMEOUT_MS = 30000;

  private static final int OID_INT4 = 23;

  private static final String STMT_UPDATE = "S_165461";
  private static final String UPDATE_QUERY =
      "UPDATE DISTRICT SET D_NEXT_O_ID = D_NEXT_O_ID + 1" +
      "  WHERE D_W_ID = $1    AND D_ID = $2" +
      "   RETURNING D_NEXT_O_ID, D_TAX";

  private static final String STMT_INSERT = "S_167793";
  private static final String INSERT_QUERY =
      "INSERT INTO NEW_ORDER (NO_O_ID, NO_D_ID, NO_W_ID)" +
      "  VALUES ( $1, $2, $3)";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> flags = new HashMap<String, String>() {
      {
        put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
      }
    };
    builder.addCommonTServerFlags(flags);
  }

  @Test
  public void testHashCollisionRegression() throws Exception {
    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);

    // Phase 1: Set up tables using unnamed statements.
    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Setup connection ready");

      String[] ddlStatements = {
          "DROP TABLE IF EXISTS NEW_ORDER",
          "DROP TABLE IF EXISTS DISTRICT",
          "CREATE TABLE DISTRICT (D_W_ID INT, D_ID INT, D_NEXT_O_ID INT, D_TAX NUMERIC)",
          "CREATE TABLE NEW_ORDER (NO_O_ID INT, NO_D_ID INT, NO_W_ID INT)",
          "INSERT INTO DISTRICT VALUES (1, 1, 100, 0.05)"
      };

      for (String ddl : ddlStatements) {
        out.write(buildParse(ddl));
        out.write(buildBind());
        out.write(buildExecute());
        out.write(buildSync());
        out.flush();

        for (int i = 0; i < 4; i++) {
          PgMessage msg = readMessage(in);
          LOG.info("DDL response: " + msg);
          if (msg.type == BE_ERROR_RESPONSE) {
            fail("Error during setup (" + ddl + "): " +
                new String(msg.body, StandardCharsets.UTF_8));
          }
        }
      }

      out.write(buildTerminate());
      out.flush();
      LOG.info("Setup complete");
    }

    // Phase 2: Execute the collision test using named statements.
    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Collision-test connection ready");

      // Step A: Parse + Bind + Execute the UPDATE (S_165461), 2 INT4 params
      int[] updateOids = {OID_INT4, OID_INT4};
      out.write(buildParse(STMT_UPDATE, UPDATE_QUERY, updateOids));
      out.write(buildBind(STMT_UPDATE, new String[]{"1", "1"}));
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();

      // Expect: ParseComplete, BindComplete, DataRow, CommandComplete, ReadyForQuery
      PgMessage parseComplete1 = readMessage(in);
      LOG.info("Step A ParseComplete: " + parseComplete1);
      assertEquals("Expected ParseComplete", BE_PARSE_COMPLETE, parseComplete1.type);

      PgMessage bindComplete1 = readMessage(in);
      LOG.info("Step A BindComplete: " + bindComplete1);
      assertEquals("Expected BindComplete", BE_BIND_COMPLETE, bindComplete1.type);

      PgMessage dataRow1 = readMessage(in);
      LOG.info("Step A DataRow: " + dataRow1);
      assertEquals("Expected DataRow", BE_DATA_ROW, dataRow1.type);
      verifyUpdateResult(dataRow1, 101, "0.05");

      PgMessage cmdComplete1 = readMessage(in);
      LOG.info("Step A CommandComplete: " + cmdComplete1);
      assertEquals("Expected CommandComplete", BE_COMMAND_COMPLETE, cmdComplete1.type);

      PgMessage ready1 = readMessage(in);
      LOG.info("Step A ReadyForQuery: " + ready1);
      assertEquals("Expected ReadyForQuery", BE_READY_FOR_QUERY, ready1.type);

      // Step B: Parse + Bind + Execute the INSERT (S_167793), 3 INT4 params.
      // On 32-bit hash, the two server_keys hash to the same value (0xd95f1311)
      // so the CM may either overwrite the UPDATE plan or fail outright.
      int[] insertOids = {OID_INT4, OID_INT4, OID_INT4};
      out.write(buildParse(STMT_INSERT, INSERT_QUERY, insertOids));
      out.write(buildBind(STMT_INSERT, new String[]{"101", "1", "1"}));
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();

      // On 64-bit hash: ParseComplete, BindComplete, CommandComplete, ReadyForQuery
      // On 32-bit hash: ErrorResponse possible here due to collision
      PgMessage stepBFirst = readMessage(in);
      LOG.info("Step B first message: " + stepBFirst);

      if (stepBFirst.type == BE_ERROR_RESPONSE) {
        String errText = new String(stepBFirst.body, StandardCharsets.UTF_8);
        fail("Hash collision caused ErrorResponse during INSERT Parse/Bind " +
            "(32-bit hash collision between S_165461 and S_167793): " + errText);
      }

      assertEquals("Expected ParseComplete", BE_PARSE_COMPLETE, stepBFirst.type);

      PgMessage bindComplete2 = readMessage(in);
      LOG.info("Step B BindComplete: " + bindComplete2);
      if (bindComplete2.type == BE_ERROR_RESPONSE) {
        String errText = new String(bindComplete2.body, StandardCharsets.UTF_8);
        fail("Hash collision caused ErrorResponse during INSERT Bind " +
            "(32-bit hash collision between S_165461 and S_167793): " + errText);
      }
      assertEquals("Expected BindComplete", BE_BIND_COMPLETE, bindComplete2.type);

      PgMessage cmdComplete2 = readMessage(in);
      LOG.info("Step B CommandComplete: " + cmdComplete2);
      if (cmdComplete2.type == BE_ERROR_RESPONSE) {
        String errText = new String(cmdComplete2.body, StandardCharsets.UTF_8);
        fail("Hash collision caused ErrorResponse during INSERT Execute " +
            "(32-bit hash collision between S_165461 and S_167793): " + errText);
      }
      assertEquals("Expected CommandComplete", BE_COMMAND_COMPLETE, cmdComplete2.type);
      String cmdTag = new String(cmdComplete2.body, StandardCharsets.UTF_8);
      assertTrue("Expected INSERT command tag, got: " + cmdTag,
          cmdTag.startsWith("INSERT"));

      PgMessage ready2 = readMessage(in);
      LOG.info("Step B ReadyForQuery: " + ready2);
      assertEquals("Expected ReadyForQuery", BE_READY_FOR_QUERY, ready2.type);

      // Step C: Re-execute the UPDATE using Bind on S_165461 (no new Parse).
      // On 32-bit hash: the server has the INSERT plan under the hashed name,
      // so Bind with 2 params fails (parameter count mismatch).
      // On 64-bit hash: each statement has its own server-side name, UPDATE
      // plan is intact, returns d_next_o_id=102, d_tax=0.05.
      out.write(buildBind(STMT_UPDATE, new String[]{"1", "1"}));
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();

      PgMessage bindComplete3 = readMessage(in);
      LOG.info("Step C first message: " + bindComplete3);

      if (bindComplete3.type == BE_ERROR_RESPONSE) {
        String errText = new String(bindComplete3.body, StandardCharsets.UTF_8);
        fail("Hash collision caused ErrorResponse on re-execute of UPDATE " +
            "(32-bit hash collision between S_165461 and S_167793): " + errText);
      }

      assertEquals("Expected BindComplete for re-executed UPDATE",
          BE_BIND_COMPLETE, bindComplete3.type);

      PgMessage dataRow3 = readMessage(in);
      LOG.info("Step C DataRow: " + dataRow3);
      assertEquals("Expected DataRow", BE_DATA_ROW, dataRow3.type);
      verifyUpdateResult(dataRow3, 102, "0.05");

      PgMessage cmdComplete3 = readMessage(in);
      LOG.info("Step C CommandComplete: " + cmdComplete3);
      assertEquals("Expected CommandComplete", BE_COMMAND_COMPLETE, cmdComplete3.type);

      PgMessage ready3 = readMessage(in);
      LOG.info("Step C ReadyForQuery: " + ready3);
      assertEquals("Expected ReadyForQuery", BE_READY_FOR_QUERY, ready3.type);

      LOG.info("All steps passed -- no hash collision detected");

      out.write(buildTerminate());
      out.flush();
    }
  }

  private void verifyUpdateResult(PgMessage dataRow, int expectedNextOId, String expectedTax) {
    ByteBuffer bb = ByteBuffer.wrap(dataRow.body);
    short numCols = bb.getShort();
    assertEquals("Expected 2 columns in RETURNING clause", 2, numCols);

    int col1Len = bb.getInt();
    byte[] col1Data = new byte[col1Len];
    bb.get(col1Data);
    String nextOId = new String(col1Data, StandardCharsets.UTF_8);
    assertEquals("d_next_o_id mismatch", Integer.toString(expectedNextOId), nextOId);

    int col2Len = bb.getInt();
    byte[] col2Data = new byte[col2Len];
    bb.get(col2Data);
    String tax = new String(col2Data, StandardCharsets.UTF_8);
    assertEquals("d_tax mismatch", expectedTax, tax);
  }
}
