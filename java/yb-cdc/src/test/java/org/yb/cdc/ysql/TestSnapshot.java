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

import java.util.*;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestSnapshot extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestSnapshot.class);

  protected final long DEFAULT_BATCH_SIZE = 250;

  private List<Integer> convertSnapshotRecordRecordToList(CdcService.CDCSDKProtoRecordPB record) {
    List<Integer> valueList = new ArrayList<>();

    int tupleSize = record.getRowMessage().getNewTupleCount();
    for (int i = 0; i < tupleSize; ++i) {
      valueList.add(record.getRowMessage().getNewTuple(i).getDatumInt32());
    }

    return valueList;
  }

  private void assertSnapshotRecords(Map<List<Integer>, Boolean> expectedRecords,
                                           List<CdcService.CDCSDKProtoRecordPB> outputList) {
    // Check whether the first record is a DDL.
    CdcService.CDCSDKProtoRecordPB ddlRecord = outputList.get(0);
    assertEquals(CdcService.RowMessage.Op.DDL, ddlRecord.getRowMessage().getOp());

    // Start iterating from the second record of the outputList, since that will be the starting
    // point for all the snapshot records.
    for (int i = 1; i < outputList.size(); ++i) {
      // Assuming that all snapshot records will have new_tuple value.
      CdcService.CDCSDKProtoRecordPB record = outputList.get(i);

      assertEquals(CdcService.RowMessage.Op.READ, outputList.get(i).getRowMessage().getOp());
      assertTrue(outputList.get(i).getRowMessage().hasCommitTime());
      List<Integer> intArr = convertSnapshotRecordRecordToList(record);

      expectedRecords.put(intArr, true);
    }

    // If all values in map are true then the record pertaining to them has already been streamed.
    for (Map.Entry<List<Integer>, Boolean> pair : expectedRecords.entrySet()) {
      assertTrue(pair.getValue());
    }
  }

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");

    // Setting back to default value before each test.
    try {
      setServerFlag(getTserverHostAndPort(), CDC_BATCH_SIZE_GFLAG,
        String.valueOf(DEFAULT_BATCH_SIZE));
        setServerFlag(getTserverHostAndPort(), CDC_POPULATE_SAFEPOINT_RECORD, "false");
    } catch (Exception e) {
      LOG.error("Error while setting up default flag value for " + CDC_BATCH_SIZE_GFLAG, e);
      System.exit(-1);
    }
  }

  private CDCSubscriber smallSnapshot() throws Exception {
    // First execute a script to fill the table with some data.
    CDCTestUtils.runSqlScript(connection, "cdc_snapshot_init.sql");

    // Check for records in snapshot response from CDC.
    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStreamAndGetSnapshot(outputList);

    Map<List<Integer>, Boolean> expectedRecordsOutOfOrder = new HashMap<>();
    expectedRecordsOutOfOrder.put(Arrays.asList(2, 3, 4), false);
    expectedRecordsOutOfOrder.put(Arrays.asList(3, 4, 5), false);
    expectedRecordsOutOfOrder.put(Arrays.asList(4, 5, 404), false);

    assertSnapshotRecords(expectedRecordsOutOfOrder, outputList);

    return testSubscriber;
  }

  private CDCSubscriber largeSnapshot() throws Exception {
    CDCTestUtils.runSqlScript(connection, "cdc_large_snapshot.sql");

    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

    setServerFlag(getTserverHostAndPort(), CDC_BATCH_SIZE_GFLAG, "20000");

    testSubscriber.createStreamAndGetSnapshot(outputList);
    int insertedRecordsUsingScript = 5000;

    // +1 for the DDL record
    assertEquals(insertedRecordsUsingScript+1, outputList.size());

    Map<List<Integer>, Boolean> expectedRecordsOutOfOrder = new HashMap<>();
    for (int i = 1; i <= insertedRecordsUsingScript; ++i) {
      expectedRecordsOutOfOrder.put(Arrays.asList(i, 400, 404), false);
    }

    assertSnapshotRecords(expectedRecordsOutOfOrder, outputList);

    return testSubscriber;
  }

  @Test
  public void testDefaultSnapshotBatchSize() {
    try {
      // Default batch size is 250.
      CDCTestUtils.runSqlScript(connection, "cdc_large_snapshot.sql");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

      testSubscriber.createStreamAndGetSnapshot(outputList);

      // +1 if for checking the DDL record because it would be added in the beginning of
      // the snapshot batch.
      assertEquals(DEFAULT_BATCH_SIZE+1, outputList.size());
    } catch (Exception e) {
      LOG.error("Test to verify default batch size failed", e);
      fail();
    }
  }

  @Test
  public void testSimpleSnapshot() {
    try {
      smallSnapshot();
    } catch (Exception e) {
      LOG.error("Test to verify the snapshot feature failed with exception", e);
      fail();
    }
  }

  @Test
  public void testLargeSnapshot() {
    try {
      largeSnapshot();
    } catch (Exception e) {
      LOG.error("Test to check for a large snapshot failed", e);
      fail();
    }
  }

  @Test
  public void testSnapshotGFlag() {
    try {
      CDCTestUtils.runSqlScript(connection, "cdc_large_snapshot.sql");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

      setServerFlag(getTserverHostAndPort(), CDC_BATCH_SIZE_GFLAG, "2500");
      setServerFlag(getTserverHostAndPort(), CDC_POPULATE_SAFEPOINT_RECORD, "false");
      testSubscriber.createStreamAndGetSnapshot(outputList);

      // We get one extra record in outputList, that record is the initial DDL containing the
      // schema of the table, the +1 is for the same DDL record only.
      assertEquals(2500+1, outputList.size());
    } catch (Exception e) {
      LOG.error("Test to verify working of GFlag for snapshots failed", e);
      fail();
    }
  }

  @Test
  public void testSmallSnapshotThenStreaming() {
    try {
      CDCSubscriber testSubscriber = smallSnapshot();

      // This statement will be executed once the snapshot is complete.
      statement.execute("INSERT INTO test VALUES (10, 11, 12);");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList, testSubscriber.getSubscriberCheckpoint());

      boolean recordAfterSnapshotAsserted = false;
      ExpectedRecord3Proto recordAfterSnapshot = new ExpectedRecord3Proto(10, 11, 12, Op.INSERT);
      for (CdcService.CDCSDKProtoRecordPB record : outputList) {
        if (record.getRowMessage().getOp() == Op.INSERT) {
          // Since only one insert record is expected, we can assert for it.
          ExpectedRecord3Proto.checkRecord(record, recordAfterSnapshot);
          recordAfterSnapshotAsserted = true;
        }
      }
      assertTrue(recordAfterSnapshotAsserted);
    } catch (Exception e) {
      LOG.error("Test to verify streaming after small snapshot failed", e);
      fail();
    }
  }

  @Test
  public void testLargeSnapshotThenStreaming() {
    try {
      CDCSubscriber testSubscriber = largeSnapshot();

      // These statements will be executed once the snapshot is complete.
      statement.execute("INSERT INTO test VALUES (10000, 11, 12);");
      statement.execute("UPDATE test SET c = c + 10 where a = 10000;");
      statement.execute("BEGIN;");
      statement.execute("DELETE FROM test WHERE a = 10000;");
      statement.execute("COMMIT;");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList, testSubscriber.getSubscriberCheckpoint());

      int idx = 0;
      int recordsAsserted = 0;
      ExpectedRecord3Proto[] postSnapshotRecords = {
        new ExpectedRecord3Proto(-1, -1, -1, Op.BEGIN),
        new ExpectedRecord3Proto(10000, 11, 12, Op.INSERT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.COMMIT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.BEGIN),
        new ExpectedRecord3Proto(10000, 11, 22, Op.UPDATE),
        new ExpectedRecord3Proto(-1, -1, -1, Op.COMMIT),
        new ExpectedRecord3Proto(-1, -1, -1, Op.BEGIN),
        new ExpectedRecord3Proto(10000, 0, 0, Op.DELETE),
        new ExpectedRecord3Proto(-1, -1, -1, Op.COMMIT) };

      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
          ExpectedRecord3Proto.checkRecord(outputList.get(i),
                                           new ExpectedRecord3Proto(-1, -1, -1, Op.DDL));
          continue;
        }

        ExpectedRecord3Proto.checkRecord(outputList.get(i), postSnapshotRecords[idx++]);
        ++recordsAsserted;
      }

      assertEquals(postSnapshotRecords.length, recordsAsserted);
    } catch (Exception e) {
      LOG.error("Test to verify streaming after large snapshot failed", e);
      fail();
    }
  }
}
