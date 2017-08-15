// Copyright (c) YugaByte, Inc.
package com.yugabyte.cql;

import org.junit.Test;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.utils.UUIDs;

import com.yugabyte.cql.PartitionAwarePolicy;

import org.yb.cql.BaseCQLTest;
import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;

import org.apache.commons.lang3.RandomStringUtils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertFalse;

import java.nio.ByteBuffer;
import java.net.InetAddress;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import java.util.UUID;

public class TestLoadBalancing extends BaseCQLTest {

  // Test Jenkins 64-bit hash function to verify it produces identical results as the C++
  // counterpart does.
  @Test
  public void testJenkins64() throws Exception {
    final long seed = 97;
    // 17, 40 and 256-byte sequences generated randomly.
    final byte b1[] = {
      (byte)0xc7, (byte)0x25, (byte)0x1d, (byte)0x5d,
      (byte)0x75, (byte)0x3a, (byte)0x4e, (byte)0x46,
      (byte)0x22, (byte)0x29, (byte)0x4d, (byte)0x6c,
      (byte)0x67, (byte)0x7a, (byte)0xa8, (byte)0x25,
      (byte)0x71};
    final byte b2[] = {
      (byte)0x83, (byte)0x8e, (byte)0x7e, (byte)0xf0,
      (byte)0x71, (byte)0xef, (byte)0x9b, (byte)0x3e,
      (byte)0x4a, (byte)0xe6, (byte)0x12, (byte)0x60,
      (byte)0xc0, (byte)0xa1, (byte)0xf9, (byte)0x94,
      (byte)0x5a, (byte)0x85, (byte)0x9b, (byte)0xb1,
      (byte)0xf6, (byte)0x86, (byte)0x97, (byte)0xe1,
      (byte)0xab, (byte)0x87, (byte)0xc8, (byte)0xab,
      (byte)0xc1, (byte)0x28, (byte)0xd1, (byte)0x72,
      (byte)0x73, (byte)0x0b, (byte)0xda, (byte)0x50,
      (byte)0xe3, (byte)0xe6, (byte)0xf9, (byte)0x42};
    final byte b3[] = {
      (byte)0xad, (byte)0xe3, (byte)0xaa, (byte)0xb7,
      (byte)0xd2, (byte)0xbc, (byte)0x3a, (byte)0xe6,
      (byte)0x60, (byte)0xe4, (byte)0xc6, (byte)0xc1,
      (byte)0x02, (byte)0x0a, (byte)0x3a, (byte)0x50,
      (byte)0x66, (byte)0xb2, (byte)0x26, (byte)0x6c,
      (byte)0x1d, (byte)0x1b, (byte)0x16, (byte)0xb1,
      (byte)0x1b, (byte)0x51, (byte)0x74, (byte)0x9c,
      (byte)0xa7, (byte)0xbb, (byte)0xad, (byte)0x46,
      (byte)0x25, (byte)0x54, (byte)0xca, (byte)0x30,
      (byte)0x3a, (byte)0x31, (byte)0xd0, (byte)0x34,
      (byte)0x56, (byte)0xac, (byte)0xb1, (byte)0xca,
      (byte)0xaf, (byte)0x7f, (byte)0x5c, (byte)0xf3,
      (byte)0x9e, (byte)0x16, (byte)0x94, (byte)0x78,
      (byte)0x84, (byte)0xca, (byte)0x60, (byte)0x66,
      (byte)0x27, (byte)0x59, (byte)0xe1, (byte)0x99,
      (byte)0xb4, (byte)0xc4, (byte)0xbd, (byte)0x50,
      (byte)0x48, (byte)0x50, (byte)0xcb, (byte)0xa6,
      (byte)0x0b, (byte)0xe1, (byte)0x71, (byte)0x31,
      (byte)0x49, (byte)0x27, (byte)0x11, (byte)0x9e,
      (byte)0xcc, (byte)0xcd, (byte)0xd8, (byte)0x19,
      (byte)0x09, (byte)0xc6, (byte)0xdf, (byte)0x15,
      (byte)0x64, (byte)0x0d, (byte)0xf7, (byte)0x25,
      (byte)0x5c, (byte)0x48, (byte)0x19, (byte)0xc7,
      (byte)0x6b, (byte)0x10, (byte)0x02, (byte)0x7e,
      (byte)0x31, (byte)0x54, (byte)0x2a, (byte)0xd8,
      (byte)0x92, (byte)0xe5, (byte)0xc5, (byte)0xab,
      (byte)0xe9, (byte)0x3d, (byte)0x57, (byte)0x99,
      (byte)0x9a, (byte)0x93, (byte)0x4f, (byte)0x48,
      (byte)0x3f, (byte)0xfa, (byte)0x73, (byte)0x36,
      (byte)0x03, (byte)0xe1, (byte)0xbd, (byte)0x27,
      (byte)0xe5, (byte)0x06, (byte)0x8a, (byte)0x21,
      (byte)0x33, (byte)0xff, (byte)0x91, (byte)0x80,
      (byte)0x36, (byte)0x4d, (byte)0x2d, (byte)0x04,
      (byte)0xc7, (byte)0x11, (byte)0xcc, (byte)0x2a,
      (byte)0xc0, (byte)0xa9, (byte)0x17, (byte)0x18,
      (byte)0x73, (byte)0xff, (byte)0xd5, (byte)0x0e,
      (byte)0x0d, (byte)0x8b, (byte)0x6f, (byte)0x8b,
      (byte)0xba, (byte)0x8c, (byte)0x37, (byte)0x49,
      (byte)0xb1, (byte)0x31, (byte)0x5b, (byte)0xf4,
      (byte)0x4d, (byte)0xd7, (byte)0x19, (byte)0x10,
      (byte)0x40, (byte)0x6e, (byte)0x61, (byte)0x41,
      (byte)0xf1, (byte)0x55, (byte)0xaa, (byte)0x44,
      (byte)0x79, (byte)0x13, (byte)0x57, (byte)0x3b,
      (byte)0x72, (byte)0xac, (byte)0xfe, (byte)0xce,
      (byte)0xf8, (byte)0xd7, (byte)0x07, (byte)0x82,
      (byte)0x05, (byte)0xef, (byte)0x0f, (byte)0x53,
      (byte)0x6c, (byte)0xfe, (byte)0x7d, (byte)0x94,
      (byte)0x48, (byte)0xa5, (byte)0x48, (byte)0x42,
      (byte)0x47, (byte)0x70, (byte)0x29, (byte)0xe7,
      (byte)0x7e, (byte)0x53, (byte)0xca, (byte)0x88,
      (byte)0x89, (byte)0x8a, (byte)0xec, (byte)0xe5,
      (byte)0x01, (byte)0x44, (byte)0xf5, (byte)0xc5,
      (byte)0xc9, (byte)0x89, (byte)0x6d, (byte)0x6a,
      (byte)0xf1, (byte)0x26, (byte)0x61, (byte)0xae,
      (byte)0x30, (byte)0x50, (byte)0x61, (byte)0x68,
      (byte)0x41, (byte)0xac, (byte)0x82, (byte)0x40,
      (byte)0xdb, (byte)0x12, (byte)0x00, (byte)0x68,
      (byte)0xad, (byte)0x34, (byte)0x52, (byte)0xb2,
      (byte)0xbb, (byte)0xc5, (byte)0x74, (byte)0xf1,
      (byte)0x3e, (byte)0x00, (byte)0x98, (byte)0x6e,
      (byte)0x1d, (byte)0xc2, (byte)0xd7, (byte)0x7d,
      (byte)0xc6, (byte)0xc7, (byte)0x10, (byte)0xb2,
      (byte)0xac, (byte)0xcf, (byte)0x8b, (byte)0x25,
      (byte)0xd9, (byte)0x7d, (byte)0xd5, (byte)0x20};

    assertEquals(Long.parseUnsignedLong("1789751740810280356"), Jenkins.hash64(b1, seed));
    assertEquals(Long.parseUnsignedLong("4001818822847464429"), Jenkins.hash64(b2, seed));
    assertEquals(Long.parseUnsignedLong("15240025333683105143"), Jenkins.hash64(b3, seed));
  }

