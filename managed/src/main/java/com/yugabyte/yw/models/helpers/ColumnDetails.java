// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

public class ColumnDetails {

  // The relative position for this column in the table and in CQL commands
  public int columnOrder;

  // The name of this column
  public String name;

  // The column of this column
  public CQLDataType type;

  // True if this column is a partition key
  public boolean isPartitionKey;

  // True if this column is a clustering key
  public boolean isClusteringKey;

  /**
   * For use in CreateTable and AlterTable CQL commands
   */
  public enum CQLDataType {
    BIGINT("bigint"),
    INT("int"),
    SMALLINT("smallint"),
    DOUBLE_PRECISION("double precision"),
    FLOAT("float"),
    VARCHAR("varchar"),
    BOOLEAN("boolean");

    String type;

    CQLDataType(String type) {
      this.type = type;
    }

    public String toString() {
      return type;
    }
  }
}
