// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.math.BigDecimal;
import java.util.Iterator;
import java.util.Random;
import java.util.TreeSet;

public class TestDecimalDataType extends TestBase {
  private String getRandomVarInt(boolean withSign) {
    String digits = "0123456789";
    final Random random = new Random();
    int length = random.nextInt(100) + 20;

    String s = "";
    for (int j = 0; j < length; j++) {
      s += digits.charAt(random.nextInt(digits.length() - 1));
    }

    if (withSign) {
      int i = random.nextInt(4);
      if (i < 2) {
        // Half of the time make this a negative number.
        s = "-" + s;
      } else if (i == 3) {
        // 25% of the time append a '+' sign to test parsing.
        s = "+" + s;
      }
    }

    return s;
  }

  private String getRandomDecimal() {
    return getRandomVarInt(true) + "." + getRandomVarInt(false);
  }

  @Test
  public void testDecimalDataTypeInHash() throws Exception {
    LOG.info("TEST CQL DECIMAL TYPE IN HASH - Start");

    // Create table
    String tableName = "test_decimal";
    String createStmt = String.format("CREATE TABLE %s " +
        "(h1 decimal, h2 int, r1 decimal, r2 int, v1 decimal, v2 int, " +
        "primary key((h1, h2), r1, r2));", tableName);
    session.execute(createStmt);

    final Random random = new Random();
    BigDecimal hashDecimal = new BigDecimal(getRandomDecimal());
    TreeSet<BigDecimal> decimals = new TreeSet<BigDecimal>();
    for (int i = 0; i < 100; i++) {
      BigDecimal decimal;
      do {
        decimal = new BigDecimal(getRandomDecimal());
      } while (!decimals.add(decimal));
      // Insert one row. Deliberately insert with same hash key but different range column values.
      final String insertStmt =
          String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) VALUES (%s, 1, %s, %d, %s, 2);",
                        tableName, hashDecimal.toString(), decimal.toString(), decimal.intValue(),
                        decimal.toString());
      LOG.info("insertStmt: " + insertStmt);
      session.execute(insertStmt);
    }

    // Select row by the hash key. Results should come sorted by range keys in ascending order.
    final String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
        "WHERE h1 = %s AND h2 = 1;", tableName, hashDecimal.toString());
    LOG.info("selectStmt: " + selectStmt);
    ResultSet rs = session.execute(selectStmt);
    assertEquals(decimals.size(), rs.getAvailableWithoutFetching());

    for (Iterator<BigDecimal> iter = decimals.iterator(); iter.hasNext();) {
      Row row = rs.one();
      BigDecimal decimal = iter.next();

      assertEquals(0, row.getDecimal("h1").compareTo(hashDecimal));
      assertEquals(1, row.getInt("h2"));
      assertEquals(0, row.getDecimal("r1").compareTo(decimal));
      assertEquals(decimal.intValue(), row.getInt("r2"));
      assertEquals(0, row.getDecimal("v1").compareTo(decimal));
      assertEquals(2, row.getInt("v2"));
    }

    final String insertStmt =
        String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) VALUES (10.1, 1, 2.0, 5, 3.0, 30);",
                      tableName);

    LOG.info("insertStmt: " + insertStmt);
    session.execute(insertStmt);

    final String[] d = { "10.1",
                         ".101E2",
                         "1.01E1",
                         ".101E+2",
                         "1.01E+1",
                         ".101e2",
                         "1.01e1",
                         ".101e+2",
                         "1.01e+1",
                         "0.101E2",
                         "0.101E+2",
                         "0.101e2",
                         "0.101e+2" };

    // Test that we can query by using different representations of the same decimal value.
    for (String dec : d) {
      final String selectStmt2 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
                                               "WHERE h1 = %s AND h2 = 1;", tableName, dec);
      LOG.info("selectStmt: " + selectStmt2);

      rs = session.execute(selectStmt2);
      if (rs.getAvailableWithoutFetching() != 1) {
        LOG.info("Failed select: " + selectStmt2);
      }
      assertEquals(1, rs.getAvailableWithoutFetching());
    }

    // Test UPDATE with hash and range decimal keys.
    for (Iterator<BigDecimal> iter = decimals.iterator(); iter.hasNext();) {
      BigDecimal rangeDecimal = iter.next();

      BigDecimal newDecimalValue = new BigDecimal(getRandomDecimal());
      final String updateStmt =
          String.format("UPDATE %s SET v1 = %s WHERE h1 = %s AND h2 = 1 and r1 = %s and r2 = %d",
                        tableName, newDecimalValue.toString(), hashDecimal.toString(),
                        rangeDecimal.toString(), rangeDecimal.intValue());
      rs = session.execute(updateStmt);

      final String selectStmt3 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
          "WHERE h1 = %s AND h2 = 1 AND r1 = %s and r2 = %d;", tableName, hashDecimal.toString(),
          rangeDecimal.toString(), rangeDecimal.intValue());
      rs = session.execute(selectStmt3);
      assertEquals(1, rs.getAvailableWithoutFetching());

      Row row = rs.one();

      assertEquals(0, row.getDecimal("h1").compareTo(hashDecimal));
      assertEquals(1, row.getInt("h2"));
      assertEquals(0, row.getDecimal("r1").compareTo(rangeDecimal));
      assertEquals(rangeDecimal.intValue(), row.getInt("r2"));
      assertEquals(0, row.getDecimal("v1").compareTo(newDecimalValue));
      assertEquals(2, row.getInt("v2"));
    }

    // Test DELETE with hash and range decimal keys.
    for (Iterator<BigDecimal> iter = decimals.iterator(); iter.hasNext();) {
      BigDecimal rangeDecimal = iter.next();

      final String deleteStmt =
          String.format("DELETE FROM %s WHERE h1 = %s AND h2 = 1 and r1 = %s and r2 = %d",
                        tableName, hashDecimal.toString(), rangeDecimal.toString(),
                        rangeDecimal.intValue());
      rs = session.execute(deleteStmt);

      final String selectStmt3 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
          "WHERE h1 = %s AND h2 = 1 AND r1 = %s and r2 = %d;", tableName, hashDecimal.toString(),
          rangeDecimal.toString(), rangeDecimal.intValue());
      rs = session.execute(selectStmt3);
      assertEquals(0, rs.getAvailableWithoutFetching());
    }

    final String dropStmt = "DROP TABLE test_decimal;";
    session.execute(dropStmt);
    LOG.info("TEST CQL DECIMAL TYPE IN HASH - End");
  }

  private void decimalDataTypeInRange(boolean sortIsAscending) throws Exception {
    LOG.info("TEST CQL DECIMAL TYPE IN RANGE - Start");

    String sortOrder = "ASC";
    if (!sortIsAscending) {
      sortOrder = "DESC";
    }
    // Create table
    String createStmt = String.format("CREATE TABLE test_decimal " +
        "(h1 varchar, r1 decimal, v1 int, primary key(h1, r1)) WITH CLUSTERING ORDER BY (r1 %s);",
        sortOrder);
    session.execute(createStmt);

    final Random random = new Random();
    TreeSet<BigDecimal> decimals = new TreeSet<BigDecimal>();
    for (int i = 0; i < 100; i++) {
      BigDecimal decimal;
      do {
        decimal = new BigDecimal(getRandomDecimal());
      } while (!decimals.add(decimal));

      // Insert one row. Deliberately insert with same hash key but different range column values.
      final String insertStmt = String.format("INSERT INTO test_decimal (h1, r1, v1) " +
                                              "VALUES ('bob', %s, 1);", decimal.toString());
      LOG.info("insertStmt: " + insertStmt);
      session.execute(insertStmt);
    }

    final String selectStmt = "SELECT h1, r1, v1 FROM test_decimal WHERE h1 = 'bob';";

    ResultSet rs = session.execute(selectStmt);
    assertEquals(decimals.size(), rs.getAvailableWithoutFetching());

    // Verify data is sorted as expected.
    Iterator<BigDecimal> iter;
    if (sortIsAscending) {
      iter = decimals.iterator();
    } else {
      iter = decimals.descendingIterator();
    }
    while (iter.hasNext()) {
      Row row = rs.one();
      assertEquals(0, row.getDecimal("r1").compareTo(iter.next()));
    }

    final String dropStmt = "DROP TABLE test_decimal;";
    session.execute(dropStmt);
    LOG.info("TEST CQL DECIMAL TYPE IN RANGE - End");
  }

  @Test
  public void testDecimalDataTypeInRange() throws Exception {
    decimalDataTypeInRange(true);
  }

  @Test
  public void testDecimalDescendingDataTypeInRange() throws Exception {
    decimalDataTypeInRange(false);
  }

  @Test
  public void testDecimalComparisonInRange() throws Exception {
    LOG.info("TEST CQL DECIMAL TYPE IN RANGE - Start");

    // Create table
    String createStmt = "CREATE TABLE test_decimal" +
                         "(h1 varchar, r1 decimal, r2 decimal, v1 int, primary key(h1, r1, r2));";
    session.execute(createStmt);

    final Random random = new Random();
    TreeSet<BigDecimal> decimals = new TreeSet<BigDecimal>();
    for (int i = 0; i < 100; i++) {
      BigDecimal decimal;
      do {
        decimal = new BigDecimal(getRandomDecimal());
      } while (!decimals.add(decimal));

      // Insert one row. Deliberately insert with same hash key but different range column values.
      final String insertStmt =
          String.format("INSERT INTO test_decimal (h1, r1, r2, v1) " +
                        "VALUES ('bob', %s, 1.1, 1);", decimal.toString());
      LOG.info("insertStmt: " + insertStmt);
      session.execute(insertStmt);
    }

    int i = 1;
    for (Iterator<BigDecimal> iter = decimals.iterator(); iter.hasNext(); i++) {
      BigDecimal decimal = iter.next();
      // Select rows that are greater than a specific decimal.
      final String selectStmt =
          String.format("SELECT h1, r1, v1 FROM test_decimal " +
                        "WHERE h1 = 'bob' AND r1 > %s;", decimal.toString());
      LOG.info("selectStmt: " + selectStmt);
      ResultSet rs = session.execute(selectStmt);
      LOG.info("got " + rs.getAvailableWithoutFetching() + " results");
      assertEquals(decimals.size() - i, rs.getAvailableWithoutFetching());
    }

    i = 1;
    for (Iterator<BigDecimal> iter = decimals.descendingIterator(); iter.hasNext(); i++) {
      BigDecimal decimal = iter.next();
      // Select rows that are greater than a specific decimal.
      final String selectStmt =
          String.format("SELECT h1, r1, v1 FROM test_decimal " +
                        "WHERE h1 = 'bob' AND r1 < %s;", decimal.toString());
      LOG.info("selectStmt: " + selectStmt);
      ResultSet rs = session.execute(selectStmt);
      LOG.info("got " + rs.getAvailableWithoutFetching() + " results");
      assertEquals(decimals.size() - i, rs.getAvailableWithoutFetching());
    }

    final String dropStmt = "DROP TABLE test_decimal;";
    session.execute(dropStmt);
    LOG.info("TEST CQL DECIMAL TYPE IN RANGE - End");
  }

  @Test
  public void testDecimalMultipleComparisonInRange() throws Exception {
    LOG.info("TEST CQL DECIMAL TYPE IN RANGE - Start");

    // Create table
    String createStmt = "CREATE TABLE test_decimal" +
                         "(h1 varchar, r1 decimal, r2 decimal, v1 int, primary key(h1, r1, r2));";
    session.execute(createStmt);

    final Random random = new Random();
    BigDecimal decimal1 = new BigDecimal(getRandomDecimal());
    BigDecimal decimal2 = new BigDecimal(getRandomDecimal());

    final String insertStmt =
        String.format("INSERT INTO test_decimal (h1, r1, r2, v1) " +
                      "VALUES ('bob', %s, %s, 1);", decimal1.toString(), decimal2.toString());
    LOG.info("insertStmt: " + insertStmt);
    session.execute(insertStmt);

    BigDecimal delta = new BigDecimal(".05");
    BigDecimal smallerDecimal1 = decimal1.subtract(delta);
    BigDecimal smallerDecimal2 = decimal2.subtract(delta);
    BigDecimal largerDecimal1 = decimal1.add(delta);
    BigDecimal largerDecimal2 = decimal2.add(delta);

    String selectStmt = String.format("SELECT h1, r1, r2, v1 FROM test_decimal " +
        "WHERE h1 = 'bob' AND r1 > %s AND r2 < %s;", smallerDecimal1.toString(),
        largerDecimal2.toString());

    ResultSet rs = session.execute(selectStmt);
    assertEquals(1, rs.getAvailableWithoutFetching());

    selectStmt = String.format("SELECT h1, r1, r2, v1 FROM test_decimal " +
        "WHERE h1 = 'bob' AND r1 < %s AND r2 > %s;", largerDecimal1.toString(),
        smallerDecimal2.toString());

    rs = session.execute(selectStmt);
    assertEquals(1, rs.getAvailableWithoutFetching());

    selectStmt = String.format("SELECT h1, r1, r2, v1 FROM test_decimal " +
        "WHERE h1 = 'bob' AND r1 > %s AND r2 < %s;", largerDecimal1.toString(),
        smallerDecimal2.toString());

    rs = session.execute(selectStmt);
    assertEquals(0, rs.getAvailableWithoutFetching());

    selectStmt =  String.format("SELECT h1, r1, r2, v1 FROM test_decimal " +
        "WHERE h1 = 'bob' AND r1 < %s AND r2 > %s;", smallerDecimal1.toString(),
        largerDecimal2.toString());

    rs = session.execute(selectStmt);
    assertEquals(0, rs.getAvailableWithoutFetching());

    final String dropStmt = "DROP TABLE test_decimal;";
    session.execute(dropStmt);
    LOG.info("TEST CQL DECIMAL TYPE IN RANGE - End");
  }

  @Test
  public void testConversions() throws Exception {
    // Test the conversions from varint -> (tinyint, smallint, int, bigint, decimal, double, float)
    // and decimal -> (double, float).
    LOG.info("TEST CQL CONVERSIONS - Start");

    String createStmt = "CREATE TABLE test_decimal" +
                         "(h1 decimal, r1 decimal, v1 tinyint, v2 smallint, v3 int, v4 bigint, " +
                         "v5 float, v6 double, v7 float, v8 double, primary key(h1, r1));";
    session.execute(createStmt);

    TreeSet<BigDecimal> decimals = new TreeSet<BigDecimal>();

    BigDecimal decimalHash;
    for (int i = 0; i < 100; i++) {
      // Create a unique decimal hash.
      do {
        decimalHash = new BigDecimal(getRandomDecimal());
      } while (!decimals.add(decimalHash));

      final Random random = new Random();
      final int yqlTinyInt = random.nextInt(255) - 128;
      final int yqlSmallInt = random.nextInt(65535) - 32768;
      final int yqlInt = random.nextInt();
      final long yqlBigInt = random.nextLong();
      final float yqlFloat = random.nextFloat() * random.nextLong();
      final double yqlDouble = random.nextDouble() * random.nextLong();
      // Insert a very large integer in a decimal column.
      String yqlVarInt = getRandomVarInt(true);
      final String insertStmt =
          String.format("INSERT INTO test_decimal (h1, r1, v1, v2, v3, v4, v5, v6, v7, v8) " +
              "VALUES (%s, %s, %d, %d, %d, %d, %d, %d, %f, %f);", decimalHash.toString(), yqlVarInt,
              yqlTinyInt, yqlSmallInt, yqlInt, yqlBigInt, yqlInt, yqlBigInt, yqlFloat, yqlDouble);
      LOG.info("Insert statement: " + insertStmt);
      session.execute(insertStmt);

      final String selectStmt = String.format("SELECT h1, r1, v1, v2, v3, v4, v5, v6, v7, v8 " +
          "FROM test_decimal WHERE h1 = %s;", decimalHash.toString());
      ResultSet rs = session.execute(selectStmt);
      assertEquals(1, rs.getAvailableWithoutFetching());

      Row row = rs.one();
      BigDecimal decimal = new BigDecimal(yqlVarInt);

      assertEquals(0, row.getDecimal("h1").compareTo(decimalHash));
      assertEquals(0, row.getDecimal("r1").compareTo(decimal));
      assertEquals(yqlTinyInt, row.getByte("v1"));
      assertEquals(yqlSmallInt, row.getShort("v2"));
      assertEquals(yqlInt, row.getInt("v3"));
      assertEquals(yqlBigInt, row.getLong("v4"));
      assertEquals(yqlInt, row.getFloat("v5"), 0);
      assertEquals(yqlBigInt, row.getDouble("v6"), 0);
      assertEquals(yqlFloat, row.getFloat("v7"), 1e-5);
      assertEquals(yqlDouble, row.getDouble("v8"), 1e-5);
    }

    // Create a unique decimal hash.
    do {
      decimalHash = new BigDecimal(getRandomDecimal());
    } while (!decimals.add(decimalHash));

    // Test the minimum values allowed for each integer type.
    final String insertStmtFmt =
        "INSERT INTO test_decimal (h1, r1, v1, v2, v3, v4, v5, v6, v7, v8) " +
        "VALUES (%s, 1, %d, %d, %d, %d, 1.0, 2.0, 1.0, 2.0);";

    String insertStmt = String.format(insertStmtFmt, decimalHash.toString(), -128, -32768,
                                      Integer.MIN_VALUE, Long.MIN_VALUE);
    session.execute(insertStmt);

    final String selectStmtFmt = "SELECT h1, v1, v2, v3, v4 FROM test_decimal WHERE h1 = %s;";
    String selectStmt = String.format(selectStmtFmt, decimalHash.toString());
    LOG.info("::selectStmt: " + selectStmt);

    ResultSet rs = session.execute(selectStmt);
    assertEquals(1, rs.getAvailableWithoutFetching());

    Row row = rs.one();
    assertEquals(0, row.getDecimal("h1").compareTo(decimalHash));
    assertEquals(-128, row.getByte("v1"));
    assertEquals(-32768, row.getShort("v2"));
    assertEquals(Integer.MIN_VALUE, row.getInt("v3"));
    assertEquals(Long.MIN_VALUE, row.getLong("v4"));

    // Test the maximum values allowed for each integer type.
    insertStmt = String.format(insertStmtFmt, decimalHash.toString(), 127, 32767, Integer.MAX_VALUE,
                               Long.MAX_VALUE);
    session.execute(insertStmt);

    selectStmt = String.format(selectStmtFmt, decimalHash.toString());

    rs = session.execute(selectStmt);
    assertEquals(1, rs.getAvailableWithoutFetching());

    row = rs.one();
    assertEquals(0, row.getDecimal("h1").compareTo(decimalHash));
    assertEquals(127, row.getByte("v1"));
    assertEquals(32767, row.getShort("v2"));
    assertEquals(Integer.MAX_VALUE, row.getInt("v3"));
    assertEquals(Long.MAX_VALUE, row.getLong("v4"));

    final String dropStmt = "DROP TABLE test_decimal;";
    session.execute(dropStmt);

    LOG.info("TEST CQL CONVERSIONS - End");
  }
}
