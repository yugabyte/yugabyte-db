// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import org.yb.client.RowResult;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.net.InetAddress;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Calendar;
import java.util.GregorianCalendar;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.stream.*;
import java.util.TreeSet;

public class TestDescendingOrder extends BaseCQLTest {
  private void createTable(String type1, String type2, String orderStmt) throws Exception {
    String createStmt = String.format("CREATE TABLE test_create " +
                                      "(h1 int, r1 %s, r2 %s, v1 int, v2 varchar, " +
                                      "primary key((h1), r1, r2)) %s " +
                                      "AND bloom_filter_fp_chance = 0.01 " +
                                      "AND comment = '' " +
                                      "AND crc_check_chance = 1.0 " +
                                      "AND dclocal_read_repair_chance = 0.1 " +
                                      "AND default_time_to_live = 0 " +
                                      "AND gc_grace_seconds = 864000 " +
                                      "AND max_index_interval = 2048 " +
                                      "AND memtable_flush_period_in_ms = 0 " +
                                      "AND min_index_interval = 128 " +
                                      "AND read_repair_chance = 0.0 " +
                                      "AND speculative_retry = '99.0PERCENTILE' " +
                                      "AND caching = { " +
                                      "    'keys' : 'ALL', " +
                                      "    'rows_per_partition' : 'NONE' " +
                                      "} " +
                                      "AND compression = { " +
                                      "    'chunk_length_in_kb' : 64, " +
                                      "    'class' : 'LZ4Compressor', " +
                                      "    'enabled' : true " +
                                      "} " +
                                      "AND compaction = { " +
                                      "    'base_time_seconds' : 60, " +
                                      "    'class' : 'DateTieredCompactionStrategy', " +
                                      "    'enabled' : true, " +
                                      "    'max_sstable_age_days' : 365, " +
                                      "    'max_threshold' : 32, " +
                                      "    'min_threshold' : 4, " +
                                      "    'timestamp_resolution' : 'MICROSECONDS', " +
                                      "    'tombstone_compaction_interval' : 86400, " +
                                      "    'tombstone_threshold' : 0.2, " +
                                      "    'unchecked_tombstone_compaction' : false " +
                                      "}; ",
                                      type1, type2, orderStmt);
    LOG.info("createStmt: " + createStmt);
    session.execute(createStmt);
  }

  private void dropTable() throws Exception {
    String drop_stmt = "DROP TABLE test_create;";
    session.execute(drop_stmt);
  }

  private ResultSet createInsertAndSelectDesc(String dataType, List<String> values)
      throws Exception {

    createTable(dataType, "varchar", "WITH CLUSTERING ORDER BY(r1 DESC)");

    for (String s : values) {
      String insertStmt = String.format("INSERT INTO test_create (h1, r1, r2, v1, v2) " +
                                        "VALUES (1, %s, 'b', 1, 'c');", s);
      session.execute(insertStmt);
    }

    String selectStmt = "SELECT h1, r1, r2, v1, v2 FROM test_create WHERE h1 = 1";
    return session.execute(selectStmt);
  }


