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
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.client.*;
import org.yb.YBTestRunner;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public class TestGetChanges extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestGetChanges.class);

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c numeric);");
  }

  @Ignore("this test passes if you would provide 127.0.0.1:7100 to CDCSubscriber()")
  @Test
  public void testGettingChangesWithNegativeIndex() {
    try {
      // This test passes if you would provide 127.0.0.1:7100 to CDCSubscriber()
      // The behaviour is flaky, so the test is disabled.
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      YBClient myClient = testSubscriber.getSyncClient();
      assertNotNull(myClient);

      // The below table would only be for the table yugabyte.test
      YBTable table = testSubscriber.getTable();
      assertNotNull(table);

      CreateCDCStreamResponse resp =
        myClient.createCDCStream(table, "yugabyte", "proto", "explicit");

      assertNotNull(resp);

      // Dummy insert.
      int res = statement.executeUpdate("insert into test values (1, 2, 20.34);");
      assertEquals(1, res);

      String dbStreamId = resp.getStreamId();

      List<LocatedTablet> locatedTablets = table.getTabletsLocations(30000);
      List<String> tabletIds = new ArrayList<>();

      for (LocatedTablet tablet : locatedTablets) {
        String tabletId = new String(tablet.getTabletId());
        tabletIds.add(tabletId);
      }

      boolean exceptionThrown = false;
      for (String tabletId : tabletIds) {
        // An exception would be thrown for an index less than 0.
        try {
          GetChangesResponse changesResponse =
            myClient.getChangesCDCSDK(
              table, dbStreamId, tabletId, 0, -1, new byte[]{}, 0, 0L, false);
        } catch (Exception e) {
          exceptionThrown = true;
          break;
        }
      }
      assertTrue(exceptionThrown);
    } catch (Exception e) {
      LOG.error("Test to verify failure on requesting changes from a negative index failed", e);
      fail();
    }
  }
}
