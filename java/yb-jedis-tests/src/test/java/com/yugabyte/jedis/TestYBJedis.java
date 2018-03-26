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
package com.yugabyte.jedis;

import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.net.*;

import java.util.*;
import java.util.List;
import java.util.concurrent.ThreadLocalRandom;

import redis.clients.jedis.*;

import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertNotNull;
import static junit.framework.TestCase.assertNull;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.fail;

public class TestYBJedis extends BaseJedisTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYBJedis.class);

  class TSValuePairs {
    TSValuePairs(int size) {
      pairs = new HashMap<>();
      minTS = Long.MAX_VALUE;
      maxTS = Long.MIN_VALUE;

      long timestamp;
      for (int i = 0; i < size; i++) {
        do {
          timestamp = ThreadLocalRandom.current().nextLong(Long.MAX_VALUE);
        } while (pairs.containsKey(timestamp));

        String v = String.format("v%d", ThreadLocalRandom.current().nextInt());
        pairs.put(timestamp, v);

        minTS = Math.min(minTS, timestamp);
        maxTS = Math.max(maxTS, timestamp);
      }
    }

    public String MinValue() throws Exception {
      if (pairs.size() < 1) {
        throw new IndexOutOfBoundsException("Empty hash map");
      }
      return pairs.get(minTS);
    }

    public String MaxValue() throws Exception {
      if (pairs.size() < 1) {
        throw new IndexOutOfBoundsException("Empty hash map");
      }
      return pairs.get(maxTS);
    }

    public Map<Long, String> pairs;

    // Minimum timestamp stored in pairs.
    public long minTS;
    // Maximum timestamp stored in pairs.
    public long maxTS;
    private Random random;
  }

  @Test
  public void testBasicCommands() throws Exception {
    assertEquals("OK", jedis_client.set("k1", "v1"));
    assertEquals("v1", jedis_client.get("k1"));
  }

  @Test
  public void testSetCommandWithOptions() throws Exception {
    // Test with lowercase "ex" option.
    assertEquals("OK", jedis_client.set("k_ex", "v", "ex", 2));
    assertEquals("v", jedis_client.get("k_ex"));
    Thread.sleep(2000);
    assertEquals(null, jedis_client.get("k_ex"));

    // Test with uppercase "EX" option.
    assertEquals("OK", jedis_client.set("k_ex", "v", "EX", 2));
    assertEquals("v", jedis_client.get("k_ex"));
    Thread.sleep(2000);
    assertEquals(null, jedis_client.get("k_ex"));

    // Test with lowercase "px" option.
    assertEquals("OK", jedis_client.set("k_px", "v", "px", 2000));
    assertEquals("v", jedis_client.get("k_px"));
    Thread.sleep(2000);
    assertEquals(null, jedis_client.get("k_px"));

    // Test with uppercase "PX" option.
    assertEquals("OK", jedis_client.set("k_px", "v", "PX", 2000));
    assertEquals("v", jedis_client.get("k_px"));
    Thread.sleep(2000);
    assertEquals(null, jedis_client.get("k_px"));

    // Test with lowercase "nx" option.
    assertEquals("OK", jedis_client.set("k_nx", "v", "nx"));
    assertEquals("v", jedis_client.get("k_nx"));
    assertEquals(null, jedis_client.set("k_nx", "v", "nx"));
    assertEquals("v", jedis_client.get("k_nx"));

    // Test with uppercase "NX" option.
    assertEquals("OK", jedis_client.set("k_NX", "v", "NX"));
    assertEquals("v", jedis_client.get("k_NX"));
    assertEquals(null, jedis_client.set("k_NX", "v", "NX"));
    assertEquals("v", jedis_client.get("k_NX"));

    // Test with lowercase "xx" option.
    assertEquals(null, jedis_client.set("k_xx", "v", "xx"));
    assertEquals(null, jedis_client.get("k_xx"));
    assertEquals("OK", jedis_client.set("k_xx", "v"));
    assertEquals("v", jedis_client.get("k_xx"));
    assertEquals("OK", jedis_client.set("k_xx", "v2", "xx"));
    assertEquals("v2", jedis_client.get("k_xx"));

    // Test with uppercase "XX" option.
    assertEquals(null, jedis_client.set("k_XX", "v", "XX"));
    assertEquals(null, jedis_client.get("k_XX"));
    assertEquals("OK", jedis_client.set("k_XX", "v"));
    assertEquals("v", jedis_client.get("k_XX"));
    assertEquals("OK", jedis_client.set("k_XX", "v2", "XX"));
    assertEquals("v2", jedis_client.get("k_XX"));
  }

  @Test
  public void testTSAddCommand() throws Exception {
    TSValuePairs pairs = new TSValuePairs((1));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));
  }

  @Test
  public void testTSGetCommand() throws Exception {
    TSValuePairs pairs = new TSValuePairs((1));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    String value = jedis_client.tsget("k0", pairs.minTS);
    assertEquals(pairs.MinValue(), value);
  }

  @Test
  public void testTSRemCommandInvalid() throws Exception {
    // Redis table is empty, but tsrem shouldn't throw any errors.
    assertEquals("OK", jedis_client.tsrem("k0", 0));
  }

  @Test
  public void testTSRemCommandOne() throws Exception {
    TSValuePairs pairs = new TSValuePairs((1));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    String value = jedis_client.tsget("k0", pairs.minTS);
    assertEquals(pairs.MinValue(), value);

    assertEquals("OK", jedis_client.tsrem("k0", pairs.minTS));

    assertNull(jedis_client.tsget("k0", pairs.minTS));
  }

  @Test
  public void testTSRemCommandMultiple() throws Exception {
    TSValuePairs pairs = new TSValuePairs((100));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    Set<Long> keys = pairs.pairs.keySet();
    Long[] timestamps = keys.toArray(new Long[keys.size()]);
    long[] long_timestamps = new long[timestamps.length];
    int i = 0;
    for (Long timestamp : timestamps) {
      String expectedValue = pairs.pairs.get(timestamp);
      assertEquals(expectedValue, jedis_client.tsget("k0", timestamp));
      long_timestamps[i++] = timestamp;
    }

    // Remove all values.
    assertEquals("OK", jedis_client.tsrem("k0", long_timestamps));

    for (Long timestamp : timestamps) {
      assertNull(jedis_client.tsget("k0", timestamp));
    }
  }

  @Test
  public void testTSRangeByTimeLong() throws Exception {
    TSValuePairs pairs = new TSValuePairs((100));
    // Number of values to insert.
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    // Read all the values.
    List<String> values = jedis_client.tsrangeByTime("k0", pairs.minTS, pairs.maxTS);
    assertNotNull(values);
    assertEquals(pairs.pairs.size() * 2, values.size());

    Set<Long> seenTS = new HashSet<>();
    Set<Long> timestamps = new TreeSet<>(pairs.pairs.keySet());
    int i = 0;
    for (Long timestamp : timestamps) {
      // Verify that we don't have repeated timestamps.
      assertFalse(seenTS.contains(timestamp));
      seenTS.add(timestamp);

      LOG.info(String.format("i=%d, timestamp=%d, received ts=%s",
          i, timestamp, values.get(i)));

      // Verify that we are reading the expected timestamp (timestamps should be sorted).
      assertEquals(timestamp, Long.valueOf(values.get(i++)));

      String expected_value = pairs.pairs.get(timestamp);
      assertEquals(expected_value, values.get(i++));
    }
  }

  @Test
  public void testTSRangeByTimeString() throws Exception {
    TSValuePairs pairs = new TSValuePairs((100));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    // Read all the values except the first and last one.
    List<String> values = jedis_client.tsrangeByTime("k0",
        String.format("(%d", pairs.minTS), String.format("(%d", pairs.maxTS));
    assertNotNull(values);
    assertEquals(pairs.pairs.size() * 2 - 4, values.size());

    Set<Long> seenTS = new HashSet<>();
    Set<Long> timestamps = new TreeSet<>(pairs.pairs.keySet());

    // Min and max timestamps shouldn't be included in the results because we used an open interval.
    timestamps.remove(pairs.minTS);
    timestamps.remove(pairs.maxTS);
    int i = 0;
    for (Long timestamp : timestamps) {
      // Verify that we don't have repeated timestamps.
      assertFalse(seenTS.contains(timestamp));
      seenTS.add(timestamp);

      // Verify that we are reading the expected timestamp (timestamps should be sorted).
      assertEquals(timestamp, Long.valueOf(values.get(i++)));

      String expected_value = pairs.pairs.get(timestamp);
      assertEquals(expected_value, values.get(i++));
    }

    values = jedis_client.tsrangeByTime("k0", Long.toString(pairs.maxTS), "+inf");
    assertNotNull(values);
    assertEquals(2, values.size());
    assertEquals(Long.toString(pairs.maxTS), values.get(0));
    assertEquals(pairs.MaxValue(), values.get(1));

    values = jedis_client.tsrangeByTime("k0", "-inf", Long.toString(pairs.minTS));
    assertNotNull(values);
    assertEquals(2, values.size());
    assertEquals(Long.toString(pairs.minTS), values.get(0));
    assertEquals(pairs.MinValue(), values.get(1));
  }

  @Test
  public void testTSRangeByTimeInvalidString() throws Exception {
    TSValuePairs pairs = new TSValuePairs((100));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    // Pass invalid timestamps to tsrangeByTime.
    try {
      List<String> values = jedis_client.tsrangeByTime("k0", "foo", "bar");
    } catch (Exception e) {
      assertTrue(e.getMessage().contains("ERR TSRANGEBYTIME: foo is not a valid number"));
      return;
    }
    // We shouldn't reach here.
    assertFalse(true);
  }

  @Test
  public void testPool() throws Exception {
    final List<InetSocketAddress> redisContactPoints = miniCluster.getRedisContactPoints();
    InetSocketAddress address = redisContactPoints.get(0);
    JedisPoolConfig config = new JedisPoolConfig();
    config.setMaxTotal(16);
    config.setTestOnBorrow(true);
    JedisPool pool = new JedisPool(
        config, address.getHostName(), address.getPort(), 10000, "password");
    Jedis jedis = pool.getResource();
    assertEquals("OK", jedis.set("k1", "v1"));
    assertEquals("v1", jedis.get("k1"));
    jedis.close();
  }

  @Test
  public void testNX() throws Exception {
    assertEquals("OK", jedis_client.set("k1", "v1", "NX", "EX", 5));
    assertEquals("v1", jedis_client.get("k1"));
    Thread.sleep(1000);
    assertNull(jedis_client.set("k1", "v1", "NX", "EX", 5));
    Thread.sleep(9000);
    assertNull(jedis_client.get("k1"));
    assertEquals("OK", jedis_client.set("k1", "v2", "NX", "EX", 5));
    assertEquals("v2", jedis_client.get("k1"));
  }

  @Test
  public void TestTSLastN() throws Exception {
    Map<Long, String> ts = new HashMap<>();
    ts.put(-50L, "v1");
    ts.put(-40L, "v2");
    ts.put(-30L, "v3");
    ts.put(-20L, "v4");
    ts.put(-10L, "v5");
    ts.put(10L, "v6");
    ts.put(20L, "v7");
    ts.put(30L, "v8");
    ts.put(40L, "v9");
    ts.put(50L, "v10");
    assertEquals("OK", jedis_client.tsadd("ts_key", ts));
    assertEquals(
        Arrays.asList("10", "v6", "20", "v7", "30", "v8", "40", "v9", "50", "v10"),
        jedis_client.tsLastN("ts_key", 5));
    assertEquals(
        Arrays.asList("20", "v7", "30", "v8", "40", "v9", "50", "v10"),
        jedis_client.tsLastN("ts_key", 4));
    assertEquals(
        Arrays.asList("30", "v8", "40", "v9", "50", "v10"),
        jedis_client.tsLastN("ts_key", 3));
    assertEquals(
        Arrays.asList("40", "v9", "50", "v10"),
        jedis_client.tsLastN("ts_key", 2));
    assertEquals(
        Arrays.asList("-50", "v1", "-40", "v2", "-30", "v3", "-20", "v4", "-10", "v5", "10", "v6",
            "20", "v7", "30", "v8", "40", "v9", "50", "v10"),
        jedis_client.tsLastN("ts_key", 10));
    assertEquals(
        Arrays.asList("-50", "v1", "-40", "v2", "-30", "v3", "-20", "v4", "-10", "v5", "10", "v6",
            "20", "v7", "30", "v8", "40", "v9", "50", "v10"),
        jedis_client.tsLastN("ts_key", 20));
  }

  @Test
  public void TestTSCard() throws Exception {
    Map<Long, String> ts1 = new HashMap<>();
    ts1.put(-50L, "v1");
    ts1.put(-40L, "v2");
    ts1.put(-30L, "v3");
    ts1.put(-20L, "v4");
    ts1.put(-10L, "v5");
    ts1.put(10L, "v6");
    ts1.put(20L, "v7");
    ts1.put(30L, "v8");
    ts1.put(40L, "v9");
    ts1.put(50L, "v10");
    assertEquals("OK", jedis_client.tsadd("ts_key", ts1));

    Map<Long, String> ts2 = new HashMap<>();
    ts2.put(10L, "v6");
    ts2.put(20L, "v7");
    ts2.put(30L, "v8");
    ts2.put(40L, "v9");
    ts2.put(50L, "v10");
    assertEquals("OK", jedis_client.tsadd("ts_key1", ts2));

    Map<Long, String> ts3 = new HashMap<>();
    ts3.put(10L, "v6");
    assertEquals("OK", jedis_client.tsadd("ts_key2", ts3));
    ts3.clear();
    ts3.put(11L, "v7");
    assertEquals("OK", jedis_client.tsadd("ts_key2", ts3, "EXPIRE_IN", 10));

    assertEquals(10L, jedis_client.tscard("ts_key").longValue());
    assertEquals(5L, jedis_client.tscard("ts_key1").longValue());
    assertEquals(2L, jedis_client.tscard("ts_key2").longValue());
    assertEquals(0L, jedis_client.tscard("non_existent_key").longValue());

    Thread.sleep(11000);
    assertEquals(1L, jedis_client.tscard("ts_key2").longValue());
    assertEquals(1L, jedis_client.zadd("zset", 10, "v1").longValue());

    try {
      // Invalid key.
      jedis_client.tscard("zset");
      fail("Did not fail!");
    } catch (Exception e) {
      LOG.info("Expected exception", e);
    }
  }
}