  // Test hash-key function in PartitionAwarePolicy to verify it is consistent with the hash
  // function used in YB (the token() function).
  @Test
  public void testHashFunction() throws Exception {

    Random rand = new Random();

    // Test hash key composed of strings and blob.
    {
      session.execute("create table t1 (h1 text, h2 text, h3 blob, h4 text, " +
                      "primary key ((h1, h2, h3, h4)));");

      // Insert a row with random hash column values.
      String h1 = RandomStringUtils.random(rand.nextInt(256));
      String h2 = RandomStringUtils.random(rand.nextInt(256));
      byte bytes[] = new byte[rand.nextInt(256)];
      for (int i = 0; i < bytes.length; i++) {
        bytes[i] = (byte)(rand.nextInt() & 0xff);
      }
      ByteBuffer h3 = ByteBuffer.wrap(bytes);
      String h4 = RandomStringUtils.random(rand.nextInt(256));
      LOG.info("h1 = \"" + h1 + "\", " +
               "h2 = \"" + h2 + "\", " +
               "h3 = \"" + makeBlobString(h3) + "\", " +
               "h4 = \"" + h4 + "\"");
      BoundStatement stmt = session.prepare("insert into t1 (h1, h2, h3, h4) values (?, ?, ?, ?);")
                            .bind(h1, h2, h3, h4);
      session.execute(stmt);

      // Select the row back using the hash key value and verify the row.
      Row row = session.execute("select * from t1 where token(h1, h2, h3, h4) = ?;",
              PartitionAwarePolicy.YBToCqlHashCode(PartitionAwarePolicy.getKey(stmt))).one();
      assertNotNull(row);
      assertEquals(h1, row.getString("h1"));
      assertEquals(h2, row.getString("h2"));
      assertEquals(h3, row.getBytes("h3"));
      assertEquals(h4, row.getString("h4"));

      session.execute("drop table t1;");
    }

    // Test hash key composed of integers and string.
    {
      session.execute("create table t2 (h1 tinyint, h2 smallint, h3 text, h4 int, h5 bigint, " +
                      "primary key ((h1, h2, h3, h4, h5)));");

      // Insert a row with random hash column values.
      byte h1 = (byte)(rand.nextInt() & 0xff);
      short h2 = (short)(rand.nextInt() & 0xffff);
      String h3 = RandomStringUtils.random(rand.nextInt(256));
      int h4 = rand.nextInt();
      long h5 = rand.nextLong();
      LOG.info("h1 = " + h1 + ", " +
               "h2 = " + h2 + ", " +
               "h3 = \"" + h3 + "\", " +
               "h4 = " + h4 + ", " +
               "h5 = " + h5);
      BoundStatement stmt = session.prepare("insert into t2 (h1, h2, h3, h4, h5) " +
                                            "values (?, ?, ?, ?, ?);")
                            .bind(Byte.valueOf(h1),
                                  Short.valueOf(h2),
                                  h3,
                                  Integer.valueOf(h4),
                                  Long.valueOf(h5));
      session.execute(stmt);

      // Select the row back using the hash key value and verify the row.
      Row row = session.execute("select * from t2 where token(h1, h2, h3, h4, h5) = ?;",
              PartitionAwarePolicy.YBToCqlHashCode(PartitionAwarePolicy.getKey(stmt))).one();
      assertNotNull(row);
      assertEquals(h1, row.getByte("h1"));
      assertEquals(h2, row.getShort("h2"));
      assertEquals(h3, row.getString("h3"));
      assertEquals(h4, row.getInt("h4"));
      assertEquals(h5, row.getLong("h5"));

      session.execute("drop table t2;");
    }

    // Test hash key composed of integer, timestamp, inet, uuid and timeuuid.
    {
      session.execute("create table t3 (h1 int, h2 timestamp, h3 inet, h4 uuid, h5 timeuuid, " +
                      "primary key ((h1, h2, h3, h4, h5)));");

      // Insert a row with random hash column values.
      int h1 = rand.nextInt();
      Date h2 = new Date(rand.nextInt(Integer.MAX_VALUE));
      byte addr[] = new byte[4];
      addr[0] = (byte)(rand.nextInt() & 0xff);
      addr[1] = (byte)(rand.nextInt() & 0xff);
      addr[2] = (byte)(rand.nextInt() & 0xff);
      addr[3] = (byte)(rand.nextInt() & 0xff);
      InetAddress h3 = InetAddress.getByAddress(addr);
      UUID h4 = UUID.randomUUID();
      UUID h5 = UUIDs.timeBased();
      LOG.info("h1 = " + h1 + ", " +
               "h2 = " + h2 + ", " +
               "h3 = " + h3 + ", " +
               "h4 = " + h4 + ", " +
               "h5 = " + h5);
      BoundStatement stmt = session.prepare("insert into t3 (h1, h2, h3, h4, h5) " +
                                            "values (?, ?, ?, ?, ?);")
                            .bind(Integer.valueOf(h1), h2, h3, h4, h5);
      session.execute(stmt);

      // Select the row back using the hash key value and verify the row.
      Row row = session.execute("select * from t3 where token(h1, h2, h3, h4, h5) = ?;",
              PartitionAwarePolicy.YBToCqlHashCode(PartitionAwarePolicy.getKey(stmt))).one();
      assertNotNull(row);
      assertEquals(h1, row.getInt("h1"));
      assertEquals(h2, row.getTimestamp("h2"));
      assertEquals(h3, row.getInet("h3"));
      assertEquals(h4, row.getUUID("h4"));
      assertEquals(h5, row.getUUID("h5"));

      session.execute("drop table t3;");
    }
  }

