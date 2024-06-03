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

import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.RowMessage.Op;
import static org.yb.AssertionWrappers.*;
import java.util.Objects;

public class ExpectedRecordCPKProto {
  public int col1;
  public int col2;
  public int col3;
  public int col4;
  public Op opType;

  public ExpectedRecordCPKProto(int col1, int col2, int col3, int col4, Op opType) {
    this.col1 = col1;
    this.col2 = col2;
    this.col3 = col3;
    this.col4 = col4;
    this.opType = opType;
  }

  private static void checkInsertRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ExpectedRecordCPKProto expectedRecord) {
    assertEquals(Op.INSERT, record.getRowMessage().getOp());
    assertEquals(expectedRecord.col1, record.getRowMessage().getNewTuple(0).getDatumInt32());
    assertEquals(expectedRecord.col2,
      record.getRowMessage().getNewTuple(1).getDatumInt32());
    assertEquals(expectedRecord.col3,
      record.getRowMessage().getNewTuple(2).getDatumInt32());
    assertEquals(expectedRecord.col4,
      record.getRowMessage().getNewTuple(3).getDatumInt32());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkDeleteRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ExpectedRecordCPKProto expectedRecord) {
    assertEquals(Op.DELETE, record.getRowMessage().getOp());
    assertEquals(expectedRecord.col1,
      record.getRowMessage().getOldTuple(0).getDatumInt32());
    assertEquals(expectedRecord.col2,
      record.getRowMessage().getOldTuple(1).getDatumInt32());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkBeginRecord(CdcService.CDCSDKProtoRecordPB record) {
    assertEquals(Op.BEGIN, record.getRowMessage().getOp());
  }

  private static void checkCommitRecord(CdcService.CDCSDKProtoRecordPB record) {
    assertEquals(Op.COMMIT, record.getRowMessage().getOp());
    assertTrue(record.hasCdcSdkOpId());
  }

  private static void checkDDLRecord(CdcService.CDCSDKProtoRecordPB record) {
    assertEquals(Op.DDL, record.getRowMessage().getOp());
    assertTrue(record.getRowMessage().hasSchema());
    assertTrue(record.getRowMessage().hasPgschemaName());
  }

  private static void checkUpdateRecord(CdcService.CDCSDKProtoRecordPB record,
                                        ExpectedRecordCPKProto expectedRecord) {
    assertEquals(Op.UPDATE, record.getRowMessage().getOp());
    assertEquals(expectedRecord.col1, record.getRowMessage().getNewTuple(0).getDatumInt32());

    if (record.getRowMessage().getNewTupleCount() == 3) {
      if (Objects.equals(record.getRowMessage().getNewTuple(1).getColumnName(), "c")) {
        assertEquals(expectedRecord.col3,
          record.getRowMessage().getNewTuple(2).getDatumInt32());
      } else if (Objects.equals(record.getRowMessage().getNewTuple(1).getColumnName(), "d")) {
        assertEquals(expectedRecord.col4,
          record.getRowMessage().getNewTuple(2).getDatumInt32());
      }
    } else if (record.getRowMessage().getNewTupleCount() == 4) {
      assertEquals(expectedRecord.col3,
        record.getRowMessage().getNewTuple(2).getDatumInt32());
      assertEquals(expectedRecord.col4,
        record.getRowMessage().getNewTuple(3).getDatumInt32());
    }

    assertTrue(record.hasCdcSdkOpId());
  }

  public static void checkRecord(CdcService.CDCSDKProtoRecordPB record,
                                 ExpectedRecordCPKProto expectedRecord) {
    switch (expectedRecord.opType) {
      case INSERT:
        checkInsertRecord(record, expectedRecord);
        break;
      case DELETE:
        checkDeleteRecord(record, expectedRecord);
        break;
      case UPDATE:
        checkUpdateRecord(record, expectedRecord);
        break;
      case BEGIN:
        checkBeginRecord(record);
        break;
      case COMMIT:
        checkCommitRecord(record);
        break;
      case DDL:
        checkDDLRecord(record);
        break;
    }
  }
}
