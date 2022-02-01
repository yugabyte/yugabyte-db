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
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.common.ExpectedRecordYSQLGeneric;
import org.yb.cdc.util.CDCSubscriber;

import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;
import org.yb.cdc.util.TestUtils;

import static org.yb.AssertionWrappers.*;

import java.util.ArrayList;
import java.util.List;

@Ignore
@RunWith(value = YBTestRunner.class)
public class TestArraysIndividually extends CDCBaseClass {
  private final static Logger LOG = Logger.getLogger(TestArraysIndividually.class);

  public static class UtilStrings {
    public static String INSERTION_TEMPLATE = "insert into %s values (1, %s, %s, %s, %s, " +
      "%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, " +
      "%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s);";

    public static String DROP_ALL_TABLES = "drop table if exists test, testmulti, testvb, " +
      "testboolval, testchval, testvchar, testdt, testdp, testinetval, testintval, testjsonval, " +
      "testjsonbval, testmac, testmac8, testmoneyval, testrl, testsi, testtextval, " +
      "testtval, testttzval, testtimestampval, testtimestamptzval, testu, testi4r, testi8r, " +
      "testdr, testtsr, testtstzr, testnr, testbx, testln, testls, testpt, testcrcl, " +
      "testpoly, testpth, testinterv, testcidrval, testtxid";

    public static String CREATE_MULTI_DIM_TABLE = "create table testmulti (a int primary key, " +
      "vb varbit(10)[], boolval boolean[], chval char(5)[], " +
      "vchar varchar(20)[], dt date[], " + "dp double precision[], " +
      "inetval inet[], intval integer[], jsonval json[], jsonbval jsonb[], mac macaddr[], " +
      "mac8 macaddr8[], moneyval money[], rl real[], si smallint[], textval text[], " +
      "tval time[], ttzval timetz[], timestampval timestamp[], timestamptzcal timestamptz[], " +
      "u uuid[], i4r int4range[], i8r int8range[], dr daterange[], tsr tsrange[], " +
      "tstzr tstzrange[], nr numrange[], bx box[], ln line[], ls lseg[], pt point[], " +
      "crcl circle[], poly polygon[], pth path[], interv interval[], cidrval cidr[], " +
      "txid txid_snapshot[]);";

