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
import org.yb.cdc.util.TestUtils;
import org.yb.client.GetTabletListToPollForCDCResponse;
import org.yb.client.YBClient;
import org.yb.util.YBTestRunnerNonTsanOnly;

import static org.yb.AssertionWrappers.*;

import java.time.Duration;
import java.util.Set;
import java.util.Map;

import org.awaitility.Awaitility;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
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

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("enable_tablet_split_of_cdcsdk_streamed_tables", "true");
    return flagMap;
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
    TestUtils.waitFor(60 /* seconds to wait */);

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
