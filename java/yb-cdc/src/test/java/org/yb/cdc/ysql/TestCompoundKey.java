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

import static org.yb.AssertionWrappers.*;;
import org.junit.Before;
import org.junit.Test;

import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;
import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecordCPKProto;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.common.ExpectedRecordCPK;
import org.yb.cdc.util.TestUtils;

import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunner.class)
public class TestCompoundKey extends CDCBaseClass {
  private Logger LOG = Logger.getLogger(TestCompoundKey.class);

  private void executeScriptAssertRecords(ExpectedRecordCPK[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream();

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    setServerFlag(getTserverHostAndPort(), CDC_INTENT_SIZE_GFLAG, "25");
    List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // ignoring the DDLs
      if (outputList.get(i).getOperation() == OperationType.DDL) {
        continue;
      }
      ExpectedRecordCPK.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // NOTE: processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int, b int, c int, d int, primary key(a, b));");
  }

  // Expected records: 1 (INSERT)
  @Test
  public void testInsert() throws Exception {
    try {
      ExpectedRecordCPK[] expectedRecords = {
        new ExpectedRecordCPK("1", "2", "3", "4", OperationType.INSERT)
      };

      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_compound_key.sql");
    } catch (Exception e) {
      LOG.error("Test to insert compound key failed with exception: ", e);
      fail();
    }
  }

  // Expected records: 7 (2_INSERT, WRITE, UPDATE, DELETE, INSERT, WRITE)
  @Test
  public void testInsertInBatch() throws Exception {
    try {
      ExpectedRecordCPK[] expectedRecords = {
        new ExpectedRecordCPK("1", "2", "3", "4", OperationType.INSERT),
        new ExpectedRecordCPK("5", "6", "7", "8", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("1", "2", "4", "4", OperationType.UPDATE),
        new ExpectedRecordCPK("5", "6", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("6", "6", "7", "8", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_cpk_insert_in_batch.sql");
    } catch (Exception e) {
      LOG.error("Test to insert in batch failed with exception: ", e);
      fail();
    }
  }

  // Execute a script with multiple command combinations
  // Expected records: 65 (see script for more details)
  @Test
  public void testExecuteALongQuery() {
    try {
      ExpectedRecordCPK[] expectedRecords = {
        new ExpectedRecordCPK("1", "2", "3", "4", OperationType.INSERT),
        new ExpectedRecordCPK("1", "2", "4", "4", OperationType.UPDATE),
        new ExpectedRecordCPK("1", "2", "4", "5", OperationType.UPDATE),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("1", "2", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("2", "2", "4", "5", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("2", "2", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("3", "3", "4", "5", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("3", "3", "5", "5", OperationType.UPDATE),
        new ExpectedRecordCPK("3", "3", "5", "6", OperationType.UPDATE),
        new ExpectedRecordCPK("7", "8", "9", "10", OperationType.INSERT),
        new ExpectedRecordCPK("7", "8", "10", "10", OperationType.UPDATE),
        new ExpectedRecordCPK("7", "8", "10", "11", OperationType.UPDATE),
        new ExpectedRecordCPK("7", "8", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("8", "8", "10", "11", OperationType.INSERT),
        new ExpectedRecordCPK("8", "8", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("9", "9", "10", "11", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("2", "3", "4", "5", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("5", "6", "7", "8", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("6", "7", "8", "9", OperationType.INSERT),
        new ExpectedRecordCPK("6", "7", "17", "9", OperationType.UPDATE),
        new ExpectedRecordCPK("6", "7", "17", "18", OperationType.UPDATE),
        new ExpectedRecordCPK("6", "7", "26", "18", OperationType.UPDATE),
        new ExpectedRecordCPK("6", "7", "26", "27", OperationType.UPDATE),
        new ExpectedRecordCPK("6", "7", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("15", "7", "26", "27", OperationType.INSERT),
        new ExpectedRecordCPK("15", "7", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("15", "16", "26", "27", OperationType.INSERT),
        new ExpectedRecordCPK("15", "16", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("24", "25", "26", "27", OperationType.INSERT),
        new ExpectedRecordCPK("24", "25", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("33", "34", "26", "27", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("33", "34", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("42", "43", "26", "27", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("60", "70", "80", "90", OperationType.INSERT),
        new ExpectedRecordCPK("60", "70", "89", "90", OperationType.UPDATE),
        new ExpectedRecordCPK("60", "70", "89", "99", OperationType.UPDATE),
        new ExpectedRecordCPK("60", "70", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("69", "79", "89", "99", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("69", "79", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("69", "80", "90", "99", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("69", "80", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("70", "80", "90", "100", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("11", "12", "13", "14", OperationType.INSERT),
        new ExpectedRecordCPK("11", "12", "14", "14", OperationType.UPDATE),
        new ExpectedRecordCPK("11", "12", "14", "15", OperationType.UPDATE),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("12", "112", "113", "114", OperationType.INSERT),
        new ExpectedRecordCPK("12", "112", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("17", "114", "115", "116", OperationType.INSERT),
        new ExpectedRecordCPK("17", "114", "116", "116", OperationType.UPDATE),
        new ExpectedRecordCPK("17", "114", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("18", "114", "116", "116", OperationType.INSERT),
        new ExpectedRecordCPK("18", "114", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("18", "115", "117", "116", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("21", "23", "24", "25", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("41", "43", "44", "45", OperationType.INSERT),
        new ExpectedRecordCPK("41", "43", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("41", "44", "45", "46", OperationType.INSERT),
        new ExpectedRecordCPK("", "", "", "", OperationType.WRITE),
        new ExpectedRecordCPK("41", "44", "", "", OperationType.DELETE),
        new ExpectedRecordCPK("41", "44", "45", "46", OperationType.INSERT)
      };

      executeScriptAssertRecords(expectedRecords, "compound_key_tests/cdc_cpk_long_script.sql");
    } catch (Exception e) {
      LOG.error("Test to execute a long script failed with exception: ", e);
      fail();
    }
  }

  // added to verify the fix for GitHub Issue
  // [#10946] Primary key columns missing in case of compound PKs streaming with Proto format
  @Test
  public void testCompoundKeyInProto() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      ExpectedRecordCPKProto[] expectedRecords = {
        new ExpectedRecordCPKProto(1, 2, 3, 4, Op.INSERT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.BEGIN),
        new ExpectedRecordCPKProto(5, 6, 7, 8, Op.INSERT),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.COMMIT),
        new ExpectedRecordCPKProto(1, 2, 0, 0, Op.DELETE),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.BEGIN),
        new ExpectedRecordCPKProto(5, 6, 8, 8, Op.UPDATE),
        new ExpectedRecordCPKProto(0, 0, 0, 0, Op.COMMIT)
      };

      // execute the script
      TestUtils.runSqlScript(connection, "compound_key_tests/cdc_cpk_proto.sql");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      int idx = 0;
      int processedRecords = 0;
      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getRowMessage().getOp() != Op.DDL) {
          ExpectedRecordCPKProto.checkRecord(outputList.get(i), expectedRecords[idx++]);
          ++processedRecords;
        }
      }

      assertEquals(expectedRecords.length, processedRecords);
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour with compound key and proto records failed", e);
      fail();
    }
  }
}
