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

import org.junit.Test;
import org.junit.runner.RunWith;
import static org.yb.AssertionWrappers.fail;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

/**
 * Tests {@link AsyncYBClient} with multiple masters.
 */
@RunWith(value=YBTestRunner.class)
public class TestMasterFailover extends BaseYBClientTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestMasterFailover.class);
  private static final String TABLE_NAME =
      TestMasterFailover.class.getName() + "-" + System.currentTimeMillis();

  @Override
  protected void afterStartingMiniCluster() throws Exception {
    super.afterStartingMiniCluster();

    createTable(TABLE_NAME, hashKeySchema, new CreateTableOptions());
  }

  @Override
  protected int getNumShardsPerTServer() {
    return 1;
  }

  @Test(timeout = 60000)
  public void testKillLeader() throws Exception {
    int countMasters = masterHostPorts.size();
    if (countMasters < 3) {
      LOG.info("This test requires at least 3 master servers, but only " + countMasters +
          " are specified.");
      return;
    }
    killMasterLeader();

    if (!waitForTServersAtMasterLeader()) {
      fail("Couldn't get all tablet servers heartbeating to new master leader.");
    }

    // Test that we can create a new table when one of the masters is down.
    String newTableName = TABLE_NAME + "-afterLeaderIsDead";
    createTable(newTableName, hashKeySchema, new CreateTableOptions());

    // Test that we can initialize a client when one of the masters specified in the
    // connection string is down.
    AsyncYBClient newClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses).build();
  }
}
