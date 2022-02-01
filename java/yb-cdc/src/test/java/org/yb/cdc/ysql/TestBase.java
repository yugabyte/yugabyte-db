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

import org.apache.log4j.Logger;

import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.common.ExpectedRecordYSQLGeneric;
import org.yb.cdc.util.TestUtils;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestBase extends CDCBaseClass {
  private Logger LOG = Logger.getLogger(TestBase.class);

  private void executeScriptAssertRecords(ExpectedRecordYSQLGeneric<?>[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream();

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // ignoring the DDLs if any
      if (outputList.get(i).getOperation() == OperationType.DDL) {
        continue;
      }
      ExpectedRecordYSQLGeneric.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // NOTE: processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int);");
  }

  // Check if the JDBC connection is working fine
  @Test
  public void testJDBCConnection() throws SQLException {
    assertFalse(connection.isClosed());
  }

  // Begin transaction, insert a row, commit
  // Expected records: 2 (INSERT, WRITE)
  @Test
  public void testInsertingSingleRow() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[]{
        new ExpectedRecordYSQLGeneric<>("1", "2", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_one_row.sql");
    } catch (Exception e) {
      LOG.error("Test to insert single row failed with exception: ", e);
      fail();
    }
  }

  // Insert a row outside transaction, try to update a row which doesn't exist
  // Expected records: 1 (INSERT)
  @Test
  public void testUpdateNonExistingRow() {
    try {
      assertFalse(statement.execute("BEGIN;"));
      assertEquals(0, statement.executeUpdate("UPDATE test SET a = 32 WHERE b = 5;"));
      assertFalse(statement.execute("COMMIT;"));

      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[]{
        new ExpectedRecordYSQLGeneric<>("1", "2", OperationType.INSERT)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_row_outside_txn.sql");
    } catch (Exception e) {
      LOG.error("Test to update non-existing row failed with exception: ", e);
      fail();
    }
  }

  // Setup condition: Table "test" is currently empty
  // Delete a row which doesn't exist
  // Expected records: 0
  @Test
  public void testDeleteNonExistingRow() {
    try {
      assertFalse(statement.execute("BEGIN;"));
      assertEquals(0, statement.executeUpdate("DELETE FROM test WHERE b = 4;"));
      assertFalse(statement.execute("COMMIT;"));

      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[]{
      };

      executeScriptAssertRecords(expectedRecords, "");
    } catch (Exception e) {
      LOG.error("Test to delete a non-existing row failed with exception: ", e);
      fail();
    }
  }
}
