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
import org.yb.cdc.common.*;
import org.yb.cdc.util.CDCSubscriber;

import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.*;

import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestDDL extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestDDL.class);

  @Before
  public void setUp() throws Exception {
    super.setUp();
    setServerFlag(getTserverHostAndPort(), CDC_POPULATE_SAFEPOINT_RECORD, "false");
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
  }

  @Test
  public void testDropColumn() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int, c int);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      // We are expecting 2 DDL records, the first one with 3 columns which will be added with the
      // following call. The second record will have 2 columns after a column is dropped.
      testSubscriber.getResponseFromCDC(outputList);

      assertFalse(statement.execute("alter table test drop column c;"));

      // The second DDL record would be added as a result of this call.
      testSubscriber.getResponseFromCDC(outputList, testSubscriber.getSubscriberCheckpoint());

      // This one would contain a schema with 3 columns.
      CdcService.CDCSDKProtoRecordPB ddl1 = outputList.get(0);

      // Here the schema would be only of 2 columns.
      CdcService.CDCSDKProtoRecordPB ddl2 = outputList.get(1);

      assertEquals(Op.DDL, ddl1.getRowMessage().getOp());
      assertEquals(3, ddl1.getRowMessage().getSchema().getColumnInfoCount());
      assertEquals("a", ddl1.getRowMessage().getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl1.getRowMessage().getSchema().getColumnInfo(1).getName());
      assertEquals("c", ddl1.getRowMessage().getSchema().getColumnInfo(2).getName());

      assertEquals(Op.DDL, ddl2.getRowMessage().getOp());
      assertEquals(2, ddl2.getRowMessage().getSchema().getColumnInfoCount());
      assertEquals("a", ddl2.getRowMessage().getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl2.getRowMessage().getSchema().getColumnInfo(1).getName());
    } catch (Exception e) {
      LOG.error("Test to verify dropping column while CDC is attached failed", e);
      fail();
    }
  }

  @Test
  public void testAddColumn() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      int dummyInsert = statement.executeUpdate("insert into test values (1, 2);");
      assertEquals(1, dummyInsert);

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      assertFalse(statement.execute("alter table test add column c int;"));

      int dummyInsert2 = statement.executeUpdate("insert into test values (2, 3, 4);");
      assertEquals(1, dummyInsert2);

      testSubscriber.getResponseFromCDC(outputList, testSubscriber.getSubscriberCheckpoint());

      // We expect 4 records in the outputList, the last two records will be added after the
      // second call to get the changes is made.
      // i.e. DDL <old>, INSERT <1, 2>, DDL <new>, INSERT <2, 3, 4>

      CdcService.CDCSDKProtoRecordPB ddl1 = outputList.get(0);
      assertEquals(Op.DDL, ddl1.getRowMessage().getOp());
      assertEquals(2, ddl1.getRowMessage().getSchema().getColumnInfoCount());
      assertEquals("a", ddl1.getRowMessage().getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl1.getRowMessage().getSchema().getColumnInfo(1).getName());

      ExpectedRecordYSQL<?> insert1 = new ExpectedRecordYSQL<>(1, 2, Op.INSERT);
      ExpectedRecordYSQL.checkRecord(outputList.get(2), insert1);

      CdcService.CDCSDKProtoRecordPB ddl2 = outputList.get(4);
      assertEquals(Op.DDL, ddl2.getRowMessage().getOp());
      assertEquals(3, ddl2.getRowMessage().getSchema().getColumnInfoCount());
      assertEquals("a", ddl2.getRowMessage().getSchema().getColumnInfo(0).getName());
      assertEquals("b", ddl2.getRowMessage().getSchema().getColumnInfo(1).getName());
      assertEquals("c", ddl2.getRowMessage().getSchema().getColumnInfo(2).getName());

      ExpectedRecord3Proto insert2 = new ExpectedRecord3Proto(2, 3, 4, Op.INSERT);
      ExpectedRecord3Proto.checkRecord(outputList.get(6), insert2);
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
      testSubscriber.createStream("proto");

      assertEquals(1, statement.executeUpdate("insert into test values (1);"));
      assertEquals(1, statement.executeUpdate("insert into test values (2, 3);"));
      assertEquals(1, statement.executeUpdate("insert into test values (3);"));

      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(-1, -1, Op.BEGIN),
        new ExpectedRecordYSQL<>(1, 404, Op.INSERT),
        new ExpectedRecordYSQL<>(-1, -1, Op.COMMIT),
        new ExpectedRecordYSQL(-1, -1, Op.BEGIN),
        new ExpectedRecordYSQL<>(2, 3, Op.INSERT),
        new ExpectedRecordYSQL<>(-1, -1, Op.COMMIT),
        new ExpectedRecordYSQL(-1, -1, Op.BEGIN),
        new ExpectedRecordYSQL<>(3, 404, Op.INSERT),
        new ExpectedRecordYSQL<>(-1, -1, Op.COMMIT)
      };

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      // Ignore DDLs while comparing now.
      int ind = 0;
      for (int i = 0; i < expectedRecords.length; ++i) {
        if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
          continue;
        }

        ExpectedRecordYSQL.checkRecord(outputList.get(i), expectedRecords[ind++]);
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
      testSubscriber.createStream("proto");

      int dummyInsert = statement.executeUpdate("insert into test values (1);");
      assertEquals(1, dummyInsert);

      assertFalse(statement.execute("alter table test alter column b set default 505;"));

      int dummyInsert2 = statement.executeUpdate("insert into test values (2);");
      assertEquals(1, dummyInsert2);

      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, 404, Op.INSERT),
        new ExpectedRecordYSQL<>(2, 505, Op.INSERT)
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
      LOG.error("Test to verify changing default value of columns failed", e);
      fail();
    }
  }

  @Test
  public void testRenameTable() {
    try {
      final String oldTableName = "test";
      assertFalse(statement.execute(String.format("create table %s (a int primary key, b int);",
                                                  oldTableName)));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      // We expect 2 DDL records, the first one would have the old table name while the second
      // one will have the new table name. The record for the first DDL record would be added
      // in the following call to get the changes.
      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      final String newTableName = "test_new";
      assertFalse(statement.execute(String.format("alter table test rename to %s;", newTableName)));

      // The second DDL record would be added in this call.
      testSubscriber.getResponseFromCDC(outputList, testSubscriber.getSubscriberCheckpoint());

      CdcService.CDCSDKProtoRecordPB ddl1 = outputList.get(0);
      assertEquals(oldTableName, ddl1.getRowMessage().getTable());

      CdcService.CDCSDKProtoRecordPB ddl2 = outputList.get(1);
      assertEquals(newTableName, ddl2.getRowMessage().getTable());
    } catch (Exception e) {
      LOG.error("Test to verify table rename while CDC is still attached failed", e);
      fail();
    }
  }
}
