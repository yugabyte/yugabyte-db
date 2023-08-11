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

package org.yb.cdc;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService.TabletCheckpointPair;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.util.CDCTestUtils;
import org.yb.client.GetTabletListToPollForCDCResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.*;

import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;
import java.time.Duration;
import java.util.*;

import org.awaitility.Awaitility;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass;

@RunWith(value = YBTestRunner.class)
public class TestGetTabletsApiCdc extends CDCBaseClass {
  private final Logger LOGGER = LoggerFactory.getLogger(TestGetTabletsApiCdc.class);

  private CDCSubscriber testSubscriber;

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int);");
  }

  // This test is to verify the fix for the following ticket
  // GitHub #16481: https://github.com/yugabyte/yugabyte-db/issues/16481
  @Test
  public void verifyGetTabletListApiOnColocatedTables() throws Exception {
    final String COLOCATED_DB = "colocated_db";
    statement.execute("drop database if exists " + COLOCATED_DB + ";");
    statement.execute("create database " + COLOCATED_DB + " with colocated = true;");

    final InetSocketAddress pgAddress = miniCluster.getPostgresContactPoints().get(0);
    String url = String.format("jdbc:yugabytedb://%s:%d/%s", pgAddress.getHostName(),
                               pgAddress.getPort(), COLOCATED_DB);
    Properties props = new Properties();
    props.setProperty("user", DEFAULT_PG_USER);
    try (Connection conn = DriverManager.getConnection(url, props)) {
      Statement st = conn.createStatement();

      st.execute("CREATE TABLE test_1 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");
      st.execute("CREATE TABLE test_2 (text_key TEXT PRIMARY KEY) WITH (COLOCATED = true);");
      st.execute("CREATE TABLE test_3 (hours FLOAT PRIMARY KEY, hours_in_text VARCHAR(40)) " +
                 "WITH (COLOCATED = true);");

      // Close statement and connection
      st.close();
    }

    testSubscriber = new CDCSubscriber(COLOCATED_DB, "test_1", getMasterAddresses());
    testSubscriber.createStream("proto");
    String dbStreamId = testSubscriber.getDbStreamId();

    List<String> tableIds = new ArrayList<>();
    YBClient ybClient = testSubscriber.getSyncClient();

    ListTablesResponse resp = ybClient.getTablesList();
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo : resp.getTableInfoList()) {
      if (tableInfo.getName().equals("test_1")
            || tableInfo.getName().equals("test_2")
            || tableInfo.getName().equals("test_3")) {
        tableIds.add(tableInfo.getId().toStringUtf8());
      }
    }

    // Call the GetTabletListToPollForCDC API on all the tables so that we know it is not failing.
    for (String tableId : tableIds) {
      YBTable table = ybClient.openTableByUUID(tableId);
      try {
        // The API should not throw any exception.
        GetTabletListToPollForCDCResponse response =
          ybClient.getTabletListToPollForCdc(table, dbStreamId, tableId);
        for (TabletCheckpointPair tabletCheckpointPair : response.getTabletCheckpointPairList()) {
          LOGGER.info("Table {} got tablet in response {} ", tableId,
            tabletCheckpointPair.getTabletLocations().getTabletId().toStringUtf8());
        }
      } catch (Exception e) {
        fail("Failed test because it the API GetTabletListToPollForCDC threw exception " + e);
      }
    }

    // Cleanup the tables created in the colocated database for this test.
    try (Connection conn = DriverManager.getConnection(url, props)) {
      Statement st = conn.createStatement();

      st.execute("DROP TABLE IF EXISTS test_1;");
      st.execute("DROP TABLE IF EXISTS test_2;");
      st.execute("DROP TABLE IF EXISTS test_3;");

      // Close statement and connection
      st.close();
    }

    // Drop the colocated database created as a part of this test.
    statement.execute("DROP DATABASE " + COLOCATED_DB + ";");
  }

  @Test
  public void verifyIfNewApiReturnsExpectedValues() throws Exception {
    setServerFlag(getTserverHostAndPort(), "update_min_cdc_indices_interval_secs", "1");
    setServerFlag(getTserverHostAndPort(), "cdc_state_checkpoint_update_interval_ms", "1");

    testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    // Insert some records in the table.
    for (int i = 0; i < 2000; ++i) {
      statement.execute(String.format("INSERT INTO test VALUES (%d,%d);", i, i+1));
    }

    // This is the tablet Id that we need to split.
    String tabletId = testSubscriber.getTabletId();

    // Call the new API to see if we are receiving the correct tabletId.
    YBClient ybClient = testSubscriber.getSyncClient();
    assertNotNull(ybClient);

    GetTabletListToPollForCDCResponse respBeforeSplit = ybClient.getTabletListToPollForCdc(
      ybClient.openTableByUUID(
        testSubscriber.getTableId()), testSubscriber.getDbStreamId(), testSubscriber.getTableId());

    // Assert that there is only one tablet checkpoint pair.
    assertEquals(1, respBeforeSplit.getTabletCheckpointPairListSize());

    // Since there is one tablet only, verify its tablet ID.
    TabletCheckpointPair pair = respBeforeSplit.getTabletCheckpointPairList().get(0);
    assertEquals(tabletId, pair.getTabletLocations().getTabletId().toStringUtf8());

    ybClient.flushTable(testSubscriber.getTableId());

    // Wait for the flush table command to succeed.
    CDCTestUtils.waitFor(60 /* seconds to wait */);

    ybClient.splitTablet(tabletId);

    // Insert more records after scheduling the split tablet task.
    for (int i = 2000; i < 10000; ++i) {
      statement.execute(String.format("INSERT INTO test VALUES (%d,%d);", i, i+1));
    }

    // Wait for tablet split to happen and verify that the tablet split has actually happened.
    waitForTabletSplit(ybClient, testSubscriber.getTableId(), 2 /* expectedTabletCount */);

    // Call the new API to get the tablets.
    GetTabletListToPollForCDCResponse respAfterSplit = ybClient.getTabletListToPollForCdc(
      ybClient.openTableByUUID(
        testSubscriber.getTableId()), testSubscriber.getDbStreamId(), testSubscriber.getTableId());

    // There would still be a single tablet since we haven't yet called get changes on the parent
    // tablet yet.
    assertEquals(1, respAfterSplit.getTabletCheckpointPairListSize());
  }

  private void waitForTabletSplit(YBClient ybClient, String tableId,
                                  int expectedTabletCount) throws Exception {
    Awaitility.await()
      .pollDelay(Duration.ofSeconds(10))
      .atMost(Duration.ofSeconds(120))
      .until(() -> {
        Set<String> tabletIds = ybClient.getTabletUUIDs(ybClient.openTableByUUID(tableId));
        return tabletIds.size() == expectedTabletCount;
      });
  }
}
