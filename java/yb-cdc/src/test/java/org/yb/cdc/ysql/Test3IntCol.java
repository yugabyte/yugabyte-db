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
import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecord3Proto;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.util.CDCTestUtils;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class Test3IntCol extends CDCBaseClass {
  private Logger LOG = LoggerFactory.getLogger(Test3IntCol.class);

  private void executeScriptAssertRecords(ExpectedRecord3Proto[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    if (!sqlScript.isEmpty()) {
      CDCTestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    setServerFlag(getTserverHostAndPort(), "cdc_max_stream_intent_records", "25");
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // Ignoring the DDLs if any.
      if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
        ExpectedRecord3Proto.checkRecord(outputList.get(i),
                                       new ExpectedRecord3Proto(-1, -1, -1, Op.DDL));
        continue;
      }

      ExpectedRecord3Proto.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }

    // NOTE: processedRecords will be the same as expRecordIndex.
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    super.setUp();
    setServerFlag(getTserverHostAndPort(), CDC_POPULATE_SAFEPOINT_RECORD, "false");
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  // Insert rows in batch outside an explicit transaction.
  // Expected records: 5 (4_INSERT, WRITE)
  // WRITE op is generated as this batch insert would be treated as multi shard transaction.
  @Test
  public void testInsertionInBatchSingleShard() {
    try {
      ExpectedRecord3Proto[] expectedRecords = {
        new ExpectedRecord3Proto(-1, -1, -1, Op.BEGIN),
        new ExpectedRecord3Proto(7, 8, 9, Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.COMMIT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.BEGIN),
        new ExpectedRecord3Proto(4, 5, 6, Op.INSERT),
        new ExpectedRecord3Proto(34, 35, 45, Op.INSERT),
        new ExpectedRecord3Proto(1000, 1001, 1004, Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_in_batch_outside_txn.sql");
    } catch (Exception e) {
      LOG.error("Test to insert in batch outside BTET block failed with exception: ", e);
      fail();
    }
  }

  // Begin transaction, insert rows in batch, commit.
  // Expected records: 5 (4_INSERT, WRITE)
  @Test
  public void testInsertionInBatch() {
    try {
      // Expect 5 records, 4 INSERT + 1 WRITE
      ExpectedRecord3Proto[] expectedRecords = {
        new ExpectedRecord3Proto(-1, -1, -1, Op.BEGIN),
        new ExpectedRecord3Proto(7, 8, 9, Op.INSERT),
        new ExpectedRecord3Proto(4, 5, 6, Op.INSERT),
        new ExpectedRecord3Proto(34, 35, 45, Op.INSERT),
        new ExpectedRecord3Proto(1000, 1001, 1004, Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_insert_in_batch.sql");
    } catch (Exception e) {
      LOG.error("Test to insert in batch failed with exception: ", e);
      fail();
    }
  }

  // Begin transaction, perform multiple queries, rollback.
  // Expected records: 0
  @Test
  public void testRollbackTransaction() throws Exception {
    try {
      ExpectedRecord3Proto[] expectedRecords = {
      };

      executeScriptAssertRecords(expectedRecords, "cdc_long_txn_rollback.sql");
    } catch (Exception e) {
      LOG.error("Test to rollback a long transaction failed with exception: ", e);
      fail();
    }
  }

  // Execute long script containing multiple operations of all kinds.
  // Expected records: 50 (see script for more details)
  @Test
  public void testLongRunningScript() {
    try {
      ExpectedRecord3Proto[] expectedRecords = {
        // insert into test values (1,2,3);
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(1, 2, 3, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // update test set c=c+1 where a=1;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(1, 2, 4, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // UPDATE PK: update test set a=a+1 where a=1;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(1, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(2, 2, 4, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // NO RECORDS FOR:
        // begin;
        // insert into test values(7,8,9);
        // rollback;

        // begin;
        // insert into test values(7,8,9);
        // update test set b=b+9 where a=7;
        // end transaction;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(7, 8, 9, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(7, 17, 9, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // insert into test values(6,7,8);
        // update test set c=c+9 where a=6;
        // update test set a=a+9 where a=6;
        // commit;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(6, 7, 8, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(6, 7, 17, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(6, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(15, 7, 17, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // NO RECORDS FOR: update test set b=b+1, c=c+1 where a=1;

        // NO RECORDS FOR:
        // begin;
        // update test set b=b+1, c=c+1 where a=1;
        // end;

        // begin;
        // insert into test values(11,12,13);
        // update test set b=b+1, c=c+1 where a=11;
        // end;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(11, 12, 13, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(11, 13, 14, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // insert into test values(12,112,113);
        // delete from test where a=12;
        // end;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(12, 112, 113, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(12, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // insert into test values(13,113,114);
        // update test set c=c+1 where a=13;
        // update test set a=a+1 where a=13;
        // end;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(13, 113, 114, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(13, 113, 115, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(13, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(14, 113, 115, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // insert into test values(17,114,115);
        // update test set c=c+1 where a=17;
        // update test set a=a+1 where a=17;
        // update test set b=b+1, c=c+1 where a=18;
        // end;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(17, 114, 115, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(17, 114, 116, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(17, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(18, 114, 116, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(18, 115, 117, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // update test set b=b+1, c=c+1 where a=18;
        // insert into test values(20,21,22);
        // update test set b=b+1, c=c+1 where a=20;
        // update test set a=a+1 where a=20;
        // end;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(18, 116, 118, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(20, 21, 22, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(20, 22, 23, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(20, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(21, 22, 23, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // update test set b=b+1, c=c+1 where a=21;
        // delete from test where a=21;
        // insert into test values(21,23,24);
        // end;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(21, 23, 24, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(21, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(21, 23, 24, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // begin;
        // insert into test values (-1,-2,-3), (-4,-5,-6);
        // insert into test values (-11, -12, -13);
        // delete from test where a=-1;
        // commit;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(-1, -2, -3, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-4, -5, -6, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-11, -12, -13, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // insert into test values (404, 405, 406), (104, 204, 304);
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(404, 405, 406, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(104, 204, 304, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // insert into test values(41,43,44);
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(41, 43, 44, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // update test set b=b+1, c=c+1 where a=41;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(41, 44, 45, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // delete from test where a=41;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(41, 0, 0, CdcService.RowMessage.Op.DELETE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // insert into test values(41,43,44);
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(41, 43, 44, CdcService.RowMessage.Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT),

        // update test set b=b+1, c=c+1 where a=41;
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.BEGIN),
        new ExpectedRecord3Proto(41, 44, 45, CdcService.RowMessage.Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, CdcService.RowMessage.Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "cdc_long_script.sql");

    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour with proto records failed", e);
      fail();
    }
  }
}