  // Test PartitionMetadata
  @Test
  public void testPartitionMetadata() throws Exception {

    // Create a PartitionMetadata with no refresh cycle.
    PartitionMetadata metadata = new PartitionMetadata(cluster, 0);
    final int MAX_WAIT_SECONDS = 10;

    // Create test table. Verify that the PartitionMetadata gets notified of the table creation
    // and loads the metadata.
    session.execute("create table test_partition1 (k int primary key);");
    TableMetadata table = cluster
                          .getMetadata()
                          .getKeyspace(BaseCQLTest.DEFAULT_TEST_KEYSPACE)
                          .getTable("test_partition1");
    boolean found = false;
    for (int i = 0; i < MAX_WAIT_SECONDS; i++) {
      if (metadata.getHostsForKey(table, 0).size() > 0) {
        found = true;
        break;
      }
      Thread.sleep(1000);
    }
    assertTrue(found);

    // Drop test table. Verify that the PartitionMetadata gets notified of the table drop
    // and clears the the metadata.
    session.execute("Drop table test_partition1;");
    for (int i = 0; i < MAX_WAIT_SECONDS; i++) {
      if (metadata.getHostsForKey(table, 0).size() == 0) {
        found = false;
        break;
      }
      Thread.sleep(1000);
    }
    assertFalse(found);

    // Create a PartitionMetadata with 1-second refresh cycle. Verify the metadata is refreshed
    // periodically even without table creation/drop.
    metadata = new PartitionMetadata(cluster, 1);
    final int MIN_LOAD_COUNT = 5;
    for (int i = 0; i < MAX_WAIT_SECONDS; i++) {
      if (metadata.loadCount.get() >= MIN_LOAD_COUNT)
        break;
      Thread.sleep(1000);
    }
    LOG.info("PartitionMetadata load count = " + metadata.loadCount.get());
    assertTrue(metadata.loadCount.get() >= MIN_LOAD_COUNT);
  }

