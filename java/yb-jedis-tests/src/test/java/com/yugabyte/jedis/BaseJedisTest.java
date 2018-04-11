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

import org.junit.After;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.YBClient;
import org.yb.Common.PartitionSchemaPB.HashSchema;
import org.yb.minicluster.BaseMiniClusterTest;
import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisCommands;
import redis.clients.jedis.YBJedis;

import java.net.InetSocketAddress;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.concurrent.ThreadLocalRandom;
import java.util.stream.Collectors;

import static junit.framework.TestCase.assertEquals;

public abstract class BaseJedisTest extends BaseMiniClusterTest {

  protected enum JedisClientType { JEDIS, YBJEDIS };

  protected static final int JEDIS_SOCKET_TIMEOUT_MS = 10000;
  protected JedisClientType jedisClientType;
  protected JedisCommands jedis_client;
  private static final Logger LOG = LoggerFactory.getLogger(BaseJedisTest.class);

  @Before
  public void setUpJedis() throws Exception {
    // Wait for all tserver heartbeats.
    waitForTServersAtMasterLeader();

    // Create the redis table.
    miniCluster.getClient().createRedisTable(YBClient.REDIS_DEFAULT_TABLE_NAME);

    GetTableSchemaResponse tableSchema = miniCluster.getClient().getTableSchema(
        YBClient.REDIS_KEYSPACE_NAME, YBClient.REDIS_DEFAULT_TABLE_NAME);

    assertEquals(HashSchema.REDIS_HASH_SCHEMA, tableSchema.getPartitionSchema().getHashSchema());

    // Setup the Jedis client.
    List<InetSocketAddress> redisContactPoints = miniCluster.getRedisContactPoints();
    assertEquals(NUM_TABLET_SERVERS, redisContactPoints.size());

    switch (jedisClientType) {
      case JEDIS:
        LOG.info("Connecting to: " + redisContactPoints.get(0).toString());
        jedis_client = new Jedis(redisContactPoints.get(0).getHostName(),
            redisContactPoints.get(0).getPort(), JEDIS_SOCKET_TIMEOUT_MS);
        break;
      case YBJEDIS:
        LOG.info("Connecting to: " + redisContactPoints.stream().map(InetSocketAddress::toString)
            .collect(Collectors.joining(",")));
        Set<HostAndPort> contactPoints = redisContactPoints.stream().map(inet ->
            new HostAndPort(inet.getHostName(), inet.getPort())).collect(Collectors.toSet());
        jedis_client = new YBJedis(contactPoints, JEDIS_SOCKET_TIMEOUT_MS);
        break;
    }

  }

  public BaseJedisTest(JedisClientType jedisClientType) {
    this.jedisClientType = jedisClientType;
  }

  @After
  public void tearDownAfter() throws Exception {
    destroyMiniCluster();

    if (jedis_client != null) {
      switch (jedisClientType) {
        case JEDIS:
          ((Jedis) jedis_client).close();
          break;
        case YBJEDIS:
          ((YBJedis) jedis_client).close();
          break;
      }
    }
  }

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
}
