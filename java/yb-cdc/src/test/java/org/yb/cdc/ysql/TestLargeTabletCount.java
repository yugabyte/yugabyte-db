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

package org.yb.cdc.ysql;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecordYSQL;

import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.*;

import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestLargeTabletCount extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestLargeTabletCount.class);

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int) split into 12 tablets;");
  }

  @Test
  public void testLargeNumberOfTablets() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.setNumberOfTablets(12);
      testSubscriber.createStream("proto");

      int ins1 = statement.executeUpdate("insert into test values (1, 2);");
      int ins2 = statement.executeUpdate("insert into test values (22, 33);");

      assertEquals(1, ins1);
      assertEquals(1, ins2);

      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, 2, Op.INSERT),
        new ExpectedRecordYSQL<>(22, 33, Op.INSERT)
      };

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      int idx = 0;
      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getRowMessage().getOp() == Op.INSERT) {
          ExpectedRecordYSQL.checkRecord(outputList.get(i), expectedRecords[idx++]);
        }
      }
    } catch (Exception e) {
      LOG.error("Test to check for a large number of tablets failed", e);
      fail();
    }
  }

}
