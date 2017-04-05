// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.net.InetAddress;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.stream.*;
import java.util.TreeSet;
import java.util.UUID;

public class TestAscendingOrder extends TestBase {
  private void createTable(String type1, String type2, String orderStmt) throws Exception {
    String createStmt = String.format("CREATE TABLE test_create " +
                                      "(h1 int, r1 %s, r2 %s, v1 int, v2 varchar, " +
                                      "primary key((h1), r1, r2)) %s;", type1, type2, orderStmt);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);
  }

  private void dropTable() throws Exception {
    String drop_stmt = "DROP TABLE test_create;";
    session.execute(drop_stmt);
  }

  private ResultSet createInsertAndSelectAsc(String dataType, List<String> values)
      throws Exception {

    createTable(dataType, "varchar", "WITH CLUSTERING ORDER BY(r1 ASC)");

    for (String s : values) {
      String insertStmt = String.format("INSERT INTO test_create (h1, r1, r2, v1, v2) " +
                                        "VALUES (1, %s, 'b', 1, 'c');", s);
      session.execute(insertStmt);
    }

    String selectStmt = "SELECT h1, r1, r2, v1, v2 FROM test_create WHERE h1 = 1";
    return session.execute(selectStmt);
  }

  private void intAsc(String type, long lower_bound, long upper_bound) throws Exception {
    LOG.info("TEST CQL " + type.toUpperCase() + " ASCENDING ORDER - START");
    // Create a unique list of random numbers.
    long[] values = new Random().longs(100, lower_bound, upper_bound).distinct().toArray();
    Arrays.sort(values);

    // Create a list of strings representing the integers in values.
    List<String> stringValues =
      Arrays.stream(values).mapToObj(value -> Long.toString(value)).collect(Collectors.toList());

    ResultSet rs = createInsertAndSelectAsc(type, stringValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.length);

    // Rows should come sorted by column r1 in ascending order.
    for (int i = 0; i < values.length; i++) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      long r1;
      switch(type) {
        case "bigint":
          r1 = row.getLong("r1"); break;
        case "int":
          r1 = row.getInt("r1"); break;
        case "smallint":
          r1 = row.getShort("r1"); break;
        case "tinyint":
          r1 = row.getByte("r1"); break;
        default:
          throw new Exception("Invalid data type " + type);
      }
      assertEquals(values[i], r1);
      assertEquals("b", row.getString("r2"));

      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
    LOG.info("TEST CQL " + type.toUpperCase() + " DESCENDING ORDER - END");
  }

  @Test
  public void testInt8Desc() throws Exception {
    intAsc("tinyint", -128, 127);
  }

  @Test
  public void testInt16Desc() throws Exception {
    intAsc("smallint", -32768, 32767);
  }

  @Test
  public void testInt32Desc() throws Exception {
    intAsc("int", Integer.MIN_VALUE, Integer.MAX_VALUE);
  }

  @Test
  public void testInt64Desc() throws Exception {
    intAsc("bigint", Long.MIN_VALUE, Long.MAX_VALUE);
  }

  @Test
  public void testStringAsc() throws Exception {
    LOG.info("TEST CQL STRING ASCENDING ORDER - START");
    String characters =
        "ABCDEFGHIJKLMNOQRSTUVXYZabcdefghijklmnoqrstuvxyz0123456789-+*/?!@#$%^&(),.:; ";

    Random random = new Random();
    TreeSet<String> values = new TreeSet<String>();

    // Create a list of 20 random strings by using specific characters.
    for (int i = 0; i < 20; i++) {
      String s = "'";
      // Create a string with up to 100 characters.
      int length = random.nextInt(100) + 1;

      for (int j = 0; j < length; j++) {
        s += characters.charAt(random.nextInt(characters.length() - 1));
      }
      s += "'";
      values.add(s);
    }

    ResultSet rs = createInsertAndSelectAsc("varchar", new ArrayList<String>(values));
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in ascending order.
    for (Iterator<String> iter = values.iterator(); iter.hasNext();) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals(iter.next().replace("'",""), row.getString("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
    LOG.info("TEST CQL STRING DESCENDING ORDER - END");
  }

  @Test
  public void testTimestampAsc() throws Exception {
    LOG.info("TEST CQL TIMESTAMP ASCENDING ORDER - START");
    final long currentTime = System.currentTimeMillis();
    final long[] values = new Random().longs(100, 0, 2 * currentTime).distinct().toArray();
    Arrays.sort(values);

    final List<String> stringValues =  Arrays.stream(values)
                                             .mapToObj(value -> Long.toString(value))
                                             .collect(Collectors.toList());

    ResultSet rs = createInsertAndSelectAsc("timestamp", stringValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.length);

    // Rows should come sorted by column r1 in ascending order.
    for (int i = 0; i < values.length; i++) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      Calendar calendar = new GregorianCalendar();
      calendar.setTimeInMillis(values[i]);
      assertEquals(calendar.getTime(), row.getTimestamp("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
    LOG.info("TEST CQL TIMESTAMP ASCENDING ORDER - END");
  }

  @Test
  public void testInetDesc() throws Exception {
    List<String> values = new ArrayList<String>(
      Arrays.asList("'1.2.3.4'", "'180::2978:9018:b288:3f6c'", "'2.2.3.4'"));
    List<String> randomValues = new ArrayList<String>(values);
    Collections.shuffle(randomValues);
    ResultSet rs = createInsertAndSelectAsc("inet", randomValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in ascending order.
    for (int i = 0; i < values.size(); i++) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals(InetAddress.getByName(values.get(i).replace("\'", "")), row.getInet("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
  }

  @Test
  public void testUUIDDesc() throws Exception {
    // UUIDs in ascending order.
    List <String> values = new ArrayList<String>(
      Arrays.asList(
        "'157c4b82-ff52-1053-bced-0eba570d969e'",
        "'157c4b82-ff32-1073-bced-0eba570d969e'",
        "'157c4b82-ff32-1075-bced-0eba570d969e'",
        "'a57c4b82-ff52-1173-bced-0eba570d969e'",
        "'167c4b82-ef22-1174-bced-0eba570d969e'",
        "'157c4b82-ef22-1274-bced-0eba570d969e'",
        "'a67c4b82-ef22-2173-bced-0eba570d969e'",
        "'c57c4b82-ef52-2073-aced-0eba570d969e'",
        "'167c4b82-ef22-3173-bced-0eba570d969e'",
        "'f67c4b82-ef22-3173-bced-0eba570d969e'",
        "'367c4b82-ef22-4173-bced-0eba570d969e'",
        "'467c4b82-ef22-4173-bced-0eba570d969e'"));
    List<String> randomValues = new ArrayList<String>(values);
    Collections.shuffle(randomValues);
    ResultSet rs = createInsertAndSelectAsc("uuid", randomValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in ascending order.
    for (int i = 0; i < values.size(); i++) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals(values.get(i).replace("\'", ""), row.getUUID("r1").toString());
      assertEquals("b", row.getString("r2"));
      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
  }
}
