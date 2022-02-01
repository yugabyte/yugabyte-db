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
import org.junit.Ignore;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.CDCSDKRecordPB.OperationType;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.cdc.common.ExpectedRecordYSQLGeneric;
import org.yb.cdc.util.TestUtils;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

@Ignore
@RunWith(value = YBTestRunner.class)
public class TestDatatypesIndividually extends CDCBaseClass {
  private final Logger LOG = Logger.getLogger(TestDatatypesIndividually.class);

  public static class UtilStrings {
    public static final String dropAllTables = "drop table if exists testbit, testboolean, " +
      "testbox, testbytea, testcidr, testcircle, testdate, testdouble, testinet, " +
      "testint, testjson, testjsonb, testline, testlseg, testmacaddr8, testmacaddr, " +
      "testmoney, testnumeric, testpath, testpoint, testpolygon, testtext, testtime, " +
      "testtimestamp, testtimetz, testuuid, testvarbit, testtstz, testint4range, " +
      "testint8range, testtsrange, testtstzrange, testdaterange, testdefault;";

    public static final String createTableWithDefaults = "create table testdefault " +
      "(a int primary key, " +
      "bitval bit(4) default '1111', boolval boolean default TRUE, " +
      "boxval box default '(0,0),(1,1)', byteval bytea default E'\\\\001', " +
      "cidrval cidr default '10.1.0.0/16', crcl circle default '0,0,5'," +
      "dt date default '2000-01-01', dp double precision default 32.34, " +
      "inetval inet default '127.0.0.1', i int default 404, " +
      "js json default '{\"a\":\"b\"}', jsb jsonb default '{\"a\":\"b\"}', " +
      "ln line default '{1,2,-8}', ls lseg default '[(0,0),(2,4)]', " +
      "mc8 macaddr8 default '22:00:5c:03:55:08:01:02', mc macaddr default '2C:54:91:88:C9:E3', " +
      "mn money default 100, nm numeric default 12.34, " +
      "pth path default '(1,2),(20,-10)', pnt point default '(0,0)', " +
      "poly polygon default '(1,3),(4,12),(2,4)', txt text default 'default text value', " +
      "tm time default '00:00:00', ts timestamp default '2000-09-01 00:00:00', " +
      "ttz timetz default '00:00:00+05:30', " +
      "u uuid default 'ffffffff-ffff-ffff-ffff-ffffffffffff', " +
      "vb varbit(4) default '11', tstz timestamptz default '1970-01-01 00:10:00+05:30', " +
      "i4r int4range default '(1,10)', i8r int8range default '(100, 200)', " +
      "tsr tsrange default '(1970-01-01 00:00:00, 1970-01-01 12:00:00)', " +
      "tstzr tstzrange default '(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)', " +
      "dr daterange default '(1970-01-01,2000-01-01)');";

    public static String[] expectedDefaultValues = {"1111", "t", "(1,1),(0,0)", "\\x01",
      "10.1.0.0/16", "<(0,0),5>", "2000-01-01", "32.34", "127.0.0.1", "404", "{\"a\":\"b\"}",
      "{\"a\": \"b\"}", "{1,2,-8}", "[(0,0),(2,4)]", "22:00:5c:03:55:08:01:02",
      "2c:54:91:88:c9:e3", "$100.00", "12.34", "((1,2),(20,-10))", "(0,0)", "((1,3),(4,12),(2,4))",
      "default text value", "00:00:00", "2000-09-01 00:00:00", "00:00:00+05:30",
      "ffffffff-ffff-ffff-ffff-ffffffffffff", "11", "1969-12-31 18:40:00+00", "[2,10)",
      "[101,200)", "(\"1970-01-01 00:00:00\",\"1970-01-01 12:00:00\")",
      "(\"2017-07-04 12:30:30+00\",\"2021-07-04 07:00:30+00\")", "[1970-01-02,2000-01-01)",
      ""};
  }

