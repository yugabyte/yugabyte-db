// Copyright (c) YugabyteDB, Inc.
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

import java.util.Arrays;
import java.util.List;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;

import org.yb.YBParameterizedTestRunner;

import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertNull;
import static junit.framework.TestCase.assertTrue;

// YEDIS smoke tests using the Jedis client, which talks to a single Redis proxy.
@RunWith(YBParameterizedTestRunner.class)
public final class TestYBJedisWithJedisClient extends TestYBJedis {
  public TestYBJedisWithJedisClient(JedisClientType jedisClientType) {
    super(jedisClientType);
  }

  @Parameterized.Parameters
  public static List<JedisClientType> jedisClients() {
    return Arrays.asList(JedisClientType.JEDIS);
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
