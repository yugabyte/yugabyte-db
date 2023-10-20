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
import org.yb.cdc.common.ExpectedRecordYSQL;
import org.yb.cdc.common.HelperValues;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.util.CDCTestUtils;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.util.ArrayList;
import java.util.List;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestAllDatatypes extends CDCBaseClass {
  private final Logger LOG = LoggerFactory.getLogger(TestAllDatatypes.class);

  public void assertRecordsOnly(ExpectedRecordYSQL<?>[] expectedRecords,
                                CDCSubscriber testSubscriber) throws Exception {
    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // Ignoring the DDLs.
      if (outputList.get(i).getRowMessage().getOp() == Op.DDL) {
        ExpectedRecordYSQL.checkRecord(outputList.get(i),
                                       new ExpectedRecordYSQL<>(-1, "", Op.DDL));
        continue;
      }
      if (outputList.get(i).getRowMessage().getOp() == Op.BEGIN ||
          outputList.get(i).getRowMessage().getOp() == Op.COMMIT) {
        continue;
      }

      ExpectedRecordYSQL.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  public void createTables() throws Exception {
    statement.execute("create table testbit (a int primary key, b bit(6));");
    statement.execute("create table testboolean (a int primary key, b boolean);");
    statement.execute("create table testbox (a int primary key, b box);");
    statement.execute("create table testbytea (a int primary key, b bytea);");
    statement.execute("create table testcidr (a int primary key, b cidr);");
    statement.execute("create table testcircle (a int primary key, b circle);");
    statement.execute("create table testdate (a int primary key, b date);");
    statement.execute("create table testdouble (a int primary key, b double precision);");
    statement.execute("create table testinet (a int primary key, b inet);");
    statement.execute("create table testint (a int primary key, b int);");
    statement.execute("create table testjson (a int primary key, b json);");
    statement.execute("create table testjsonb (a int primary key, b jsonb);");
    statement.execute("create table testline (a int primary key, b line);");
    statement.execute("create table testlseg (a int primary key, b lseg);");
    statement.execute("create table testmacaddr8 (a int primary key, b macaddr8);");
    statement.execute("create table testmacaddr (a int primary key, b macaddr);");
    statement.execute("create table testmoney (a int primary key, b money);");
    statement.execute("create table testnumeric (a int primary key, b numeric);");
    statement.execute("create table testpath (a int primary key, b path);");
    statement.execute("create table testpoint (a int primary key, b point);");
    statement.execute("create table testpolygon (a int primary key, b polygon);");
    statement.execute("create table testtext (a int primary key, b text);");
    statement.execute("create table testtime (a int primary key, b time);");
    statement.execute("create table testtimestamp (a int primary key, b timestamp);");
    statement.execute("create table testtimetz (a int primary key, b timetz);");
    statement.execute("create table testuuid (a int primary key, b uuid);");
    statement.execute("create table testvarbit (a int primary key, b varbit(10));");
    statement.execute("create table testtstz (a int primary key, b timestamptz);");
    statement.execute("create table testint4range (a int primary key, b int4range);");
    statement.execute("create table testint8range (a int primary key, b int8range);");
    statement.execute("create table testtsrange (a int primary key, b tsrange);");
    statement.execute("create table testtstzrange (a int primary key, b tstzrange);");
    statement.execute("create table testdaterange (a int primary key, b daterange);");
    statement.execute("CREATE TYPE coupon_discount_type AS ENUM ('FIXED','PERCENTAGE');");
    statement.execute("create table testdiscount (a int primary key, b coupon_discount_type);");
  }

  @Before
  public void setUp() throws Exception {
    super.setUp();
    setServerFlag(getTserverHostAndPort(), CDC_POPULATE_SAFEPOINT_RECORD, "false");
    statement = connection.createStatement();
  }

  @Test
  public void testAllTypes() {
    try {
      statement.execute(HelperValues.dropAllTablesWithTypes);
      createTables();

      CDCSubscriber bitSub = new CDCSubscriber("testbit", getMasterAddresses());
      bitSub.createStream("proto");
      CDCSubscriber booleanSub = new CDCSubscriber("testboolean", getMasterAddresses());
      booleanSub.createStream("proto");
      CDCSubscriber boxSub = new CDCSubscriber("testbox", getMasterAddresses());
      boxSub.createStream("proto");
      CDCSubscriber byteaSub = new CDCSubscriber("testbytea", getMasterAddresses());
      byteaSub.createStream("proto");
      CDCSubscriber cidrSub = new CDCSubscriber("testcidr", getMasterAddresses());
      cidrSub.createStream("proto");
      CDCSubscriber circleSub = new CDCSubscriber("testcircle", getMasterAddresses());
      circleSub.createStream("proto");
      CDCSubscriber dateSub = new CDCSubscriber("testdate", getMasterAddresses());
      dateSub.createStream("proto");
      CDCSubscriber doubleSub = new CDCSubscriber("testdouble", getMasterAddresses());
      doubleSub.createStream("proto");
      CDCSubscriber inetSub = new CDCSubscriber("testinet", getMasterAddresses());
      inetSub.createStream("proto");
      CDCSubscriber intSub = new CDCSubscriber("testint", getMasterAddresses());
      intSub.createStream("proto");
      CDCSubscriber jsonSub = new CDCSubscriber("testjson", getMasterAddresses());
      jsonSub.createStream("proto");
      CDCSubscriber jsonbSub = new CDCSubscriber("testjsonb", getMasterAddresses());
      jsonbSub.createStream("proto");
      CDCSubscriber lineSub = new CDCSubscriber("testline", getMasterAddresses());
      lineSub.createStream("proto");
      CDCSubscriber lsegSub = new CDCSubscriber("testlseg", getMasterAddresses());
      lsegSub.createStream("proto");
      CDCSubscriber macaddr8Sub = new CDCSubscriber("testmacaddr8", getMasterAddresses());
      macaddr8Sub.createStream("proto");
      CDCSubscriber macaddrSub = new CDCSubscriber("testmacaddr", getMasterAddresses());
      macaddrSub.createStream("proto");
      CDCSubscriber moneySub = new CDCSubscriber("testmoney", getMasterAddresses());
      moneySub.createStream("proto");
      CDCSubscriber numericSub = new CDCSubscriber("testnumeric", getMasterAddresses());
      numericSub.createStream("proto");
      CDCSubscriber pathSub = new CDCSubscriber("testpath", getMasterAddresses());
      pathSub.createStream("proto");
      CDCSubscriber pointSub = new CDCSubscriber("testpoint", getMasterAddresses());
      pointSub.createStream("proto");
      CDCSubscriber polygonSub = new CDCSubscriber("testpolygon", getMasterAddresses());
      polygonSub.createStream("proto");
      CDCSubscriber textSub = new CDCSubscriber("testtext", getMasterAddresses());
      textSub.createStream("proto");
      CDCSubscriber timeSub = new CDCSubscriber("testtime", getMasterAddresses());
      timeSub.createStream("proto");
      CDCSubscriber timestampSub = new CDCSubscriber("testtimestamp", getMasterAddresses());
      timestampSub.createStream("proto");
      CDCSubscriber timetzSub = new CDCSubscriber("testtimetz", getMasterAddresses());
      timetzSub.createStream("proto");
      CDCSubscriber uuidSub = new CDCSubscriber("testuuid", getMasterAddresses());
      uuidSub.createStream("proto");
      CDCSubscriber varbitSub = new CDCSubscriber("testvarbit", getMasterAddresses());
      varbitSub.createStream("proto");
      CDCSubscriber tstzSub = new CDCSubscriber("testtstz", getMasterAddresses());
      tstzSub.createStream("proto");
      CDCSubscriber int4rangeSub = new CDCSubscriber("testint4range", getMasterAddresses());
      int4rangeSub.createStream("proto");
      CDCSubscriber int8rangeSub = new CDCSubscriber("testint8range", getMasterAddresses());
      int8rangeSub.createStream("proto");
      CDCSubscriber tsrangeSub = new CDCSubscriber("testtsrange", getMasterAddresses());
      tsrangeSub.createStream("proto");
      CDCSubscriber tstzrangeSub = new CDCSubscriber("testtstzrange", getMasterAddresses());
      tstzrangeSub.createStream("proto");
      CDCSubscriber daterangeSub = new CDCSubscriber("testdaterange", getMasterAddresses());
      daterangeSub.createStream("proto");
      CDCSubscriber udtSub = new CDCSubscriber("testdiscount", getMasterAddresses());
      udtSub.createStream("proto");

      CDCTestUtils.runSqlScript(connection, "sql_datatype_script/complete_datatype_test.sql");

      // -1 is used as a placeholder only.
      ExpectedRecordYSQL<?>[] expectedRecordsInteger = new ExpectedRecordYSQL[]{
        new ExpectedRecordYSQL<>(1, 2, Op.INSERT),
        new ExpectedRecordYSQL<>(3, 4, Op.INSERT),
        new ExpectedRecordYSQL<>(3, 5, Op.UPDATE),
        new ExpectedRecordYSQL<>(7, 8, Op.INSERT),
        new ExpectedRecordYSQL<>(7, "", Op.DELETE),
        new ExpectedRecordYSQL<>(8, 8, Op.INSERT),
        new ExpectedRecordYSQL<>(8, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsInteger, intSub);

      ExpectedRecordYSQL<?>[] expectedRecordsBoolean = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, false, Op.INSERT),
        new ExpectedRecordYSQL<>(3, true, Op.INSERT),
        new ExpectedRecordYSQL<>(3, false, Op.UPDATE),
        new ExpectedRecordYSQL<>(1, false, Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsBoolean, booleanSub);

      ExpectedRecordYSQL<?>[] expectedRecordsDouble = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, 10.42, Op.INSERT),
        new ExpectedRecordYSQL<>(3, 0.5, Op.INSERT),
        new ExpectedRecordYSQL<>(3, "", Op.DELETE),
        new ExpectedRecordYSQL<>(4, 0.5, Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsDouble, doubleSub);

      ExpectedRecordYSQL<?>[] expectedRecordsText = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "sample string with pk 1", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "sample string with pk 3", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "sample string with pk 2", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "random sample string", Op.UPDATE)
      };
      assertRecordsOnly(expectedRecordsText, textSub);

      ExpectedRecordYSQL<?>[] expectedRecordsUuid = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "ffffffff-ffff-ffff-ffff-ffffffffffff", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "ffffffff-ffff-ffff-ffff-ffffffffffff", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "123e4567-e89b-12d3-a456-426655440000", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "123e4567-e89b-12d3-a456-426655440000", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsUuid, uuidSub);

      ExpectedRecordYSQL<?>[] expectedRecordsTimestamp = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "2017-07-04 12:30:30", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "2021-09-29 00:00:00", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "1970-01-01 00:00:10", Op.UPDATE)
      };
      assertRecordsOnly(expectedRecordsTimestamp, timestampSub);

      ExpectedRecordYSQL<?>[] expectedRecordsDate = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "2021-09-20", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "2021-09-29", Op.UPDATE),
        new ExpectedRecordYSQL<>(2, "2000-01-01", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE),
        new ExpectedRecordYSQL<>(3, "1970-01-01", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "", Op.DELETE),
        new ExpectedRecordYSQL<>(4, "1970-01-01", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsDate, dateSub);

      ExpectedRecordYSQL<?>[] expectedRecordsInet = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "127.0.0.1", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "0.0.0.0", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "192.168.1.1", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsInet, inetSub);

      ExpectedRecordYSQL<?>[] expectedRecordsMacaddr = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "2c:54:91:88:c9:e3", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "2c:54:91:e8:99:d2", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "2c:54:91:e8:99:d2", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsMacaddr, macaddrSub);

      ExpectedRecordYSQL<?>[] expectedRecordsMacaddr8 = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "22:00:5c:03:55:08:01:02", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "22:00:5c:04:55:08:01:02", Op.UPDATE),
        new ExpectedRecordYSQL<>(2, "22:00:5c:03:55:08:01:02", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE),
        new ExpectedRecordYSQL<>(3, "22:00:5c:05:55:08:01:02", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsMacaddr8, macaddr8Sub);

      ExpectedRecordYSQL<?>[] expectedRecordsJson = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "{\"first_name\":\"vaibhav\"}", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "{\"last_name\":\"kushwaha\"}", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "{\"name\":\"vaibhav kushwaha\"}", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(3, "{\"a\":97, \"b\":\"98\"}", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsJson, jsonSub);

      // Do note that there is a space after the colon (:) coming into the streamed records.
      ExpectedRecordYSQL<?>[] expectedRecordsJsonb = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "{\"first_name\": \"vaibhav\"}", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "{\"last_name\": \"kushwaha\"}", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "{\"name\": \"vaibhav kushwaha\"}", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(3, "{\"a\": 97, \"b\": \"98\"}", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsJsonb, jsonbSub);

      ExpectedRecordYSQL<?>[] expectedRecordsBit = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "001111", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "110101", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "111111", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(0, "000000", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsBit, bitSub);

      ExpectedRecordYSQL<?>[] expectedRecordsVarbit = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "001111", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "1101011101", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "11", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(0, "0", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsVarbit, varbitSub);

      ExpectedRecordYSQL<?>[] expectedRecordsTime = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "11:30:59", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "23:30:59", Op.UPDATE),
        new ExpectedRecordYSQL<>(2, "00:00:01", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "00:01:00", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsTime, timeSub);

      ExpectedRecordYSQL<?>[] expectedRecordsTimetz = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "11:30:59+05:30", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "23:30:59+05:30", Op.UPDATE),
        new ExpectedRecordYSQL<>(2, "00:00:01+00", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsTimetz, timetzSub);

      ExpectedRecordYSQL<?>[] expectedRecordsNumeric = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, 20.5, Op.INSERT),
        new ExpectedRecordYSQL<>(2, 100.75, Op.INSERT),
        new ExpectedRecordYSQL<>(3, 3.456, Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsNumeric, numericSub);

      ExpectedRecordYSQL<?>[] expectedRecordsMoney = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "$100.50", Op.INSERT),
        new ExpectedRecordYSQL<>(2, "$10.12", Op.INSERT),
        new ExpectedRecordYSQL<>(3, "$1.23", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "$90.50", Op.UPDATE),
        new ExpectedRecordYSQL<>(2, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsMoney, moneySub);

      ExpectedRecordYSQL<?>[] expectedRecordsCidr = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "10.1.0.0/16", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "12.2.0.0/22", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "12.2.0.0/22", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsCidr, cidrSub);

      ExpectedRecordYSQL<?>[] expectedRecordsBytea = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "\\x01", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "\\xdeadbeef", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "\\xdeadbeef", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsBytea, byteaSub);

      ExpectedRecordYSQL<?>[] expectedRecordsBox = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "(8,9),(1,3)", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "(10,31),(8,9)", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(2, "(10,31),(8,9)", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsBox, boxSub);

      ExpectedRecordYSQL<?>[] expectedRecordsCircle = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(10, "<(2,3),32>", Op.INSERT),
        new ExpectedRecordYSQL<>(10, "<(0,0),10>", Op.UPDATE),
        new ExpectedRecordYSQL<>(10, "", Op.DELETE),
        new ExpectedRecordYSQL<>(1000, "<(0,0),4>", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsCircle, circleSub);

      ExpectedRecordYSQL<?>[] expectedRecordsPath = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(23, "((1,2),(20,-10))", Op.INSERT),
        new ExpectedRecordYSQL<>(23, "((-1,-1))", Op.UPDATE),
        new ExpectedRecordYSQL<>(23, "", Op.DELETE),
        new ExpectedRecordYSQL<>(34, "((0,0),(3,4),(5,5),(1,2))", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsPath, pathSub);

      ExpectedRecordYSQL<?>[] expectedRecordsPoint = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(11, "(0,-1)", Op.INSERT),
        new ExpectedRecordYSQL<>(11, "(1,3)", Op.UPDATE),
        new ExpectedRecordYSQL<>(11, "", Op.DELETE),
        new ExpectedRecordYSQL<>(21, "(33,44)", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsPoint, pointSub);

      ExpectedRecordYSQL<?>[] expectedRecordsPolygon = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "((1,3),(4,12),(2,4))", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "((1,3),(4,12),(2,4),(1,4))", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(27, "((1,3),(2,4),(1,4))", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsPolygon, polygonSub);

      ExpectedRecordYSQL<?>[] expectedRecordsLine = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "{1,2,-8}", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "{1,1,-5}", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(29, "{2.5,-1,0}", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsLine, lineSub);

      ExpectedRecordYSQL<?>[] expectedRecordsLseg = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "[(0,0),(2,4)]", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "[(-1,-1),(10,-8)]", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE),
        new ExpectedRecordYSQL<>(37, "[(1,3),(3,5)]", Op.INSERT)
      };
      assertRecordsOnly(expectedRecordsLseg, lsegSub);

      ExpectedRecordYSQL<?>[] expectedRecordsTimestamptz = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "1969-12-31 18:40:00+00", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "2021-12-31 18:40:00+00", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsTimestamptz, tstzSub);

      ExpectedRecordYSQL<?>[] expectedRecordsInt4Range = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "[5,14)", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "[6,43)", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsInt4Range, int4rangeSub);

      ExpectedRecordYSQL<?>[] expectedRecordsInt8Range = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "[5,15)", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "[2,100000)", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsInt8Range, int8rangeSub);

      ExpectedRecordYSQL<?>[] expectedRecordsTsRange = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "(\"1970-01-01 00:00:00\",\"2000-01-01 12:00:00\")",
          Op.INSERT),
        new ExpectedRecordYSQL<>(1, "(\"1970-01-01 00:00:00\",\"2022-11-01 12:00:00\")",
          Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsTsRange, tsrangeSub);

      ExpectedRecordYSQL<?>[] expectedRecordsTstzRange = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1,
          "(\"2017-07-04 12:30:30+00\",\"2021-07-04 07:00:30+00\")", Op.INSERT),
        new ExpectedRecordYSQL<>(1,
          "(\"2017-07-04 12:30:30+00\",\"2021-10-04 07:00:30+00\")", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsTstzRange, tstzrangeSub);

      ExpectedRecordYSQL<?>[] expectedRecordsDateRange = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "[2019-10-08,2021-10-07)", Op.INSERT),
        new ExpectedRecordYSQL<>(1, "[2019-10-08,2020-10-07)", Op.UPDATE),
        new ExpectedRecordYSQL<>(1, "", Op.DELETE)
      };
      assertRecordsOnly(expectedRecordsDateRange, daterangeSub);

      ExpectedRecordYSQL<?>[] expectedRecordsUDT = new ExpectedRecordYSQL[] {
        new ExpectedRecordYSQL<>(1, "FIXED", Op.INSERT),
      };
      assertRecordsOnly(expectedRecordsUDT, udtSub);
    } catch (Exception e) {
      LOG.error("Failed to test all datatypes", e);
      fail();
    }
  }

  @Test
  public void testDefaultForAllTypes() {
    try {
      assertFalse(statement.execute("drop table if exists testdefault;"));
      assertFalse(statement.execute(HelperValues.createTableWithDefaults));

      CDCSubscriber testSubscriber = new CDCSubscriber("testdefault", getMasterAddresses());
      testSubscriber.createStream("proto");
      assertEquals(1, statement.executeUpdate("insert into testdefault values (1);"));

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      boolean checkedInsertRecord = false;
      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getRowMessage().getOp() == Op.INSERT) {
          checkedInsertRecord = true;

          int changesCount = outputList.get(i).getRowMessage().getNewTupleCount();
          // there are 34 columns including the primary key one
          assertEquals(34, changesCount);
          assertEquals(1, outputList.get(i).getRowMessage().getNewTuple(0).getDatumInt32());

          for (int j = 1; j < changesCount; ++j) {
            Object streamedValue = ExpectedRecordYSQL.getValue(outputList.get(i), j);
            assertEquals(HelperValues.expectedDefaultValues[j-1], streamedValue);
          }
        }
      }

      // Just check whether an INSERT record was streamed.
      assertTrue(checkedInsertRecord);
    } catch (Exception e) {
      LOG.error("Test to verify default value streaming for all types failed", e);
      fail();
    }
  }
}
