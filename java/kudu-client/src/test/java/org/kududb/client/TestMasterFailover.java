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
package org.kududb.client;

import org.junit.BeforeClass;
import org.junit.Ignore;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.junit.Assert.assertEquals;


/**
 * Tests {@link AsyncKuduClient} with multiple masters.
 */
public class TestMasterFailover extends BaseKuduTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestMasterFailover.class);
  private static final String TABLE_NAME =
      TestMasterFailover.class.getName() + "-" + System.currentTimeMillis();

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();
    createTable(TABLE_NAME, basicSchema, new CreateTableOptions());
  }

  /**
   * This test is disabled as we're not supporting multi-master just yet.
   */
  @Test(timeout = 30000)
  @Ignore
  public void testKillLeader() throws Exception {
    int countMasters = masterHostPorts.size();
    if (countMasters < 3) {
      LOG.info("This test requires at least 3 master servers, but only " + countMasters +
          " are specified.");
      return;
    }
    killMasterLeader();

    // Test that we can open a previously created table after killing the leader master.
    KuduTable table = openTable(TABLE_NAME);
    assertEquals(0, countRowsInScan(client.newScannerBuilder(table).build()));

    // Test that we can create a new table when one of the masters is down.
    String newTableName = TABLE_NAME + "-afterLeaderIsDead";
    createTable(newTableName, basicSchema, new CreateTableOptions());
    table = openTable(newTableName);
    assertEquals(0, countRowsInScan(client.newScannerBuilder(table).build()));

    // Test that we can initialize a client when one of the masters specified in the
    // connection string is down.
    AsyncKuduClient newClient = new AsyncKuduClient.AsyncKuduClientBuilder(masterAddresses).build();
    table = newClient.openTable(newTableName).join(DEFAULT_SLEEP);
    assertEquals(0, countRowsInScan(newClient.newScannerBuilder(table).build()));
  }
}
