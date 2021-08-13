// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.ApiUtils;
import org.junit.Test;
import org.yb.ColumnSchema;

public class TableDetailsTest {

  private static int oneKey = 1;
  private static int multiKey = 2;
  private static long noTtl = -1;
  private static long withTtl = 1000;
  private static ColumnSchema.SortOrder none = ColumnSchema.SortOrder.NONE;
  private static ColumnSchema.SortOrder asc = ColumnSchema.SortOrder.ASC;
  private static ColumnSchema.SortOrder desc = ColumnSchema.SortOrder.DESC;

  @Test
  public void testCQLCreateKeyspace() {
    String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS \"dummy_ks\"";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(oneKey, noTtl);
    assertEquals(createKeyspace, tableDetails.getCQLCreateKeyspaceString());
  }

  @Test
  public void testCQLUseKeyspace() {
    String useKeyspace = "USE \"dummy_ks\"";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(oneKey, noTtl);
    assertEquals(useKeyspace, tableDetails.getCQLUseKeyspaceString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_NoCKey_NoTTL() {
    String createTable = "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(oneKey, noTtl);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_NoCKey_WithTTL() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0)) WITH "
            + "default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(oneKey, withTtl);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_MultiPKeys_NoCKey_NoTTL() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, " + "primary key ((k0, k1)));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(multiKey, noTtl);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_MultiPKeys_NoCKey_WithTTL() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, "
            + "primary key ((k0, k1))) WITH default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(multiKey, withTtl);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_NoCKey_ignoresSortOrder() {
    // NOTE: UI should never allow this combination, but a badly formatted REST payload from outside
    // the UI could possible cause this
    String createTable = "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, 0, noTtl, asc);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_WithCKey_NoTTL_NoSortOrder() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, " + "primary key (k0, k1));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, noTtl, none);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_WithCKey_WithTTL_NoSortOrder() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, "
            + "primary key (k0, k1)) WITH default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, withTtl, none);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_WithCKey_noTTL_ASC() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, "
            + "primary key (k0, k1)) WITH CLUSTERING ORDER BY (k1 ASC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, noTtl, asc);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_WithCKey_noTTL_DESC() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, "
            + "primary key (k0, k1)) WITH CLUSTERING ORDER BY (k1 DESC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, noTtl, desc);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_WithCKey_withTTL_ASC() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, "
            + "primary key (k0, k1)) WITH default_time_to_live = 1000 AND CLUSTERING ORDER BY (k1 ASC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, withTtl, asc);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_OnePKey_WithCKey_withTTL_DESC() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, "
            + "primary key (k0, k1)) WITH default_time_to_live = 1000 AND CLUSTERING ORDER BY (k1 DESC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, withTtl, desc);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_MultiPKeys_WithCKey_NoTTL_NoSortOrder() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, k2 int, v varchar, "
            + "primary key ((k0, k1), k2));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(multiKey, oneKey, noTtl, none);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_MultiPKeys_WithCKey_WithTTL_NoSortOrder() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, k1 int, k2 int, v varchar, "
            + "primary key ((k0, k1), k2)) WITH default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(multiKey, oneKey, withTtl, none);
    assertEquals(createTable, tableDetails.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_WithMap() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, v varchar, v2 map<uuid, varchar>, "
            + "primary key (k0));";
    TableDetails details = ApiUtils.getDummyCollectionsTableDetails(ColumnDetails.YQLDataType.MAP);
    assertEquals(createTable, details.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_WithSet() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, v varchar, v2 set<uuid>, " + "primary key (k0));";
    TableDetails details = ApiUtils.getDummyCollectionsTableDetails(ColumnDetails.YQLDataType.SET);
    assertEquals(createTable, details.getCQLCreateTableString());
  }

  @Test
  public void testCQLCreateTableString_WithList() {
    String createTable =
        "CREATE TABLE dummy_table (k0 int, v varchar, v2 list<uuid>, " + "primary key (k0));";
    TableDetails details = ApiUtils.getDummyCollectionsTableDetails(ColumnDetails.YQLDataType.LIST);
    assertEquals(createTable, details.getCQLCreateTableString());
  }

  @Test(expected = IllegalArgumentException.class)
  public void testCQLCreateTableStringBadColumnOrder() {
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(multiKey, oneKey, noTtl, none);
    for (ColumnDetails column : tableDetails.columns) {
      if (column.name.startsWith("k")) {
        column.columnOrder = 9999;
        break;
      }
    }
    tableDetails.getCQLCreateTableString();
  }
}
