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

package org.yb.cdc.ycql;

import com.datastax.oss.driver.api.core.CqlSession;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;

import org.junit.runner.RunWith;
import org.yb.cdc.CDCConsoleSubscriber;
import org.yb.cdc.CdcService;
import org.yb.cdc.CdcService.RowMessage.Op;
import org.yb.cdc.common.ExpectedRecordYCQLGeneric;
import org.yb.cdc.util.CDCTestUtils;
import org.yb.YBTestRunner;

import java.math.BigInteger;
import java.net.InetSocketAddress;
import java.sql.Timestamp;
import java.time.LocalTime;
import java.util.*;

/**
 * This test has been disabled for now since the YCQL support for CDC is not there yet.
 * See the corresponding
 * <a href="https://github.com/yugabyte/yugabyte-db/issues/11320">GitHub issue</a>
 */
@Ignore("Disabled until fix for YCQL lands")
@RunWith(value = YBTestRunner.class)
public class TestDatatypes {
  private CqlSession session;

  public void assertRecords(ExpectedRecordYCQLGeneric<?>[] expectedRecords) throws Exception{
    List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
    CDCConsoleSubscriber cdcSubscriberObj = CDCTestUtils.initJavaClient(outputList);

    assertEquals(expectedRecords.length, outputList.size());

    for (int i = 0; i < outputList.size(); ++i) {
      ExpectedRecordYCQLGeneric.checkRecord(outputList.get(i), expectedRecords[i]);
    }

    cdcSubscriberObj.close();
  }

  @Before
  public void setUp() {
    CDCTestUtils.clearStreamId("");
    String createKeyspace = "create keyspace if not exists yugabyte;";
    String dropTable = "drop table if exists yugabyte.test;";

    session = CqlSession
      .builder()
      .addContactPoint(new InetSocketAddress("127.0.0.1", 9042))
      .withLocalDatacenter("datacenter1")
      .build();

    session.execute(createKeyspace);
    session.execute(dropTable);
  }

  @Test
  public void testConnection() {
    assertFalse(session.isClosed());
  }

