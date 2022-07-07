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

import static org.yb.AssertionWrappers.*;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.List;

import org.yb.YBParameterizedTestRunner;

@RunWith(YBParameterizedTestRunner.class)
public class TestYBJedisTSRevRangeByTime extends BaseJedisTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestYBJedis.class);

  public TestYBJedisTSRevRangeByTime(JedisClientType jedisClientType) {
    super(jedisClientType);
  }

  // Run each test with Jedis, JedisCluster, and YBJedis clients.
  @Parameterized.Parameters
  public static List<JedisClientType> jedisClients() {
    return Arrays.asList(JedisClientType.JEDIS, JedisClientType.JEDISCLUSTER,
        JedisClientType.YBJEDIS);
  }

  private String GenerateErrorString(TSValuePairs tsValuePairs, String key,
                                     List<String> received, List<String> expected,
                                     String startTS, String endTS, String limit) {

    String list = new String();
    for (Map.Entry<Long, String> pair : tsValuePairs.pairs.entrySet()) {
      if (!list.isEmpty()) {
        list += " ";
      }
      list += String.format("%d %s", pair.getKey(), pair.getValue());
    }
    String tsAddCmd = String.format("TSADD %s %s", key, list);
    String tsRevRangeByTimeCmd = String.format("TSREVRANGEBYTIME %s %s %s", key, startTS, endTS);
    if (!limit.isEmpty()) {
      tsRevRangeByTimeCmd += String.format(" LIMIT %s", limit);
    }
    return String.format("Test failed\nGot:      %s\nExpected: %s\nCommands:\n%s\n%s",
                         received, expected, tsAddCmd, tsRevRangeByTimeCmd);
  }

  @Test
  public void WholeRange() throws Exception {
    TSValuePairs pairs = new TSValuePairs((100));
    assertEquals("OK", jedis_client.tsadd("k0", pairs.pairs));

    // Read all the values.
    List<String> values1 = jedis_client.tsrevrangeByTime("k0",
        String.format("%d", pairs.minTS), String.format("%d", pairs.maxTS));

    // Same as above, but using a different interface.
    List<String> values2 = jedis_client.tsrevrangeByTime("k0", pairs.minTS, pairs.maxTS);

    assertEquals(values1, values2);
    assertEquals(values1, pairs.GetReverseValuesList());
  }

  @Test
  public void WholeRangeWithLimit() throws Exception {
    Random random = new Random();
    for (int nElements : new int[] {1, 100, 1000}) {
      TSValuePairs pairs = new TSValuePairs((nElements));
      String key = String.format("k%d", nElements);
      assertEquals("OK", jedis_client.tsadd(key, pairs.pairs));

      for (int i = 0; i < 100; i++) {
        int limit = random.nextInt(2 * nElements) + 1;

        // Read all the values with limit.
        List<String> receivedValues1 = jedis_client.tsrevrangeByTime(key,
            String.format("%d", pairs.minTS), String.format("%d", pairs.maxTS), limit);
        // Same as above, but using a different interface.
        List<String> receivedValues2 =
            jedis_client.tsrevrangeByTime(key, pairs.minTS, pairs.maxTS, limit);

        int maxSize = Math.min(limit, nElements);

        // The two lists received using different APIs should be equal.
        assertEquals(receivedValues1, receivedValues2);

        List<String> expectedValues = pairs.GetReverseValuesList().subList(0, 2 * maxSize);
        if(!receivedValues1.equals(expectedValues)) {
          fail(GenerateErrorString(pairs, key, receivedValues1, expectedValues,
              Long.toString(pairs.minTS), Long.toString(pairs.maxTS), Integer.toString(limit)));
        }
      }
    }
  }

  @Test
  public void RandomRanges() throws Exception {
    for (int nElements : new int[] {1, 100, 1000}) {
      TSValuePairs pairs = new TSValuePairs((nElements));
      String key = String.format("k%d", nElements);
      assertEquals("OK", jedis_client.tsadd(key, pairs.pairs));

      for (int i = 0; i < 100; i++) {
        List<Long> randomTS = pairs.GetRandomTimestamps();

        // Get a random range.
        List<String> receivedValues1 = jedis_client.tsrevrangeByTime(key,
            String.format("%d", randomTS.get(0)), String.format("%d", randomTS.get(1)));

        // Same as above, but using a different interface.
        List<String> receivedValues2 =
            jedis_client.tsrevrangeByTime(key, randomTS.get(0), randomTS.get(1));

        // The two lists received using different APIs should be equal.
        assertTrue(receivedValues1.equals(receivedValues2));

        List<String> expectedValues = pairs.GetReverseValuesList(randomTS.get(0), randomTS.get(1));
        if(!receivedValues1.equals(expectedValues)) {
          fail(GenerateErrorString(pairs, key, receivedValues1, expectedValues,
              Long.toString(randomTS.get(0)), Long.toString(randomTS.get(1)), ""));
        }
      }
    }
  }

  @Test
  public void ExclusiveRandomRanges() throws Exception {
    for (int nElements : new int[] {1, 100, 1000}) {
      TSValuePairs pairs = new TSValuePairs((nElements));
      String key = String.format("k%d", nElements);
      assertEquals("OK", jedis_client.tsadd(key, pairs.pairs));

      Random random = new Random();

      for (int i = 0; i < 100; i++) {
        List<Long> randomTS = pairs.GetRandomTimestamps();

        boolean ts1Exclusive = random.nextBoolean();
        boolean ts2Exclusive = random.nextBoolean();

        String ts1, ts2;
        if (ts1Exclusive) {
          ts1 = String.format("(%d", randomTS.get(0));
        } else {
          ts1 = String.format("%d", randomTS.get(0));
        }

        if (ts2Exclusive) {
          ts2 = String.format("(%d", randomTS.get(1));
        } else {
          ts2 = String.format("%d", randomTS.get(1));
        }

        List<String> receivedValues = jedis_client.tsrevrangeByTime(key, ts1, ts2);

        long min = randomTS.get(0), max = randomTS.get(1);
        if (ts1Exclusive) {
          min += 1;
        }
        if (ts2Exclusive) {
          max -= 1;
        }

        List<String> expectedValues = pairs.GetReverseValuesList(min, max);
        if(!receivedValues.equals(expectedValues)) {
          fail(GenerateErrorString(pairs, key, receivedValues, expectedValues, ts1, ts2, ""));
        }
      }
    }
  }

  @Test
  public void ExclusiveRandomRangesWithLimits() throws Exception {
    for (int nElements : new int[]{1, 100, 1000}) {
      TSValuePairs pairs = new TSValuePairs((nElements));
      String key = String.format("k%d", nElements);
      assertEquals("OK", jedis_client.tsadd(key, pairs.pairs));

      Random random = new Random();

      for (int i = 0; i < 100; i++) {
        List<Long> randomTS = pairs.GetRandomTimestamps();

        boolean ts1Exclusive = random.nextBoolean();
        boolean ts2Exclusive = random.nextBoolean();

        String ts1, ts2;
        if (ts1Exclusive) {
          ts1 = String.format("(%d", randomTS.get(0));
        } else {
          ts1 = String.format("%d", randomTS.get(0));
        }

        if (ts2Exclusive) {
          ts2 = String.format("(%d", randomTS.get(1));
        } else {
          ts2 = String.format("%d", randomTS.get(1));
        }

        int limit = random.nextInt(2 * nElements) + 1;

        List<String> receivedValues = jedis_client.tsrevrangeByTime(key, ts1, ts2, limit);

        long min = randomTS.get(0), max = randomTS.get(1);
        if (ts1Exclusive) {
          min += 1;
        }
        if (ts2Exclusive) {
          max -= 1;
        }

        List<String> expectedValues = pairs.GetReverseValuesList(min, max);
        int maxSize = Math.min(expectedValues.size(), 2 * limit);

        if(!receivedValues.equals(expectedValues.subList(0, maxSize))) {
          fail(GenerateErrorString(pairs, key, receivedValues, expectedValues.subList(0, maxSize),
              ts1, ts2, Long.toString(limit)));
        }
      }
    }
  }
}
