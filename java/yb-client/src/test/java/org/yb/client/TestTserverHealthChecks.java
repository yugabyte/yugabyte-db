// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.ServerInfo;

import static org.yb.AssertionWrappers.*;

import java.net.MalformedURLException;
import java.net.URL;
import java.util.Map;
import java.util.Scanner;

@RunWith(value= YBTestRunner.class)
public class TestTserverHealthChecks extends BaseYBClientTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestTserverHealthChecks.class);
  private static final String TABLE_NAME =
      TestMasterFailover.class.getName() + "-" + System.currentTimeMillis();

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    // Without this flag, we'll end up deleting the failed tablet, and the count of failed_tablets
    // will be 0.
    flagMap.put("TEST_disable_tablet_deletion", "true");
    // Set the tablet_creation_timeout_ms to 30 sec so that the tablet marked as failed will be
    // replaced after 30 sec and the CreateTable operation finishs successfully.
    flagMap.put("tablet_creation_timeout_ms", "30000");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_force_single_tablet_failure", "true");
    flagMap.put("TEST_delay_removing_peer_with_failed_tablet_secs", "120");
    return flagMap;
  }

  @Override
  protected void afterStartingMiniCluster() throws Exception {
    super.afterStartingMiniCluster();
    createTable(TABLE_NAME, hashKeySchema, new CreateTableOptions());
  }

  @Override
  protected int getNumShardsPerTServer() {
    return 1;
  }

  private JsonElement getTserverHealthValue(String host, int port, String key) throws Exception {
    try {
      // Call the health-check JSON endpoint.
      URL url = new URL(String.format("http://%s:%d/api/v1/health-check", host, port));
      Scanner scanner = new Scanner(url.openConnection().getInputStream());
      JsonParser parser = new JsonParser();
      JsonElement tree = parser.parse(scanner.useDelimiter("\\A").next());
      JsonObject kvs = tree.getAsJsonObject();
      LOG.info("Health Value: " + key + " = " + kvs.get(key));
      return kvs.get(key);
    } catch (MalformedURLException e) {
      throw new InternalError(e.getMessage());
    }
  }

  private JsonElement getTserverHealthValue(String key) throws Exception {
    // Perform the Health check on a random Tserver.
    Map<HostAndPort, MiniYBDaemon> tservers = miniCluster.getTabletServers();
    Map.Entry<HostAndPort,MiniYBDaemon> entry = tservers.entrySet().iterator().next();
    HostAndPort tserver = entry.getKey();
    int port = tservers.get(tserver).getWebPort();

    return getTserverHealthValue(tserver.getHost(), port, key);
  }

  @Test(timeout = 60000)
  public void testTserverHealthChecks() throws Exception {
    int countMasters = masterHostPorts.size();
    assertEquals(3, countMasters);
    JsonArray deadTablets = getTserverHealthValue("failed_tablets").getAsJsonArray();
    assertEquals(1, deadTablets.size());
  }
}
