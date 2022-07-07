// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableSet;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import org.yb.ColumnSchema;
import org.yb.Type;

@ApiModel(description = "Details of a CQL database column")
public class ColumnDetails {

  // The relative position for this column in the table and in CQL commands
  @ApiModelProperty(
      value = "Relative position (column order) for this column, in the table and in CQL commands")
  public int columnOrder;

  // The name of this column
  @ApiModelProperty(value = "Column name")
  public String name;

  // The type of this column
  @ApiModelProperty(value = "The column's data type")
  public YQLDataType type;

  // For collections, this is the item type (key type for maps)
  @ApiModelProperty(value = "Column key type")
  public YQLDataType keyType;

  // For maps, this is the value type
  @ApiModelProperty(value = "Column value name")
  public YQLDataType valueType;

  // True if this column is a partition key
  @ApiModelProperty(value = "True if this column is a partition key")
  public boolean isPartitionKey;

  // True if this column is a clustering key
  @ApiModelProperty(value = "True if this column is a clustering key")
  public boolean isClusteringKey;

  // SortOrder for this column (only valid for clustering columns)
  @ApiModelProperty(value = "Sort order for this column. Valid only for clustering columns.")
  public ColumnSchema.SortOrder sortOrder = ColumnSchema.SortOrder.NONE;

  public static ColumnDetails createWithColumnSchema(ColumnSchema columnSchema) {
    ColumnDetails columnDetails = new ColumnDetails();
    columnDetails.columnOrder = columnSchema.getId();
    columnDetails.name = columnSchema.getName();
    columnDetails.type = YQLDataType.createFromGenericType(columnSchema.getType());
    if (columnDetails.type == null) {
      throw new IllegalArgumentException(
          "Could not find CQL data type matching " + columnSchema.getType());
    }
    columnDetails.isPartitionKey = columnSchema.isHashKey();
    columnDetails.isClusteringKey = !columnDetails.isPartitionKey && columnSchema.isKey();
    if (columnSchema.getSortOrder() != null) {
      columnDetails.sortOrder = columnSchema.getSortOrder();
    }
    return columnDetails;
  }

  public enum YQLDataType {
    TINYINT("tinyint"),
    SMALLINT("smallint"),
    INT("int"),
    BIGINT("bigint"),
    VARCHAR("varchar"),
    BOOLEAN("boolean"),
    FLOAT("float"),
    DOUBLE_PRECISION("double precision"),
    BLOB("blob"),
    TIMESTAMP("timestamp"),
    DECIMAL("decimal"),
    VARINT("varint"),
    INET("inet"),
    LIST("list"),
    MAP("map"),
    SET("set"),
    UUID("uuid"),
    TIMEUUID("timeuuid"),
    FROZEN("frozen"),
    DATE("date"),
    TIME("time"),
    JSONB("jsonb"),
    USER_DEFINED_TYPE("user_defined_type");

    static final Set<YQLDataType> COLLECTION_TYPES = ImmutableSet.of(LIST, MAP, SET);

    String type;

    YQLDataType(String type) {
      this.type = type;
    }

    public boolean isCollection() {
      return COLLECTION_TYPES.contains(this);
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
      switch (type) {
        case INT8:
          return TINYINT;
        case INT16:
          return SMALLINT;
        case INT32:
          return INT;
        case INT64:
          return BIGINT;
        case STRING:
          return VARCHAR;
        case BOOL:
          return BOOLEAN;
        case FLOAT:
          return FLOAT;
        case DOUBLE:
          return DOUBLE_PRECISION;
        case BINARY:
          return BLOB;
        case TIMESTAMP:
          return TIMESTAMP;
        case DECIMAL:
          return DECIMAL;
        case VARINT:
          return VARINT;
        case INET:
          return INET;
        case LIST:
          return LIST;
        case MAP:
          return MAP;
        case SET:
          return SET;
        case UUID:
          return UUID;
        case TIMEUUID:
          return TIMEUUID;
        case FROZEN:
          return FROZEN;
        case DATE:
          return DATE;
        case TIME:
          return TIME;
        case JSONB:
          return JSONB;
        case USER_DEFINED_TYPE:
          return USER_DEFINED_TYPE;
      }
      throw new IllegalArgumentException("Type " + type + " has no YQL equivalent.");
    }
  }
}
