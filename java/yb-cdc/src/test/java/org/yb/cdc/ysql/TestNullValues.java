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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.util.ArrayList;
import java.util.List;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common.DatumMessagePB;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;

@RunWith(value = YBTestRunner.class)
public class TestNullValues extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestNullValues.class);

  protected int DEFAULT_KEY_VALUE = 1;

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
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

  @Test
  public void testPartialInsert() {
    try {
      final String createTable = "CREATE TABLE test (a int primary key, b int, c int, d int);";
      statement.execute(createTable);

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      // Insert a row with value of one column
      statement.execute("BEGIN;");
      statement.execute("INSERT INTO test (a, c) VALUES (1, 3);");
      statement.execute("UPDATE test SET b = 2, d = 4 WHERE a = 1;");
      statement.execute("COMMIT;");

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      // This outputList should contain 6 records --> first DDL, BEGIN, INSERT, UPDATE, UPDATE
      // and COMMIT. Assert for each one of the record.

      // Declaring an object to be used later.
      CdcService.RowMessage rowMessage;

      // DDL record.
      rowMessage = outputList.get(0).getRowMessage();
      assertEquals(Op.DDL, rowMessage.getOp());
      // There will be 4 columns in the schema.
      assertEquals(4, rowMessage.getSchema().getColumnInfoCount());

      // BEGIN record.
      rowMessage = outputList.get(1).getRowMessage();
      assertEquals(Op.BEGIN, rowMessage.getOp());

      // INSERT record.
      rowMessage = outputList.get(2).getRowMessage();
      assertEquals(Op.INSERT, rowMessage.getOp());
      // Now since we have inserted only two columns (a, c), only they will have the value field,
      // the other columns won't have that field since the value for them is null.
      assertEquals("a", rowMessage.getNewTuple(0).getColumnName());
      assertTrue(rowMessage.getNewTuple(0).hasDatumInt32());
      assertEquals(1, rowMessage.getNewTuple(0).getDatumInt32());
      // Column b won't have any value.
      assertEquals("b", rowMessage.getNewTuple(1).getColumnName());
      assertFalse(rowMessage.getNewTuple(1).hasDatumInt32());
      // Column c will have a value.
      assertEquals("c", rowMessage.getNewTuple(2).getColumnName());
      assertTrue(rowMessage.getNewTuple(2).hasDatumInt32());
      assertEquals(3, rowMessage.getNewTuple(2).getDatumInt32());
      // Column d won't have any value.
      assertEquals("d", rowMessage.getNewTuple(3).getColumnName());
      assertFalse(rowMessage.getNewTuple(3).hasDatumInt32());

      // UPDATE record for update of column b.
      rowMessage = outputList.get(3).getRowMessage();
      assertEquals(Op.UPDATE, rowMessage.getOp());
      assertEquals("a", rowMessage.getNewTuple(0).getColumnName());
      assertEquals(1, rowMessage.getNewTuple(0).getDatumInt32());
      assertEquals("b", rowMessage.getNewTuple(1).getColumnName());
      assertEquals(2, rowMessage.getNewTuple(1).getDatumInt32());
      assertEquals("d", rowMessage.getNewTuple(2).getColumnName());
      assertEquals(4, rowMessage.getNewTuple(2).getDatumInt32());

      // COMMIT record.
      rowMessage = outputList.get(4).getRowMessage();
      assertEquals(Op.COMMIT, rowMessage.getOp());
    } catch (Exception e) {
      LOG.error("Test to verify partial insert of columns failed");
      fail();
    }
  }

  private void assertForNullValues(List<CdcService.CDCSDKProtoRecordPB> outputList) {
    assertNotNull(outputList);

    for (int i = 0; i < outputList.size(); ++i) {
      if (outputList.get(i).getRowMessage().getOp() == CdcService.RowMessage.Op.INSERT) {
        CdcService.RowMessage rm = outputList.get(i).getRowMessage();
        assertEquals(DEFAULT_KEY_VALUE, rm.getNewTuple(0).getDatumInt32());

        DatumMessagePB dm = rm.getNewTuple(1);
        // If it is null, no value type is going to be there.
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

  private void checkNullBehaviourWithType(String type) {
    try {
      String createTable = String.format("create table test (a int primary key, b %s);", type);
      assertFalse(statement.execute(createTable));

      // Creating one stream.
      CDCSubscriber testSubscriberProto = new CDCSubscriber(getMasterAddresses());
      testSubscriberProto.createStream("proto");

      int dummyInsert = statement.executeUpdate(String.format("insert into test values (%d);",
        DEFAULT_KEY_VALUE));
      assertEquals(1, dummyInsert);

      // Receiving response.
      List<CdcService.CDCSDKProtoRecordPB> outputListProto = new ArrayList<>();
      testSubscriberProto.getResponseFromCDC(outputListProto);

      assertForNullValues(outputListProto);
    } catch (Exception e) {
      LOG.error("Test to check for null values of column type " + type + " failed", e);
      fail();
    }
  }
}
