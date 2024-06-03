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

import java.sql.*;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;
import static org.yb.AssertionWrappers.*;

// TODO (rbarigidad) GH #20350: Improve tests to handle more scenarios.
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestStickyConnections extends BaseYsqlConnMgr {
    private final int NUM_THREADS = 10;

    final String CREATE_TEMP_TABLE_SQL = "CREATE TEMP TABLE TEMP_TABLE%d (ID INT, name TEXT)";
    final String INSERT_SINGLE_ROW_SQL = "INSERT INTO TEMP_TABLE%d VALUES(%d, 'test_%d')";
    final String SELECT_ROW_COUNT_SQL = "SELECT COUNT(*) as row_count FROM TEMP_TABLE%d";

    final String CREATE_CURSOR_SQL = "DECLARE TEST_CURSOR%d CURSOR WITH HOLD FOR %s";
    final String SELECT_CURSOR_SQL = "SELECT * FROM TEMP_TABLE%d";
    final String FETCH_CURSOR_SQL = "FETCH TEST_CURSOR%d";

    @Test
    public void testStickyConn() throws Exception {
        TransactionRunnable[] runnables = new TransactionRunnable[NUM_THREADS];
        Thread[] threads = new Thread[NUM_THREADS];
        for (int i = 0; i < NUM_THREADS; i++) {
            runnables[i] = new TransactionRunnable(i);
            threads[i] = new Thread(runnables[i]);
        }

        // Start the threads and wait for each of them to execute.
        for (Thread thread : threads) {
            thread.start();
            thread.join();
        }

        // Close each connection that was created.
        for (TransactionRunnable runnable : runnables) {
            runnable.closeConnection();
        }
    }

    private class TransactionRunnable implements Runnable {
        private final int table_id;
        private Connection conn;

        public TransactionRunnable(int table_id) {
            this.table_id = table_id;
        }

        @Override
        public void run() {
            try {
                conn = getConnectionBuilder()
                        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                        .withAutoCommit(AutoCommit.DISABLED)
                        .connect();
                Statement stmt = conn.createStatement();

                // Test TEMP TABLES.
                stmt.executeUpdate(String.format(CREATE_TEMP_TABLE_SQL, table_id));

                // Create additional interleaving between threads, this would cause failures in
                // upstream odyssey.
                for (int j = 0; j < 10; j++) {
                    stmt.executeUpdate(String.format(INSERT_SINGLE_ROW_SQL, table_id, j, j));
                    conn.commit();
                    ResultSet rs = stmt.executeQuery(String.format(SELECT_ROW_COUNT_SQL, table_id));
                    assertTrue(rs.next());
                    assertEquals(rs.getInt("row_count"), j + 1);
                }

                // Test WITH HOLD CURSORS.
                stmt.executeUpdate(String.format(CREATE_CURSOR_SQL, table_id,
                        String.format(SELECT_CURSOR_SQL, table_id)));
                conn.commit();

                // No need to parallelly fetch each single row, upstream odyssey would have
                // failed while declaring the cursors.
                ResultSet rs = stmt.executeQuery(String.format(FETCH_CURSOR_SQL, table_id));
                assertTrue(rs.next());
                assertEquals(rs.getInt("ID"), 0);
                assertFalse("Only one row was expected in the result set", rs.next());

            } catch (Exception e) {
                LOG.error("Transaction failed: ", e);
                fail();
            }
        }

        public void closeConnection() {
            try {
                if (conn != null && !conn.isClosed()) {
                    conn.close();
                }
            } catch (SQLException e) {
                // Handle the exception
                LOG.error("Got an unexpected error while closing the connections: ", e);
                fail();
            }
        }

    }
}
