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
import org.yb.cdc.CdcService;
import static org.yb.AssertionWrappers.*;
import java.util.Objects;

public class ExpectedRecord3Col {
  public String col1;
  public String col2;
  public String col3;
  public CdcService.CDCSDKRecordPB.OperationType opType;

  private static final int columnCount = 3;

  public ExpectedRecord3Col(String col1, String col2, String col3,
                            CdcService.CDCSDKRecordPB.OperationType opType) {
    this.col1 = col1;
    this.col2 = col2;
    this.col3 = col3;
    this.opType = opType;
  }

  private static void checkInsertRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecord3Col expectedRecord) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.INSERT, record.getOperation());
    assertEquals(expectedRecord.col1, record.getKey(0).getValue().getStringValue().toStringUtf8());
    assertEquals(expectedRecord.col2,
                 record.getChanges(0).getValue().getStringValue().toStringUtf8());
    assertEquals(expectedRecord.col3,
                 record.getChanges(1).getValue().getStringValue().toStringUtf8());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkDeleteRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecord3Col expectedRecord) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.DELETE, record.getOperation());
    assertEquals(expectedRecord.col1,
                 record.getKey(0).getValue().getStringValue().toStringUtf8());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkWriteRecord(CdcService.CDCSDKRecordPB record) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.WRITE, record.getOperation());
    assertEquals(Transaction.TransactionStatus.APPLYING, record.getTransactionState().getStatus());
    assertTrue(record.hasTransactionState());
  }

  private static void checkDDLRecord(CdcService.CDCSDKRecordPB record, int numberOfColumns) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.DDL, record.getOperation());
    assertTrue(record.hasSchema());
    assertEquals(numberOfColumns, record.getSchema().getColumnInfoCount());
  }

  private static void checkUpdateRecord(CdcService.CDCSDKRecordPB record,
                                        ExpectedRecord3Col expectedRecord) {
    assertEquals(CdcService.CDCSDKRecordPB.OperationType.UPDATE, record.getOperation());
    assertEquals(expectedRecord.col1, record.getKey(0).getValue().getStringValue().toStringUtf8());

    if (record.getChangesCount() == 1) {
      if (Objects.equals(record.getChanges(0).getKey().toStringUtf8(), "b")) {
        assertEquals(expectedRecord.col2,
                     record.getChanges(0).getValue().getStringValue().toStringUtf8());
      } else if (Objects.equals(record.getChanges(0).getKey().toStringUtf8(), "c")) {
        assertEquals(expectedRecord.col3,
                     record.getChanges(0).getValue().getStringValue().toStringUtf8());
      }
    } else if (record.getChangesCount() == 2) {
      assertEquals(expectedRecord.col2,
                   record.getChanges(0).getValue().getStringValue().toStringUtf8());
      assertEquals(expectedRecord.col3,
                   record.getChanges(1).getValue().getStringValue().toStringUtf8());
    }

    assertTrue(record.hasCdcSdkOpId());
  }

  public static void checkRecord(CdcService.CDCSDKRecordPB record,
                                 ExpectedRecord3Col expectedRecord) {
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
