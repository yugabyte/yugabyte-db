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
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;

// TODO (rbarigidad) GH #20350: Improve tests to handle more scenarios.
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestUnstickyConnections extends BaseYsqlConnMgr {
    // TODO: Revert to 2 connections after bug fix DB-7395 lands.
    // TODO: Change it to appropriate value once the temporary change #18723 is reverted.

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        //TODO(mkumar) GH##23761 This test is failing in tsan build with
        // warmup random mode in ysql connection manager.
        disableWarmupRandomMode(builder);
        Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
            {
                put("ysql_conn_mgr_max_conns_per_db",
                Integer.toString(isTestRunningInWarmupRandomMode() ? 3 : 1));
            }
        };

        builder.addCommonTServerFlags(additionalTserverFlags);
    }

    @Test
    public void testUnstickyConnections() throws Exception {
        Connection conn = null;
        Connection conn2 = null;
        Connection conn3 = null;
        Statement stmt = null;
        Statement stmt2 = null;
        Statement stmt3 = null;
        TransactionRunnable runnable = new TransactionRunnable();
        try {
            conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withAutoCommit(AutoCommit.DISABLED)
                    .connect();
            stmt = conn.createStatement();

            // Introduce a temp table to create stickiness,
            // the second connection coming in should wait in a queue.
            stmt.executeUpdate("CREATE TEMP TABLE t(ID INT)");
            conn.commit();

            if (isTestRunningInWarmupRandomMode())
            {
                // In random warmup mode, 3 physical connections are created, so need to create
                // 3 sticky connections in order to make all backend process attached/occupied,
                // so that any additional connection attempt has to wait which is purpose of
                // the test.
                conn2 = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withAutoCommit(AutoCommit.DISABLED)
                    .connect();
                stmt2 = conn2.createStatement();

                // Introduce a temp table to create stickiness
                stmt2.executeUpdate("CREATE TEMP TABLE t(ID INT)");
                conn2.commit();

                conn3 = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withAutoCommit(AutoCommit.DISABLED)
                    .connect();
                stmt3 = conn3.createStatement();

                // Introduce a temp table to create stickiness
                stmt3.executeUpdate("CREATE TEMP TABLE t(ID INT)");
                conn3.commit();
            }

            Thread queueThread = new Thread(runnable);
            queueThread.start();

            // Allow queueThread to queue a connection request and wait.
            queueThread.join(500);
            assertTrue("queue thread should not have finished execution",
                    queueThread.isAlive());

            // Drop stickiness of connection by dropping the sticky object.
            stmt.executeUpdate("DROP TABLE t");
            conn.commit();

            // Allow the queued thread to connect and close its connection.
            queueThread.join(500);
            assertFalse("expected queue thread to complete execution",
                    queueThread.isAlive());
            conn.close();
            if (isTestRunningInWarmupRandomMode())
            {
                conn2.close();
                conn3.close();
            }
        } catch (Exception e) {
            LOG.error("Got an unexpected error while creating a connection: ", e);
            fail("connection faced an unexpected issue");
        }
    }

    @Test
    public void testRolledBackTransactions() throws Exception {
        TransactionRunnable runnable = new TransactionRunnable();
        try {
            Connection conn = getConnectionBuilder()
                    .withAutoCommit(AutoCommit.DISABLED)
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
            Statement stmt = conn.createStatement();
            Connection conn2 = null;
            Connection conn3 = null;
            Statement stmt2 = null;
            Statement stmt3 = null;


            // Increment count of sticky objects but do not commit the transaction.
            stmt.executeUpdate("CREATE TEMP TABLE t(ID INT)");

            if (isTestRunningInWarmupRandomMode())
            {
                // In random warmup mode, 3 physical connections are created, so need to create
                // 3 sticky connections in order to make all backend process attached/occupied,
                // so that any additional connection attempt has to wait which is purpose of
                // the test.
                conn2 = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withAutoCommit(AutoCommit.DISABLED)
                    .connect();
                stmt2 = conn2.createStatement();

                // Introduce a temp table to create stickiness,
                // the second connection coming in should wait in a queue.
                stmt2.executeUpdate("CREATE TEMP TABLE t(ID INT)");
                conn2.commit();

                conn3 = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withAutoCommit(AutoCommit.DISABLED)
                    .connect();
                stmt3 = conn3.createStatement();

                // Introduce a temp table to create stickiness,
                // the second connection coming in should wait in a queue.
                stmt3.executeUpdate("CREATE TEMP TABLE t(ID INT)");
                conn3.commit();
            }

            Thread queueThread = new Thread(runnable);
            queueThread.start();

            // Allow queueThread to queue a connection request,
            // it should wait as a transaction is in progress.
            queueThread.join(500);
            assertTrue("queue thread should not have finished execution",
                    queueThread.isAlive());

            // Rollback the transaction and confirm that the queued
            // connection was able to connect to the backend process.
            conn.rollback();
            queueThread.join(500);
            assertFalse("expected queue thread to complete execution",
                    queueThread.isAlive());
            conn.close();
            if (isTestRunningInWarmupRandomMode())
            {
                conn2.close();
                conn3.close();
            }

        } catch (Exception e) {
            LOG.error("Got an unexpected error while creating a connection: ", e);
            fail("connection faced an unexpected issue");
        }
    }

    private class TransactionRunnable implements Runnable {
        public void run() {
            try {
                getConnectionBuilder()
                        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                        .connect()
                        .close();
            } catch (Exception e) {
                LOG.error("Got an unexpected error while creating a connection: ", e);
                fail("connection faced an unexpected issue");
            }
        }
    }
}
