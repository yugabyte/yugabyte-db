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

package org.yb.cdc.common;

import org.yb.Common;
import org.yb.Transaction;
import org.yb.cdc.CDCDecoderYCQL;
import org.yb.cdc.CdcService;

import static org.yb.AssertionWrappers.*;

public class ExpectedRecordYCQLGeneric<T> {
  public int col1;
  public T col2;
  public CdcService.CDCSDKRecordPB.OperationType opType;

  private static final int columnCount = 2;

  public ExpectedRecordYCQLGeneric(int col1, T col2,
                                   CdcService.CDCSDKRecordPB.OperationType opType) {
    this.col1 = col1;
    this.col2 = col2;
    this.opType = opType;
  }

  private static void checkInsertRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecordYCQLGeneric<?> expectedRecord) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.INSERT, record.getOperation());
    assertEquals(expectedRecord.col1, record.getKey(0).getValue().getInt32Value());
    assertEquals(expectedRecord.col2,
                            CDCDecoderYCQL.parseCDCValue(record.getChanges(0).getValue()));
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkDeleteRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecordYCQLGeneric<?> expectedRecord) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.DELETE, record.getOperation());
    assertEquals(expectedRecord.col1, record.getKey(0).getValue().getInt32Value());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkWriteRecord(CdcService.CDCSDKRecordPB record) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.WRITE, record.getOperation());
    assertEquals(Transaction.TransactionStatus.APPLYING, record.getTransactionState().getStatus());
    assertTrue(record.hasTransactionState());
  }

  private static void checkDDLRecord(CdcService.CDCSDKRecordPB record,
                                     int numberOfColumns) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.DDL, record.getOperation());
    assertTrue(record.hasSchema());
    assertEquals(numberOfColumns, record.getSchema().getColumnInfoCount());
  }

  private static void checkUpdateRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecordYCQLGeneric<?> expectedRecord) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.UPDATE, record.getOperation());
    assertEquals(expectedRecord.col1, record.getKey(0).getValue().getInt32Value());
    assertEquals(expectedRecord.col2,
      CDCDecoderYCQL.parseCDCValue(record.getChanges(0).getValue()));
    assertTrue(record.hasCdcSdkOpId());
  }

  public static void checkRecord(CdcService.CDCSDKRecordPB record,
                                 ExpectedRecordYCQLGeneric<?> expectedRecord) {
    switch (expectedRecord.opType) {
      case INSERT:
        checkInsertRecord(record, expectedRecord);
        break;
      case DELETE:
        checkDeleteRecord(record, expectedRecord);
        break;
      case WRITE:
        checkWriteRecord(record);
        break;
      case UPDATE:
        checkUpdateRecord(record, expectedRecord);
        break;
      case DDL:
        checkDDLRecord(record, columnCount);
    }
  }
}
