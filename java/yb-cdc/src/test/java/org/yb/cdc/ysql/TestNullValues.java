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
import org.junit.*;
import org.junit.runner.RunWith;
import org.yb.Common;
import org.yb.Value;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;

import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public class TestNullValues extends CDCBaseClass {
  private final static Logger LOG = Logger.getLogger(TestNullValues.class);

  protected int DEFAULT_KEY_VALUE = 1;

  private void assertForNullValuesProto(List<CdcService.CDCSDKProtoRecordPB> outputList) {
    assertNotNull(outputList);

    for (int i = 0; i < outputList.size(); ++i) {
      if (outputList.get(i).getRowMessage().getOp() == CdcService.RowMessage.Op.INSERT) {
        CdcService.RowMessage rm = outputList.get(i).getRowMessage();
        assertEquals(DEFAULT_KEY_VALUE, rm.getNewTuple(0).getDatumInt32());

        Value.DatumMessagePB dm = rm.getNewTuple(1);
        // if it is null, no value type is going to be there
        assertFalse(dm.hasDatumBool());
        assertFalse(dm.hasDatumBytes());
        assertFalse(dm.hasDatumDouble());
        assertFalse(dm.hasDatumFloat());
        assertFalse(dm.hasDatumInt32());
        assertFalse(dm.hasDatumInt64());
        assertFalse(dm.hasDatumString());
      }
    }
  }

  private void assertForNullValuesJson(List<CdcService.CDCSDKRecordPB> outputList) {
    assertNotNull(outputList);

    for (int i = 0; i < outputList.size(); ++i) {
      if (outputList.get(i).getOperation() == CdcService.CDCSDKRecordPB.OperationType.INSERT) {
        String key = outputList.get(i).getKey(0).getValue().getStringValue().toStringUtf8();
        assertEquals(String.valueOf(DEFAULT_KEY_VALUE), key);

        int changesCount = outputList.get(i).getChangesCount();
        for (int cnt = 0; cnt < changesCount; ++cnt) {
          Value.QLValuePB val = outputList.get(i).getChanges(cnt).getValue();
          assertFalse(val.hasStringValue());
        }
      }
    }
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
  }

  private void checkNullBehaviourWithType(String type) {
    try {
      String createTable = String.format("create table test (a int primary key, b %s);", type);
      assertFalse(statement.execute(createTable));

      // creating one stream with proto format
      CDCSubscriber testSubscriberProto = new CDCSubscriber(getMasterAddresses());
      testSubscriberProto.createStream("proto");

      // creating another stream with json format
      CDCSubscriber testSubscriberJson = new CDCSubscriber(getMasterAddresses());
      testSubscriberJson.createStream("json");

      int dummyInsert = statement.executeUpdate(String.format("insert into test values (%d);",
        DEFAULT_KEY_VALUE));
      assertEquals(1, dummyInsert);

      // receiving response in proto
      List<CdcService.CDCSDKProtoRecordPB> outputListProto = new ArrayList<>();
      testSubscriberProto.getResponseFromCDC(outputListProto);

      // receiving response in json from another stream
      // note that testSubscriberProto and testSubscriberJson work in isolation with each other
      List<CdcService.CDCSDKRecordPB> outputListJson = new ArrayList<>();
      testSubscriberJson.getResponseFromCDC(outputListJson);

      assertForNullValuesProto(outputListProto);
      assertForNullValuesJson(outputListJson);
    } catch (Exception e) {
      LOG.error("Test to check for null values of column type " + type + " failed", e);
      fail();
    }
  }

  @Test
  public void testArr() {
    checkNullBehaviourWithType("int[]");
  }

  @Test
  public void testBigInt() {
    checkNullBehaviourWithType("bigint");
  }

  @Test
  public void testBit() {
    checkNullBehaviourWithType("bit(6)");
  }

  @Test
  public void testVarbit() {
    checkNullBehaviourWithType("varbit(6)");
  }

  @Test
  public void testBoolean() {
    checkNullBehaviourWithType("boolean");
  }

  @Test
  public void testBox() {
    checkNullBehaviourWithType("box");
  }

  @Test
  public void testBytea() {
    checkNullBehaviourWithType("bytea");
  }

  @Test
  public void testChar() {
    checkNullBehaviourWithType("char(10)");
  }

  @Test
  public void testVarchar() {
    checkNullBehaviourWithType("varchar(10)");
  }

  @Test
  public void testCidr() {
    checkNullBehaviourWithType("cidr");
  }

  @Test
  public void testCircle() {
    checkNullBehaviourWithType("circle");
  }

  @Test
  public void testDate() {
    checkNullBehaviourWithType("date");
  }

  @Test
  public void testDoublePrecision() {
    checkNullBehaviourWithType("double precision");
  }

  @Test
  public void testInet() {
    checkNullBehaviourWithType("inet");
  }

  @Test
  public void testInteger() {
    checkNullBehaviourWithType("int");
  }

  @Test
  public void testInterval() {
    checkNullBehaviourWithType("interval");
  }

  @Test
  public void testJson() {
    checkNullBehaviourWithType("json");
  }

  @Test
  public void testJsonb() {
    checkNullBehaviourWithType("jsonb");
  }

  @Test
  public void testLine() {
    checkNullBehaviourWithType("line");
  }

  @Test
  public void testLseg() {
    checkNullBehaviourWithType("lseg");
  }

  @Test
  public void testMacaddr() {
    checkNullBehaviourWithType("macaddr");
  }

  @Test
  public void testMacaddr8() {
    checkNullBehaviourWithType("macaddr8");
  }

  @Test
  public void testMoney() {
    checkNullBehaviourWithType("money");
  }

  @Test
  public void testNumeric() {
    checkNullBehaviourWithType("numeric");
  }

  @Test
  public void testPath() {
    checkNullBehaviourWithType("path");
  }

  @Test
  public void testPoint() {
    checkNullBehaviourWithType("point");
  }

  @Test
  public void testPolygon() {
    checkNullBehaviourWithType("polygon");
  }

  @Test
  public void testReal() {
    checkNullBehaviourWithType("real");
  }

  @Test
  public void testSmallint() {
    checkNullBehaviourWithType("smallint");
  }

  @Test
  public void testInt4Range() {
    checkNullBehaviourWithType("int4range");
  }

  @Test
  public void testInt8Range() {
    checkNullBehaviourWithType("int8range");
  }

  @Test
  public void testNumRange() {
    checkNullBehaviourWithType("numrange");
  }

  @Test
  public void testTsrange() {
    checkNullBehaviourWithType("tsrange");
  }

  @Test
  public void testTstzrange() {
    checkNullBehaviourWithType("tstzrange");
  }

  @Test
  public void testDaterange() {
    checkNullBehaviourWithType("daterange");
  }

  @Test
  public void testText() {
    checkNullBehaviourWithType("text");
  }

  @Test
  public void testTime() {
    checkNullBehaviourWithType("time");
  }

  @Test
  public void testTimetz() {
    checkNullBehaviourWithType("timetz");
  }

  @Test
  public void testTimestamp() {
    checkNullBehaviourWithType("timestamp");
  }

  @Test
  public void testTimestamptz() {
    checkNullBehaviourWithType("timestamptz");
  }

  @Test
  public void testTsquery() {
    checkNullBehaviourWithType("tsquery");
  }

  @Test
  public void testTsvector() {
    checkNullBehaviourWithType("tsvector");
  }

  @Test
  public void testTxidSnapshot() {
    checkNullBehaviourWithType("txid_snapshot");
  }

  @Test
  public void testUuid() {
    checkNullBehaviourWithType("uuid");
  }
}
