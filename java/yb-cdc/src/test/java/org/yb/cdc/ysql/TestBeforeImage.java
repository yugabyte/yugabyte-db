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
import org.yb.cdc.util.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestBeforeImage extends CDCBaseClass {
  private Logger LOG = LoggerFactory.getLogger(TestBeforeImage.class);

  private void executeScriptAssertRecords(ExpectedRecordYSQL<?>[] expectedRecords,
                                          String sqlScript) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
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
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b text default 'default_val', "
                      + "c double precision default 12.34);");
  }

  @Test
  public void verifyBasicCorrectness() throws Exception {
    LOG.info("Starting verifyBasicCorrectness");

    // Create a stream.
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto", "all");

    statement.execute("insert into test values (1);");
    statement.execute("update test set b = 'updated_val' where a = 1;");
    statement.execute("update test set b = 'updated_val_again', c = 56.78 where a = 1;");

    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    // Expect 4 records: DDL + INSERT + UPDATE + UPDATE.
    assertEquals(4, outputList.size());

    for (int i = 0; i < outputList.size(); ++i) {
      LOG.info("Record " + i + ": " + outputList.get(i));
    }

    // The first record is a DDL record.
    assertEquals(Op.DDL, outputList.get(0).getRowMessage().getOp());

    // The second record (INSERT) will only have new image.
    assertEquals(Op.INSERT, outputList.get(1).getRowMessage().getOp());
    assertEquals(1, outputList.get(1).getRowMessage().getNewTuple(0).getDatumInt32());
    assertEquals("default_val", outputList.get(1).getRowMessage().getNewTuple(1).getDatumString());
    assertEquals(12.34, outputList.get(1).getRowMessage().getNewTuple(2).getDatumDouble());

    // The third record is an update record, it will have an old image as well as a new image.
    assertEquals(Op.UPDATE, outputList.get(2).getRowMessage().getOp());
    assertEquals(1, outputList.get(2).getRowMessage().getOldTuple(0).getDatumInt32());
    assertEquals("default_val", outputList.get(2).getRowMessage().getOldTuple(1).getDatumString());
    assertEquals(12.34, outputList.get(2).getRowMessage().getOldTuple(2).getDatumDouble());

    assertEquals(1, outputList.get(2).getRowMessage().getNewTuple(0).getDatumInt32());
    assertEquals("updated_val", outputList.get(2).getRowMessage().getNewTuple(1).getDatumString());
    assertEquals(12.34, outputList.get(2).getRowMessage().getNewTuple(2).getDatumDouble());

    // The fourth record is an UPDATE record having both old and new images.
    assertEquals(Op.UPDATE, outputList.get(3).getRowMessage().getOp());
    assertEquals(1, outputList.get(3).getRowMessage().getOldTuple(0).getDatumInt32());
    assertEquals("updated_val", outputList.get(3).getRowMessage().getOldTuple(1).getDatumString());
    assertEquals(12.34, outputList.get(3).getRowMessage().getOldTuple(2).getDatumDouble());

    assertEquals(1, outputList.get(3).getRowMessage().getNewTuple(0).getDatumInt32());
    assertEquals("updated_val_again",
                 outputList.get(3).getRowMessage().getNewTuple(1).getDatumString());
    assertEquals(56.78, outputList.get(3).getRowMessage().getNewTuple(2).getDatumDouble());
  }
}