  // Get IO metrics of all tservers.
  private Map<MiniYBDaemon, IOMetrics> getTSMetrics() throws Exception {
    Map<MiniYBDaemon, IOMetrics> initialMetrics = new HashMap<>();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = new IOMetrics(new Metrics(ts.getLocalhostIP(),
                                                    ts.getCqlWebPort(),
                                                    "server"));
      initialMetrics.put(ts, metrics);
    }
    return initialMetrics;
  }

  // Get combined IO metrics of all tservers since a certain point.
  private IOMetrics getCombinedMetrics(Map<MiniYBDaemon, IOMetrics> initialMetrics)
      throws Exception {
    IOMetrics totalMetrics = new IOMetrics();
    int tsCount = miniCluster.getTabletServers().values().size();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = new IOMetrics(new Metrics(ts.getLocalhostIP(),
                                                    ts.getCqlWebPort(),
                                                    "server"))
                          .subtract(initialMetrics.get(ts));
      LOG.info("Metrics of " + ts.toString() + ": " + metrics.toString());
      totalMetrics.add(metrics);
    }
    LOG.info("Total metrics: " + totalMetrics.toString());
    return totalMetrics;
  }

  // Test load-balancing policy with DMLs.
  @Test
  public void testDML() throws Exception {

    final int NUM_KEYS = 100;

    // Create test table.
    session.execute("create table test_lb (h1 int, h2 text, c int, primary key ((h1, h2)));");

    // Since partition metadata is refreshed asynchronously after a new table is created, let's
    // wait for a little or else the initial statements will be executed without the partition
    // metadata and will be dispatched to a random node.
    Thread.sleep(10000);

    // Get the initial metrics.
    Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();

    PreparedStatement stmt;

    stmt = session.prepare("insert into test_lb (h1, h2, c) values (?, ?, ?);");
    for (int i = 1; i <= NUM_KEYS; i++) {
      session.execute(stmt.bind(Integer.valueOf(i), "v" + i, Integer.valueOf(i)));
    }

    stmt = session.prepare("update test_lb set c = ? where h1 = ? and h2 = ?;");
    for (int i = 1; i <= NUM_KEYS; i++) {
      session.execute(stmt.bind(Integer.valueOf(i * 2), Integer.valueOf(i), "v" + i));
    }

    stmt = session.prepare("select c from test_lb where h1 = ? and h2 = ?;");
    for (int i = 1; i <= NUM_KEYS; i++) {
      Row row = session.execute(stmt.bind(Integer.valueOf(i), "v" + i)).one();
      assertNotNull(row);
      assertEquals(i * 2, row.getInt("c"));
    }

    stmt = session.prepare("delete from test_lb where h1 = ? and h2 = ?;");
    for (int i = 1; i <= NUM_KEYS; i++) {
      session.execute(stmt.bind(Integer.valueOf(i), "v" + i));
    }

    // Check the metrics again.
    IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);

    // Verify that the majority of read and write calls are local.
    //
    // With PartitionAwarePolicy, all calls should be local ideally but there is no 100% guarantee
    // because as soon as the test table has been created and the partition metadata has been
    // loaded, the cluster's load-balancer may still be rebalancing the leaders.
    assertTrue(totalMetrics.localReadCount >= NUM_KEYS * 0.7);
    assertTrue(totalMetrics.localWriteCount >= NUM_KEYS * 3 * 0.7);
  }

  // Test load-balancing policy with BatchStatement
  @Test
  public void testBatchStatement() throws Exception {

    final int NUM_KEYS = 100;

    // Create test table.
    session.execute("create table test_lb (h int, r text, c int, primary key ((h), r));");

    // Since partition metadata is refreshed asynchronously after a new table is created, let's
    // wait for a little or else the initial statements will be executed without the partition
    // metadata and will be dispatched to a random node.
    Thread.sleep(10000);

    // Get the initial metrics.
    Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();

    PreparedStatement stmt;
    stmt = session.prepare("insert into test_lb (h, r, c) values (?, ?, ?);");
    for (int i = 1; i <= NUM_KEYS; i++) {
      BatchStatement batch = new BatchStatement();
      for (int j = 1; j <= 5; j++) {
        batch.add(stmt.bind(Integer.valueOf(i), "v" + j, Integer.valueOf(i)));
      }
      session.execute(batch);
    }

    stmt = session.prepare("select c from test_lb where h = ? and r = 'v1';");
    for (int i = 1; i <= NUM_KEYS; i++) {
      Row row = session.execute(stmt.bind(Integer.valueOf(i))).one();
      assertNotNull(row);
      assertEquals(i, row.getInt("c"));
    }

    // Check the metrics again.
    IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);

    // Verify that the majority of read and write calls are local.
    //
    // With PartitionAwarePolicy, all calls should be local ideally but there is no 100% guarantee
    // because as soon as the test table has been created and the partition metadata has been
    // loaded, the cluster's load-balancer may still be rebalancing the leaders.
    assertTrue(totalMetrics.localReadCount >= NUM_KEYS * 0.7);
    assertTrue(totalMetrics.localWriteCount >= NUM_KEYS * 5 * 0.7);
  }
}
