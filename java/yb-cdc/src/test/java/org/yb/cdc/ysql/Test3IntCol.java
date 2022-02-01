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
import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.common.ExpectedRecord3Col;
import org.yb.cdc.util.TestUtils;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;

@RunWith(value = YBTestRunner.class)
public class Test3IntCol extends CDCBaseClass {
  private Logger LOG = Logger.getLogger(Test3IntCol.class);

  private void executeScriptAssertRecords(ExpectedRecord3Col[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream();

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
    setServerFlag(getTserverHostAndPort(), "cdc_max_stream_intent_records", "25");
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // ignoring the DDLs if any
      if (outputList.get(i).getOperation() == OperationType.DDL) {
        continue;
      }
      ExpectedRecord3Col.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // NOTE: processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  // Insert rows in batch outside an explicit transaction
  // Expected records: 5 (4_INSERT, WRITE)
  // WRITE op is generated as this batch insert would be treated as multi shard transaction
  @Test
  public void testInsertionInBatchSingleShard() {
    try {
      ExpectedRecord3Col[] expectedRecords = {
        new ExpectedRecord3Col("7", "8", "9", OperationType.INSERT),
        new ExpectedRecord3Col("4", "5", "6", OperationType.INSERT),
        new ExpectedRecord3Col("34", "35", "45", OperationType.INSERT),
        new ExpectedRecord3Col("1000", "1001", "1004", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_in_batch_outside_txn.sql");
    } catch (Exception e) {
      LOG.error("Test to insert in batch outside BTET block failed with exception: ", e);
      fail();
    }
  }


  // Begin transaction, insert rows in batch, commit
  // Expected records: 5 (4_INSERT, WRITE)
  @Test
  public void testInsertionInBatch() {
    try {
      // Expect 5 records, 4 INSERT + 1 WRITE
      ExpectedRecord3Col[] expectedRecords = {
        new ExpectedRecord3Col("7", "8", "9", OperationType.INSERT),
        new ExpectedRecord3Col("4", "5", "6", OperationType.INSERT),
        new ExpectedRecord3Col("34", "35", "45", OperationType.INSERT),
        new ExpectedRecord3Col("1000", "1001", "1004", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_in_batch.sql");
    } catch (Exception e) {
      LOG.error("Test to insert in batch failed with exception: ", e);
      fail();
    }
  }

  // Begin transaction, perform multiple queries, rollback
  // Expected records: 0
  @Test
  public void testRollbackTransaction() throws Exception {
    try {
      ExpectedRecord3Col[] expectedRecords = {
      };

      executeScriptAssertRecords(expectedRecords, "cdc_long_txn_rollback.sql");
    } catch (Exception e) {
      LOG.error("Test to rollback a long transaction failed with exception: ", e);
      fail();
    }
  }

  // Execute long script containing multiple operations of all kinds
  // Expected records: 50 (see script for more details)
  @Test
  public void testLongRunningScript() {
    try {
      ExpectedRecord3Col[] expectedRecords = {
        new ExpectedRecord3Col("1", "2", "3", OperationType.INSERT),
        new ExpectedRecord3Col("1", "2", "4", OperationType.UPDATE),
        new ExpectedRecord3Col("1", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("2", "2", "4", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("7", "8", "9", OperationType.INSERT),
        new ExpectedRecord3Col("7", "17", "9", OperationType.UPDATE),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("6", "7", "8", OperationType.INSERT),
        new ExpectedRecord3Col("6", "7", "17", OperationType.UPDATE),
        new ExpectedRecord3Col("6", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("15", "7", "17", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("11", "12", "13", OperationType.INSERT),
        new ExpectedRecord3Col("11", "13", "13", OperationType.UPDATE),
        new ExpectedRecord3Col("11", "13", "14", OperationType.UPDATE),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("12", "112", "113", OperationType.INSERT),
        new ExpectedRecord3Col("12", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("13", "113", "114", OperationType.INSERT),
        new ExpectedRecord3Col("13", "113", "115", OperationType.UPDATE),
        new ExpectedRecord3Col("13", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("14", "113", "115", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("17", "114", "115", OperationType.INSERT),
        new ExpectedRecord3Col("17", "114", "116", OperationType.UPDATE),
        new ExpectedRecord3Col("17", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("18", "114", "116", OperationType.INSERT),
        new ExpectedRecord3Col("18", "115", "116", OperationType.UPDATE),
        new ExpectedRecord3Col("18", "115", "117", OperationType.UPDATE),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("18", "116", "117", OperationType.UPDATE),
        new ExpectedRecord3Col("18", "116", "118", OperationType.UPDATE),
        new ExpectedRecord3Col("20", "21", "22", OperationType.INSERT),
        new ExpectedRecord3Col("20", "22", "22", OperationType.UPDATE),
        new ExpectedRecord3Col("20", "22", "23", OperationType.UPDATE),
        new ExpectedRecord3Col("20", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("21", "22", "23", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("21", "23", "23", OperationType.UPDATE),
        new ExpectedRecord3Col("21", "23", "24", OperationType.UPDATE),
        new ExpectedRecord3Col("21", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("21", "23", "24", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("-1", "-2", "-3", OperationType.INSERT),
        new ExpectedRecord3Col("-4", "-5", "-6", OperationType.INSERT),
        new ExpectedRecord3Col("-11", "-12", "-13", OperationType.INSERT),
        new ExpectedRecord3Col("-1", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("404", "405", "406", OperationType.INSERT),
        new ExpectedRecord3Col("104", "204", "304", OperationType.INSERT),
        new ExpectedRecord3Col("", "", "", OperationType.WRITE),
        new ExpectedRecord3Col("41", "43", "44", OperationType.INSERT),
        new ExpectedRecord3Col("41", "44", "45", OperationType.UPDATE),
        new ExpectedRecord3Col("41", "", "", OperationType.DELETE),
        new ExpectedRecord3Col("41", "43", "44", OperationType.INSERT),
        new ExpectedRecord3Col("41", "44", "45", OperationType.UPDATE)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_long_script.sql");
    } catch (Exception e) {
      LOG.error("Test to execute a long transaction failed with exception: ", e);
      fail();
    }
  }
}
