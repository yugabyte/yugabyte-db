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
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.net.InetSocketAddress;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

import org.yb.YBParameterizedTestRunner;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisPool;
import redis.clients.jedis.JedisPoolConfig;
import redis.clients.jedis.Tuple;

import static com.yugabyte.jedis.BaseJedisTest.JedisClientType.JEDISCLUSTER;
import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertNotNull;
import static junit.framework.TestCase.assertNull;
import static junit.framework.TestCase.assertTrue;
import static org.yb.AssertionWrappers.fail;

@RunWith(YBParameterizedTestRunner.class)
public class TestYBJedis extends BaseJedisTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYBJedis.class);

  public TestYBJedis(JedisClientType jedisClientType) {
    super(jedisClientType);
  }

  // Run each test with Jedis, JedisCluster, and YBJedis clients.
  @Parameterized.Parameters
  public static List<JedisClientType> jedisClients() {
    return Arrays.asList(JedisClientType.JEDIS, JEDISCLUSTER,
        JedisClientType.YBJEDIS);
  }

  @Test
  public void testBasicCommands() throws Exception {
    assertEquals("OK", jedis_client.set("k1", "v1"));
    assertEquals("v1", jedis_client.get("k1"));
  }

  @Test
  public void testKeysWithDelete() throws Exception {
    assertEquals("OK", jedis_client.set("k1", "v1"));
    assertEquals("OK", jedis_client.set("k2", "v2"));

    Map<Long, String> ts = new HashMap<>();
    ts.put(10L, "v1");
    ts.put(20L, "v2");
    ts.put(30L, "v3");
    ts.put(40L, "v4");
    ts.put(50L, "v5");
    assertEquals("OK", jedis_client.tsadd("k3", ts));

    Map<String, Double> pairs = new HashMap<String, Double>();
    pairs.put("v0", 0.0);
    pairs.put("v1", 1.0);
    assertEquals(2L, jedis_client.zadd("k4", pairs).longValue());

    Set<String> keys = new HashSet<String>(Arrays.asList("k1", "k2", "k3", "k4"));

    // Test all 4 keys in the db.
    assertEquals(keys, jedis_client.keys("*"));

    // Make sure expired and deleted entries don't show up.
    assertEquals(1L, jedis_client.del("k2").longValue());
    assertEquals(1L, jedis_client.expire("k4", 2).longValue());
    keys.remove("k2");
    keys.remove("k4");

    // Make sure ts with only one entry expired still shows up.
    Map<Long, String> ts_exp = new HashMap<>();
    ts_exp.put(0L, "v0");
    assertEquals("OK", jedis_client.tsadd("k3", ts_exp, "EXPIRE_IN", 2));
    Thread.sleep(2000);

    // Test k1 and k3 still in db.
    assertEquals(keys, jedis_client.keys("*"));

    //Now add back keys and make sure they show up.
    assertEquals("OK", jedis_client.set("k2", "v2"));
    assertEquals("OK", jedis_client.set("k4", "v4"));
    keys.add("k2");
    keys.add("k4");

    assertEquals(keys, jedis_client.keys("*"));
  }

  @Test
  public void testZScore() throws Exception {
    Map<String, Double> pairs = new HashMap<String, Double>();
    pairs.put("v0", 0.0);
    pairs.put("v_neg", -5.0);
    pairs.put("v1", 1.0);
    pairs.put("v2", 2.5);
    assertEquals(4L, jedis_client.zadd("z_key", pairs).longValue());

    assertEquals(0.0, jedis_client.zscore("z_key", "v0"));
    assertEquals(-5.0, jedis_client.zscore("z_key", "v_neg"));
    assertEquals(1.0, jedis_client.zscore("z_key", "v1"));
    assertEquals(2.5, jedis_client.zscore("z_key", "v2"));

    assertNull(jedis_client.zscore("z_key", "v3"));
    assertNull(jedis_client.zscore("z_no_exist", "v0"));

    TSValuePairs tsPairs = new TSValuePairs((1));
    assertEquals("OK", jedis_client.tsadd("ts_key", tsPairs.pairs));

    try {
      double d = jedis_client.zscore("ts_key", "v0");
    } catch (Exception e) {
      assertTrue(e.getMessage().contains(
          "WRONGTYPE Operation against a key holding the wrong kind of value"));
      return;
    }
    assertTrue(false);
  }

  @Test
  public void testZRangeByScore() throws Exception {
    Map<String, Double> pairs = new HashMap<String, Double>();
    pairs.put("v_min", -Double.MAX_VALUE);
    pairs.put("v_neg", -5.0);
    pairs.put("v0", 0.0);
    pairs.put("v1", 1.0);
    pairs.put("v2", 2.5);
    pairs.put("v_max", Double.MAX_VALUE);

    assertEquals(6L, jedis_client.zadd("z_key", pairs).longValue());
    Set<String> values = new HashSet<String>(
        Arrays.asList("v_min", "v_neg", "v0", "v1", "v2", "v_max"));
    assertEquals(values, jedis_client.zrangeByScore(
        "z_key", Double.NEGATIVE_INFINITY, Double.POSITIVE_INFINITY));
    assertEquals(values, jedis_client.zrangeByScore("z_key","-inf", "+inf"));

    Set<Tuple> valuesScores = new HashSet<Tuple>(
        Arrays.asList(new Tuple("v_min", -Double.MAX_VALUE)));
    assertEquals(valuesScores, jedis_client.zrangeByScoreWithScores("z_key", "-inf", "(-5.0"));

    valuesScores = new HashSet<Tuple>(Arrays.asList(new Tuple("v_max", Double.MAX_VALUE)));
    assertEquals(valuesScores, jedis_client.zrangeByScoreWithScores("z_key", "(2.5", "+inf"));

    values = new HashSet<String>(Arrays.asList("v_neg", "v0", "v1", "v2"));
    assertEquals(values, jedis_client.zrangeByScore("z_key", -5.0, 2.5));

    values = new HashSet<String>(Arrays.asList("v_neg", "v0"));
    // offset = 1, the element to start scanning from once we have done our scan.
    // limit = 2, the number of elements to return.
    assertEquals(values, jedis_client.zrangeByScore("z_key","-inf", "+inf", 1, 2));

    valuesScores = new HashSet<Tuple>(
        Arrays.asList(new Tuple("v0", 0.0), new Tuple("v1", 1.0), new Tuple("v2", 2.5)));
    assertEquals(valuesScores, jedis_client.zrangeByScoreWithScores("z_key","-inf", "+inf", 2, 3));

    // Queries with either negative offset or 0 limit returns empty list.
    values = new HashSet<String>();
    assertEquals(values, jedis_client.zrangeByScore("z_key","-inf", "+inf", -1, 6));
    assertEquals(values, jedis_client.zrangeByScore("z_key","-inf", "+inf", 1, 0));
    assertEquals(values, jedis_client.zrangeByScore("z_key","-inf", "+inf", -1, -1));

    // Queries with positive offset and limit either negative or larger than length
    // just scans from offset till the end.
    values = new HashSet<String>(Arrays.asList("v2", "v_max"));
    assertEquals(values, jedis_client.zrangeByScore(
        "z_key",Double.NEGATIVE_INFINITY, Double.POSITIVE_INFINITY, 4, 100));
    assertEquals(values, jedis_client.zrangeByScore("z_key","-inf", "+inf", 4, -3));

    // Key that doesn't exist returns empty list.
    values = new HashSet<String>();
    assertEquals(values, jedis_client.zrangeByScore("key_not_exists","-inf", "+inf"));

    // Key of different type returns error.
    TSValuePairs tsPairs = new TSValuePairs((1));
    assertEquals("OK", jedis_client.tsadd("ts_key", tsPairs.pairs));
    try {
      jedis_client.zrangeByScore("ts_key", "-inf", "+inf");
    } catch (Exception e) {
      assertTrue(e.getMessage().contains(
          "WRONGTYPE Operation against a key holding the wrong kind of value"));
      return;
    }
    assertTrue(false);
  }

  @Test
  public void testKeys() throws Exception {
    Map <String, String> fields = new HashMap<>();
    fields.put("f1", "v1");
    fields.put("f2", "v2");
    fields.put("f3", "v3");
    for (int i = 0; i < 100; i++) {
      for (int j = 0; j < 100; j++) {
        String str = String.format("key_%d_%d", i, j);
        if (i < 32) {
          assertEquals("OK", jedis_client.set(str, "v"));
        } else if (i < 65) {
          assertEquals("OK", jedis_client.hmset(str, fields));
        } else {
          assertEquals(1L, jedis_client.zadd(str, 1, "a").longValue());
        }
      }
    }

    assertEquals(10000, jedis_client.keys("*").size());
    assertEquals(10000, jedis_client.keys("key_*_*").size());
    assertEquals(100, jedis_client.keys("key_*_20").size());
    assertEquals(1000, jedis_client.keys("key_?_*").size());
    assertEquals(36, jedis_client.keys("key_[0123]_[^0]").size());
    assertEquals(0, jedis_client.keys("\\*").size());

    assertEquals("OK", jedis_client.set("key_over_limit", "v"));
    assertEquals(100, jedis_client.keys("key_?_?").size());

    try {
      jedis_client.keys("*");
      fail("Should have failed!");
    } catch (Exception e) {
      LOG.info("Expected exception", e);
    }
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
        config, address.getHostName(), address.getPort(), 10000);
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

  @Test
  public void testMultipleDBs() throws Exception {
    final String secondDBName = "second";
    createRedisTableForDB(secondDBName);

    List<String> dbs = Arrays.asList(DEFAULT_DB_NAME, secondDBName);
    // Do a read/writes to test everything is working correctly.
    readAndWriteFromDBs(dbs, 10);
  }

  public void ttlTest(String key, String value, long true_ttl, int max_ttl_error) throws Exception {
    long ttl = jedis_client.ttl(key);
    long pttl = jedis_client.pttl(key);
    assertTrue(Math.abs(ttl - true_ttl) <= max_ttl_error);
    assertTrue(Math.abs(pttl - true_ttl * 1000) <= max_ttl_error * 1000);
    assertEquals(value, jedis_client.get(key));
  }

  public void expiredTest(String key) throws Exception {
    assertNull(jedis_client.get(key));
    assertEquals(-2L, jedis_client.ttl(key).longValue());
    assertEquals(-2L, jedis_client.pttl(key).longValue());
    assertEquals(0L, jedis_client.expire(key, 7).longValue());
  }

  @Test
  public void testExpireTtl() throws Exception {
    String k1 = "foo";
    String k2 = "fu";
    String k3 = "phu";
    String value = "bar";
    int error = 1;

    // Checking expected behavior on a key with no ttl.
    assertEquals("OK", jedis_client.set(k1, value));
    assertEquals(value, jedis_client.get(k1));
    assertEquals(-2L, jedis_client.ttl(k2).longValue());
    assertEquals(-2L, jedis_client.pttl(k2).longValue());
    assertEquals(-1L, jedis_client.ttl(k1).longValue());
    assertEquals(-1L, jedis_client.pttl(k1).longValue());
    // Setting a ttl and checking expected return values.
    assertEquals(1L, jedis_client.expire(k1, 3).longValue());
    ttlTest(k1, value, 3, error);
    Thread.sleep(2000);
    ttlTest(k1, value, 1, error);
    Thread.sleep(2000);
    expiredTest(k1);
    // Testing functionality with SETEX.
    assertEquals("OK", jedis_client.setex(k1, 5, value));
    ttlTest(k1, value, 5, error);
    // Set a new, earlier expiration.
    assertEquals(1L, jedis_client.expire(k1, 2).longValue());
    ttlTest(k1, value, 2, error);
    // Check that the value expires as expected.
    Thread.sleep(2000);
    expiredTest(k1);
    // Initialize with SET using the EX flag.
    assertEquals("OK", jedis_client.set(k1, value, "EX", 2));
    // Set a new, later, expiration.
    assertEquals(1L, jedis_client.expire(k1, 8).longValue());
    ttlTest(k1, value, 8, error);
    // Checking expected return values after a while, before expiration.
    Thread.sleep(4000);
    ttlTest(k1, value, 4, error);
    // Persisting the key and checking expected return values.
    assertEquals(1L, jedis_client.persist(k1).longValue());
    assertEquals(value, jedis_client.get(k1));
    assertEquals(-1L, jedis_client.ttl(k1).longValue());
    assertEquals(-1L, jedis_client.pttl(k1).longValue());
    // Check that the key and value are still there after a while.
    Thread.sleep(30000);
    assertEquals(value, jedis_client.get(k1));
    assertEquals(-1L, jedis_client.ttl(k1).longValue());
    assertEquals(-1L, jedis_client.pttl(k1).longValue());
    // Persist a key that does not exist.
    assertEquals(0L, jedis_client.persist(k2).longValue());
    // Persist a key that has no TTL.
    assertEquals(0L, jedis_client.persist(k1).longValue());
    // Vanilla set on a key and persisting it.
    assertEquals("OK", jedis_client.set(k2, value));
    assertEquals(0L, jedis_client.persist(k2).longValue());
    assertEquals(-1L, jedis_client.ttl(k2).longValue());
    assertEquals(-1L, jedis_client.pttl(k2).longValue());
    // Test that setting a zero-valued TTL properly expires the value.
    assertEquals(1L, jedis_client.expire(k2, 0).longValue());
    expiredTest(k2);
    // One more time with a negative TTL.
    assertEquals("OK", jedis_client.set(k2, value));
    assertEquals(1L, jedis_client.expire(k2, -7).longValue());
    expiredTest(k2);
    assertEquals("OK", jedis_client.setex(k2, -7, value));
    expiredTest(k2);
    // Test PExpire
    assertEquals("OK", jedis_client.set(k2, value));
    assertEquals(1L, jedis_client.pexpire(k2, 3200).longValue());
    ttlTest(k2, value, 3, error);
    Thread.sleep(1000);
    ttlTest(k2, value, 2, error);
    Thread.sleep(3000);
    expiredTest(k2);
    // Test PSetEx
    assertEquals("OK", jedis_client.psetex(k3, 2300, value));
    ttlTest(k3, value, 2, error);
    Thread.sleep(1000);
    ttlTest(k3, value, 1, error);
    Thread.sleep(2000);
    expiredTest(k3);
  }
}
