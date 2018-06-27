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
import org.apache.commons.text.RandomStringGenerator;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBDaemon;
import redis.clients.jedis.exceptions.JedisConnectionException;
import redis.clients.jedis.exceptions.JedisClusterMaxRedirectionsException;

import java.util.*;

import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertTrue;

@RunWith(Parameterized.class)
public class TestYBJedisCluster extends BaseJedisTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYBJedis.class);
  private static final int MIN_BACKOFF_TIME_MS = 1000;
  private static final int MAX_BACKOFF_TIME_MS = 50000;
  private static final int MAX_RETRIES = 20;
  private static final int KEY_LENGTH = 20;

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
    tserverArgs.add(Arrays.asList("--assert_local_op=true"));
    tserverArgs.add(Arrays.asList("--assert_local_op=true"));
    tserverArgs.add(Arrays.asList("--assert_local_op=true"));
    createMiniCluster(3, masterArgs, tserverArgs);

    setUpJedis();

    RandomStringGenerator generator = new RandomStringGenerator.Builder()
        .withinRange('a', 'z').build();

    for (int i = 0; i < 1000; i++) {
      String s = generator.generate(KEY_LENGTH);
      assertEquals("OK", jedis_client.set(s, "v"));
      assertEquals("v", jedis_client.get(s));
    }
  }

  private void doWritesAndVerify(int nIterations) throws Exception {
    RandomStringGenerator generator = new RandomStringGenerator.Builder()
        .withinRange('a', 'z').build();

    for (int i = 0; i < nIterations; i++) {
      String s = generator.generate(KEY_LENGTH);
      int retriesLeft = MAX_RETRIES;
      int sleepTime = MIN_BACKOFF_TIME_MS;
      while (retriesLeft > 0) {
        try {
          LOG.debug("Writing key {}", s);
          assertEquals("OK", jedis_client.set(s, "v"));
          LOG.debug("Reading key {}", s);
          assertEquals("v", jedis_client.get(s));
          break;
        } catch (JedisConnectionException e) {
          LOG.info("JedisConnectionException exception " + e.toString());
        } catch (JedisClusterMaxRedirectionsException e) {
          LOG.info("JedisClusterMaxRedirectionsException exception " + e.toString());
        }
        --retriesLeft;
        Thread.sleep(sleepTime);
        sleepTime = Math.min(sleepTime * 2, MAX_BACKOFF_TIME_MS);
      }
      if (retriesLeft < 1) {
        assertTrue("Exceeded maximum number of retries", false);
      }
      if (retriesLeft < MAX_RETRIES) {
        LOG.info("Operation on key {} took {} retries", s, MAX_RETRIES - retriesLeft);
      }
    }
  }

  @Test
  public void testMovedReply() throws Exception {
    destroyMiniCluster();

    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    tserverArgs.add(Arrays.asList("--forward_redis_requests=false"));
    tserverArgs.add(Arrays.asList("--forward_redis_requests=false"));
    tserverArgs.add(Arrays.asList("--forward_redis_requests=false"));
    createMiniCluster(3, masterArgs, tserverArgs);

    waitForTServersAtMasterLeader();

    LOG.info("Created cluster");

    setUpJedis();

    // Do a few writes to test everything is working correctly.
    doWritesAndVerify(100);

    // Add a node and verify our commands succeed.
    LOG.info("Adding a new node ");
    miniCluster.startTServer(Arrays.asList("--forward_redis_requests=false"));

    // Test that everything works correctly after adding a node. The load balancer will assign
    // tablets to the new node.
    doWritesAndVerify(5000);

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
    doWritesAndVerify(5000);
  }
}
