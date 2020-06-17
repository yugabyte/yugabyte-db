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

import com.google.common.net.HostAndPort;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBParameterizedTestRunner;
import org.yb.minicluster.MiniYBDaemon;
import java.util.*;
import redis.clients.jedis.JedisCommands;

import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertTrue;

@RunWith(value=YBParameterizedTestRunner.class)
public class TestYBJedisCluster extends BaseJedisTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYBJedis.class);

  public TestYBJedisCluster(JedisClientType jedisClientType) {
    super(jedisClientType);
  }

  // Run each test with both Jedis and YBJedis clients.
  @Parameterized.Parameters
  public static Collection jedisClients() {
    return Arrays.asList(JedisClientType.JEDISCLUSTER);
  }

  public int getTestMethodTimeoutSec() {
    return 1200;
  }

  @Test
  public void testLocalOps() throws Exception {
    destroyMiniCluster();

    // We don't want the tablets to move while we are testing because Jedis will be left with a
    // stale key partition map since it won't receive a MOVED request which triggers the update.
    List<String> masterArgs = Arrays.asList("--enable_load_balancing=false");

    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    // Give more memory to support 2 databases.
    final long MEMORY_LIMIT = 2 * 1024 * 1024 * 1024l;
    tserverArgs.add(Arrays.asList("--TEST_assert_local_op=true",
                                  "--memory_limit_hard_bytes=" + MEMORY_LIMIT));
    tserverArgs.add(Arrays.asList("--TEST_assert_local_op=true",
                                  "--memory_limit_hard_bytes=" + MEMORY_LIMIT));
    tserverArgs.add(Arrays.asList("--TEST_assert_local_op=true",
                                  "--memory_limit_hard_bytes=" + MEMORY_LIMIT));
    createMiniCluster(3, masterArgs, tserverArgs);

    setUpJedis();
    final String secondDBName = "1";
    createRedisTableForDB(secondDBName);

    Collection dbs = Arrays.asList(DEFAULT_DB_NAME, secondDBName);
    // Do a few writes to test everything is working correctly.
    readAndWriteFromDBs(dbs, 1000);
  }

  @Test
  public void testMovedReply() throws Exception {
    destroyMiniCluster();

    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    // Give more memory to support 2 databases.
    final long MEMORY_LIMIT = 2 * 1024 * 1024 * 1024l;
    tserverArgs.add(Arrays.asList("--forward_redis_requests=false",
                                  "--memory_limit_hard_bytes=" + MEMORY_LIMIT));
    tserverArgs.add(Arrays.asList("--forward_redis_requests=false",
                                  "--memory_limit_hard_bytes=" + MEMORY_LIMIT));
    tserverArgs.add(Arrays.asList("--forward_redis_requests=false",
                                  "--memory_limit_hard_bytes=" + MEMORY_LIMIT));
    createMiniCluster(3, masterArgs, tserverArgs);

    waitForTServersAtMasterLeader();

    LOG.info("Created cluster");

    setUpJedis();
    final String secondDBName = "1";
    createRedisTableForDB(secondDBName);

    Collection dbs = Arrays.asList(DEFAULT_DB_NAME, secondDBName);
    // Do a few writes to test everything is working correctly.
    readAndWriteFromDBs(dbs, 100);

    // Add a node and verify our commands succeed.
    LOG.info("Adding a new node ");
    miniCluster.startTServer(
        Arrays.asList("--forward_redis_requests=false",
                      "--memory_limit_hard_bytes=" + MEMORY_LIMIT));

    // Test that everything works correctly after adding a node. The load balancer will assign
    // tablets to the new node.
    readAndWriteFromDBs(dbs, 5000);

    Map<HostAndPort, MiniYBDaemon> tabletServers = miniCluster.getTabletServers();

    int counter = 0;
    // Remove a node and verify that our commands succeed.
    for (HostAndPort hostAndPort : tabletServers.keySet()) {
      LOG.info("Removing node " + hostAndPort);
      miniCluster.killTabletServerOnHostPort(hostAndPort);
      break;
    }

    // Verify that we can continue writing to the cluster after the node has been removed. Even if
    // we receive stale placement information, we should eventually get the new configuration and
    // finish all the operations without a failure.
    readAndWriteFromDBs(dbs, 5000);
  }
}
