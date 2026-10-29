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
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBParameterizedTestRunner;

import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertNotNull;

// YEDIS smoke tests using the partition-aware YBJedis client.
@RunWith(YBParameterizedTestRunner.class)
public final class TestYBJedisWithYBJedisClient extends TestYBJedis {
  private static final Logger LOG = LoggerFactory.getLogger(TestYBJedisWithYBJedisClient.class);

  public TestYBJedisWithYBJedisClient(JedisClientType jedisClientType) {
    super(jedisClientType);
  }

  @Parameterized.Parameters
  public static List<JedisClientType> jedisClients() {
    return Arrays.asList(JedisClientType.YBJEDIS);
  }

  // Overridden rather than moved here, because both TestYBJedisWithJedisClusterClient and
  // TestYBJedisWithYBJedisClient run this test.
  @Override
  @Test
  public void testMultipleDBs() throws Exception {
    super.testMultipleDBs();
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
}
