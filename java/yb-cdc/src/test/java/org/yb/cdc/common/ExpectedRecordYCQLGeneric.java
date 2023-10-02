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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.Common.DatumMessagePB;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.RowMessage.Op;

public class ExpectedRecordYCQLGeneric<T> {
  private static final int columnCount = 2;
  public static Object getValue(CdcService.CDCSDKProtoRecordPB record) {
    return getValue(record, 1);
  }
  public static Object getValue(CdcService.CDCSDKProtoRecordPB record, int index) {
    // Considering the index 1 since only the second column is going to be varying type here.
    DatumMessagePB tuple = record.getRowMessage().getNewTuple(index);

    if (tuple.hasDatumBool()) {
      return tuple.getDatumBool();
    } else if (tuple.hasDatumDouble()) {
      return tuple.getDatumDouble();
    } else if (tuple.hasDatumFloat()) {
      return tuple.getDatumFloat();
    } else if (tuple.hasDatumString()) {
      return tuple.getDatumString();
    } else if (tuple.hasDatumInt32()) {
      return tuple.getDatumInt32();
    } else if (tuple.hasDatumInt64()) {
      return tuple.getDatumInt64();
    } else {
      return null;
    }
  }

  public static void checkRecord(CdcService.CDCSDKProtoRecordPB record,
                                 ExpectedRecordYCQLGeneric<?> expectedRecord) {
    switch (expectedRecord.opType) {
      case INSERT:
        checkInsertRecord(record, expectedRecord);
        break;
      case DELETE:
        checkDeleteRecord(record, expectedRecord);
        break;
      case BEGIN:
        checkBeginRecord(record);
        break;
      case COMMIT:
        checkCommitRecord(record);
        break;
      case UPDATE:
        checkUpdateRecord(record, expectedRecord);
        break;
      case DDL:
        checkDDLRecord(record, columnCount);
    }
  }

  private static void checkInsertRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ExpectedRecordYCQLGeneric<?> expectedRecord) {
    assertEquals(Op.INSERT, record.getRowMessage().getOp());
    assertEquals(expectedRecord.col1, record.getRowMessage().getNewTuple(0).getDatumInt32());
    assertEquals(expectedRecord.col2, getValue(record));
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkDeleteRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ExpectedRecordYCQLGeneric<?> expectedRecord) {
    assertEquals(Op.DELETE, record.getRowMessage().getOp());
    assertEquals(expectedRecord.col1, record.getRowMessage().getNewTuple(0).getDatumInt32());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkBeginRecord(CdcService.CDCSDKProtoRecordPB record) {
    assertEquals(Op.BEGIN, record.getRowMessage().getOp());
  }

  private static void checkCommitRecord(CdcService.CDCSDKProtoRecordPB record) {
    assertEquals(Op.COMMIT, record.getRowMessage().getOp());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkDDLRecord(CdcService.CDCSDKProtoRecordPB record,
                                     int numberOfColumns) {
    assertEquals(Op.DDL, record.getRowMessage().getOp());
    assertTrue(record.getRowMessage().hasSchema());
    assertEquals(numberOfColumns, record.getRowMessage().getSchema().getColumnInfoCount());
  }

  private static void checkUpdateRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ExpectedRecordYCQLGeneric<?> expectedRecord) {
    assertEquals(Op.UPDATE, record.getRowMessage().getOp());
    assertEquals(expectedRecord.col1, record.getRowMessage().getNewTuple(0).getDatumInt32());
    assertEquals(expectedRecord.col2, getValue(record));
    assertTrue(record.hasCdcSdkOpId());
  }

  public int col1;

  public T col2;

  public Op opType;

  public ExpectedRecordYCQLGeneric(int col1, T col2, Op opType) {
    this.col1 = col1;
    this.col2 = col2;
    this.opType = opType;
  }
}
