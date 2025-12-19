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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import com.google.gson.JsonObject;
import com.yugabyte.PGConnection;
import com.yugabyte.replication.PGReplicationConnection;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestReplicationConn extends BaseYsqlConnMgr {

    private static final int YSQ_MAX_CONNECTIONS = 64;
    private static final int MAX_REPLICATION_SLOTS = 50;
    private static final int MAX_WAL_SENDERS = 50;
    private static int walsender_created_count = 0;

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
            {
                put("ysql_max_connections", String.valueOf(YSQ_MAX_CONNECTIONS));
                put("ysql_pg_conf_csv","max_wal_senders=" + String.valueOf(MAX_WAL_SENDERS));
                put("ysql_conn_mgr_stats_interval", Integer.toString(STATS_UPDATE_INTERVAL));
            }
        };
        builder.addCommonTServerFlags(additionalTserverFlags);
        Map<String, String> additionalMasterFlags = new HashMap<String, String>() {
            {
                put("max_replication_slots", String.valueOf(MAX_REPLICATION_SLOTS));
            }
        };
        builder.addMasterFlags(additionalMasterFlags);
    }

    private void createRole() throws Exception {
        try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withPassword("yugabyte")
                    .connect();){
            Statement stmt = conn.createStatement();
            stmt.execute("CREATE ROLE replicator WITH LOGIN REPLICATION"
            +  " PASSWORD 'your_secure_password';");
        } catch (Exception e) {
            LOG.error("Got an unexpected error while creating a database and role: ", e);
            fail("database and role creation failed");
        }
    }

    @Test
    public void testReplicationConn() throws Exception {
        createRole();
    // Create a list to store replication connection threads
        Connection[] replicationConnections = new Connection[MAX_WAL_SENDERS];

        for (int i = 0; i < MAX_WAL_SENDERS; i++) {
            try {
                replicationConnections[i] = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("replicator")
                    .withPassword("your_secure_password")
                    .replicationConnect();

                PGReplicationConnection replConnection =
                replicationConnections[i].unwrap(PGConnection.class).getReplicationAPI();

                try {
                    replConnection.createReplicationSlot()
                        .logical()
                        .withSlotName("test_slot_" + i)
                        .withOutputPlugin("pgoutput")
                        .make();
                    walsender_created_count++;
                    LOG.info("Replication connection " + i + " created slot: " + "test_slot_" + i);
                } catch (Exception e) {
                    if (i == MAX_WAL_SENDERS - 1) {
                        // #GH: 28962 Race condition while freeing the replication connections.
                        // But test very well checks ysql connections are independent of replication
                        // connections. If except for 50th replication slot rest are created,
                        // when ysql max connection limit is 14 only, test should get passed.
                        LOG.info("The last replication connection is expected to fail" +
                            "sometimes.", e);
                    }
                    else {
                        LOG.error("Replication connection " + i + " failed to create slot: ", e);
                        fail("Replication connection " + i + " failed to create slot");
                    }
                }
            } catch (Exception e) {
                LOG.error("Replication connection " + i + " failed: ", e);
                fail("Replication connection " + i + " failed");
            }
        }

        Thread.sleep((2 + STATS_UPDATE_INTERVAL) * 1000);

        JsonObject pool_stats = getPool("yugabyte", "replicator");
        assertNotNull(pool_stats);
        assertEquals(walsender_created_count,
            pool_stats.get("active_logical_connections").getAsInt());
        assertEquals(walsender_created_count,
            pool_stats.get("active_physical_connections").getAsInt());
        assertTrue(pool_stats.get("logical_rep").getAsBoolean());
        assertEquals(walsender_created_count,
            pool_stats.get("sticky_connections").getAsInt());
        // In PG11, max_wal_senders + superuser_reserved_connections must be less than.
        // max_connections. This test verifies replication connections are created by
        // conn mgr and stats are updated in correct way.

        for (int i = 0; i < MAX_WAL_SENDERS; i++) {
            if (replicationConnections[i] != null) {
                replicationConnections[i].close();
            }
        }
    }
}
