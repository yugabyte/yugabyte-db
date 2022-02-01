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
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecord3Col;
import org.yb.cdc.common.ExpectedRecordYSQLGeneric;
import org.yb.cdc.util.CDCSubscriber;

import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;
import static org.yb.AssertionWrappers.*;

import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestDDL extends CDCBaseClass {
  private final static Logger LOG = Logger.getLogger(TestDDL.class);

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
  }

  @Test
  public void testDropColumn() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int, c int);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream();

      assertFalse(statement.execute("alter table test drop column c;"));

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      // we will get 2 DDLs, one that is attached by default from a getChangesCDCSDK() call
      // the other would be the one because of alter table command
      testSubscriber.getResponseFromCDC(outputList);

      // this one would contain a schema with 3 columns
      CdcService.CDCSDKRecordPB ddl1 = outputList.get(0);
      // here the schema would be only of 2 columns
      CdcService.CDCSDKRecordPB ddl2 = outputList.get(1);

      assertEquals(OperationType.DDL, ddl1.getOperation());
      assertEquals(3, ddl1.getSchema().getColumnInfoCount());
      assertEquals("a", ddl1.getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl1.getSchema().getColumnInfo(1).getName());
      assertEquals("c", ddl1.getSchema().getColumnInfo(2).getName());

      assertEquals(OperationType.DDL, ddl2.getOperation());
      assertEquals(2, ddl2.getSchema().getColumnInfoCount());
      assertEquals("a", ddl2.getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl2.getSchema().getColumnInfo(1).getName());
    } catch (Exception e) {
      LOG.error("Test to verify dropping column while CDC is attached failed", e);
      fail();
    }
  }

  @Test
  public void testAddColumn() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int);"));

      int dummyInsert = statement.executeUpdate("insert into test values (1, 2);");
      assertEquals(1, dummyInsert);

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream();

      assertFalse(statement.execute("alter table test add column c int;"));

      int dummyInsert2 = statement.executeUpdate("insert into test values (2, 3, 4);");
      assertEquals(1, dummyInsert2);

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      // we will get 4 records in outputList
      // i.e. INSERT <1, 2>, DDL <old>, DDL <new>, INSERT <2, 3, 4>
      testSubscriber.getResponseFromCDC(outputList);

      ExpectedRecordYSQLGeneric<?> insert1 =
          new ExpectedRecordYSQLGeneric<>("1", "2", OperationType.INSERT);
      ExpectedRecordYSQLGeneric.checkRecord(outputList.get(0), insert1);

      CdcService.CDCSDKRecordPB ddl1 = outputList.get(1);
      assertEquals(OperationType.DDL, ddl1.getOperation());
      assertEquals(2, ddl1.getSchema().getColumnInfoCount());
      assertEquals("a", ddl1.getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl1.getSchema().getColumnInfo(1).getName());

      CdcService.CDCSDKRecordPB ddl2 = outputList.get(2);
      assertEquals(OperationType.DDL, ddl2.getOperation());
      assertEquals(3, ddl2.getSchema().getColumnInfoCount());
      assertEquals("a", ddl2.getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl2.getSchema().getColumnInfo(1).getName());
      assertEquals("c", ddl2.getSchema().getColumnInfo(2).getName());

      ExpectedRecord3Col insert2 = new ExpectedRecord3Col("2", "3", "4", OperationType.INSERT);
      ExpectedRecord3Col.checkRecord(outputList.get(3), insert2);
    } catch (Exception e) {
      LOG.error("Test to verify adding a column while CDC still attached failed", e);
      fail();
    }
  }

  @Test
  public void testColumnWithDefaultValue() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int default 404);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream();

      assertEquals(1, statement.executeUpdate("insert into test values (1);"));
      assertEquals(1, statement.executeUpdate("insert into test values (2, 3);"));
      assertEquals(1, statement.executeUpdate("insert into test values (3);"));

      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "404", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "3", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "404", OperationType.INSERT)
      };

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      // ignore DDLs while comparing now
      int ind = 0;
      for (int i = 0; i < expectedRecords.length; ++i) {
        if (outputList.get(i).getOperation() == OperationType.DDL) {
          continue;
        }

        ExpectedRecordYSQLGeneric.checkRecord(outputList.get(i), expectedRecords[ind++]);
      }
    } catch (Exception e) {
      LOG.error("Test to verify column with default value failed", e);
      fail();
    }
  }

  @Test
  public void testChangeDefaultValue() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int default 404);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream();

      int dummyInsert = statement.executeUpdate("insert into test values (1);");
      assertEquals(1, dummyInsert);

      assertFalse(statement.execute("alter table test alter column b set default 505;"));

      int dummyInsert2 = statement.executeUpdate("insert into test values (2);");
      assertEquals(1, dummyInsert2);

      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "404", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "505", OperationType.INSERT)
      };

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      int idx = 0;
      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getOperation() == OperationType.INSERT) {
          ExpectedRecordYSQLGeneric.checkRecord(outputList.get(i), expectedRecords[idx++]);
        }
      }
    } catch (Exception e) {
      LOG.error("Test to verify changing default value of columns failed", e);
      fail();
    }
  }

  @Test
  public void testRenameTable() {
    try {
      String oldTableName = "test";
      assertFalse(statement.execute(String.format("create table %s (a int primary key, b int);",
                                                  oldTableName)));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream();

      String newTableName = "test_new";
      assertFalse(statement.execute(String.format("alter table test rename to %s;", newTableName)));

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      // we will get 2 DDLs, one with old table name and the other with new one
      testSubscriber.getResponseFromCDC(outputList);

      CdcService.CDCSDKRecordPB ddl1 = outputList.get(0);
      assertEquals(oldTableName, ddl1.getNewTableName());

      CdcService.CDCSDKRecordPB ddl2 = outputList.get(1);
      assertEquals(newTableName, ddl2.getNewTableName());
    } catch (Exception e) {
      LOG.error("Test to verify table rename while CDC is still attached failed", e);
      fail();
    }
  }
}
