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
import org.yb.cdc.common.ExpectedRecordCPKProto;
import org.yb.cdc.util.TestUtils;
import org.yb.YBTestRunner;

import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestCompoundKey extends CDCBaseClass {
  private Logger LOG = LoggerFactory.getLogger(TestCompoundKey.class);

  private void executeScriptAssertRecords(ExpectedRecordCPKProto[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    setServerFlag(getTserverHostAndPort(), CDC_INTENT_SIZE_GFLAG, "25");
    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // Ignoring the DDLs.
      if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
        continue;
      }

      ExpectedRecordCPKProto.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
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
    statement.execute("create table test (a int, b int, c int, d int, primary key(a, b));");
  }

  // Expected records: 1 (INSERT)
  @Test
  public void testInsert() {
    try {
      ExpectedRecordCPKProto[] expectedRecords = {
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 3, 4, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_compound_key.sql");
    } catch (Exception e) {
      LOG.error("Test to insert compound key failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testInsertInBatch() {
    try {
      ExpectedRecordCPKProto[] expectedRecords = {
        // insert into test values (1, 2, 3, 4), (5, 6, 7, 8);
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 3, 4, Op.INSERT),
        new ExpectedRecordCPKProto(5, 6, 7, 8, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),

        // update test set c = c + 1 where a = 1 and b = 2;
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 4, 4, Op.UPDATE),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),

        // update test set a = a + 1 where c = 7;
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(5, 6, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(6, 6, 7, 8, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
      };

      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_cpk_insert_in_batch.sql");
    } catch (Exception e) {
      LOG.error("Test to insert in batch failed with exception: ", e);
      fail();
    }
  }

  // Execute a script with multiple command combinations.
  // Expected records: 65 (see script for more details)
  @Test
  public void testExecuteALongQuery() {
    try {
      ExpectedRecordCPKProto[] expectedRecords = {
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 3, 4, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 4, 5, Op.UPDATE),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(2, 2, 4, 5, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(2, 2, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(3, 3, 4, 5, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(3, 3, 5, 6, Op.UPDATE),
        new ExpectedRecordCPKProto(7, 8, 9, 10, Op.INSERT),
        new ExpectedRecordCPKProto(7, 8, 10, 11, Op.UPDATE),
        new ExpectedRecordCPKProto(7, 8, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(8, 8, 10, 11, Op.INSERT),
        new ExpectedRecordCPKProto(8, 8, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(9, 9, 10, 11, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(2, 3, 4, 5, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(5, 6, 7, 8, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(6, 7, 8, 9, Op.INSERT),
        new ExpectedRecordCPKProto(6, 7, 17, 18, Op.UPDATE),
        new ExpectedRecordCPKProto(6, 7, 26, 27, Op.UPDATE),
        new ExpectedRecordCPKProto(6, 7, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(15, 7, 26, 27, Op.INSERT),
        new ExpectedRecordCPKProto(15, 7, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(15, 16, 26, 27, Op.INSERT),
        new ExpectedRecordCPKProto(15, 16, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(24, 25, 26, 27, Op.INSERT),
        new ExpectedRecordCPKProto(24, 25, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(33, 34, 26, 27, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(33, 34, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(42, 43, 26, 27, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(60, 70, 80, 90, Op.INSERT),
        new ExpectedRecordCPKProto(60, 70, 89, 99, Op.UPDATE),
        new ExpectedRecordCPKProto(60, 70, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(69, 79, 89, 99, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(69, 79, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(69, 80, 90, 99, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(69, 80, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(70, 80, 90, 100, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(11, 12, 13, 14, Op.INSERT),
        new ExpectedRecordCPKProto(11, 12, 14, 15, Op.UPDATE),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(12, 112, 113, 114, Op.INSERT),
        new ExpectedRecordCPKProto(12, 112, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(17, 114, 115, 116, Op.INSERT),
        new ExpectedRecordCPKProto(17, 114, 116, 116, Op.UPDATE),
        new ExpectedRecordCPKProto(17, 114, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(18, 114, 116, 116, Op.INSERT),
        new ExpectedRecordCPKProto(18, 114, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(18, 115, 117, 116, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(21, 23, 24, 25, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(41, 43, 44, 45, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(41, 43, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(41, 44, 45, 46, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(41, 44, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.BEGIN),
        new ExpectedRecordCPKProto(41, 44, 45, 46, Op.INSERT),
        new ExpectedRecordCPKProto(-1, -1, -1, -1, Op.COMMIT)
      };

      setServerFlag(getTserverHostAndPort(), CDC_ENABLE_CONSISTENT_RECORDS, "false");
      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_cpk_long_script.sql");
    } catch (Exception e) {
      LOG.error("Test to execute a long script failed with exception: ", e);
      fail();
    }
  }

  // Added to verify the fix for GitHub Issue.
  // [#10946] Primary key columns missing in case of compound PKs streaming with Proto format
  @Test
  public void testCompoundKeyInProto() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      ExpectedRecordCPKProto[] expectedRecords = {
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 3, 4, Op.INSERT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.COMMIT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.BEGIN),
        new ExpectedRecordCPKProto(5, 6, 7, 8, Op.INSERT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.COMMIT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.BEGIN),
        new ExpectedRecordCPKProto(1, 2, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.COMMIT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.BEGIN),
        new ExpectedRecordCPKProto(5, 6, 8, 8, Op.UPDATE),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.COMMIT)
      };

      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_cpk_proto.sql");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour with compound key and proto records failed", e);
      fail();
    }
  }
}