  public void executeScriptAssertRecords(ExpectedRecordYSQLGeneric<?>[] expectedRecords,
                                         String sqlScript, String tableName) throws Exception {
    CDCSubscriber testSubscriber = new CDCSubscriber(tableName, getMasterAddresses());
    testSubscriber.createStream();

    if (!sqlScript.isEmpty()) {
      TestUtils.runSqlScript(connection, sqlScript);
    } else {
      LOG.info("No SQL script specified...");
    }

    List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
    testSubscriber.getResponseFromCDC(outputList);

    int expRecordIndex = 0;
    int processedRecords = 0;
    for (int i = 0; i < outputList.size(); ++i) {
      // ignoring the DDLs
      if (outputList.get(i).getOperation() == OperationType.DDL) {
        continue;
      }
      ExpectedRecordYSQLGeneric.checkRecord(outputList.get(i), expectedRecords[expRecordIndex++]);
      ++processedRecords;
    }
    // processedRecords will be the same as expRecordIndex
    assertEquals(expectedRecords.length, processedRecords);
  }

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute(UtilStrings.dropAllTables);

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
  }

  @Test
  public void testIntegerTypeColumn() {
    /*
     * works the same for
     * - bigint
     * - bigserial^
     * - int
     * - integer
     * - smallint
     * - smallserial^
     * - serial^
     *
     * types marked as ^ are auto-incrementing ones and generate a WRITE op with them too
     */
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[]{
        new ExpectedRecordYSQLGeneric<>("1", "2", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "4", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "5", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("7", "8", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("7", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("8", "8", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("8", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testint");
    } catch (Exception e) {
      LOG.error("Test for int column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testBooleanTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "f", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "t", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "f", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("1", "f", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testboolean");
    } catch (Exception e) {
      LOG.error("Test for boolean column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testDoubleTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "10.42", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "0.5", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("4", "0.5", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testdouble");
    } catch (Exception e) {
      LOG.error("Test for double precision column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testTextTypeColumn() {
    /* works the same for char(n), varchar(n) and text */
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "sample string with pk 1", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "sample string with pk 3", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "sample string with pk 2", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("3", "random sample string", OperationType.UPDATE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtext");
    } catch (Exception e) {
      LOG.error("Test for text column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testUuidTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "ffffffff-ffff-ffff-ffff-ffffffffffff",
                                        OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "ffffffff-ffff-ffff-ffff-ffffffffffff",
                                        OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "123e4567-e89b-12d3-a456-426655440000",
                                        OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "123e4567-e89b-12d3-a456-426655440000",
                                        OperationType.INSERT)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testuuid");
    } catch (Exception e) {
      LOG.error("Test for uuid column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testTimestampTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "2017-07-04 12:30:30", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "2021-09-29 00:00:00", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "1970-01-01 00:00:10", OperationType.UPDATE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtimestamp");
    } catch (Exception e) {
      LOG.error("Test for timestamp column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testDateTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "2021-09-20", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "2021-09-29", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("2", "2000-01-01", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("3", "1970-01-01", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("4", "1970-01-01", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testdate");
    } catch (Exception e) {
      LOG.error("Test for date column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testInetAddressTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "127.0.0.1", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "0.0.0.0", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "192.168.1.1", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testinet");
    } catch (Exception e) {
      LOG.error("Test for inet column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testMacAddressTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "2c:54:91:88:c9:e3", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "2c:54:91:e8:99:d2", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "2c:54:91:e8:99:d2", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testmacaddr");
    } catch (Exception e) {
      LOG.error("Test for mac address column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testMacAddress8TypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "22:00:5c:03:55:08:01:02", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "22:00:5c:04:55:08:01:02", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("2", "22:00:5c:03:55:08:01:02", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("3", "22:00:5c:05:55:08:01:02", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testmacaddr8");
    } catch (Exception e) {
      LOG.error("Test for macaddr8 column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testJsonTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "{\"first_name\":\"vaibhav\"}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "{\"last_name\":\"kushwaha\"}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "{\"name\":\"vaibhav kushwaha\"}",
                                        OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("3", "{\"a\":97, \"b\":\"98\"}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testjson");
    } catch (Exception e) {
      LOG.error("Test for json type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testJsonbTypeColumn() {
    try {
      /* do note that there is a space after the colon (:) coming into the streamed records */
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "{\"first_name\": \"vaibhav\"}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "{\"last_name\": \"kushwaha\"}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "{\"name\": \"vaibhav kushwaha\"}",
                                        OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("3", "{\"a\": 97, \"b\": \"98\"}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testjsonb");
    } catch (Exception e) {
      LOG.error("Test for jsonb type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testBitTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "001111", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "110101", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "111111", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("0", "000000", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testbit");
    } catch (Exception e) {
      LOG.error("Test for bit(n) type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testVarbitTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "001111", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "1101011101", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "11", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("0", "0", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testvarbit");
    } catch (Exception e) {
      LOG.error("Test for varbit(n) type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testTimeTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "11:30:59", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "23:30:59", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("2", "00:00:01", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "00:01:00", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtime");
    } catch (Exception e) {
      LOG.error("Test for time type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testTimetzTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "11:30:59+05:30", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "23:30:59+05:30", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("2", "00:00:01+00", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtimetz");
    } catch (Exception e) {
      LOG.error("Test for timetz type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testNumericTypeColumn() {
    /* works the same for numeric/decimal */
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "20.5", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("2", "100.75", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("3", "3.456", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testnumeric");
    } catch (Exception e) {
      LOG.error("Test for numeric type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testMoneyTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "$100.50", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("2", "$10.12", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("3", "$1.23", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "$90.50", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE),
        new ExpectedRecordYSQLGeneric<>("2", "", OperationType.DELETE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testmoney");
    } catch (Exception e) {
      LOG.error("Test for money type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testCidrTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "10.1.0.0/16", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "12.2.0.0/22", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "12.2.0.0/22", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testcidr");
    } catch (Exception e) {
      LOG.error("Test for cidr type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testByteaTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "\\x01", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "\\xdeadbeef", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "\\xdeadbeef", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testbytea");
    } catch (Exception e) {
      LOG.error("Test for bytea type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testBoxTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "(8,9),(1,3)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "(10,31),(8,9)", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("2", "(10,31),(8,9)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testbox");
    } catch (Exception e) {
      LOG.error("Test for box type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testCircleTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("10", "<(2,3),32>", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("10", "<(0,0),10>", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("10", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("1000", "<(0,0),4>", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testcircle");
    } catch (Exception e) {
      LOG.error("Test for circle type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testPathTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("23", "((1,2),(20,-10))", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("23", "((-1,-1))", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("23", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("34", "((0,0),(3,4),(5,5),(1,2))", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testpath");
    } catch (Exception e) {
      LOG.error("Test for path type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testPointTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("11", "(0,-1)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("11", "(1,3)", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("11", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("21", "(33,44)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testpoint");
    } catch (Exception e) {
      LOG.error("Test for point type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testPolygonTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "((1,3),(4,12),(2,4))", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "((1,3),(4,12),(2,4),(1,4))", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("27", "((1,3),(2,4),(1,4))", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testpolygon");
    } catch (Exception e) {
      LOG.error("Test for polygon type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testLineTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "{1,2,-8}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "{1,1,-5}", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("29", "{2.5,-1,0}", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testline");
    } catch (Exception e) {
      LOG.error("Test for line type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testLsegTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "[(0,0),(2,4)]", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "[(-1,-1),(10,-8)]", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("37", "[(1,3),(3,5)]", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testlseg");
    } catch (Exception e) {
      LOG.error("Test for lseg type column type failed with exception: ", e);
      fail();
    }
  }

  @Test
  public void testTimestamptzTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "1969-12-31 18:40:00+00", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "2021-12-31 18:40:00+00", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtstz");
    } catch (Exception e) {
      LOG.error("Test for timestamptz type column failed", e);
      fail();
    }
  }

  @Test
  public void testInt4RangeTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "[5,14)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "[6,43)", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testint4range");
    } catch (Exception e) {
      LOG.error("Test for int4range type column failed", e);
      fail();
    }
  }

  @Test
  public void testInt8RangeTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "[5,15)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "[2,100000)", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testint8range");
    } catch (Exception e) {
      LOG.error("Test for int8range type column failed", e);
      fail();
    }
  }

  @Test
  public void testTsrangeTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "(\"1970-01-01 00:00:00\",\"2000-01-01 12:00:00\")",
                                        OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "(\"1970-01-01 00:00:00\",\"2022-11-01 12:00:00\")",
                                        OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtsrange");
    } catch (Exception e) {
      LOG.error("Test for tsrange type column failed", e);
      fail();
    }
  }

  @Test
  public void testTstzrangeTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1",
            "(\"2017-07-04 12:30:30+00\",\"2021-07-04 07:00:30+00\")", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1",
            "(\"2017-07-04 12:30:30+00\",\"2021-10-04 07:00:30+00\")", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testtstzrange");
    } catch (Exception e) {
      LOG.error("Test for tstzrange type column failed", e);
      fail();
    }
  }

  @Test
  public void testDaterangeTypeColumn() {
    try {
      ExpectedRecordYSQLGeneric<?>[] expectedRecords = new ExpectedRecordYSQLGeneric[] {
        new ExpectedRecordYSQLGeneric<>("1", "[2019-10-08,2021-10-07)", OperationType.INSERT),
        new ExpectedRecordYSQLGeneric<>("1", "[2019-10-08,2020-10-07)", OperationType.UPDATE),
        new ExpectedRecordYSQLGeneric<>("1", "", OperationType.DELETE),
        new ExpectedRecordYSQLGeneric<>("", "", OperationType.WRITE)
      };

      executeScriptAssertRecords(expectedRecords, "sql_datatype_script/complete_datatype_test.sql",
                                 "testdaterange");
    } catch (Exception e) {
      LOG.error("Test for daterange type column failed", e);
      fail();
    }
  }

  @Test
  public void testDefaultForAllTypes() {
    try {
      assertFalse(statement.execute(UtilStrings.createTableWithDefaults));

      CDCSubscriber testSubscriber = new CDCSubscriber("testdefault", getMasterAddresses());
      testSubscriber.createStream();

      assertEquals(1, statement.executeUpdate("insert into testdefault values (1);"));

      List<CdcService.CDCSDKRecordPB> outputList = new ArrayList<>();
      testSubscriber.getResponseFromCDC(outputList);

      boolean checkedInsertRecord = false;
      for (int i = 0; i < outputList.size(); ++i) {
        if (outputList.get(i).getOperation() == OperationType.INSERT) {
          checkedInsertRecord = true;
          int changesCount = outputList.get(i).getChangesCount();
          // there are 33 columns except for the primary key one
          assertEquals(33, changesCount);
          assertEquals("1", outputList.get(i).getKey(0).getValue().getStringValue().toStringUtf8());
          for (int j = 0; j < changesCount; ++j) {
            String streamedValue =
              outputList.get(i).getChanges(j).getValue().getStringValue().toStringUtf8();
            assertEquals(UtilStrings.expectedDefaultValues[j], streamedValue);
          }
        }
      }
      assertTrue(checkedInsertRecord);
    } catch (Exception e) {
      LOG.error("Test to verify default value streaming for all types failed", e);
      fail();
    }
  }
}
