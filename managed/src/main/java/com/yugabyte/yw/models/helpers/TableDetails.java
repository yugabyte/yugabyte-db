// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import org.yb.ColumnSchema;
import org.yb.Schema;

import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;

public class TableDetails {

  // The name of the table
  public String tableName;

  // Details of the columns that make up the table (to be used to create ColumnSchemas)
  public List<ColumnDetails> columns;

  /**
   * Create a new TableDetails object based on the provided Schema. tableName will still need to be
   * defined after using this constructor.
   *
   * @param schema The Schema that defines this table
   */
  public static TableDetails createWithSchema(Schema schema) {
    TableDetails tableDetails = new TableDetails();
    tableDetails.columns = new LinkedList<>();
    for (ColumnSchema columnSchema : schema.getColumns()) {
      tableDetails.columns.add(ColumnDetails.createWithColumnSchema(columnSchema));
    }
    return tableDetails;
  }

  /**
   * This method produces a CQL statement of the following format to create a table from the
   * TableDetails representation of it from the UI:
   * CREATE TABLE tablename (
   *   col1 type1,
   *   col2 type2,
   *   col3 type3,
   *   col4 type4,
   *   col5 type5,
   *   primary key ((col1, col2), col3, col4)
   * );
   *
   * @return a CQL CREATE TABLE statement for the table represented by this TableDetails object
   */
  public String toCQLCreateString() {
    List<String> partitionKeys = new ArrayList<>();
    List<String> clusteringKeys = new ArrayList<>();
    StringBuilder builder = new StringBuilder("CREATE TABLE ");
    builder.append(tableName);

    // Sort Columns by columnOrder property (HashKeys < PrimaryKeys < Non-Keys)
    columns.sort((col1, col2) -> Integer.compare(col1.columnOrder, col2.columnOrder));

    // Add sorted columns in order, checking that order is correct
    ColumnDetails lastColumn = null;
    builder.append(" (");
    for (ColumnDetails column : columns) {
      builder.append(column.name);
      builder.append(" ");
      builder.append(column.type);
      builder.append(", ");
      if (column.isPartitionKey) {
        if (lastColumn == null || lastColumn.isPartitionKey) {
          partitionKeys.add(column.name);
        } else {
          throw new IllegalArgumentException("Partition keys must come first in column order.");
        }
      } else if (column.isClusteringKey) {
        if (lastColumn != null && (lastColumn.isPartitionKey || lastColumn.isClusteringKey)) {
          clusteringKeys.add(column.name);
        } else {
          throw new IllegalArgumentException(
              "Any clustering keys must come immediately after partition keys.");
        }
      }
      lastColumn = column;
    }

    // Add hash keys (there must always be at least 1)
    // Potential primary key combos:
    // (partition key)
    // (partition key, clustering key, ...)
    // ((partition key, partition key), clustering key, ...)
    // ((partition key, partition key))
    if (partitionKeys.size() == 0) {
      throw new IllegalArgumentException("You must have at least one partition key");
    }
    builder.append("primary key (");
    builder.append(partitionKeys.size() > 1 ? "(" : "");
    for (int i = 0; i < partitionKeys.size(); ++i) {
      if (i > 0) {
        builder.append(", ");
      }
      builder.append(partitionKeys.get(i));
    }
    builder.append(partitionKeys.size() > 1 ? ")" : "");

    // Add non-hash primary keys (if any)
    for (String primaryKey : clusteringKeys) {
      builder.append(", ");
      builder.append(primaryKey);
    }
    builder.append("));");

    return builder.toString();
  }

}