  @Test
  public void testScanWithBoundsDesc() throws Exception {
    // Setting up.
    createTable("int", "varchar", "WITH CLUSTERING ORDER BY(r1 DESC)");
    String insertTemplate = "INSERT INTO test_create (h1, r1, r2, v1, v2) " +
        "VALUES (1, %d, 'str_%d', 1, 'a');";
    for (int i = 1; i <= 5; i++) {
      for (int j = 1; j <= 5; j++) {
        String insertStmt = String.format(insertTemplate, i, j);
        session.execute(insertStmt);
      }
    }

    String selectTemplate = "SELECT * FROM test_create WHERE h1 = 1 AND " +
        "r1 %s %d AND r2 %s 'str_%d'";

    // Upper bound for both DESC and ASC columns.
    String selectStmt = String.format(selectTemplate, "<", 3, "<", 4);
    ResultSet rs = session.execute(selectStmt);
    // {2, 1} * {1, 2, 3}
    assertEquals(6, rs.getAvailableWithoutFetching());
    for (int i = 2; i > 0; i--) { // expecting r1 values in DESC order
      for (int j = 1; j < 4; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    // Lower bound for both DESC and ASC columns.
    selectStmt = String.format(selectTemplate, ">", 3, ">", 2);
    rs = session.execute(selectStmt);
    // {5, 4} * {3, 4, 5}
    assertEquals(6, rs.getAvailableWithoutFetching());
    for (int i = 5; i > 3; i--) { // expecting r1 values in DESC order
      for (int j = 3; j <= 5; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    // Lower bound for DESC column and upper bound for ASC column.
    selectStmt = String.format(selectTemplate, ">", 2, "<", 3);
    rs = session.execute(selectStmt);
    // {5, 4, 3} * {1, 2}
    assertEquals(6, rs.getAvailableWithoutFetching());
    for (int i = 5; i > 2; i--) { // expecting r1 values in DESC order
      for (int j = 1; j < 3; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    // Upper bound for DESC column and lower bound for ASC column.
    selectStmt = String.format(selectTemplate, "<", 4, ">", 3);
    rs = session.execute(selectStmt);
    // {3, 2, 1} * {4, 5}
    assertEquals(6, rs.getAvailableWithoutFetching());
    for (int i = 3; i >= 1; i--) { // expecting r1 values in DESC order
      for (int j = 4; j <= 5; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    // Upper and lower bound for DESC column only.
    selectStmt = "SELECT * FROM test_create WHERE h1 = 1 AND r1 > 1 AND r1 < 4;";
    rs = session.execute(selectStmt);
    // {3, 2} * {1, 2, 3, 4, 5}
    assertEquals(10, rs.getAvailableWithoutFetching());
    for (int i = 3; i > 1; i--) { // expecting r1 values in DESC order
      for (int j = 1; j <= 5; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    // Upper and lower bound for ASC column only.
    selectStmt = "SELECT * FROM test_create WHERE h1 = 1 AND r2 > 'str_2' AND r2 < 'str_5';";
    rs = session.execute(selectStmt);
    // {5, 4, 3, 2, 1} * {3, 4}
    assertEquals(10, rs.getAvailableWithoutFetching());
    for (int i = 5; i >= 1; i--) { // expecting r1 values in DESC order
      for (int j = 3; j < 5; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    // Upper and lower bounds for both DESC and ASC columns.
    selectStmt = "SELECT * FROM test_create WHERE h1 = 1 AND r1 > 1 AND r1 < 4 AND " +
        "r2 > 'str_2' AND r2 < 'str_5';";
    rs = session.execute(selectStmt);
    // {3, 2} * {3, 4}
    assertEquals(4, rs.getAvailableWithoutFetching());
    for (int i = 3; i > 1; i--) { // expecting r1 values in DESC order
      for (int j = 3; j < 5; j++) { // expecting r2 values in ASC order
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(i, row.getInt("r1"));
        assertEquals("str_" + Integer.toString(j), row.getString("r2"));
      }
    }

    dropTable();
  }

  private void intDesc(String type, long lower_bound, long upper_bound) throws Exception {
    LOG.info("TEST CQL " + type.toUpperCase() + " DESCENDING ORDER - START");
    // Create a unique list of random numbers.
    long[] values = new Random().longs(100, lower_bound, upper_bound).distinct().toArray();
    Arrays.sort(values);

    // Create a list of strings representing the integers in values.
    List<String> stringValues =
      Arrays.stream(values).mapToObj(value -> Long.toString(value)).collect(Collectors.toList());

    ResultSet rs = createInsertAndSelectDesc(type, stringValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.length);

    // Rows should come sorted by column r1 in descending order.
    for (int i = values.length - 1; i >= 0; i--) {
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
    intDesc("tinyint", -128, 127);
  }

  @Test
  public void testInt16Desc() throws Exception {
    intDesc("smallint", -32768, 32767);
  }

  @Test
  public void testInt32Desc() throws Exception {
    intDesc("int", Integer.MIN_VALUE, Integer.MAX_VALUE);
  }

  @Test
  public void testInt64Desc() throws Exception {
    intDesc("bigint", Long.MIN_VALUE, Long.MAX_VALUE);
  }

  @Test
  public void testStringDesc() throws Exception {
    LOG.info("TEST CQL STRING DESCENDING ORDER - START");
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

    ResultSet rs = createInsertAndSelectDesc("varchar", new ArrayList<String>(values));
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in descending order.
    for (Iterator<String> iter = values.descendingIterator(); iter.hasNext();) {
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
  public void testTimestampDesc() throws Exception {
    LOG.info("TEST CQL TIMESTAMP DESCENDING ORDER - START");
    final long currentTime = System.currentTimeMillis();
    final long[] values = new Random().longs(100, 0, 2 * currentTime).distinct().toArray();
    Arrays.sort(values);

    final List<String> stringValues =  Arrays.stream(values)
                                             .mapToObj(value -> Long.toString(value))
                                             .collect(Collectors.toList());

    ResultSet rs = createInsertAndSelectDesc("timestamp", stringValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.length);

    // Rows should come sorted by column r1 in descending order.
    for (int i = values.length - 1; i >= 0; i--) {
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
    LOG.info("TEST CQL TIMESTAMP DESCENDING ORDER - END");
  }

  @Test
  public void testInetDesc() throws Exception {
    List <String> values = new ArrayList<String>(
      Arrays.asList("'1.2.3.4'", "'180::2978:9018:b288:3f6c'", "'2.2.3.4'"));
    ResultSet rs = createInsertAndSelectDesc("inet", values);
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in descending order.
    for (int i = values.size() - 1; i >= 0; i--) {
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
        "157c4b82-ff52-1053-bced-0eba570d969e",
        "157c4b82-ff32-1073-bced-0eba570d969e",
        "157c4b82-ff32-1075-bced-0eba570d969e",
        "a57c4b82-ff52-1173-bced-0eba570d969e",
        "167c4b82-ef22-1174-bced-0eba570d969e",
        "157c4b82-ef22-1274-bced-0eba570d969e",
        "a67c4b82-ef22-2173-bced-0eba570d969e",
        "c57c4b82-ef52-2073-aced-0eba570d969e",
        "167c4b82-ef22-3173-bced-0eba570d969e",
        "f67c4b82-ef22-3173-bced-0eba570d969e",
        "367c4b82-ef22-4173-bced-0eba570d969e",
        "467c4b82-ef22-4173-bced-0eba570d969e"));
    List<String> randomValues = new ArrayList<String>(values);
    Collections.shuffle(randomValues);
    ResultSet rs = createInsertAndSelectDesc("uuid", randomValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in descending order.
    for (int i = values.size() - 1; i >= 0; i--) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals(values.get(i), row.getUUID("r1").toString());
      assertEquals("b", row.getString("r2"));
      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
  }

  @Test
  public void testTimeUUIDDesc() throws Exception {
    // UUIDs in ascending order.
    List <String> values = new ArrayList<String>(
      Arrays.asList(
        "040c318e-3422-11e7-a919-92ebcb67fe33",
        "36fa2f10-3422-11e7-b687-92ebcb67fe33",
        "5c077b28-3422-11e7-a919-92ebcb67fe33",
        "692c076a-3422-11e7-b687-92ebcb67fe33"));
    List<String> randomValues = new ArrayList<String>(values);
    Collections.shuffle(randomValues);
    ResultSet rs = createInsertAndSelectDesc("timeuuid", randomValues);
    assertEquals(rs.getAvailableWithoutFetching(), values.size());

    // Rows should come sorted by column r1 in descending order.
    for (int i = values.size() - 1; i >= 0; i--) {
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals(values.get(i), row.getUUID("r1").toString());
      assertEquals("b", row.getString("r2"));
      assertEquals(1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
    }

    dropTable();
  }

  private void insertValues(int[] numbers, String[] strings) throws Exception {
    for (int number : numbers) {
      for (String s : strings) {
        String insertStmt = String.format("INSERT INTO test_create (h1, r1, r2, v1, v2) " +
                                          "VALUES (1, %s, '%s', 1, 'c');", number, s);
        session.execute(insertStmt);
      }
    }
  }

  private void selectAndVerifyValues(int[] numbers, String[] strings) throws Exception {
    String selectStmt = "SELECT h1, r1, r2, v1, v2 FROM test_create WHERE h1 = 1";
    ResultSet rs = session.execute(selectStmt);
    assertEquals(rs.getAvailableWithoutFetching(), numbers.length * strings.length);

    for (int i = 0; i < numbers.length; i++) {
      for (int j = 0; j < strings.length; j++) {
        Row row = rs.one();
        assertEquals(1, row.getInt("h1"));
        assertEquals(numbers[i], row.getInt("r1"));
        assertEquals(strings[j], row.getString("r2"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("c", row.getString("v2"));
      }
    }
  }

  private void r1r2(int[] numbers, String[] strings,
                    int[] expected_numbers, String[] expected_strings,
                    String orderR1, String orderR2) throws Exception {
    final String testMsg = String.format("TEST CQL R1 %s, R2 %s - START ",
                                         orderR1.toUpperCase(), orderR2.toUpperCase());
    LOG.info(testMsg + " - START");

    createTable("int", "varchar",
                String.format("WITH CLUSTERING ORDER BY(r1 %s, r2 %s)", orderR1, orderR2));

    insertValues(numbers, strings);
    selectAndVerifyValues(expected_numbers, expected_strings);

    dropTable();
    LOG.info(testMsg + " - END");
  }

  @Test
  public void testR1DescR2Desc() throws Exception {
    r1r2(numbers, strings, reversed_numbers, reversed_strings, "DESC", "DESC");
  }

  @Test
  public void testR1DescR2Asc() throws Exception {
    r1r2(reversed_numbers, strings, reversed_numbers, strings, "DESC", "ASC");
  }

  @Test
  public void testR1AscR2Desc() throws Exception {
    r1r2(reversed_numbers, strings, numbers, reversed_strings, "ASC", "DESC");
  }

  @Test
  public void testR1AscR2Asc() throws Exception {
    r1r2(reversed_numbers, reversed_strings, numbers, strings, "ASC", "ASC");
  }

  private static int[] numbers = {3, 5, 9};
  private static String[] strings = {"ant", "bear", "cat", "dog", "eagle", "fox", "goat"};
  private static int[] reversed_numbers = {9, 5, 3};
  private static String[] reversed_strings =
      Arrays.stream(strings).sorted(Collections.reverseOrder()).toArray(String[]::new);
}
