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

import static org.yb.AssertionWrappers.*;

import org.junit.Before;
import org.junit.Test;


import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.common.ExpectedRecordYSQL;
import org.yb.cdc.util.CDCTestUtils;
import org.yb.YBTestRunner;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestBase extends CDCBaseClass {
  private Logger LOG = LoggerFactory.getLogger(TestBase.class);

  private void executeScriptAssertRecords(ExpectedRecordYSQL<?>[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    if (!sqlScript.isEmpty()) {
      CDCTestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // Ignoring the DDLs if any.
      if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
        ExpectedRecordYSQL.checkRecord(outputList.get(i),
                                       new ExpectedRecordYSQL<>(-1, "", Op.DDL));
        continue;
      }

      ExpectedRecordYSQL.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // NOTE: processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    super.setUp();
    setServerFlag(getTserverHostAndPort(), CDC_POPULATE_SAFEPOINT_RECORD, "false");
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int);");
  }

  // Check if the JDBC connection is working fine.
  @Test
  public void testJDBCConnection() throws SQLException {
    assertFalse(connection.isClosed());
  }

  // Begin transaction, insert a row, commit.
  // Expected records: 2 (INSERT, WRITE)
  @Test
  public void testInsertingSingleRow() {
    try {
      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[]{
        new ExpectedRecordYSQL<>(-1, "", Op.BEGIN),
        new ExpectedRecordYSQL<>(1, 2, Op.INSERT),
        new ExpectedRecordYSQL<>(-1, "", Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_one_row.sql");
    } catch (Exception e) {
      LOG.error("Test to insert single row failed with exception: ", e);
      fail();
    }
  }

  // Insert a row outside transaction, try to update a row which doesn't exist.
  // Expected records: 1 (INSERT)
  @Test
  public void testUpdateNonExistingRow() {
    try {
      assertFalse(statement.execute("BEGIN;"));
      assertEquals(0, statement.executeUpdate("UPDATE test SET a = 32 WHERE b = 5;"));
      assertFalse(statement.execute("COMMIT;"));

      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[]{
        new ExpectedRecordYSQL<>(-1, "", Op.BEGIN),
        new ExpectedRecordYSQL<>(1, 2, Op.INSERT),
        new ExpectedRecordYSQL<>(-1, "", Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_row_outside_txn.sql");
    } catch (Exception e) {
      LOG.error("Test to update non-existing row failed with exception: ", e);
      fail();
    }
  }

  // Setup condition: Table "test" is currently empty.
  // Delete a row which doesn't exist.
  // Expected records: 0
  @Test
  public void testDeleteNonExistingRow() {
    try {
      assertFalse(statement.execute("BEGIN;"));
      assertEquals(0, statement.executeUpdate("DELETE FROM test WHERE b = 4;"));
      assertFalse(statement.execute("COMMIT;"));

      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[]{
      };

      executeScriptAssertRecords(expectedRecords, "");
    } catch (Exception e) {
      LOG.error("Test to delete a non-existing row failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testConflictsWhileInsertion() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      assertEquals(1, statement.executeUpdate("INSERT INTO test VALUES (1, 2);"));
      assertFalse(statement.execute("BEGIN;"));
      assertEquals(1, statement.executeUpdate("INSERT INTO test VALUES (1, 3) ON CONFLICT (a) " +
        "DO UPDATE SET b = EXCLUDED.b;"));
      assertFalse(statement.execute("COMMIT;"));

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      ExpectedRecordYSQL<?>[] expectedRecords = new ExpectedRecordYSQL[]{
        new ExpectedRecordYSQL<>(-1, -1, Op.BEGIN),
        new ExpectedRecordYSQL<>(1, 2, Op.INSERT),
        new ExpectedRecordYSQL<>(-1, -1, Op.COMMIT),
        new ExpectedRecordYSQL<>(-1, -1, Op.BEGIN),
        new ExpectedRecordYSQL<>(1, 3, Op.UPDATE),
        new ExpectedRecordYSQL<>(-1, -1, Op.COMMIT)
      };

      int expRecordIndex = 0;
      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
          ExpectedRecordYSQL.checkRecord(outputList.get(i),
            new ExpectedRecordYSQL<>(-1, -1, Op.DDL));
          continue;
        }

        ExpectedRecordYSQL.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      }
    } catch (Exception e) {
      LOG.error("Test failure for conflicts while insertion");
      fail();
    }
  }
}
