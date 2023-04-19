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

import org.awaitility.Awaitility;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService.TabletCheckpointPair;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.client.GetDBStreamInfoResponse;
import org.yb.client.GetTabletListToPollForCDCResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.*;

import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

@RunWith(value = YBTestRunner.class)
public class TestDBStreamInfo extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestDBStreamInfo.class);

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("drop table if exists test_2;");
    statement.execute("drop table if exists test_3;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  @Test
  public void testDBStreamInfoResponse() throws Exception {
    LOG.info("Starting testDBStreamInfoResponse");

    // Inserting a dummy row.
    int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
    assertEquals(1, rowsAffected);

    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    GetDBStreamInfoResponse resp = testSubscriber.getDBStreamInfo();

    assertNotEquals(0, resp.getTableInfoList().size());
    assertNotEquals(0, resp.getNamespaceId().length());
  }

  @Test
  public void streamInfoShouldHaveNewTablesAfterCreation() throws Exception {
    LOG.info("Starting streamInfoShouldHaveNewTablesAfterCreation");

    // Create a stream.
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    String dbStreamId = testSubscriber.getDbStreamId();

    YBClient ybClient = testSubscriber.getSyncClient();

    GetDBStreamInfoResponse resp = ybClient.getDBStreamInfo(dbStreamId);

    // The response should have one table at this point in time.
    assertEquals(1, resp.getTableInfoList().size());

    // Create new tables.
    statement.execute("create table test_2 (a int primary key, b text);");
    statement.execute("create table test_3 (a serial primary key, b varchar(30));");

    // Wait for some time for the background thread to add these tables to cdc_state and related
    // metadata objects. This will also verify that the response eventually contains 3 tables.
    waitForTablesInDBStreamInfoResponse(ybClient, dbStreamId, 3);
  }

  @Test
  public void newTablesShouldBeAddedToMultipleStreams() throws Exception {
    LOG.info("Starting newTablesShouldBeAddedToMultipleStreams");

    int streamsToBeCreated = 5;

    List<String> streamIds = new ArrayList<>();

    // Create streams.
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

    for (int i = 0; i < streamsToBeCreated; ++i) {
      // The way CDCSubscriber object works is that it creates one stream and stores it within a
      // string variable, so everytime we create a stream, the object will be updated with the
      // newly created stream and before creating a new stream, we can simply add the stream to
      // our list.
      testSubscriber.createStream("proto");
      streamIds.add(testSubscriber.getDbStreamId());
    }

    // Get the YBClient object to perform operations.
    YBClient ybClient = testSubscriber.getSyncClient();

    // Verify that all the streams have one table each.
    for (int i = 0; i < streamsToBeCreated; ++i) {
      GetDBStreamInfoResponse resp = ybClient.getDBStreamInfo(streamIds.get(i));
      assertEquals(1, resp.getTableInfoList().size());
    }

    // Create new tables.
    statement.execute("create table test_2 (a int primary key, b text);");
    statement.execute("create table test_3 (a serial primary key, b varchar(30));");

    // Wait for some time for the tables to get added to the cdc_state table.
    TestUtils.waitForTTL(20000);

    // Verify that all the streams now contain newly added tables as well.
    for (int i = 0; i < streamsToBeCreated; ++i) {
      GetDBStreamInfoResponse resp = ybClient.getDBStreamInfo(streamIds.get(i));
      assertEquals(3, resp.getTableInfoList().size());
    }
  }

  @Test
  public void tableWithoutPrimaryKeyShouldNotBeAdded() throws Exception {
    LOG.info("Starting tableWithoutPrimaryKeyShouldNotBeAdded");

    // Create a stream.
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    String dbStreamId = testSubscriber.getDbStreamId();

    // Get the YBClient instance for the test.
    YBClient ybClient = testSubscriber.getSyncClient();

    GetDBStreamInfoResponse resp = ybClient.getDBStreamInfo(dbStreamId);

    // The response should have one table at this point in time.
    assertEquals(1, resp.getTableInfoList().size());

    // Create new tables.
    statement.execute("create table test_2 (a int, b text);");
    statement.execute("create table test_3 (a serial primary key, b varchar(30));");

    // Wait for some time for the background thread to work. There should only be 2 tables in the
    // response since one of the newly created tables does not have a primary key and thus it won't
    // be added to the stream
    waitForTablesInDBStreamInfoResponse(ybClient, dbStreamId, 2);

    GetDBStreamInfoResponse respAfterTableCreation = ybClient.getDBStreamInfo(dbStreamId);

    List<org.yb.master.MasterReplicationOuterClass.GetCDCDBStreamInfoResponsePB.TableInfo>
      tableInfoList = respAfterTableCreation.getTableInfoList();
    Set<String> tableIds = new HashSet<>();
    tableInfoList.forEach(tableInfo -> {
      tableIds.add(tableInfo.getTableId().toStringUtf8());
    });

    // Assert that there are only 2 tables
    assertEquals(2, tableIds.size());

    ListTablesResponse listTablesResponse = ybClient.getTablesList();

    // Table test_1 is supposed to be in the DB stream Info response.
    String tableOneUuid = getTableId(listTablesResponse, "test");
    assertTrue(tableIds.contains(tableOneUuid));

    // Table test_3 is another table which is supposed to be there in the response.
    String tableThreeUuid = getTableId(listTablesResponse, "test_3");
    assertTrue(tableIds.contains(tableThreeUuid));
  }

  @Test
  public void verifyAllTabletsOfNewlyCreatedTableAreAddedToCdcState() throws Exception {
    LOG.info("Starting verifyAllTabletsOfNewlyCreatedTableAreAddedToCdcState");

    // Create a stream.
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    String dbStreamId = testSubscriber.getDbStreamId();

    // Get the YBClient instance for the test.
    YBClient ybClient = testSubscriber.getSyncClient();

    GetDBStreamInfoResponse resp = ybClient.getDBStreamInfo(dbStreamId);

    // The response should have one table at this point in time.
    assertEquals(1, resp.getTableInfoList().size());

    // Create new table.
    statement.execute("create table test_2 (a int primary key, b text) split into 10 tablets;");

    // Wait for some time for the background thread to work and verify if the new tables have been
    // added.
    waitForTablesInDBStreamInfoResponse(ybClient, dbStreamId, 2);

    ListTablesResponse listTablesResponse = ybClient.getTablesList();

    String tableOneUuid = getTableId(listTablesResponse, "test");
    String tableTwoUuid = getTableId(listTablesResponse, "test_2");

    // Verify that all the tablets of table test (tableOne) are there in the cdc_state table.
    YBTable tableOne = ybClient.openTableByUUID(tableOneUuid);
    Set<String> tabletsInTableOne = ybClient.getTabletUUIDs(tableOne);
    GetTabletListToPollForCDCResponse resp1 =
      ybClient.getTabletListToPollForCdc(tableOne, dbStreamId, tableOneUuid);

    assertEquals(tabletsInTableOne.size(), resp1.getTabletCheckpointPairListSize());

    // Assert that we receive all the tablets in the response.
    for (TabletCheckpointPair pair : resp1.getTabletCheckpointPairList()) {
      assertTrue(
          tabletsInTableOne.contains(pair.getTabletLocations().getTabletId().toStringUtf8()));
    }

    // Verify that all the tablets of table test_2 (tableTwo) are there in the cdc_state table.
    YBTable tableTwo = ybClient.openTableByUUID(tableTwoUuid);
    Set<String> tabletsInTableTwo = ybClient.getTabletUUIDs(tableTwo);
    GetTabletListToPollForCDCResponse resp2 =
      ybClient.getTabletListToPollForCdc(tableTwo, dbStreamId, tableTwoUuid);

    assertEquals(tabletsInTableTwo.size(), resp2.getTabletCheckpointPairListSize());

    // Assert that we receive all the tablets in the response.
    for (TabletCheckpointPair pair : resp2.getTabletCheckpointPairList()) {
      assertTrue(
          tabletsInTableTwo.contains(pair.getTabletLocations().getTabletId().toStringUtf8()));
    }
  }

  /**
   * Helper function to get the UUID of the table from the given {@link ListTablesResponse} and
   * table name
   * @param resp the ListTablesResponse
   * @param tableName name of the table
   * @return UUID of the provided table
   */
  private String getTableId(ListTablesResponse resp, String tableName) {
    for (TableInfo tableInfo : resp.getTableInfoList()) {
      if (tableInfo.getName().equals(tableName)) {
        return tableInfo.getId().toStringUtf8();
      }
    }

    // Returning null means that there is no table with the specified name in the response.
    return null;
  }

  /**
   * Helper function to wait till the tables appear in the DB stream info.
   * @param ybClient the YBClient instance
   * @param streamId the DB stream ID to get the info for
   * @param tableCount number of tables which should appear in the response
   */
  private void waitForTablesInDBStreamInfoResponse(YBClient ybClient, String streamId,
                                                   int tableCount) throws Exception {
    Awaitility.await()
      .atMost(Duration.ofSeconds(20))
      .until(() -> {
        GetDBStreamInfoResponse resp = ybClient.getDBStreamInfo(streamId);

        return resp.getTableInfoList().size() == tableCount;
      });
  }
}
