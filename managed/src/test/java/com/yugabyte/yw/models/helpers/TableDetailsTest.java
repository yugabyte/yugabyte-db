// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.ApiUtils;
import org.junit.Test;
import org.yb.ColumnSchema;

import static org.junit.Assert.assertEquals;

public class TableDetailsTest {

  private static int oneKey = 1;
  private static int multiKey = 2;
  private static long noTtl = -1;
  private static long withTtl = 1000;
  private static ColumnSchema.SortOrder none = ColumnSchema.SortOrder.NONE;
  private static ColumnSchema.SortOrder asc = ColumnSchema.SortOrder.ASC;
  private static ColumnSchema.SortOrder desc = ColumnSchema.SortOrder.DESC;

  @Test
  public void testCQLCreateString_OnePKey_NoCKey_NoTTL() {
    String create = "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(oneKey, noTtl);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_NoCKey_WithTTL() {
    String create = "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0)) WITH " +
        "default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(oneKey, withTtl);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_MultiPKeys_NoCKey_NoTTL() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key ((k0, k1)));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(multiKey, noTtl);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_MultiPKeys_NoCKey_WithTTL() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key ((k0, k1)))" +
        " WITH default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetailsNoClusteringKey(multiKey, withTtl);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_NoCKey_ignoresSortOrder() {
    // NOTE: UI should never allow this combination, but a badly formatted REST payload from outside
    // the UI could possible cause this
    String create = "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, 0, noTtl, asc);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_WithCKey_NoTTL_NoSortOrder() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, noTtl, none);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_WithCKey_WithTTL_NoSortOrder() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1))" +
        " WITH default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, withTtl, none);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_WithCKey_noTTL_ASC() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1))" +
        " WITH CLUSTERING ORDER BY (k1 ASC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, noTtl, asc);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_WithCKey_noTTL_DESC() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1))" +
        " WITH CLUSTERING ORDER BY (k1 DESC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, noTtl, desc);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_WithCKey_withTTL_ASC() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1))" +
        " WITH default_time_to_live = 1000 AND CLUSTERING ORDER BY (k1 ASC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, withTtl, asc);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_OnePKey_WithCKey_withTTL_DESC() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1))" +
        " WITH default_time_to_live = 1000 AND CLUSTERING ORDER BY (k1 DESC);";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(oneKey, oneKey, withTtl, desc);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_MultiPKeys_WithCKey_NoTTL_NoSortOrder() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, k2 int, v varchar, " +
        "primary key ((k0, k1), k2));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(multiKey, oneKey, noTtl, none);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateString_MultiPKeys_WithCKey_WithTTL_NoSortOrder() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, k2 int, v varchar, " +
        "primary key ((k0, k1), k2)) WITH default_time_to_live = 1000;";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(multiKey, oneKey, withTtl, none);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test(expected = IllegalArgumentException.class)
  public void testCQLCreateStringBadColumnOrder() {
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(multiKey, oneKey, noTtl, none);
    for (ColumnDetails column : tableDetails.columns) {
      if (column.name.startsWith("k")) {
        column.columnOrder = 9999;
        break;
      }
    }
    tableDetails.toCQLCreateString();
  }
}
