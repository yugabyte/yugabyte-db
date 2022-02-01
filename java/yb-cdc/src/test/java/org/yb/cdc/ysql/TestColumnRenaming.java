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

import org.apache.log4j.Logger;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecordColName;
import org.yb.cdc.util.CDCSubscriber;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;

import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;

@RunWith(value = YBTestRunner.class)
public class TestColumnRenaming extends CDCBaseClass {
  private final static Logger LOG = Logger.getLogger(TestColumnRenaming.class);

  private void verifyColNameInDDLRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecordColName expectedRecord) {
    CdcService.CDCSDKSchemaPB schema = record.getSchema();

    String actualCol1 = schema.getColumnInfo(0).getName();
    String actualCol2 = schema.getColumnInfo(1).getName();

    assertEquals(expectedRecord.col1, actualCol1);
    assertEquals(expectedRecord.col2, actualCol2);
  }

  private void verifyColNameInInsertRecord(CdcService.CDCSDKRecordPB record,
                                           ExpectedRecordColName expectedRecord) {
    String actualCol1 = record.getKey(0).getKey().toStringUtf8();
    String actualCol2 = record.getChanges(0).getKey().toStringUtf8();

    assertEquals(expectedRecord.col1, actualCol1);
    assertEquals(expectedRecord.col2, actualCol2);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
  }

  @Test
  public void testRenaming() {
    try {
      assertFalse(connection.isClosed()); // check if the connection is active

      // this will result in a DDL record (the initial header DDL)
      assertFalse(statement.execute("create table test (a int primary key, b int);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream();

      // will result in insert records
      assertEquals(1, statement.executeUpdate("insert into test values (1, 2);"));
      assertEquals(1, statement.executeUpdate("insert into test values (101, 202);"));

      // will send a DDL
      assertFalse(statement.execute("alter table test rename column b to new_name_for_b;"));

      // will result in insert records
      assertEquals(1, statement.executeUpdate("insert into test values (22, 33);"));
      assertEquals(1, statement.executeUpdate("insert into test values (202, 303);"));

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      ExpectedRecordColName[] expectedRecords = new ExpectedRecordColName[] {
        new ExpectedRecordColName("a", "b", OperationType.DDL), // initial header
        new ExpectedRecordColName("a", "b", OperationType.INSERT), // (1, 2)
        new ExpectedRecordColName("a", "b", OperationType.INSERT), // (101, 202)
        new ExpectedRecordColName("a", "new_name_for_b", OperationType.DDL), // alter
        new ExpectedRecordColName("a", "new_name_for_b", OperationType.INSERT), // (22. 33)
        new ExpectedRecordColName("a", "new_name_for_b", OperationType.INSERT) // (202, 303)
      };


      for (int i = 0; i < outputList.size(); ++i) {
        switch (outputList.get(i).getOperation()) {
          case DDL:
            verifyColNameInDDLRecord(outputList.get(i), expectedRecords[i]);
            break;
          case INSERT:
            verifyColNameInInsertRecord(outputList.get(i), expectedRecords[i]);
            break;
        }
      }

    } catch (Exception e) {
      LOG.error("Test to verify rename behaviour failed with exception", e);
      fail();
    }
  }
}
