// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import org.yb.ColumnSchema;
import org.yb.Type;

public class ColumnDetails {

  // The relative position for this column in the table and in CQL commands
  public int columnOrder;

  // The name of this column
  public String name;

  // The column of this column
  public YQLDataType type;

  // True if this column is a partition key
  public boolean isPartitionKey;

  // True if this column is a clustering key
  public boolean isClusteringKey;

  // SortOrder for this column (only valid for clustering columns)
  public ColumnSchema.SortOrder sortOrder = ColumnSchema.SortOrder.NONE;

  public static ColumnDetails createWithColumnSchema(ColumnSchema columnSchema) {
    ColumnDetails columnDetails = new ColumnDetails();
    columnDetails.columnOrder = columnSchema.getId();
    columnDetails.name = columnSchema.getName();
    columnDetails.type = YQLDataType.createFromGenericType(columnSchema.getType());
    if (columnDetails.type == null) {
      throw new IllegalArgumentException("Could not find CQL data type matching " +
          columnSchema.getType());
    }
    columnDetails.isPartitionKey = columnSchema.isHashKey();
    columnDetails.isClusteringKey = !columnDetails.isPartitionKey && columnSchema.isKey();
    if (columnSchema.getSortOrder() != null) {
      columnDetails.sortOrder = columnSchema.getSortOrder();
    }
    return columnDetails;
  }

  public enum YQLDataType {
    BIGINT("bigint"),
    INT("int"),
    SMALLINT("smallint"),
    DOUBLE_PRECISION("double precision"),
    FLOAT("float"),
    VARCHAR("varchar"),
    BOOLEAN("boolean"),
    TIMESTAMP("timestamp");

    String type;

    YQLDataType(String type) {
      this.type = type;
    }

    public String toString() {
      return type;
    }

    /**
     * Takes in a generic (Kudu) data type and returns the equivalent YQLDataType.
     *
     * @param type a generic (Kudu) data type
     * @return an instance of YQLDataType
     */
    public static YQLDataType createFromGenericType(Type type) {
      switch(type) {
        case INT64:
          return BIGINT;
        case INT32:
          return INT;
        case INT16:
          return SMALLINT;
        case DOUBLE:
          return DOUBLE_PRECISION;
        case FLOAT:
          return FLOAT;
        case STRING:
          return VARCHAR;
        case BOOL:
          return BOOLEAN;
        case TIMESTAMP:
          return TIMESTAMP;
      }
      throw new IllegalArgumentException("Type " + type + " has no YQL equivalent.");
    }
  }
}
