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

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;
import org.yb.pgsql.ConnectionEndpoint;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestProtocolEdgeCases extends BaseYsqlConnMgr {

    // There is an optimisation in connection manager which checks the packet type to check
    // if the callback function (`od_frontend_server_remote` for server relay) can be called
    // with an incomplete packet. This makes sense for packets that the callback function
    // doesn't care about, and might even have performance benefits due to relaying data immediately
    // over buffering it.
    //
    // We try to hit the above described case here so that the callback `od_frontend_server_remote`
    // is called with an incomplete 'T' packet (RowDescription). This is made possible by large
    // columns and reduced `ysql_conn_mgr_readahead_buffer_size`.
    //
    // We have also set a GUC variable with GUC_REPORT so that a ParameterStatus packet also comes.
    // We're trying to verify whether the packet stream stays correct in this scenario. One way it
    // was going wrong was that processing a YbParameterStatus from server was resulting in a new
    // constructed ParameterStatus being written directly to the client socket, resulting in a
    // broken stream
    //
    // To understand it better, suppose the stream was: T_part1 + T_part2 + YbParamStatus
    // Note that T_part1 and T_part2 only make sense when presented together, CM is just processing
    // them separately due to the above mentioned process
    //
    // First read of socket gives T_part1 -- Gets added to async write buffer and then written out
    // Second read of socket gives T_part2 & YbParamStatus (These both fit into the readahead
    // buffer)
    //      - T_part2 gets added to async write buffer
    //      - Processing of YBParamStatus results in ParamStatus write to socket
    //      - Async write buffer gets written to socket
    //
    // Thus the stream reaching the client becomes T_part1 + ParamStatus + T_part2, a broken stream
    @Test
    public void testRowDescriptionPacketSplit() throws Exception {
        restartClusterWithAdditionalFlags(Collections.emptyMap(), new HashMap<String, String>() {
            {
                put("ysql_conn_mgr_readahead_buffer_size", "512");
            }
        });

        try (Connection conn = getConnectionBuilder()
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
                Statement stmt = conn.createStatement()) {

            // Drop function if exists
            stmt.execute("DROP FUNCTION IF EXISTS wide_t_with_guc_noise");

            // Create function that returns a wide table and floods ParameterStatus
            stmt.execute(
                "CREATE OR REPLACE FUNCTION wide_t_with_guc_noise()\n" +
                "RETURNS TABLE (\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_01 int,\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_02 int,\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_03 int,\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_04 int,\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_05 int,\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_06 int,\n" +
                "    col_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_07 int\n" +
                ")\n" +
                "LANGUAGE plpgsql\n" +
                "AS $$\n" +
                "BEGIN\n" +
                "    -- Flood ParameterStatus\n" +
                "    SET application_name = 'something_something';\n" +
                "    RETURN QUERY\n" +
                "    SELECT\n" +
                "      1, 1, 1, 1,\n" +
                "      1, 1, 1;\n" +
                "END;\n" +
                "$$");

            // Execute the function and verify results
            try (ResultSet rs = stmt.executeQuery("SELECT * FROM wide_t_with_guc_noise()")) {
                assertTrue("Expected one row from wide_t_with_guc_noise()", rs.next());

                // Verify all 7 columns return 1
                for (int i = 1; i <= 7; i++) {
                    assertEquals(String.format("Column %d should be 1", i), 1, rs.getInt(i));
                }
            }

        }
    }

    // A simple Query pipelined behind a failing extended-protocol message is discarded by the
    // backend's ignore_till_sync window and gets no ReadyForQuery. ConnMgr must not keep counting
    // it as a pending sync, or the relay paused at the following Sync never resumes and the Query
    // pipelined after the Sync is never forwarded.
    @Test
    public void testQueryDiscardedBeforeSync() throws Exception {
        try (WireConn conn = rawConnBuilder().socketTimeoutMs(15000).connect()) {
            conn.createPipeline()
                .executePortal("").expectError("34000", "portal \"\" does not exist")
                .query("DROP TABLE IF EXISTS t_discarded")
                .sync()
                .query("SELECT 1").row("1")
                .run();

            conn.createPipeline()
                .query("SELECT 1").row("1")
                .executePortal("").expectError("34000", "portal \"\" does not exist")
                .query("DROP TABLE IF EXISTS t_discarded")
                .query("DROP TABLE IF EXISTS t_discarded")
                .sync()
                .query("SELECT 2").row("2")
                .run();
        }
    }

    // A client disconnects mid-Query on a backend holding more prepared statements than
    // ysql_conn_mgr_max_prepared_statements.
    // 1. BEGIN keeps the client attached, so no detach-time reset evicts s1/s2 before the
    //    disconnect.
    // 2. The reset path, not the relay, receives the Query's YbQueryAck.
    // 3. Reset's LRU cleanup then sends a Sync of its own, and its ROLLBACK ends the open
    //    transaction so the backend goes back to the pool.
    // 4. The next client on that backend must not hang at its first Sync.
    @Test
    public void testQueryAckDuringResetWithLruCleanup() throws Exception {
        Map<String, String> tserverFlags = new HashMap<>(SINGLE_BACKEND_FLAGS);
        tserverFlags.put("ysql_conn_mgr_max_prepared_statements", "1");
        restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

        String pid;
        try (WireConn conn = rawConnBuilder().connect()) {
            pid = conn.createPipeline()
                .query("BEGIN")
                .query("SELECT pg_backend_pid()").label("pid")
                .parse("s1", "SELECT 1")
                .parse("s2", "SELECT 2")
                .sync()
                .run()
                .value("pid");
            conn.sendRaw(PgWireProtocol.buildQuery("SELECT pg_sleep(2)"));
            Thread.sleep(500);
            conn.closeAbruptly();
        }

        try (WireConn conn = rawConnBuilder().socketTimeoutMs(15000).connect()) {
            PipelineResult r = conn.createPipeline()
                .parse("SELECT 1").bind().execute().row("1")
                .sync()
                .query("SELECT pg_backend_pid()").label("pid")
                .run();
            assertEquals("Expected the single pooled backend to be reused", pid, r.value("pid"));
        }
    }
}
