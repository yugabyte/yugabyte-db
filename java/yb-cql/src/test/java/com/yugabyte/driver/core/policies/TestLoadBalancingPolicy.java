// Copyright (c) YugaByte, Inc.
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
//
package com.yugabyte.driver.core.policies;

import org.junit.Test;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.utils.UUIDs;

import org.yb.cql.BaseCQLTest;
import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.MiniYBDaemon;

import org.apache.commons.lang3.RandomStringUtils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import java.nio.ByteBuffer;
import java.net.InetAddress;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import java.util.UUID;

public class TestLoadBalancingPolicy extends BaseCQLTest {

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
    assertTrue(totalMetrics.localWriteCount >= NUM_KEYS * 0.7);
  }
}
