// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.ColumnSchema;
import org.yb.ColumnSchema.SortOrder;
import org.yb.Schema;

@ApiModel(description = "Table details")
public class TableDetails {

  // The name of the table.
  @ApiModelProperty(value = "Table name")
  public String tableName;

  // The keyspace that this table belongs to.
  @ApiModelProperty(value = "Keyspace to which this table belongs")
  public String keyspace;

  // The default table-level time to live (in seconds).
  @ApiModelProperty(
      value =
          "The default table-level time to live, in seconds. A value of `-1` represents an infinite"
              + " TTL.")
  public long ttlInSeconds = -1;

  // Details of the columns that make up the table (to be used to create ColumnSchemas).
  @ApiModelProperty(value = "Details of all columns in the table")
  public List<ColumnDetails> columns;

  @ApiModelProperty(value = "Primary key values used to split table into tablets")
  public List<String> splitValues;

  /**
   * Create a new TableDetails object based on the provided Schema. tableName will still need to be
   * defined after using this constructor.
   *
   * @param schema The Schema that defines this table
   */
  public static TableDetails createWithSchema(Schema schema) {
    TableDetails tableDetails = new TableDetails();
    if (schema.getTimeToLiveInMillis() > 0) {
      tableDetails.ttlInSeconds = schema.getTimeToLiveInMillis() / 1000;
    }
    tableDetails.columns = new LinkedList<>();
    for (ColumnSchema columnSchema : schema.getColumns()) {
      tableDetails.columns.add(ColumnDetails.createWithColumnSchema(columnSchema));
    }
    return tableDetails;
  }

  /**
   * This method produces a PgSql statement to create described table;
   *
   * @return a PgSql CREATE TABLE statement for the table represented by this TableDetails object
   */
  @JsonIgnore
  public String getPgSqlCreateTableString(boolean ifNotExists) {
    String queryTemplate = "CREATE TABLE %s\"%s\" (%s%s)%s;";
    String ifNotExistsPart = ifNotExists ? "IF NOT EXISTS " : "";
    String fieldsPart =
        columns.stream()
            .map(column -> "\"" + column.name + "\" " + column.type.type)
            .collect(Collectors.joining(", "));
    List<ColumnDetails> primaryKeys =
        columns.stream()
            .filter(columnDetails -> columnDetails.isPartitionKey || columnDetails.isClusteringKey)
            .collect(Collectors.toList());
    String primaryKeysPart = "";
    if (!primaryKeys.isEmpty()) {
      primaryKeysPart =
          ", primary key ("
              + primaryKeys.stream()
                  .map(
                      key ->
                          key.isClusteringKey && key.sortOrder != SortOrder.NONE
                              ? "\"" + key.name + "\" " + key.sortOrder.name()
                              : "\"" + key.name + "\"")
                  .collect(Collectors.joining(", "))
              + ")";
    }
    String splitValuesPart = "";
    if (CollectionUtils.isNotEmpty(splitValues)) {
      splitValuesPart =
          " SPLIT AT VALUES ("
              + splitValues.stream()
                  .map(value -> "(" + value + ")")
                  .collect(Collectors.joining(", "))
              + ")";
    }
    return String.format(
        queryTemplate, ifNotExistsPart, tableName, fieldsPart, primaryKeysPart, splitValuesPart);
  }

  @JsonIgnore
  public String getCQLCreateKeyspaceString() {
    return "CREATE KEYSPACE IF NOT EXISTS \"" + keyspace + "\"";
  }

  @JsonIgnore
  public String getCQLUseKeyspaceString() {
    return "USE \"" + keyspace + "\"";
  }

  /**
   * This method produces a CQL statement of the following format to create a table from the
   * TableDetails representation of it from the UI: CREATE TABLE tablename ( col1 type1, col2 type2,
   * col3 type3, col4 type4, col5 type5, primary key ((col1, col2), col3, col4) );
   *
   * @return a CQL CREATE TABLE statement for the table represented by this TableDetails object
   */
  @JsonIgnore
  public String getCQLCreateTableString() {
    List<String> partitionKeys = new ArrayList<>();
    List<String> clusteringKeys = new ArrayList<>();
    Map<String, String> sortOrderColumns = new HashMap<>();
    StringBuilder builder = new StringBuilder("CREATE TABLE ");
    builder.append(tableName);

    // Sort Columns by columnOrder property (HashKeys < PrimaryKeys < Non-Keys)
    columns.sort((col1, col2) -> Integer.compare(col1.columnOrder, col2.columnOrder));

    // Add sorted columns in order, checking that order is correct
    ColumnDetails lastColumn = null;
    builder.append(" (");
    for (ColumnDetails column : columns) {
      builder.append(column.name).append(" ");
      builder.append(column.type);
      if (column.type.isCollection()) {
        builder.append("<").append(column.keyType);
        if (column.type.equals(ColumnDetails.YQLDataType.MAP)) {
          builder.append(", ").append(column.valueType);
        }
        builder.append(">");
      }
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
      if (!column.sortOrder.equals(ColumnSchema.SortOrder.NONE)) {
        sortOrderColumns.put(column.name, column.sortOrder.toString());
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
      builder.append(", ").append(primaryKey);
    }
    builder.append("))");

    // Add default time to live and sort order (if any)
    boolean hasTimeToLive = ttlInSeconds > 0;
    boolean hasSortOrder = sortOrderColumns.size() > 0;
    if (hasTimeToLive || hasSortOrder) {
      builder.append(" WITH ");
      if (hasTimeToLive) {
        builder.append("default_time_to_live = ");
        builder.append(Long.toString(ttlInSeconds));
      }
      if (hasTimeToLive && hasSortOrder) {
        builder.append(" AND ");
      }
      if (hasSortOrder) {
        builder.append("CLUSTERING ORDER BY (");
        Iterator<String> columnNamesIterator = sortOrderColumns.keySet().iterator();
        while (columnNamesIterator.hasNext()) {
          String columnName = columnNamesIterator.next();
          String sortOrder = sortOrderColumns.get(columnName);
          builder.append(columnName).append(" ");
          builder.append(sortOrder);
          if (columnNamesIterator.hasNext()) {
            builder.append(", ");
          }
        }
        builder.append(")");
      }
    }
    builder.append(";");

    return builder.toString();
  }
}
