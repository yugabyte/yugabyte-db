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

import com.sun.xml.internal.rngom.parse.host.Base;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.YBClient;
import org.yb.Common.PartitionSchemaPB.HashSchema;
import org.yb.minicluster.BaseMiniClusterTest;
import redis.clients.jedis.Jedis;

import java.net.InetSocketAddress;
import java.util.List;

import static junit.framework.TestCase.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.yb.client.YBClient.REDIS_DEFAULT_TABLE_NAME;
import static org.yb.client.YBClient.REDIS_KEYSPACE_NAME;

public class BaseJedisTest extends BaseMiniClusterTest {

  private static final int JEDIS_SOCKET_TIMEOUT_MS = 10000;
  protected Jedis jedis_client;
  private static final Logger LOG = LoggerFactory.getLogger(BaseJedisTest.class);

  @Before
  public void setUpJedis() throws Exception {
    // Wait for all tserver heartbeats.
    waitForTServersAtMasterLeader();

    // Create the redis table.
    miniCluster.getClient().createRedisTable(YBClient.REDIS_DEFAULT_TABLE_NAME);

    YBClient ybClient;

    GetTableSchemaResponse tableSchema = miniCluster.getClient().getTableSchema(
        YBClient.REDIS_KEYSPACE_NAME, YBClient.REDIS_DEFAULT_TABLE_NAME);

    assertEquals(HashSchema.REDIS_HASH_SCHEMA, tableSchema.getPartitionSchema().getHashSchema());

    // Setup the jedis client.
    List<InetSocketAddress> redisContactPoints = miniCluster.getRedisContactPoints();
    assertEquals(NUM_TABLET_SERVERS, redisContactPoints.size());

    LOG.info("Connecting to: " + redisContactPoints.get(0).toString());
    jedis_client = new Jedis(redisContactPoints.get(0).getHostName(),
      redisContactPoints.get(0).getPort(), JEDIS_SOCKET_TIMEOUT_MS);
  }

  @After
  public void tearDownAfter() throws Exception {
    destroyMiniCluster();
  }
}
