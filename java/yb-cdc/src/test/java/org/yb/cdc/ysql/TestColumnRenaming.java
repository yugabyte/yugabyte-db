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

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;

import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestColumnRenaming extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestColumnRenaming.class);

  private static class ColumnName {
    public String col1;
    public String col2;
    public CdcService.RowMessage.Op opType;

    public ColumnName(String columnOneName, String columnTwoName,
                      CdcService.RowMessage.Op opType) {
      this.col1 = columnOneName;
      this.col2 = columnTwoName;
      this.opType = opType;
    }
  }

  private void verifyColNameInDDLRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ColumnName expectedRecord) {
    CdcService.CDCSDKSchemaPB schema = record.getRowMessage().getSchema();

    String actualCol1 = schema.getColumnInfo(0).getName();
    String actualCol2 = schema.getColumnInfo(1).getName();

    assertEquals(expectedRecord.col1, actualCol1);
    assertEquals(expectedRecord.col2, actualCol2);
  }

  private void verifyColNameInInsertRecord(CdcService.CDCSDKProtoRecordPB record,
                                           ColumnName expectedRecord) {
    String actualCol1 = record.getRowMessage().getNewTuple(0).getColumnName();
    String actualCol2 = record.getRowMessage().getNewTuple(1).getColumnName();

    assertEquals(expectedRecord.col1, actualCol1);
    assertEquals(expectedRecord.col2, actualCol2);
  }

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
  }

  @Test
  public void testRenaming() {
    try {
      assertFalse(connection.isClosed()); // Check if the connection is active.

      // This will result in a DDL record (the initial header DDL).
      assertFalse(statement.execute("create table test (a int primary key, b int);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      // Will result in insert records.
      assertEquals(1, statement.executeUpdate("insert into test values (1, 2);"));
      assertEquals(1, statement.executeUpdate("insert into test values (101, 202);"));

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      // Will send a DDL.
      assertFalse(statement.execute("alter table test rename column b to new_name_for_b;"));

      // Will result in insert records.
      assertEquals(1, statement.executeUpdate("insert into test values (22, 33);"));
      assertEquals(1, statement.executeUpdate("insert into test values (202, 303);"));

      testSubscriber.getResponseFromCDC(outputList, testSubscriber.getSubscriberCheckpoint());

      ColumnName[] expectedRecords = new ColumnName[] {
        new ColumnName("a", "b", Op.DDL), // initial header
        new ColumnName("a", "b", Op.INSERT), // (1, 2)
        new ColumnName("a", "b", Op.INSERT), // (101, 202)
        new ColumnName("a", "new_name_for_b", Op.DDL), // alter.
        // The above record will be added after the second call to get the changes is made.
        new ColumnName("a", "new_name_for_b", Op.INSERT), // (22. 33)
        new ColumnName("a", "new_name_for_b", Op.INSERT) // (202, 303)
      };


      int expectedRecordCount = 0;
      for (int i = 0; i < outputList.size(); ++i) {
        switch (outputList.get(i).getRowMessage().getOp()) {
          case DDL:
            verifyColNameInDDLRecord(outputList.get(i), expectedRecords[expectedRecordCount]);
            expectedRecordCount++;
            break;
          case INSERT:
            verifyColNameInInsertRecord(outputList.get(i), expectedRecords[expectedRecordCount]);
            expectedRecordCount++;
            break;
          case BEGIN:
          case COMMIT: break;
        }
      }

    } catch (Exception e) {
      LOG.error("Test to verify rename behaviour failed with exception", e);
      fail();
    }
  }
}
