// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.ApiUtils;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class TableDetailsTest {

  @Test
  public void testCQLCreateStringOnePartitionKeyNoClusteringKey() {
    String create = "CREATE TABLE dummy_table (k0 int, v varchar, primary key (k0));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(1, 0);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateStringMultiplePartitionKeysNoClusteringKey() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key ((k0, k1)));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(2, 0);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateStringOnePartitionKeyWithClusteringKey() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, v varchar, primary key (k0, k1));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(1, 1);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test
  public void testCQLCreateStringMultiplePartitionKeysWithClusteringKey() {
    String create = "CREATE TABLE dummy_table (k0 int, k1 int, k2 int, v varchar, " +
        "primary key ((k0, k1), k2));";
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(2, 1);
    assertEquals(create, tableDetails.toCQLCreateString());
  }

  @Test(expected = IllegalArgumentException.class)
  public void testCQLCreateStringBadColumnOrder() {
    TableDetails tableDetails = ApiUtils.getDummyTableDetails(2, 1);
    for (ColumnDetails column : tableDetails.columns) {
      if (column.name.startsWith("k")) {
        column.columnOrder = 9999;
        break;
      }
    }
    tableDetails.toCQLCreateString();
  }
}