    public static String[] expectedMultiDimRecords = {
      "{{1011,011101,1101110111},{1011,011101,1101110111}}",
      "{{f,t,t,f},{f,t,t,f}}",
      "{{five5,five5},{five5,five5}}",
      "{{\"sample varchar\",\"test string\"},{\"sample varchar\",\"test string\"}}",
      "{{2021-10-07,1970-01-01},{2021-10-07,1970-01-01}}",
      "{{1.23,2.34,3.45},{1.23,2.34,3.45}}",
      "{{127.0.0.1,192.168.1.1},{127.0.0.1,192.168.1.1}}",
      "{{1,2,3},{1,2,3}}",
      "{{\"{\\\"a\\\":\\\"b\\\"}\",\"{\\\"c\\\":\\\"d\\\"}\"}," +
        "{\"{\\\"a\\\":\\\"b\\\"}\",\"{\\\"c\\\":\\\"d\\\"}\"}}",
      "{{\"{\\\"a\\\": \\\"b\\\"}\",\"{\\\"c\\\": \\\"d\\\"}\"}," +
        "{\"{\\\"a\\\": \\\"b\\\"}\",\"{\\\"c\\\": \\\"d\\\"}\"}}",
      "{{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3,2c:54:f1:88:c9:e3}," +
        "{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3,2c:54:f1:88:c9:e3}}",
      "{{22:00:5c:03:55:08:01:02,22:10:5c:03:55:d8:f1:02}," +
        "{22:00:5c:03:55:08:01:02,22:10:5c:03:55:d8:f1:02}}",
      "{{$100.55,$200.50,$50.05},{$100.55,$200.50,$50.05}}",
      "{{1.23,4.56,7.8901},{1.23,4.56,7.8901}}",
      "{{1,2,3,4,5,6},{1,2,3,4,5,6}}",
      "{{sample1,sample2},{sample1,sample2}}",
      "{{12:00:32,22:10:20,23:59:59,00:00:00},{12:00:32,22:10:20,23:59:59,00:00:00}}",
      "{{11:00:00+05:30,23:00:59+00,09:59:00+00},{11:00:00+05:30,23:00:59+00,09:59:00+00}}",
      "{{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}," +
        "{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}}",
      "{{\"1969-12-31 18:30:10+00\",\"2000-01-01 00:00:10+00\"}," +
        "{\"1969-12-31 18:30:10+00\",\"2000-01-01 00:00:10+00\"}}",
      "{{123e4567-e89b-12d3-a456-426655440000,123e4567-e89b-12d3-a456-426655440000}," +
        "{123e4567-e89b-12d3-a456-426655440000,123e4567-e89b-12d3-a456-426655440000}}",
      "{{\"[2,5)\",\"[11,100)\"},{\"[2,5)\",\"[11,100)\"}}",
      "{{\"[2,10)\",\"[901,10000)\"},{\"[2,10)\",\"[901,10000)\"}}",
      "{{\"[2000-09-21,2021-10-08)\",\"[1970-01-02,2000-01-01)\"}," +
        "{\"[2000-09-21,2021-10-08)\",\"[1970-01-02,2000-01-01)\"}}",
      "{{\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"," +
        "\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"}," +
        "{\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"," +
        "\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"}}",
      "{{\"(\\\"2017-07-04 12:30:30+00\\\",\\\"2021-07-04 07:00:30+00\\\")\"," +
        "\"(\\\"1970-09-14 12:30:30+00\\\",\\\"2021-10-13 04:02:30+00\\\")\"}," +
        "{\"(\\\"2017-07-04 12:30:30+00\\\",\\\"2021-07-04 07:00:30+00\\\")\"," +
        "\"(\\\"1970-09-14 12:30:30+00\\\",\\\"2021-10-13 04:02:30+00\\\")\"}}",
      "{{\"(10.42,11.354)\",\"(-0.99,100.9)\"},{\"(10.42,11.354)\",\"(-0.99,100.9)\"}}",
      "{{(8,9),(1,3);(9,27),(-1,-1)};{(8,9),(1,3);(9,27),(-1,-1)}}",
      "{{\"{2.5,-1,0}\",\"{1,2,-10}\"},{\"{2.5,-1,0}\",\"{1,2,-10}\"}}",
      "{{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"},{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"}}",
      "{{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"},{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"}}",
      "{{\"<(1,2),4>\",\"<(-1,0),5>\"},{\"<(1,2),4>\",\"<(-1,0),5>\"}}",
      "{{\"((1,3),(4,12),(2,4))\",\"((1,-1),(4,-12),(-2,-4))\"}," +
        "{\"((1,3),(4,12),(2,4))\",\"((1,-1),(4,-12),(-2,-4))\"}}",
      "{{\"((1,2),(10,15),(0,0))\",\"((1,2),(10,15),(10,0),(-3,-2))\"}," +
        "{\"((1,2),(10,15),(0,0))\",\"((1,2),(10,15),(10,0),(-3,-2))\"}}",
      "{{01:16:06.2,\"29 days\"},{01:16:06.2,\"29 days\"}}",
      "{{12.2.0.0/22,10.1.0.0/16},{12.2.0.0/22,10.1.0.0/16}}",
      "{{3:3:,3:3:},{3:3:,3:3:}}"};

    public static String[] expectedSingleDimRecords = {
      "{1011,011101,1101110111}",
      "{f,t,t,f}",
      "{five5,five5}",
      "{\"sample varchar\",\"test string\"}",
      "{2021-10-07,1970-01-01}",
      "{1.23,2.34,3.45}",
      "{127.0.0.1,192.168.1.1}",
      "{1,2,3}",
      "{\"{\\\"a\\\":\\\"b\\\"}\",\"{\\\"c\\\":\\\"d\\\"}\"}",
      "{\"{\\\"a\\\": \\\"b\\\"}\",\"{\\\"c\\\": \\\"d\\\"}\"}",
      "{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3,2c:54:f1:88:c9:e3}",
      "{22:00:5c:03:55:08:01:02,22:10:5c:03:55:d8:f1:02}",
      "{$100.55,$200.50,$50.05}",
      "{1.23,4.56,7.8901}",
      "{1,2,3,4,5,6}",
      "{sample1,sample2}",
      "{12:00:32,22:10:20,23:59:59,00:00:00}",
      "{11:00:00+05:30,23:00:59+00,09:59:00+00}",
      "{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}",
      "{\"1969-12-31 18:30:10+00\",\"2000-01-01 00:00:10+00\"}",
      "{123e4567-e89b-12d3-a456-426655440000,123e4567-e89b-12d3-a456-426655440000}",
      "{\"[2,5)\",\"[11,100)\"}",
      "{\"[2,10)\",\"[901,10000)\"}",
      "{\"[2000-09-21,2021-10-08)\",\"[1970-01-02,2000-01-01)\"}",
      "{\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"," +
        "\"(\\\"1970-01-01 00:00:00\\\",\\\"2000-01-01 12:00:00\\\")\"}",
      "{\"(\\\"2017-07-04 12:30:30+00\\\",\\\"2021-07-04 07:00:30+00\\\")\"," +
        "\"(\\\"1970-09-14 12:30:30+00\\\",\\\"2021-10-13 04:02:30+00\\\")\"}",
      "{\"(10.42,11.354)\",\"(-0.99,100.9)\"}",
      "{(8,9),(1,3);(9,27),(-1,-1)}",
      "{\"{2.5,-1,0}\",\"{1,2,-10}\"}",
      "{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"}",
      "{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"}",
      "{\"<(1,2),4>\",\"<(-1,0),5>\"}",
      "{\"((1,3),(4,12),(2,4))\",\"((1,-1),(4,-12),(-2,-4))\"}",
      "{\"((1,2),(10,15),(0,0))\",\"((1,2),(10,15),(10,0),(-3,-2))\"}",
      "{01:16:06.2,\"29 days\"}",
      "{12.2.0.0/22,10.1.0.0/16}",
      "{3:3:,3:3:}"};
  }

  private void assertArrayRecords(ExpectedRecordYSQLGeneric<?> expectedRecord,
                             String sqlScript, String tableName) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(tableName, getMasterAddresses());
    testSubscriber.createStream();

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    // for now there is just one insert in the script
    List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    boolean insertRecordVerified = false;
    for (int i = 0; i < outputList.size(); ++i) {
      if (outputList.get(i).getOperation() == OperationType.INSERT) {
        insertRecordVerified = true;
        ExpectedRecordYSQLGeneric.checkRecord(outputList.get(i), expectedRecord);
      }
    }
    assertTrue(insertRecordVerified);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute(UtilStrings.DROP_ALL_TABLES);

    statement.execute("create table testvb (a int primary key, vb varbit(10)[]);");
    statement.execute("create table testboolval (a int primary key, boolval boolean[]);");
    statement.execute("create table testchval (a int primary key, chval char(5)[]);");
    statement.execute("create table testvchar (a int primary key, vchar varchar(20)[]);");
    statement.execute("create table testdt (a int primary key, dt date[]);");
    statement.execute("create table testdp (a int primary key, dp double precision[]);");
    statement.execute("create table testinetval (a int primary key, inetval inet[]);");
    statement.execute("create table testintval (a int primary key, intval integer[]);");
    statement.execute("create table testjsonval (a int primary key, jsonval json[]);");
    statement.execute("create table testjsonbval (a int primary key, jsonbval jsonb[]);");
    statement.execute("create table testmac (a int primary key, mac macaddr[]);");
    statement.execute("create table testmac8 (a int primary key, mac8 macaddr8[]);");
    statement.execute("create table testmoneyval (a int primary key, moneyval money[]);");
    statement.execute("create table testrl (a int primary key, rl real[]);");
    statement.execute("create table testsi (a int primary key, si smallint[]);");
    statement.execute("create table testtextval (a int primary key, textval text[]);");
    statement.execute("create table testtval (a int primary key, tval time[]);");
    statement.execute("create table testttzval (a int primary key, ttzval timetz[]);");
    statement.execute("create table testtimestampval (a int primary key, " +
      "timestampval timestamp[]);");
    statement.execute("create table testtimestamptzval (a int primary key, " +
      "timestamptzval timestamptz[]);");
    statement.execute("create table testu (a int primary key, u uuid[]);");
    statement.execute("create table testi4r (a int primary key, i4r int4range[]);");
    statement.execute("create table testi8r (a int primary key, i8r int8range[]);");
    statement.execute("create table testdr (a int primary key, dr daterange[]);");
    statement.execute("create table testtsr (a int primary key, tsr tsrange[]);");
    statement.execute("create table testtstzr (a int primary key, tstzr tstzrange[]);");
    statement.execute("create table testnr (a int primary key, nr numrange[]);");
    statement.execute("create table testbx (a int primary key, bx box[]);");
    statement.execute("create table testln (a int primary key, ln line[]);");
    statement.execute("create table testls (a int primary key, ls lseg[]);");
    statement.execute("create table testpt (a int primary key, pt point[]);");
    statement.execute("create table testcrcl (a int primary key, crcl circle[]);");
    statement.execute("create table testpoly (a int primary key, poly polygon[]);");
    statement.execute("create table testpth (a int primary key, pth path[]);");
    statement.execute("create table testinterv (a int primary key, interv interval[]);");
    statement.execute("create table testcidrval (a int primary key, cidrval cidr[]);");
    statement.execute("create table testtxid (a int primary key, txid txid_snapshot[]);");
  }

  @Test
  public void testMultiDimArrays() {
    try {
      assertFalse(statement.execute(UtilStrings.CREATE_MULTI_DIM_TABLE));
      CDCSubscriber testSubscriber = new CDCSubscriber("testmulti", getMasterAddresses());
      testSubscriber.createStream("proto");

      String varBit = "'{{1011, 011101, 1101110111}, {1011, 011101, 1101110111}}'::varbit(10)[]";
      String booleanVal = "'{{FALSE, TRUE, TRUE, FALSE}, {FALSE, TRUE, TRUE, FALSE}}'::boolean[]";
      String charVal = "'{{\"five5\", \"five5\"}, {\"five5\", \"five5\"}}'::char(5)[]";
      String varChar = "'{{\"sample varchar\", \"test string\"}, " +
        "{\"sample varchar\", \"test string\"}}'::varchar(20)[]";
      String dt = "'{{\"2021-10-07\", \"1970-01-01\"}, {\"2021-10-07\", \"1970-01-01\"}}'::date[]";
      String doublePrecision = "'{{1.23, 2.34, 3.45}, {1.23, 2.34, 3.45}}'::double precision[]";
      String inetVal = "'{{127.0.0.1, 192.168.1.1}, {127.0.0.1, 192.168.1.1}}'::inet[]";
      String integer = "'{{1, 2, 3}, {1, 2, 3}}'::integer[]";
      String jsonVal = "array[['{\"a\":\"b\"}', '{\"c\":\"d\"}'], " +
        "['{\"a\":\"b\"}', '{\"c\":\"d\"}']]::json[]";
      String jsonBVal = "array[['{\"a\":\"b\"}', '{\"c\":\"d\"}'], " +
        "['{\"a\":\"b\"}', '{\"c\":\"d\"}']]::jsonb[]";
      String macaddr = "'{{2c:54:91:88:c9:e3, 2c:b8:01:76:c9:e3, 2c:54:f1:88:c9:e3}, " +
        "{2c:54:91:88:c9:e3, 2c:b8:01:76:c9:e3, 2c:54:f1:88:c9:e3}}'::macaddr[]";
      String macaddr8 = "'{{22:00:5c:03:55:08:01:02, 22:10:5c:03:55:d8:f1:02}, " +
        "{22:00:5c:03:55:08:01:02, 22:10:5c:03:55:d8:f1:02}}'::macaddr8[]";
      String money = "'{{100.55, 200.50, 50.05}, {100.55, 200.50, 50.05}}'::money[]";
      String realVal = "'{{1.23, 4.56, 7.8901}, {1.23, 4.56, 7.8901}}'::real[]";
      String smallInt = "'{{1, 2, 3, 4, 5, 6}, {1, 2, 3, 4, 5, 6}}'::smallint[]";
      String text = "'{{\"sample1\", \"sample2\"}, {\"sample1\", \"sample2\"}}'::text[]";
      String time = "'{{12:00:32, 22:10:20, 23:59:59, 00:00:00}, " +
        "{12:00:32, 22:10:20, 23:59:59, 00:00:00}}'::time[]";
      String timetz = "'{{11:00:00+05:30, 23:00:59+00, 09:59:00 UTC}, " +
        "{11:00:00+05:30, 23:00:59+00, 09:59:00 UTC}}'::timetz[]";
      String timestamp = "'{{1970-01-01 0:00:10, 2000-01-01 0:00:10}, " +
        "{1970-01-01 0:00:10, 2000-01-01 0:00:10}}'::timestamp[]";
      String timestamptz = "'{{1970-01-01 0:00:10+05:30, 2000-01-01 0:00:10 UTC}, " +
        "{1970-01-01 0:00:10+05:30, 2000-01-01 0:00:10 UTC}}'::timestamptz[]";
      String uuid = "'{{123e4567-e89b-12d3-a456-426655440000, " +
        "123e4567-e89b-12d3-a456-426655440000}, " +
        "{123e4567-e89b-12d3-a456-426655440000, " +
        "123e4567-e89b-12d3-a456-426655440000}}'::uuid[]";
      String i4r = "array[['(1, 5)', '(10, 100)'], ['(1, 5)', '(10, 100)']]::int4range[]";
      String i8r = "array[['(1, 10)', '(900, 10000)'], ['(1, 10)', '(900, 10000)']]::int8range[]";
      String dr = "array[['(2000-09-20, 2021-10-08)', '(1970-01-01, 2000-01-01)'], " +
        "['(2000-09-20, 2021-10-08)', '(1970-01-01, 2000-01-01)']]::daterange[]";
      String tsr = "array[['(1970-01-01 00:00:00, 2000-01-01 12:00:00)', " +
        "'(1970-01-01 00:00:00, 2000-01-01 12:00:00)'], " +
        "['(1970-01-01 00:00:00, 2000-01-01 12:00:00)', " +
        "'(1970-01-01 00:00:00, 2000-01-01 12:00:00)']]::tsrange[]";
      String tstzr = "array[['(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)', " +
        "'(1970-09-14 12:30:30 UTC, 2021-10-13 09:32:30+05:30)'], " +
        "['(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)', " +
        "'(1970-09-14 12:30:30 UTC, 2021-10-13 09:32:30+05:30)']]::tstzrange[]";
      String nr = "array[['(10.42, 11.354)', '(-0.99, 100.9)'], " +
        "['(10.42, 11.354)', '(-0.99, 100.9)']]::numrange[]";
      String box = "array[['(8,9), (1,3)', '(-1,-1), (9,27)'], " +
        "['(8,9), (1,3)', '(-1,-1), (9,27)']]::box[]";
      String line = "array[['[(0, 0), (2, 5)]', '{1, 2, -10}'], " +
        "['[(0, 0), (2, 5)]', '{1, 2, -10}']]::line[]";
      String lseg = "array[['[(0, 0), (2, 5)]', '[(0, 5), (6, 2)]'], " +
        "['[(0, 0), (2, 5)]', '[(0, 5), (6, 2)]']]::lseg[]";
      String point = "array[['(1, 2)', '(10, 11.5)', '(0, -1)'], " +
        "['(1, 2)', '(10, 11.5)', '(0, -1)']]::point[]";
      String circle = "array[['1, 2, 4', '-1, 0, 5'], ['1, 2, 4', '-1, 0, 5']]::circle[]";
      String polygon = "array[['(1, 3), (4, 12), (2, 4)', '(1, -1), (4, -12), (-2, -4)'], " +
        "['(1, 3), (4, 12), (2, 4)', '(1, -1), (4, -12), (-2, -4)']]::polygon[]";
      String path = "array[['(1, 2), (10, 15), (0, 0)', '(1, 2), (10, 15), (10, 0), (-3, -2)'], " +
        "['(1, 2), (10, 15), (0, 0)', '(1, 2), (10, 15), (10, 0), (-3, -2)']]::path[]";
      String interval = "array[['2020-03-10 13:47:19.7':: timestamp - " +
        "'2020-03-10 12:31:13.5':: timestamp, " +
        "'2020-03-10 00:00:00':: timestamp - '2020-02-10 00:00:00':: timestamp], " +
        "['2020-03-10 13:47:19.7':: timestamp - '2020-03-10 12:31:13.5':: timestamp, " +
        "'2020-03-10 00:00:00':: timestamp - '2020-02-10 00:00:00':: timestamp]]::interval[]";
      String cidr = "array[['12.2.0.0/22', '10.1.0.0/16'], ['12.2.0.0/22', '10.1.0.0/16']]::cidr[]";
      String txidSnapshot = "array[[txid_current_snapshot(), txid_current_snapshot()], " +
        "[txid_current_snapshot(), txid_current_snapshot()]]::txid_snapshot[]";

      String insertIntoTable = String.format(UtilStrings.INSERTION_TEMPLATE, "testmulti",
        varBit, booleanVal, charVal, varChar, dt, doublePrecision, inetVal, integer, jsonVal,
        jsonBVal, macaddr, macaddr8, money, realVal, smallInt, text, time, timetz, timestamp,
        timestamptz, uuid, i4r, i8r, dr, tsr, tstzr, nr, box, line, lseg, point, circle, polygon,
        path, interval, cidr, txidSnapshot);

      String[] expectedRecords = UtilStrings.expectedMultiDimRecords;

      int insert = statement.executeUpdate(insertIntoTable);
      assertEquals(1, insert);

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);
      assertTrue(outputList.size() > 1);

      boolean insertRecordAsserted = false;
      for (CdcService.CDCSDKProtoRecordPB record : outputList) {
        if (record.getRowMessage().getOp() == CdcService.RowMessage.Op.INSERT) {
          insertRecordAsserted = true;
          CdcService.RowMessage rm = record.getRowMessage();
          int tupCount = rm.getNewTupleCount();
          // the first one in our case is a int value
          assertEquals(1, rm.getNewTuple(0).getDatumInt32());

          for (int i = 1; i < tupCount; ++i) {
            assertEquals(expectedRecords[i-1], rm.getNewTuple(i).getDatumString());
          }
        }
      }
      assertTrue(insertRecordAsserted);
    } catch (Exception e) {
      LOG.error("Test to verify CDC streaming for multi-dimensional arrays failed", e);
      fail();
    }
  }

  @Test
  public void testAllArrayTypes() {
    try {
      assertFalse(statement.execute(UtilStrings.DROP_ALL_TABLES));

      String createTable = "create table test (a int primary key, " +
        "vb varbit(10)[], boolval boolean[], chval char(5)[], " +
        "vchar varchar(20)[], dt date[], " + "dp double precision[], " +
        "inetval inet[], intval integer[], jsonval json[], jsonbval jsonb[], mac macaddr[], " +
        "mac8 macaddr8[], moneyval money[], rl real[], si smallint[], textval text[], " +
        "tval time[], ttzval timetz[], timestampval timestamp[], timestamptzcal timestamptz[], " +
        "u uuid[], i4r int4range[], i8r int8range[], dr daterange[], tsr tsrange[], " +
        "tstzr tstzrange[], nr numrange[], bx box[], ln line[], ls lseg[], pt point[], " +
        "crcl circle[], poly polygon[], pth path[], interv interval[], cidrval cidr[], " +
        "txid txid_snapshot[]);";

      assertFalse(statement.execute(createTable));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      String insertIntoTable = "insert into test values (1, " +
        "'{1011, 011101, 1101110111}', " +
        "'{FALSE, TRUE, TRUE, FALSE}', " +
        "'{\"five5\", \"five5\"}', " +
        "'{\"sample varchar\", \"test string\"}', " +
        "'{\"2021-10-07\", \"1970-01-01\"}', " +
        "'{1.23, 2.34, 3.45}', " +
        "'{127.0.0.1, 192.168.1.1}', " +
        "'{1, 2, 3}', " +
        "array['{\"a\":\"b\"}', '{\"c\":\"d\"}']::json[], " +
        "array['{\"a\":\"b\"}', '{\"c\":\"d\"}']::jsonb[], " +
        "'{2c:54:91:88:c9:e3, 2c:b8:01:76:c9:e3, 2c:54:f1:88:c9:e3}', " +
        "'{22:00:5c:03:55:08:01:02, 22:10:5c:03:55:d8:f1:02}', " +
        "'{100.55, 200.50, 50.05}', " +
        "'{1.23, 4.56, 7.8901}', " +
        "'{1, 2, 3, 4, 5, 6}', " +
        "'{\"sample1\", \"sample2\"}', " +
        "'{12:00:32, 22:10:20, 23:59:59, 00:00:00}', " +
        "'{11:00:00+05:30, 23:00:59+00, 09:59:00 UTC}', " +
        "'{1970-01-01 0:00:10, 2000-01-01 0:00:10}', " +
        "'{1970-01-01 0:00:10+05:30, 2000-01-01 0:00:10 UTC}', " +
        "'{123e4567-e89b-12d3-a456-426655440000, 123e4567-e89b-12d3-a456-426655440000}', " +
        "array['(1, 5)', '(10, 100)']::int4range[], " +
        "array['(1, 10)', '(900, 10000)']::int8range[], " +
        "array['(2000-09-20, 2021-10-08)', '(1970-01-01, 2000-01-01)']::daterange[], " +
        "array['(1970-01-01 00:00:00, 2000-01-01 12:00:00)', '(1970-01-01 00:00:00, " +
        "2000-01-01 12:00:00)']::tsrange[], " +
        "array['(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)', " +
        "'(1970-09-14 12:30:30 UTC, 2021-10-13 09:32:30+05:30)']::tstzrange[], " +
        "array['(10.42, 11.354)', '(-0.99, 100.9)']::numrange[], " +
        "array['(8,9), (1,3)', '(-1,-1), (9,27)']::box[], " +
        "array['[(0, 0), (2, 5)]', '{1, 2, -10}']::line[], " +
        "array['[(0, 0), (2, 5)]', '[(0, 5), (6, 2)]']::lseg[], " +
        "array['(1, 2)', '(10, 11.5)', '(0, -1)']::point[], " +
        "array['1, 2, 4', '-1, 0, 5']::circle[], " +
        "array['(1, 3), (4, 12), (2, 4)', '(1, -1), (4, -12), (-2, -4)']::polygon[], " +
        "array['(1, 2), (10, 15), (0, 0)', '(1, 2), (10, 15), (10, 0), (-3, -2)']::path[], " +
        "array['2020-03-10 13:47:19.7':: timestamp - '2020-03-10 12:31:13.5':: timestamp, " +
        "'2020-03-10 00:00:00':: timestamp - '2020-02-10 00:00:00':: timestamp]::interval[], " +
        "array['12.2.0.0/22', '10.1.0.0/16']::cidr[], " +
        "array[txid_current_snapshot(), txid_current_snapshot()]::txid_snapshot[]);";

      int rows = statement.executeUpdate(insertIntoTable);
      assertEquals(1, rows);

      String[] expectedRecords = UtilStrings.expectedSingleDimRecords;

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);
      assertTrue(outputList.size() > 1);

      boolean insertRecordAsserted = false;
      for (CdcService.CDCSDKProtoRecordPB record : outputList) {
        if (record.getRowMessage().getOp() == CdcService.RowMessage.Op.INSERT) {
          insertRecordAsserted = true;
          CdcService.RowMessage rm = record.getRowMessage();
          int tupCount = rm.getNewTupleCount();
          // the first one in our case is a int value
          assertEquals(1, rm.getNewTuple(0).getDatumInt32());

          for (int i = 1; i < tupCount; ++i) {
            assertEquals(expectedRecords[i-1], rm.getNewTuple(i).getDatumString());
          }
        }
      }

      assertTrue(insertRecordAsserted);
    } catch (Exception e) {
      LOG.error("Test to verify array types failed with exception", e);
      fail();
    }
  }

  @Test
  public void testVarbitArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{1011,011101,1101110111}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
                         "testvb");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for varbit array failed", e);
      fail();
    }
  }

  @Test
  public void testBooleanArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{f,t,t,f}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testboolval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for boolean array failed", e);
      fail();
    }

  }

  @Test
  public void testCharArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{five5,five5}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testchval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for char(n) array failed", e);
      fail();
    }
  }

  @Test
  public void testVarcharArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"sample varchar\",\"test string\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testvchar");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for varchar(n) array failed", e);
      fail();
    }
  }

  @Test
  public void testDateArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{2021-10-07,1970-01-01}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testdt");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for date array failed", e);
      fail();
    }
  }

  @Test
  public void testDoublePrecisionArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{1.23,2.34,3.45}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testdp");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for double precision array failed", e);
      fail();
    }
  }

  @Test
  public void testInetArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{127.0.0.1,192.168.1.1}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testinetval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for inet array failed", e);
      fail();
    }
  }

  @Test
  public void testIntegerArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{1,2,3}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testintval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for int array failed", e);
      fail();
    }
  }

  @Test
  public void testJsonArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"{\\\"a\\\":\\\"b\\\"}\"," +
            "\"{\\\"c\\\":\\\"d\\\"}\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testjsonval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for json array failed", e);
      fail();
    }
  }

  @Test
  public void testJsonbArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"{\\\"a\\\": \\\"b\\\"}\"," +
            "\"{\\\"c\\\": \\\"d\\\"}\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testjsonbval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for jsonb array failed", e);
      fail();
    }
  }

  @Test
  public void testMacaddrArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{2c:54:91:88:c9:e3,2c:b8:01:76:c9:e3," +
            "2c:54:f1:88:c9:e3}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testmac");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for macaddr array failed", e);
      fail();
    }
  }

  @Test
  public void testMacaddr8Arr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{22:00:5c:03:55:08:01:02," +
            "22:10:5c:03:55:d8:f1:02}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testmac8");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for macaddr8 array failed", e);
      fail();
    }
  }

  @Test
  public void testMoneyArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{$100.55,$200.50,$50.05}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testmoneyval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for money array failed", e);
      fail();
    }
  }

  @Test
  public void testRealArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{1.23,4.56,7.8901}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testrl");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for real array failed", e);
      fail();
    }
  }

  @Test
  public void testSmallintArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{1,2,3,4,5,6}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testsi");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for smallint array failed", e);
      fail();
    }
  }

  @Test
  public void testTextArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{sample1,sample2}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtextval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for text array failed", e);
      fail();
    }
  }

  @Test
  public void testTimeArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{12:00:32,22:10:20,23:59:59,00:00:00}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for time array failed", e);
      fail();
    }
  }

  @Test
  public void testTimetzArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{11:00:00+05:30,23:00:59+00,09:59:00+00}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testttzval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for timetz array failed", e);
      fail();
    }
  }

  @Test
  public void testTimestampArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"1970-01-01 00:00:10\",\"2000-01-01 00:00:10\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtimestampval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for timestamp array failed", e);
      fail();
    }
  }

  @Test
  public void testTimestamptzArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"1969-12-31 18:30:10+00\"," +
            "\"2000-01-01 00:00:10+00\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtimestamptzval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for timestamptz array failed", e);
      fail();
    }
  }

  @Test
  public void testUuidArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{123e4567-e89b-12d3-a456-426655440000," +
            "123e4567-e89b-12d3-a456-426655440000}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testu");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for uuid array failed", e);
      fail();
    }
  }

  @Test
  public void testI4RArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"[2,5)\",\"[11,100)\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testi4r");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for int4range array failed", e);
      fail();
    }
  }

  @Test
  public void testI8RArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"[2,10)\",\"[901,10000)\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testi8r");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for int8range array failed", e);
      fail();
    }
  }

  @Test
  public void testDaterangeArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"[2000-09-21,2021-10-08)\"," +
            "\"[1970-01-02,2000-01-01)\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testdr");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for daterange array failed", e);
      fail();
    }
  }

  @Test
  public void testTsrangeArr() {
     try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"(\\\"1970-01-01 00:00:00\\\"," +
            "\\\"2000-01-01 12:00:00\\\")\",\"(\\\"1970-01-01 00:00:00\\\"," +
            "\\\"2000-01-01 12:00:00\\\")\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtsr");
    } catch (Exception e) {
       LOG.error("Test to verify CDC behaviour for tsrange array failed", e);
       fail();
     }
  }

  @Test
  public void testTstzrangeArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"(\\\"2017-07-04 12:30:30+00\\\"," +
            "\\\"2021-07-04 07:00:30+00\\\")\",\"(\\\"1970-09-14 12:30:30+00\\\"," +
            "\\\"2021-10-13 04:02:30+00\\\")\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtstzr");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for tstzrange array failed", e);
      fail();
    }
  }

  @Test
  public void testNumrangeArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"(10.42,11.354)\",\"(-0.99,100.9)\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testnr");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for numrange array failed", e);
      fail();
    }
  }

  @Test
  public void testBoxArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{(8,9),(1,3);(9,27),(-1,-1)}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testbx");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for box array failed", e);
      fail();
    }
  }

  @Test
  public void testLineArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"{2.5,-1,0}\",\"{1,2,-10}\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testln");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for line array failed", e);
      fail();
    }
  }

  @Test
  public void testLsegArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"[(0,0),(2,5)]\",\"[(0,5),(6,2)]\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testls");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for lseg array failed", e);
      fail();
    }
  }

  @Test
  public void testPointArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"(1,2)\",\"(10,11.5)\",\"(0,-1)\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testpt");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for point array failed", e);
      fail();
    }
  }

  @Test
  public void testCircleArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"<(1,2),4>\",\"<(-1,0),5>\"}",
                                        OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testcrcl");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for circle array failed", e);
      fail();
    }
  }

  @Test
  public void testPolygonArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"((1,3),(4,12),(2,4))\"," +
            "\"((1,-1),(4,-12),(-2,-4))\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testpoly");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for polygon array failed", e);
      fail();
    }
  }

  @Test
  public void testPathArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{\"((1,2),(10,15),(0,0))\"," +
            "\"((1,2),(10,15),(10,0),(-3,-2))\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testpth");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for path array failed", e);
      fail();
    }
  }

  @Test
  public void testIntervalArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{01:16:06.2,\"29 days\"}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testinterv");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for interval array failed", e);
      fail();
    }
  }

  @Test
  public void testCidrArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{12.2.0.0/22,10.1.0.0/16}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testcidrval");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for cidr array failed", e);
      fail();
    }
  }

  @Test
  public void testTxidSnapshotArr() {
    try {
      ExpectedRecordYSQLGeneric<?> expectedRecord =
        new ExpectedRecordYSQLGeneric<>("1", "{3:3:,3:3:}", OperationType.INSERT);

      assertArrayRecords(expectedRecord, "sql_datatype_script/complete_array_types.sql",
        "testtxid");
    } catch (Exception e) {
      LOG.error("Test to verify CDC behaviour for txid_snapshot array failed", e);
      fail();
    }
  }
}
