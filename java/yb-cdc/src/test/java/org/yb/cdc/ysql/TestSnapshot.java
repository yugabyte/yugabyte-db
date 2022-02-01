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
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.util.TestUtils;

import java.util.*;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;

@RunWith(value = YBTestRunner.class)
public class TestSnapshot extends CDCBaseClass {
  private final static Logger LOG = Logger.getLogger(TestSnapshot.class);

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
    // check whether the first record is a DDL
    CdcService.CDCSDKProtoRecordPB ddlRecord = outputList.get(0);
    assertEquals(CdcService.RowMessage.Op.DDL, ddlRecord.getRowMessage().getOp());

    // start iterating from the second record of the outputList, since that will be the starting
    // point for all the snapshot records
    for (int i = 1; i < outputList.size(); ++i) {
      // assuming that all snapshot records will have new_tuple value
      CdcService.CDCSDKProtoRecordPB record = outputList.get(i);

      assertEquals(CdcService.RowMessage.Op.READ, outputList.get(i).getRowMessage().getOp());
      assertTrue(outputList.get(i).getRowMessage().hasCommitTime());
      List<Integer> intArr = convertSnapshotRecordRecordToList(record);

      expectedRecords.put(intArr, true);
    }

    // if all values in map are true then the record pertaining to them has already been streamed
    for (Map.Entry<List<Integer>, Boolean> pair : expectedRecords.entrySet()) {
      assertTrue(pair.getValue());
    }
  }

  static class SortByValue implements Comparator<CdcService.CDCSDKProtoRecordPB> {
    @Override
    public int compare(CdcService.CDCSDKProtoRecordPB o1, CdcService.CDCSDKProtoRecordPB o2) {
      int v1 = o1.getRowMessage().getNewTuple(0).getDatumInt32();
      return v1 - o2.getRowMessage().getNewTuple(0).getDatumInt32();
    }
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");

    // setting back to default value before each test
    try {
      setServerFlag(getTserverHostAndPort(), CDC_BATCH_SIZE_GFLAG,
        String.valueOf(DEFAULT_BATCH_SIZE));
    } catch (Exception e) {
      LOG.error("Error while setting up default flag value for " + CDC_BATCH_SIZE_GFLAG, e);
      System.exit(-1);
    }
  }

  @Test
  public void testDefaultSnapshotBatchSize() {
    try {
      // default batch size is 250
      TestUtils.runSqlScript(connection, "cdc_large_snapshot.sql");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

      testSubscriber.createStreamAndGetSnapshot(outputList);

      // +1 if for checking the DDL record because it would be added in the beginning of
      // the snapshot batch
      assertEquals(DEFAULT_BATCH_SIZE+1, outputList.size());
    } catch (Exception e) {
      LOG.error("Test to verify default batch size failed", e);
      fail();
    }
  }

  @Test
  public void testSimpleSnapshot() {
    try {
      // first execute a script to fill the table with some data
      TestUtils.runSqlScript(connection, "cdc_snapshot_init.sql");

      // check for records in snapshot response from CDC
      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStreamAndGetSnapshot(outputList);

      Map<List<Integer>, Boolean> expectedRecordsOutOfOrder = new HashMap<>();
      expectedRecordsOutOfOrder.put(Arrays.asList(2, 3, 4), false);
      expectedRecordsOutOfOrder.put(Arrays.asList(3, 4, 5), false);
      expectedRecordsOutOfOrder.put(Arrays.asList(4, 5, 404), false);

      assertSnapshotRecords(expectedRecordsOutOfOrder, outputList);

    } catch (Exception e) {
      LOG.error("Test to verify the snapshot feature failed with exception", e);
      fail();
    }
  }

  @Test
  public void testLargeSnapshot() {
    try {
      TestUtils.runSqlScript(connection, "cdc_large_snapshot.sql");

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
    } catch (Exception e) {
      LOG.error("Test to check for a large snapshot failed", e);
      fail();
    }
  }

  @Test
  public void testSnapshotGFlag() {
    try {
      TestUtils.runSqlScript(connection, "cdc_large_snapshot.sql");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

      setServerFlag(getTserverHostAndPort(), CDC_BATCH_SIZE_GFLAG, "2500");
      testSubscriber.createStreamAndGetSnapshot(outputList);

      // we get one extra record in outputList, that record is the initial DDL containing the
      // schema of the table, the +1 is for the same DDL record only
      assertEquals(2500+1, outputList.size());
    } catch (Exception e) {
      LOG.error("Test to verify working of GFlag for snapshots failed", e);
      fail();
    }
  }
}