  @Test
  public void testYCQLInet() throws Exception {

    session.execute("create table yugabyte.test (a int primary key, b inet);");

    session.execute("insert into yugabyte.test (a, b) values (1, '127.0.0.1');");

    int arraySize = 3;
    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, "", Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, "127.0.0.1", Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(0, "", Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testUUID() throws Exception {
    // Works the same for timeuuid.
    session.execute("create table yugabyte.test (a int primary key, b uuid);");
    session.execute(
      "insert into yugabyte.test (a, b) values (1, 123e4567-e89b-12d3-a456-426655440000);");
    session.execute(
      "insert into yugabyte.test (a, b) values (2, 123e4567-e89b-12d3-a456-426655440000);");
    session.execute(
      "insert into yugabyte.test (a, b) values (3, 123e4567-e89b-12d3-a456-426655440000);");

    // UUID.randomUUID() is just being used as a placeholder
    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, UUID.randomUUID(), Op.DDL),
      new ExpectedRecordYCQLGeneric<>(
        1,
        UUID.fromString("123e4567-e89b-12d3-a456-426655440000"),
        Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(
        2,
        UUID.fromString("123e4567-e89b-12d3-a456-426655440000"),
        Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(
        3,
        UUID.fromString("123e4567-e89b-12d3-a456-426655440000"),
        Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(0, UUID.randomUUID(), Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testYCQLJsonb() throws Exception {
    session.execute("create table yugabyte.test (a int primary key, b jsonb);");
    session.execute("insert into yugabyte.test (a, b) values (1, '{\"fName\":\"vaibhav\"}');");
    session.execute("insert into yugabyte.test (a, b) values (2, '{\"lName\":\"kushwaha\"}');");

    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, "", Op.DDL),
      new ExpectedRecordYCQLGeneric<>(
        1,
        "{\"fName\":\"vaibhav\"}",
        Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(
        2,
        "{\"lName\":\"kushwaha\"}",
        Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(0, "", Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testTimestamp() throws Exception {
    Timestamp ts1 = new Timestamp(System.currentTimeMillis());
    String s1 = ts1.toString();
    String s2 = ts1.toString();

    session.execute("create table yugabyte.test (a int primary key, b timestamp);");

    session.execute("insert into yugabyte.test (a, b) values (1, '" + s1 + "');");
    session.execute("insert into yugabyte.test (a, b) values (2, '" + s2 + "');");
    session.execute("delete from yugabyte.test where a = 2;");

    Timestamp ts2 = new Timestamp(System.currentTimeMillis());
    String s3 = ts2.toString();
    session.execute("insert into yugabyte.test (a, b) values (3, '" + s3 + "');");

    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, new Date(), Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, new Date(ts1.getTime()), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, new Date(ts1.getTime()), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, new Date(), Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(3, new Date(ts2.getTime()), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(0, new Date(), Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testInteger() throws Exception {
    // Works with bigint, counter, int, integer, smallint, tinyint

    session.execute("create table yugabyte.test (a int primary key, b tinyint) "
                  + "with transactions = {'enabled' : true};");
    session.execute("insert into yugabyte.test (a, b) values (1, 2);");
    session.execute("insert into yugabyte.test (a, b) values (2, 3);");
    session.execute("insert into yugabyte.test (a, b) values (4, 5);");
    session.execute("insert into yugabyte.test (a, b) values (6, 7);");
    session.execute("begin transaction delete from yugabyte.test where a = 2; end transaction;");

    session.execute("update yugabyte.test set b = 6 where a = 4;");
    session.execute("delete from yugabyte.test where a = 6;");

    /*
     * If you are testing for bigint, make sure you pass the value in expected record as Long
     * i.e. ExpectedRecordGeneric<>(int-value, 2L, Op.SomeOperation)
     */
    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, 2, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, 3, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(4, 5, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(6, 7, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, 0, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.COMMIT),
      new ExpectedRecordYCQLGeneric<>(4, 6, Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(6, 0, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.DDL),
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testBoolean() throws Exception {

    session.execute("create table yugabyte.test (a int primary key, b boolean) "
                  + "with transactions = {'enabled' : true};");
    session.execute("insert into yugabyte.test (a, b) values (1, FALSE);");
    session.execute("insert into yugabyte.test (a, b) values (2, TRUE);");
    session.execute("insert into yugabyte.test (a, b) values (4, TRUE);");
    session.execute("insert into yugabyte.test (a, b) values (8, FALSE);");
    session.execute("begin transaction delete from yugabyte.test where a = 2; end transaction;");

    String multiQueryTxn = "begin transaction "
                         + "insert into yugabyte.test (a, b) values (100, FALSE); "
                         + "delete from yugabyte.test where a = 1; "
                         + "end transaction;";

    session.execute(multiQueryTxn);

    session.execute("update yugabyte.test set b = TRUE where a = 100;");
    session.execute("update yugabyte.test set b = FALSE where a = 4;");
    session.execute("delete from yugabyte.test where a = 8;");

    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, false, Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, false, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, true, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(4, true, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(8, false, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, false, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, false, Op.COMMIT),
      new ExpectedRecordYCQLGeneric<>(100, false, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(1, false, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, false, Op.COMMIT),
      new ExpectedRecordYCQLGeneric<>(100, true, Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(4, false, Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(8, false, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, false, Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testDouble() throws Exception {
    // Works the same for FLOAT.

    session.execute("create table yugabyte.test (a int primary key, b double) "
                  + "with transactions = {'enabled' : true};");
    session.execute("insert into yugabyte.test (a, b) values (1, 32.123);");
    session.execute("insert into yugabyte.test (a, b) values (2, 12.345);");
    session.execute("insert into yugabyte.test (a, b) values (4, 98.765);");
    session.execute("begin transaction delete from yugabyte.test where a = 2; end transaction;");

    session.execute(
      "begin transaction update yugabyte.test set b = 11.223 where a = 1; end transaction;");
    session.execute("delete from yugabyte.test where a = 4;");

    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, 32.123, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, 12.345, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(4, 98.765, Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, 0, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.COMMIT),
      new ExpectedRecordYCQLGeneric<>(1, 11.223, Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.COMMIT),
      new ExpectedRecordYCQLGeneric<>(4, 0, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, 0, Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testTime() throws Exception {
    session.execute("create table yugabyte.test (a int primary key, b time) "
                  + "with transactions = {'enabled' : true};");

    session.execute("insert into yugabyte.test (a, b) values (1, '11:00:00');");
    session.execute("insert into yugabyte.test (a, b) values (2, '21:00:00');");

    String multiQueryTxn = "begin transaction "
                         + "delete from yugabyte.test where a = 1; "
                         + "insert into yugabyte.test (a, b) values (3, '17:30:25'); "
                         + "end transaction;";
    session.execute(multiQueryTxn);

    // LocalTime.MIN is a random value used as placeholder.
    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, LocalTime.MIN, Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, LocalTime.of(11, 0, 0), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, LocalTime.of(21, 0, 0), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(1, LocalTime.MIN, Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(3, LocalTime.of(17, 30, 25), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(0, LocalTime.MIN, Op.COMMIT),
      new ExpectedRecordYCQLGeneric<>(0, LocalTime.MIN, Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testVarint() throws Exception {
    session.execute("create table yugabyte.test (a int primary key, b varint) "
                  + "with transactions = {'enabled':true};");

    session.execute("insert into yugabyte.test (a, b) values (1, 1001);");
    session.execute("insert into yugabyte.test (a, b) values (2, 2001);");

    /*
     * This would stream a DELETE record across CDC even though the record is not present in
     * the table, this behaviour is because YCQL uses upsert semantics where if a row is not
     * present while performing an UPDATE or DELETE operation, it would be inserted first and
     * then the operation would be performed.
     *
     * DO NOTE THAT even though the row is inserted first, it won't come
     * in the CDC streamed records.
     */
    session.execute("delete from yugabyte.test where a = 33;");

    session.execute("update yugabyte.test set b = 10000 where a = 2;");
    session.execute("update yugabyte.test set b = 20000 where a = 1;");

    session.execute("delete from yugabyte.test where a = 1;");

    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, BigInteger.valueOf(0), Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, BigInteger.valueOf(1001), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, BigInteger.valueOf(2001), Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(33, BigInteger.valueOf(0), Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(2, BigInteger.valueOf(10000), Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(1, BigInteger.valueOf(20000), Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(1, BigInteger.valueOf(0), Op.DELETE),
      new ExpectedRecordYCQLGeneric<>(0, BigInteger.valueOf(0), Op.DDL)
    };

    assertRecords(expectedRecords);
  }

  @Test
  public void testBlob() throws Exception {
    session.execute("create table yugabyte.test (a int primary key, b blob);");

    session.execute("insert into yugabyte.test (a, b) values (1, 0xff);");
    session.execute("insert into yugabyte.test (a, b) values (2, 0x10ef);");

    ExpectedRecordYCQLGeneric<?>[] expectedRecords = new ExpectedRecordYCQLGeneric[] {
      new ExpectedRecordYCQLGeneric<>(0, "", Op.DDL),
      new ExpectedRecordYCQLGeneric<>(1, "ff", Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(2, "10ef", Op.INSERT),
      new ExpectedRecordYCQLGeneric<>(1, "01", Op.UPDATE),
      new ExpectedRecordYCQLGeneric<>(0, "", Op.DDL)
    };

    assertRecords(expectedRecords);
  }
}
