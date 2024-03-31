package org.yb.pgsql;

import java.io.PrintWriter;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.Map;
import org.junit.Assert;
import org.junit.Test;

public class TestYbRetryTime extends BasePgSQLTest {

    private void setConfigAndRestartCluster() throws Exception {
        Map<String, String> flagMap = super.getTServerFlags();
        flagMap.put("enable_wait_queues", "false");
        restartClusterWithFlags(Collections.emptyMap(), flagMap);
    }

    @Test
    public void testRetryTimeWithWaitQueueDisabled() throws Exception {
        setConfigAndRestartCluster();

        try (Connection c1 = getConnectionBuilder().connect();
                Connection c2 = getConnectionBuilder().connect();
                Statement s1 = c1.createStatement();
                Statement s2 = c2.createStatement();
                PrintWriter writer = new PrintWriter("/Users/ishanchhangani/test.txt", "UTF-8");) {

            s1.execute("CREATE TABLE test (k int, v int)");
            waitForTServerHeartbeat();
            assertQuery(s1, "SELECT * FROM test");
            s1.execute("INSERT INTO test VALUES (1, 1)");
            s1.execute("SELECT pg_stat_statements_reset()");
            s1.execute("UPDATE test SET v = 2 WHERE k = 1");
            ResultSet rs = s1
                    .executeQuery("SELECT mean_time,calls FROM pg_stat_statements WHERE query like '%UPDATE test%'");
            rs.next();
            double meanTime = rs.getDouble(1);
            int calls = rs.getInt(2);
            assert (calls == 1);
            writer.println("--------------- normal ---------------");
            writer.println("meanTime:" + meanTime);
            s1.execute("BEGIN");
            s2.execute("BEGIN");
            s1.execute("UPDATE test SET v = 3 WHERE k = 1");
            s1.execute("select pg_stat_statements_reset()");
            // s2.execute("UPDATE test SET v = 5 WHERE k = 1");
            writer.println("waiting for 5 seconds");
            new Thread(() -> {
                try {
                    s2.execute("UPDATE test SET v = 55 WHERE k = 1");
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }).start();
            Thread.sleep(5100);
            writer.println("done waiting");
            s1.execute("COMMIT");
            s2.execute("COMMIT");
            rs = s1.executeQuery(
                    "SELECT mean_time,calls FROM pg_stat_statements WHERE query like '%UPDATE test%'");
            rs.next();
            double meanTimeConflict = rs.getDouble(1);
            writer.println("--------------- kconflict ---------------");
            writer.println("meanTime:" + meanTimeConflict);
            calls = rs.getInt(2);
            assert (calls == 1);
            Assert.assertEquals("calls != 1", calls, 1);
            Assert.assertTrue("Time is 5 times of meanTime", meanTimeConflict > 5000); // as backoff_time is also added
            Assert.assertTrue("MeanTime in case of kConflict should be greater than without kconflict", meanTimeConflict > meanTime); // as backoff_time is also added and to take into concern the time taken by update query.
            s1.execute("DROP TABLE test");
        }
    }

    @Test
    public void testRetryTimeWithWaitQueueEnabled() throws Exception {
        try (Connection c1 = getConnectionBuilder().connect();
                Connection c2 = getConnectionBuilder().connect();
                Statement s1 = c1.createStatement();
                Statement s2 = c2.createStatement();
                PrintWriter writer = new PrintWriter("/Users/ishanchhangani/test.txt", "UTF-8");) {

            s1.execute("CREATE TABLE test (k int, v int)");
            waitForTServerHeartbeat();
            assertQuery(s1, "SELECT * FROM test");
            s1.execute("INSERT INTO test VALUES (1, 1)");
            s1.execute("SELECT pg_stat_statements_reset()");
            s1.execute("UPDATE test SET v = 2 WHERE k = 1");
            ResultSet rs = s1
                    .executeQuery("SELECT mean_time,calls FROM pg_stat_statements WHERE query like '%UPDATE test%'");
            rs.next();
            double meanTime = rs.getDouble(1);
            int calls = rs.getInt(2);
            assert (calls == 1);
            writer.println("--------------- normal ---------------");
            writer.println("meanTime:" + meanTime);
            s1.execute("BEGIN");
            s2.execute("BEGIN");
            s1.execute("UPDATE test SET v = 3 WHERE k = 1");
            s1.execute("select pg_stat_statements_reset()");
            writer.println("waiting for 5 seconds");
            new Thread(() -> {
                try {
                    s2.execute("UPDATE test SET v = 55 WHERE k = 1");
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }).start();
            Thread.sleep(5100);
            writer.println("done waiting");
            s1.execute("COMMIT");
            s2.execute("COMMIT");
            rs = s1.executeQuery(
                    "SELECT mean_time,calls FROM pg_stat_statements WHERE query like '%UPDATE test%'");
            rs.next();
            double meanTimeConflict = rs.getDouble(1);
            writer.println("--------------- kconflict ---------------");
            writer.println("meanTime:" + meanTimeConflict);
            calls = rs.getInt(2);
            assert (calls == 1);
            Assert.assertEquals("calls != 1", calls, 1);
            Assert.assertTrue("Time is 5 times of meanTime", meanTimeConflict > 5000); // as backoff_time is also added and to take into concern the time taken by update query.
            Assert.assertTrue("MeanTime in case of kConflict should be greater than without kconflict", meanTimeConflict > meanTime); // as backoff_time is also added and to take into concern the time taken by update query.
            s1.execute("DROP TABLE test");
        }
    }
}